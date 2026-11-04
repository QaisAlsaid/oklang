// OR-1 "O Resolution" - the compile-time module spine, driver side.
//
// the shape (MODULES.md section 3): ok_modules_prepare walks one TU's import
// statements; each import resolves through the ladder (file-relative, the
// std/ registry, then OKPATH), then loads depth-first: read, parse, prepare
// ITS imports (recursion - the active chain catches cycles), compile in
// module mode (pub top-levels recorded), run the main (top-level code runs
// once, on first import, before the importer's), harvest the exports from
// the hidden globals into a sealed namespace instance, register. by the
// time the importer compiles, every binding is a live value riding its
// constant pool - the compiler resolves nothing at run time, ever. the std
// modules (OR-2) are built-in: no source, no run, the same registry.
//
// diagnostics use the import site's own file:line:col through okdiag, so a
// module-world error reads exactly like every other oklang error.

#define _XOPEN_SOURCE 700 // strdup + realpath (XSI; the c99 build is strict)

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "okmodules.h"

#include "okdiag.h"
#include "okparser.h"
#include "okstate.h"
#include "okutils.h"
#include "okvm.h"

// the arrays okmodules.h declares (this file is their one definition site).
// a registry record owns its canonical path and its import list (raw copies
// through the user's allocators); the deinit hook is the array macro's
// per-element call.
static void ok_module_record_deinit(ok_module_record* p_record, allocators* p_alloc) {
  for (uint32_t i = 0; i < p_record->imports_count; ++i) {
    p_alloc->release(p_alloc, p_record->imports[i]);
  }
  if (p_record->imports != NULL) {
    p_alloc->release(p_alloc, p_record->imports);
  }
  p_record->imports = NULL;
  p_record->imports_count = 0;
  p_alloc->release(p_alloc, p_record->canonical);
  p_record->canonical = NULL;
}

ARRAY_DEFINE_DEFAULT(ok_module_records, ok_module_record, ok_module_record_deinit)
ARRAY_DEFINE_DEFAULT(ok_import_binding_array, ok_import_binding, ARRAY_DEFAULT_TYPE_DEINIT)

// ---------------------------------------------------------------- the user's allocators

// the module system's plumbing is raw bytes (paths, arenas, traces,
// loader buffers), invisible to the gc, so it rides the USER's raw table
// straight, never libc: a host that routes allocations through its own
// allocator sees every byte this file touches. value-bearing arrays keep
// the gc-hooked wrapper (ok->alloc), same as the rest of the library.
static void* mod_alloc(const ok_allocators* p_a, const size_t p_size) {
  return p_a->allocate(p_size);
}

static void* mod_realloc(const ok_allocators* p_a, void* p_ptr, const size_t p_size) {
  return p_a->reallocate(p_ptr, p_size);
}

static void mod_free(const ok_allocators* p_a, void* p_ptr) {
  p_a->release(p_ptr);
}

static char* mod_strdup(const ok_allocators* p_a, const char* p_text) {
  const size_t length = strlen(p_text) + 1;
  char* out = (char*)mod_alloc(p_a, length);
  if (out != NULL) {
    memcpy(out, p_text, length);
  }
  return out;
}

// ---------------------------------------------------------------- the fs loader

// the CLI default. an embedder replaces it through ok_modules_set_loader
// (OR-3 exposes that door publicly); the language never guesses a filesystem.
// the read buffer comes from the instance's own user allocators: the loader
// contract (okmodules.h) is "a buffer the instance may free with its release
// allocator", and the default loader honors it by construction.
static bool fs_read_source(void* p_ctx, const char* p_path, ok_source* p_out) {
  module_system* modules = (module_system*)p_ctx;
  const ok_allocators* a = &modules->ok->raw_alloc;
  // a regular file only: fopen alone happily "opens" a directory on linux,
  // and ftell then answers a garbage-huge size (the allocation blast asan
  // caught). the probe contract needs the read to fail for anything that is
  // not a file.
  struct stat info;
  if (stat(p_path, &info) != 0 || !S_ISREG(info.st_mode)) {
    return false;
  }
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return false;
  }
  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  const long size = ftell(file);
  rewind(file);
  if (size < 0 || (unsigned long)size > (1u << 30)) {
    // unreadable or absurd: refuse rather than trust ftell on exotic nodes
    fclose(file);
    return false;
  }
  char* buff = (char*)mod_alloc(a, (size_t)size + 1);
  if (buff == NULL) {
    fclose(file);
    return false;
  }
  const size_t read = fread(buff, sizeof(char), (size_t)size, file);
  fclose(file);
  if (read != (size_t)size) {
    mod_free(a, buff);
    return false;
  }
  buff[read] = '\0';
  p_out->code = buff;
  p_out->path = p_path;
  return true;
}

// ---------------------------------------------------------------- diagnostics

// one driver-side error at an import site: the same header + snippet contract
// every parse/compile error uses. first-wins does not apply here - the driver
// aborts on the first failure, so one report per failure path.
static void module_report(const ok_source* p_source, const token* p_site, const char* p_message) {
  bool panic = false;
  bool had_error = false;
  const ok_span span = ok_span_of_token(p_site);
  okdiag_report(&panic,
                &had_error,
                p_source->path,
                p_source->code,
                &span,
                p_site->type == TOKEN_EOF,
                REPORT_SEVERITY_ERROR,
                ok_create_string_view(p_message, OK_STRING_VIEW_CALCULATE_LENGTH, true),
                NULL);
}

// a located note line, the renderer's exact format: continuation context
// under the error (the 404's tried-locations trace, the cycle's chain).
static void module_note(const ok_source* p_source, const token* p_site, const char* p_text) {
  const ok_span span = ok_span_of_token(p_site);
  fprintf(stderr, "%s:%u:%u: note: %s\n", p_source->path, span.line, span.column, p_text);
}



// ---------------------------------------------------------------- path plumbing

// a growable list of copied strings (the 404's trace). raw user-allocator
// copies: these live only inside one resolve call.
typedef struct {
  char** items;
  uint32_t count;
  uint32_t capacity;
} str_list;

static void str_list_init(str_list* p_list) {
  p_list->items = NULL;
  p_list->count = 0;
  p_list->capacity = 0;
}

static void str_list_deinit(str_list* p_list, const ok_allocators* p_a) {
  for (uint32_t i = 0; i < p_list->count; ++i) {
    mod_free(p_a, p_list->items[i]);
  }
  if (p_list->items != NULL) {
    mod_free(p_a, p_list->items);
  }
  p_list->items = NULL;
  p_list->count = 0;
  p_list->capacity = 0;
}

static bool str_list_append(str_list* p_list, const ok_allocators* p_a, const char* p_text) {
  if (p_list->count == p_list->capacity) {
    const uint32_t grown = p_list->capacity == 0 ? 8 : p_list->capacity * 2;
    char** items = (char**)mod_realloc(p_a, p_list->items, sizeof(char*) * grown);
    if (items == NULL) {
      return false;
    }
    p_list->items = items;
    p_list->capacity = grown;
  }
  char* copy = mod_strdup(p_a, p_text);
  if (copy == NULL) {
    return false;
  }
  p_list->items[p_list->count++] = copy;
  return true;
}

// does the text end with the given suffix?
static bool ends_with(const char* p_text, const char* p_suffix) {
  const size_t length = strlen(p_text);
  const size_t suffix = strlen(p_suffix);
  return length >= suffix && memcmp(p_text + length - suffix, p_suffix, suffix) == 0;
}

// one resolved import: everything load_module needs, nothing more. the
// canonical is owned; the source's code is the loader's read (the driver
// frees it with the user's release); natives carry no source, only the
// loader's descriptor and handle.
typedef struct {
  char* canonical;
  ok_source source;
  bool is_native;
  const ok_native_module* descriptor;
  void* handle;
} resolved_module;

static void resolved_init(resolved_module* p_resolved) {
  p_resolved->canonical = NULL;
  p_resolved->source.code = NULL;
  p_resolved->source.path = NULL;
  p_resolved->is_native = false;
  p_resolved->descriptor = NULL;
  p_resolved->handle = NULL;
}

static void resolved_deinit(const ok_allocators* p_a, resolved_module* p_resolved) {
  if (p_resolved->source.code != NULL) {
    mod_free(p_a, (void*)p_resolved->source.code);
  }
  if (p_resolved->canonical != NULL) {
    mod_free(p_a, p_resolved->canonical);
  }
  resolved_init(p_resolved);
}

// consume the resolved's read buffer (the loader's allocation): the one
// free per exit, idempotent so the caller's resolved_deinit never doubles.
static void resolved_consume_code(const ok_allocators* p_a, resolved_module* p_resolved) {
  if (p_resolved->source.code != NULL) {
    mod_free(p_a, (void*)p_resolved->source.code);
    p_resolved->source.code = NULL;
  }
}

// the registry's probe, needed by the ladder (defined in the registry
// section below)
static ok_module_record* registry_find(module_system* p_modules, const char* p_canonical);

// canonicalize (symlinks resolved): the registry key, the class name, and
// the stack-trace path all use this one spelling. realpath's own buffer is
// libc's, so a hit is copied onto the user's allocators and the original
// released: the registry only ever frees what it allocated. a realpath
// refusal (exotic fs, or a memory-backed loader's virtual path) keeps the
// found path itself as the key.
static char* canonicalize_path(const ok_allocators* p_a, char* p_found) {
  char* real = realpath(p_found, NULL);
  if (real == NULL) {
    return p_found;
  }
  char* owned = mod_strdup(p_a, real);
  free(real);
  mod_free(p_a, p_found);
  return owned; // NULL only on OOM
}

// the module source probe is the loader's read: the library never stats
// or opens files itself (M14), and a memory-backed loader serves virtual
// paths through the same door. a failed read is a plain miss.

// lexical join + normalize: dir's segments, then rel's, with '.' skipped and
// '..' popping. rel never starts with '/' (the parser rejects absolute
// paths). the result rides the user's allocators; NULL = OOM.
static char* join_normalize(const ok_allocators* p_a, const char* p_dir, const ok_string_view p_rel) {
  const size_t dir_length = strlen(p_dir);
  const uint64_t rel_length = ok_string_view_get_length(&p_rel);
  char* out = (char*)mod_alloc(p_a, dir_length + rel_length + 4);
  if (out == NULL) {
    return NULL;
  }
  size_t olen = 0;
  // the dir goes in as-is (its trailing '/' is handled below)
  memcpy(out, p_dir, dir_length);
  olen = dir_length;
  // walk rel's segments
  uint64_t i = 0;
  while (i < rel_length) {
    uint64_t j = i;
    while (j < rel_length && p_rel.chars[j] != '/') {
      ++j;
    }
    const uint64_t seg_length = j - i;
    const bool is_dot = seg_length == 1 && p_rel.chars[i] == '.';
    const bool is_dotdot = seg_length == 2 && p_rel.chars[i] == '.' && p_rel.chars[i + 1] == '.';
    if (is_dotdot) {
      // pop the last segment (keep the leading '/', stop at the root)
      while (olen > 0 && out[olen - 1] != '/') {
        --olen;
      }
      if (olen > 0) {
        --olen; // the '/' itself
      }
    } else if (!is_dot && seg_length > 0) {
      if (olen > 0 && out[olen - 1] != '/') {
        out[olen++] = '/';
      } else if (olen == 0) {
        out[olen++] = '/'; // a relative dir with no segments yet (".")
      }
      memcpy(out + olen, p_rel.chars + i, seg_length);
      olen += seg_length;
    }
    i = j + 1;
  }
  if (olen == 0) {
    out[olen++] = '.';
  }
  out[olen] = '\0';
  return out;
}

// the resolution ladder at one candidate location (M3/M6): the exact file,
// then + ".ok", then + "/index.ok", each probed through the loader's read,
// then the native candidate, probed through load_native. the
// OK_CAP_NATIVE_LOAD gate applies: a refused capability skips the native
// probe entirely. p_native_error carries the loader's message when a
// native candidate EXISTS but is broken: that is a hard error, not a miss.
static bool probe_ladder(module_system* p_modules,
                         const char* p_base,
                         str_list* p_tried,
                         char* p_native_error,
                         size_t p_native_error_capacity,
                         bool p_where,
                         resolved_module* p_out) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;
  static const char* const forms[] = {"", ".ok", "/index.ok"};
  const char* ext = ok_dylib_extension();

  for (uint32_t f = 0; f < 3; ++f) {
    const size_t base_length = strlen(p_base);
    char* candidate = (char*)mod_alloc(a, base_length + strlen(forms[f]) + 1);
    if (candidate == NULL) {
      return false;
    }
    memcpy(candidate, p_base, base_length);
    strcpy(candidate + base_length, forms[f]);
    // a candidate named like a native is a native candidate, never an ok
    // source: reading a shared object as code would parse garbage
    if (ends_with(candidate, ext)) {
      str_list_append(p_tried, a, candidate); // best effort: the trace wants it
      mod_free(a, candidate);
      continue;
    }
    if (p_modules->loader.read_source != NULL &&
        p_modules->loader.read_source(p_modules->loader.p_ctx, candidate, &p_out->source)) {
      p_out->canonical = canonicalize_path(a, candidate);
      if (p_out->canonical == NULL) {
        mod_free(a, (void*)p_out->source.code);
        p_out->source.code = NULL;
        return false;
      }
      p_out->source.path = p_out->canonical;
      return true;
    }
    str_list_append(p_tried, a, candidate); // best effort: the trace wants it
    mod_free(a, candidate);
  }

  // the native lane (M6: natives are first-class at every step)
  if ((p_modules->ok->caps_dropped & OK_CAP_NATIVE_LOAD) != 0 || p_modules->loader.load_native == NULL) {
    return false;
  }
  char* candidate = NULL;
  if (ends_with(p_base, ext)) {
    candidate = mod_strdup(a, p_base); // the explicit spelling: import "./x.so"
  } else {
    const size_t base_length = strlen(p_base);
    candidate = (char*)mod_alloc(a, base_length + strlen(ext) + 1);
    if (candidate != NULL) {
      memcpy(candidate, p_base, base_length);
      strcpy(candidate + base_length, ext);
    }
  }
  if (candidate == NULL) {
    return false;
  }
  char* canonical = canonicalize_path(a, candidate);
  if (canonical == NULL) {
    return false;
  }
  // OR-4's where mode: report the candidate without opening it. the
  // existence probe is the loader's read (a diagnostic may dlopen nothing);
  // a broken native still shows as found by path, the import is the truth.
  if (p_where) {
    ok_source probe;
    if (p_modules->loader.read_source != NULL &&
        p_modules->loader.read_source(p_modules->loader.p_ctx, canonical, &probe)) {
      mod_free(a, (void*)probe.code);
      p_out->canonical = canonical;
      p_out->is_native = true;
      return true;
    }
    str_list_append(p_tried, a, canonical); // best effort: the trace wants it
    mod_free(a, canonical);
    return false;
  }
  // an already-registered native answers from the registry (a module loads
  // once per ok instance, ever)
  if (registry_find(p_modules, canonical) != NULL) {
    p_out->canonical = canonical;
    p_out->is_native = true;
    return true;
  }
  const ok_native_module* descriptor = NULL;
  void* handle = NULL;
  char err[256];
  err[0] = '\0';
  const bool loaded = p_modules->loader.load_native(p_modules->loader.p_ctx, canonical, ok_api_table(), &descriptor,
                                                   &handle, err, sizeof(err));
  if (!loaded) {
    if (err[0] != '\0') {
      // the loader's contract: a message means a native EXISTS here but is
      // broken. that is a hard error, the caller reports it.
      snprintf(p_native_error, p_native_error_capacity, "%s", err);
    }
    str_list_append(p_tried, a, canonical); // best effort: the trace wants it
    mod_free(a, canonical);
    return false;
  }
  p_out->canonical = canonical;
  p_out->is_native = true;
  p_out->descriptor = descriptor;
  p_out->handle = handle;
  return true;
}

// the importing file's directory ("" -> "."). a copy on the user's allocators.
static char* directory_of(const ok_allocators* p_a, const char* p_path) {
  const char* last = strrchr(p_path, '/');
  if (last == NULL) {
    return mod_strdup(p_a, ".");
  }
  if (last == p_path) {
    return mod_strdup(p_a, "/");
  }
  const size_t length = (size_t)(last - p_path);
  char* dir = (char*)mod_alloc(p_a, length + 1);
  if (dir == NULL) {
    return NULL;
  }
  memcpy(dir, p_path, length);
  dir[length] = '\0';
  return dir;
}

// does the view start with the given prefix?
static bool view_starts_with(const ok_string_view p_view, const char* p_prefix) {
  const size_t prefix_length = strlen(p_prefix);
  return ok_string_view_get_length(&p_view) >= prefix_length &&
         memcmp(p_view.chars, p_prefix, prefix_length) == 0;
}

static bool view_equals(const ok_string_view p_view, const char* p_text) {
  const size_t length = strlen(p_text);
  return ok_string_view_get_length(&p_view) == length && memcmp(p_view.chars, p_text, length) == 0;
}

// ---------------------------------------------------------------- the registry

// the per-run name arena (root names the binding table views). user
// allocators, freed at the next ok_run's reset (ok.c) and at deinit.
static char* arena_strdup(module_system* p_modules, const char* p_text) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;
  if (p_modules->name_arena_count == p_modules->name_arena_capacity) {
    const uint32_t grown = p_modules->name_arena_capacity == 0 ? 16 : p_modules->name_arena_capacity * 2;
    char** arena = (char**)mod_realloc(a, p_modules->name_arena, sizeof(char*) * grown);
    if (arena == NULL) {
      return NULL;
    }
    p_modules->name_arena = arena;
    p_modules->name_arena_capacity = grown;
  }
  char* copy = mod_strdup(a, p_text);
  if (copy == NULL) {
    return NULL;
  }
  p_modules->name_arena[p_modules->name_arena_count++] = copy;
  return copy;
}

static ok_module_record* registry_find(module_system* p_modules, const char* p_canonical) {
  for (uint32_t i = 0; i < p_modules->loaded.count; ++i) {
    if (strcmp(p_modules->loaded.data[i].canonical, p_canonical) == 0) {
      return &p_modules->loaded.data[i];
    }
  }
  return NULL;
}

// the bare-name probe for force-loaded natives: only they register under a
// bare name (file keys are absolute, std keys carry the "std/" prefix). the
// import literal is a LEXER SLICE, not a NUL-terminated string: the compare
// is length-bounded, never a strcmp over the view.
static ok_module_record* registry_find_native(module_system* p_modules, const ok_string_view p_name) {
  const uint64_t length = ok_string_view_get_length(&p_name);
  for (uint32_t i = 0; i < p_modules->loaded.count; ++i) {
    const ok_module_record* record = &p_modules->loaded.data[i];
    if (record->is_native && strlen(record->canonical) == length &&
        memcmp(record->canonical, p_name.chars, length) == 0) {
      return &p_modules->loaded.data[i];
    }
  }
  return NULL;
}

// ---------------------------------------------------------------- namespaces

// build one module namespace: a fresh class (module_namespace = true, so
// field reads are public and writes refused), one immutable field per export
// in declaration order, frozen, instantiated, filled from the hidden globals
// the module's epilogue stored. the class's name is the canonical path -
// `print m` reads `<module /abs/path>`; identity is the class, not the name.
static bool build_namespace(module_system* p_modules,
                            const char* p_canonical,
                            const module_exports* p_exports,
                            value* p_out) {
  ok* okay = p_modules->ok;
  object_specs specs = {&okay->alloc, &okay->objects_store};
  gc* collector = okay->alloc.gc;

  object_string* name = string_interner_intern(
      &okay->interner, ok_create_string_view(p_canonical, (uint32_t)strlen(p_canonical), true), &specs);
  if (name == NULL) {
    return false;
  }
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(name))) {
    return false;
  }
  object_class* klass = create_object_class(name, okay->vm.object_singleton, &specs);
  gc_guard_down(collector); // the class roots its name from here
  if (klass == NULL) {
    return false;
  }
  klass->module_namespace = true;
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(klass))) {
    return false;
  }
  for (uint32_t i = 0; i < p_exports->count; ++i) {
    object_string* field_name = string_interner_intern(&okay->interner, p_exports->data[i].name, &specs);
    if (field_name == NULL) {
      gc_guard_down(collector);
      return false;
    }
    if (!gc_guard_up(collector, OBJECT_AS_VALUE(field_name))) {
      gc_guard_down(collector);
      return false;
    }
    const object_string* previous_owner = NULL;
    const bool added =
        object_class_add_field(klass, field_name, false /* mut */, true /* pub: an export */,
                              NULL /* init thunk */, NULL_AS_VALUE() /* default: the loader fills the slot */,
                              &previous_owner);
    gc_guard_down(collector); // the class owns the name now
    if (!added) {
      gc_guard_down(collector);
      return false;
    }
  }
  if (!object_class_freeze(klass)) {
    gc_guard_down(collector);
    return false;
  }
  object_instance* inst = create_object_instance(klass, &specs);
  gc_guard_down(collector); // the instance roots the class
  if (inst == NULL) {
    return false;
  }
  // the harvest needs no guard: nothing between the instance's creation and
  // the caller's gc_guard_up allocates - the values ride straight from the
  // hidden globals into the export slots.
  for (uint32_t i = 0; i < p_exports->count; ++i) {
    const module_export* entry = &p_exports->data[i];
    inst->fields[i] = okay->globals_store.global_values.data[entry->hidden.index];
  }
  *p_out = OBJECT_AS_VALUE(inst);
  return true;
}

// ---------------------------------------------------------------- the pending root tree

// a TU's root namespaces, pre-materialization. one node per path segment
// between the root and a module leaf; M5's merging is multiple leaves under
// one root, and a node that is both a leaf and a branch is the M13-style
// conflict (a path cannot bind a module and a namespace at once).
// nodes OWN their names (strdup) - the segment buffers they came from die
// with each import statement, the tree lives until materialization.
typedef struct pending_ns {
  char* name;               // the segment, owned (user allocators)
  value module_leaf;        // set when a module lands exactly here
  bool has_leaf;
  struct pending_ns* children;
  uint32_t children_count;
  uint32_t children_capacity;
} pending_ns;

static void pending_ns_init(const ok_allocators* p_a, pending_ns* p_node, const char* p_name) {
  p_node->name = mod_strdup(p_a, p_name);
  p_node->module_leaf = NULL_AS_VALUE();
  p_node->has_leaf = false;
  p_node->children = NULL;
  p_node->children_count = 0;
  p_node->children_capacity = 0;
}

static void pending_ns_deinit(const ok_allocators* p_a, pending_ns* p_node) {
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    pending_ns_deinit(p_a, &p_node->children[i]);
  }
  if (p_node->children != NULL) {
    mod_free(p_a, p_node->children);
  }
  mod_free(p_a, p_node->name);
  p_node->name = NULL;
  p_node->children = NULL;
  p_node->children_count = 0;
  p_node->children_capacity = 0;
}

static pending_ns* pending_ns_child(const ok_allocators* p_a, pending_ns* p_node, const char* p_name) {
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    if (strcmp(p_node->children[i].name, p_name) == 0) {
      return &p_node->children[i];
    }
  }
  if (p_node->children_count == p_node->children_capacity) {
    const uint32_t grown = p_node->children_capacity == 0 ? 4 : p_node->children_capacity * 2;
    pending_ns* children = (pending_ns*)mod_realloc(p_a, p_node->children, sizeof(pending_ns) * grown);
    if (children == NULL) {
      return NULL;
    }
    p_node->children = children;
    p_node->children_capacity = grown;
  }
  pending_ns* child = &p_node->children[p_node->children_count++];
  pending_ns_init(p_a, child, p_name);
  return child;
}

// materialize a pending node into a live namespace instance: class (one
// field per child), freeze, instance, recurse into children, fill slots.
// slot i is child i - a fresh class assigns fields in declaration order.
static bool materialize_ns(module_system* p_modules, pending_ns* p_node, value* p_out) {
  ok* okay = p_modules->ok;
  object_specs specs = {&okay->alloc, &okay->objects_store};
  gc* collector = okay->alloc.gc;

  object_string* name = string_interner_intern(
      &okay->interner, ok_create_string_view(p_node->name, (uint32_t)strlen(p_node->name), true), &specs);
  if (name == NULL) {
    return false;
  }
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(name))) {
    return false;
  }
  object_class* klass = create_object_class(name, okay->vm.object_singleton, &specs);
  gc_guard_down(collector); // the class roots its name from here
  if (klass == NULL) {
    return false;
  }
  klass->module_namespace = true;
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(klass))) {
    return false;
  }
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    object_string* field_name = string_interner_intern(
        &okay->interner,
        ok_create_string_view(p_node->children[i].name, (uint32_t)strlen(p_node->children[i].name), true),
        &specs);
    if (field_name == NULL) {
      gc_guard_down(collector);
      return false;
    }
    if (!gc_guard_up(collector, OBJECT_AS_VALUE(field_name))) {
      gc_guard_down(collector);
      return false;
    }
    const object_string* previous_owner = NULL;
    const bool added =
        object_class_add_field(klass, field_name, false /* mut */, true /* pub: an export */,
                              NULL /* init thunk */, NULL_AS_VALUE() /* default: the loader fills the slot */,
                              &previous_owner);
    gc_guard_down(collector); // the class owns the name now
    if (!added) {
      gc_guard_down(collector);
      return false;
    }
  }
  if (!object_class_freeze(klass)) {
    gc_guard_down(collector);
    return false;
  }
  object_instance* inst = create_object_instance(klass, &specs);
  gc_guard_down(collector); // the instance roots the class
  if (inst == NULL) {
    return false;
  }
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(inst))) {
    return false;
  }
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    pending_ns* child = &p_node->children[i];
    value child_value;
    if (child->children_count == 0) {
      // a leaf: a module value (registry-rooted) or an alias-style direct value
      child_value = child->module_leaf;
    } else if (!materialize_ns(p_modules, child, &child_value)) {
      gc_guard_down(collector);
      return false;
    }
    // the assignment roots the child value; a leaf came pre-rooted, a
    // materialized branch was guarded only until this store
    inst->fields[i] = child_value;
  }
  *p_out = OBJECT_AS_VALUE(inst);
  gc_guard_down(collector);
  return true;
}

// ---------------------------------------------------------------- module loading

// build one namespace from live (name, value) pairs: the native lane's
// builder (the fn table + the mount window's exports). same shape as
// build_namespace: fresh module-namespace class, one immutable field per
// export in order, frozen, instantiated, filled. the values are either
// guarded by the caller or already rooted (the mount window is marked).
static bool build_namespace_members(module_system* p_modules,
                                    const char* p_canonical,
                                    const char* const* p_names,
                                    const value* p_values,
                                    uint32_t p_count,
                                    value* p_out) {
  ok* okay = p_modules->ok;
  object_specs specs = {&okay->alloc, &okay->objects_store};
  gc* collector = okay->alloc.gc;

  object_string* name = string_interner_intern(
      &okay->interner, ok_create_string_view(p_canonical, (uint32_t)strlen(p_canonical), true), &specs);
  if (name == NULL) {
    return false;
  }
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(name))) {
    return false;
  }
  object_class* klass = create_object_class(name, okay->vm.object_singleton, &specs);
  gc_guard_down(collector); // the class roots its name from here
  if (klass == NULL) {
    return false;
  }
  klass->module_namespace = true;
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(klass))) {
    return false;
  }
  for (uint32_t i = 0; i < p_count; ++i) {
    object_string* field_name = string_interner_intern(
        &okay->interner, ok_create_string_view(p_names[i], (uint32_t)strlen(p_names[i]), true), &specs);
    if (field_name == NULL) {
      gc_guard_down(collector);
      return false;
    }
    if (!gc_guard_up(collector, OBJECT_AS_VALUE(field_name))) {
      gc_guard_down(collector);
      return false;
    }
    const object_string* previous_owner = NULL;
    const bool added =
        object_class_add_field(klass, field_name, false /* mut */, true /* pub: an export */,
                              NULL /* init thunk */, NULL_AS_VALUE() /* default: the loader fills the slot */,
                              &previous_owner);
    gc_guard_down(collector); // the class owns the name now
    if (!added) {
      gc_guard_down(collector);
      return false;
    }
  }
  if (!object_class_freeze(klass)) {
    gc_guard_down(collector);
    return false;
  }
  object_instance* inst = create_object_instance(klass, &specs);
  gc_guard_down(collector); // the instance roots the class
  if (inst == NULL) {
    return false;
  }
  if (!gc_guard_up(collector, OBJECT_AS_VALUE(inst))) {
    return false;
  }
  for (uint32_t i = 0; i < p_count; ++i) {
    inst->fields[i] = p_values[i]; // the store roots each value
  }
  *p_out = OBJECT_AS_VALUE(inst);
  gc_guard_down(collector);
  return true;
}

// one diagnostic without an import site: the force-load lane (the CLI's
// --native). bare `error: message` + a note, same renderer as the rest.
static void plain_report(const char* p_path, const char* p_message) {
  bool panic = false;
  bool had_error = false;
  okdiag_report(&panic,
                &had_error,
                p_path,
                NULL,
                NULL,
                false,
                REPORT_SEVERITY_ERROR,
                ok_create_string_view(p_message, OK_STRING_VIEW_CALCULATE_LENGTH, true),
                NULL);
}

// OR-4: the where lane's site-less note (ok_module_where has no import
// statement to point at, the query itself is the context).
static void plain_note(const char* p_display, const char* p_text) {
  fprintf(stderr, "%s: note: %s\n", p_display, p_text);
}

// report/note through whichever lane is active: an import site, or the
// where display. p_where NULL = the normal import lane.
static void lane_report(const char* p_where, const ok_source* p_source, const token* p_site, const char* p_message) {
  if (p_where != NULL) {
    plain_report(p_where, p_message);
  } else {
    module_report(p_source, p_site, p_message);
  }
}

static void lane_note(const char* p_where, const ok_source* p_source, const token* p_site, const char* p_text) {
  if (p_where != NULL) {
    plain_note(p_where, p_text);
  } else {
    module_note(p_source, p_site, p_text);
  }
}

// the project lanes' tri-state: a miss (fall through to OKPATH), a find,
// or a hard error (already printed, resolution stops, no 404 on top).
typedef enum {
  PROJECT_MISS = 0,
  PROJECT_FOUND,
  PROJECT_ERROR,
} project_lane_result;

// the register lane's one report: at the import site when there is one,
// bare with the module's own path when there is not (force-load).
static void module_report_or_plain(const ok_source* p_from_source,
                                   const token* p_site,
                                   const char* p_display,
                                   const char* p_message) {
  if (p_from_source != NULL) {
    module_report(p_from_source, p_site, p_message);
  } else {
    plain_report(p_display, p_message);
  }
}

// a formatted report for the same two lanes (the messages that name the
// module). NULL p_message.chars degrades to p_fallback.
static void module_report_or_plain_fmt(const ok_source* p_from_source,
                                       const token* p_site,
                                       const char* p_display,
                                       const string* p_message,
                                       const char* p_fallback) {
  module_report_or_plain(p_from_source, p_site, p_display,
                         p_message->chars != NULL ? p_message->chars : p_fallback);
}

// the last path segment of a canonical, extension stripped: the import
// stem the native name check compares against ("/a/b/hello.so" -> "hello").
static bool import_stem_of(const char* p_canonical, char* p_buffer, size_t p_capacity) {
  const char* last = strrchr(p_canonical, '/');
  const char* stem = last != NULL ? last + 1 : p_canonical;
  size_t length = strlen(stem);
  const char* ext = ok_dylib_extension();
  const size_t ext_length = strlen(ext);
  if (length > ext_length && memcmp(stem + length - ext_length, ext, ext_length) == 0) {
    length -= ext_length;
  }
  if (length == 0 || length + 1 > p_capacity) {
    return false;
  }
  memcpy(p_buffer, stem, length);
  p_buffer[length] = '\0';
  return true;
}

// the native registration: everything after load_native said yes. checks
// abi + name, runs the lifecycle, stages the members, freezes, registers.
// p_key overrides the registry key (the force-load lane registers under
// the declared name); p_display names the shared object in diagnostics.
// p_from_source/p_site locate the import site (both NULL = force-load).
static ok_result register_native(module_system* p_modules,
                                  const ok_native_module* p_descriptor,
                                  void* p_handle,
                                  const char* p_key,
                                  const char* p_display,
                                  const ok_source* p_from_source,
                                  const token* p_site,
                                  const char* p_import_stem) {
  ok* okay = p_modules->ok;
  const ok_allocators* a = &okay->raw_alloc;

  // the abi check (patch 52, semver-shaped): major must match, minor must be
  // a real version at most the host's. an older minor loads unchanged (the
  // additions were append-only, the old entries never moved), a newer minor
  // refuses with the upgrade message, a major mismatch with the rebuild
  // message. minor 0 is not a version: a zeroed descriptor is a rebuild.
  // (both directions is the module's own job at export time, comparing
  // against the api table it was handed)
  {
    const uint32_t module_major = p_descriptor->abi >> 16;
    const uint32_t module_minor = p_descriptor->abi & 0xffffu;
    if (module_major != OK_ABI_MAJOR) {
      string message =
          asprint(&okay->alloc,
                  "module '%s': abi major %u, expected %u, rebuild it against include/ok/ok_module.h.",
                  p_display,
                  (unsigned)module_major,
                  (unsigned)OK_ABI_MAJOR);
      module_report_or_plain_fmt(p_from_source, p_site, p_display, &message, "native module abi mismatch.");
      string_deinit(&message, &okay->alloc);
      return OK_COMPILE_ERROR;
    }
    if (module_minor == 0 || module_minor > OK_ABI_MINOR) {
      string message = asprint(&okay->alloc,
                               "module '%s': abi %u.%u is newer than this ok (%u.%u), upgrade ok or rebuild the "
                               "module against include/ok/ok_module.h.",
                               p_display,
                               (unsigned)module_major,
                               (unsigned)module_minor,
                               (unsigned)OK_ABI_MAJOR,
                               (unsigned)OK_ABI_MINOR);
      module_report_or_plain_fmt(p_from_source, p_site, p_display, &message, "native module abi mismatch.");
      string_deinit(&message, &okay->alloc);
      return OK_COMPILE_ERROR;
    }
  }
  // the name check (graph loads): the descriptor's declared name and the
  // import's stem must agree, so a moved or renamed .so cannot lie about
  // what it is. force-loads skip it (the name IS the registration).
  if (p_import_stem != NULL && strcmp(p_descriptor->name, p_import_stem) != 0) {
    string message =
        asprint(&okay->alloc, "the shared object declares the name '%s', the import stem is '%s'.", p_descriptor->name,
                p_import_stem);
    module_report_or_plain_fmt(p_from_source, p_site, p_display, &message, "native module name mismatch.");
    string_deinit(&message, &okay->alloc);
    return OK_COMPILE_ERROR;
  }

  // on_load: the module's C init, before anything else touches it. a failed
  // on_load fires on_unload (its cleanup chance) and refuses the load.
  if (p_descriptor->on_load != NULL && !p_descriptor->on_load(okay)) {
    if (p_descriptor->on_unload != NULL) {
      p_descriptor->on_unload(okay);
    }
    string message = asprint(&okay->alloc, "module '%s': the on_load callback failed.", p_display);
    module_report_or_plain_fmt(p_from_source, p_site, p_display, &message, "native on_load failed.");
    string_deinit(&message, &okay->alloc);
    return OK_COMPILE_ERROR;
  }

  // stage the fn members (guarded one at a time into the marked mount
  // window, then the values list carries them). the window is marked for
  // the WHOLE staging+mount+build stretch: aggressive gc collects on every
  // allocation, and the staged values ride the window until the namespace
  // instance's fields root them (the matrix caught exactly this window).
  native_mount mount;
  mount.names = NULL;
  value_array_init(&mount.values, &okay->alloc);
  mount.count = 0;
  mount.capacity = 0;
  mount.failure = NULL;
  p_modules->mounting = &mount;

  for (uint32_t i = 0; i < p_descriptor->fn_count; ++i) {
    const ok_native_fn_def* fn = &p_descriptor->fns[i];
    if (fn->name == NULL || fn->call == NULL) {
      continue;
    }
    if (fn->arity != OK_NATIVE_ARITY_ANY && fn->arity > UINT8_MAX) {
      mount.failure = "a native function arity above 255 is not representable.";
      break;
    }
    // duplicate fn names refuse the load (M13's collision rule)
    bool duplicate = false;
    for (uint32_t j = 0; j < mount.count; ++j) {
      if (strcmp(mount.names[j], fn->name) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      mount.failure = "duplicate export name in the fn table.";
      break;
    }
    if (mount.count == mount.capacity) {
      const uint32_t grown = mount.capacity == 0 ? 8 : mount.capacity * 2;
      char** names = mod_realloc(a, mount.names, sizeof(char*) * grown);
      if (names == NULL) {
        mount.failure = "out of memory while staging the module's functions.";
        break;
      }
      mount.names = names;
      mount.capacity = grown;
    }
    object_specs obj_specs = {&okay->alloc, &okay->objects_store};
    object_native_function* native = create_object_native_function(
        ok_create_string_view(fn->name, (uint32_t)strlen(fn->name), true), fn->arity, fn->call, &obj_specs);
    if (native == NULL) {
      mount.failure = "out of memory while staging the module's functions.";
      break;
    }
    if (!gc_guard_up(&okay->gc, OBJECT_AS_VALUE(native))) {
      mount.failure = "out of memory while staging the module's functions.";
      break;
    }
    if (!value_array_append(&mount.values, OBJECT_AS_VALUE(native))) {
      gc_guard_down(&okay->gc);
      mount.failure = "out of memory while staging the module's functions.";
      break;
    }
    gc_guard_down(&okay->gc); // the marked window roots it now
    mount.names[mount.count++] = mod_strdup(a, fn->name);
    if (mount.names[mount.count - 1] == NULL) {
      mount.count--; // the value stays staged, the name slot is retried never: OOM refuses below
      mount.failure = "out of memory while staging the module's functions.";
      break;
    }
  }

  // the mount window: the class surface (and any constants) joins the same
  // list, api->module_export appends through the same marked window
  if (mount.failure == NULL && p_descriptor->mount_classes != NULL) {
    const bool mounted = p_descriptor->mount_classes(okay);
    if (!mounted) {
      mount.failure = mount.failure != NULL ? mount.failure : "the mount callback failed.";
    }
  }

  ok_result result = OK;
  value namespace = NULL_AS_VALUE();
  if (mount.failure != NULL) {
    string message = asprint(&okay->alloc, "module '%s': %s", p_display, mount.failure);
    module_report_or_plain_fmt(p_from_source, p_site, p_display, &message, "native module mount failed.");
    string_deinit(&message, &okay->alloc);
    result = OK_COMPILE_ERROR;
  } else if (!build_namespace_members(p_modules, p_key, (const char* const*)mount.names, mount.values.data,
                                      mount.count, &namespace)) {
    module_report_or_plain(p_from_source, p_site, p_display, "out of memory while building the module's namespace.");
    result = OK_INIT_ERROR;
  } else {
    ok_module_record record;
    record.canonical = mod_strdup(a, p_key);
    record.name = VALUE_AS_INSTANCE(namespace)->klass->name;
    record.namespace_class = VALUE_AS_INSTANCE(namespace)->klass;
    record.namespace = namespace;
    record.imports = NULL;
    record.imports_count = 0;
    record.is_native = true;
    record.descriptor = p_descriptor;
    if (!gc_guard_up(&okay->gc, namespace)) {
      ok_module_record_deinit(&record, &okay->alloc);
      module_report_or_plain(p_from_source, p_site, p_display, "out of memory while registering the module.");
      result = OK_INIT_ERROR;
    } else {
      const bool registered = record.canonical != NULL && ok_module_records_append(&p_modules->loaded, record);
      gc_guard_down(&okay->gc);
      if (!registered) {
        ok_module_record_deinit(&record, &okay->alloc);
        module_report_or_plain(p_from_source, p_site, p_display, "out of memory while registering the module.");
        result = OK_INIT_ERROR;
      }
    }
  }

  p_modules->mounting = NULL; // the namespace (or the failure) owns everything now

  for (uint32_t i = 0; i < mount.count; ++i) {
    mod_free(a, mount.names[i]);
  }
  if (mount.names != NULL) {
    mod_free(a, mount.names);
  }
  value_array_deinit(&mount.values, &okay->alloc);
  (void)p_handle; // closed by the default lane at ok_free, never mid-run
  return result;
}

// load (or find) one module, depth-first. the site + the importing source
// position every diagnostic this can raise; a NULL site is the force-load
// lane (bare reporting, the module's own path as the location).
static ok_result load_module(module_system* p_modules,
                            resolved_module* p_resolved,
                            const ok_source* p_from_source,
                            const token* p_site) {
  ok* okay = p_modules->ok;
  const ok_allocators* a = &okay->raw_alloc;
  const char* p_canonical = p_resolved->canonical;

  // loaded already? one TU per module, ever (the registry is the cache).
  // the probe's read (a cache hit re-read the source for nothing) dies
  // here. the RESOLVED stays the caller's: prepare still views the canonical.
  ok_module_record* found = registry_find(p_modules, p_canonical);
  if (found != NULL) {
    resolved_consume_code(a, p_resolved);
    return OK;
  }
  // in the active chain? a cycle (M13): the design is in the importer's
  // graph, and the error walks the chain so the fix is obvious.
  for (uint32_t i = 0; i < p_modules->chain_depth; ++i) {
    if (strcmp(p_modules->chain[i], p_canonical) == 0) {
      string chain = asprint(&okay->alloc, "the import chain is:");
      for (uint32_t j = i; j < p_modules->chain_depth; ++j) {
        string part = asprint(&okay->alloc, "%s %s ->", chain.chars, p_modules->chain[j]);
        string_deinit(&chain, &okay->alloc);
        chain = part;
      }
      string part = asprint(&okay->alloc, "%s %s (back to the start)", chain.chars, p_canonical);
      string_deinit(&chain, &okay->alloc);
      chain = part;
      module_report(p_from_source, p_site, "circular import: this module is already being loaded.");
      module_note(p_from_source, p_site, chain.chars);
      string_deinit(&chain, &okay->alloc);
      return OK_COMPILE_ERROR;
    }
  }
  if (p_modules->chain_depth >= OK_MODULES_MAX_DEPTH) {
    module_report(p_from_source,
                  p_site,
                  "import chain too deep: the limit is 128.");
    return OK_COMPILE_ERROR;
  }

  // the native lane: the loader already opened the shared object and handed
  // back the descriptor. the import's stem rides the chain for the name
  // check (binding_segments' spelling of the last segment).
  if (p_resolved->is_native) {
    char stem[256];
    return register_native(p_modules, p_resolved->descriptor, p_resolved->handle, p_canonical, p_canonical,
                           p_from_source, p_site, import_stem_of(p_canonical, stem, sizeof(stem)) ? stem : NULL);
  }

  // parse (the source is the probe's read, its path the canonical)
  parser parser;
  parser_specs parser_specs = {.alloc = &okay->alloc, .source = &p_resolved->source};
  parser_init(&parser, parser_specs);
  parse_result parsed = parser_parse(&parser);
  if (parsed.status != PARSE_OK) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    resolved_consume_code(a, p_resolved);
    p_resolved->source.code = NULL;
    return OK_PARSE_ERROR;
  }

  // the module's own imports, depth-first (the chain guards the recursion)
  p_modules->chain[p_modules->chain_depth++] = p_canonical;
  ok_import_binding_array bindings;
  ok_import_binding_array_init(&bindings, &okay->alloc);
  char** imports_owned = NULL; // ownership moves to the record below
  uint32_t imports_owned_count = 0;
  const ok_result prepared = ok_modules_prepare(
      p_modules, parsed.root, &p_resolved->source, &bindings, &imports_owned, &imports_owned_count);
  p_modules->chain_depth--;
  if (prepared != OK) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    ok_import_binding_array_deinit(&bindings, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    return prepared;
  }

  // compile in module mode: pub top-levels recorded, the export epilogue
  // emitted, an early top-level return refused.
  compile_specs compile_specs;
  compile_specs.source = &p_resolved->source;
  compile_specs.root = parsed.root;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(okay, &err);
  if (err) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    ok_import_binding_array_deinit(&bindings, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    return OK_INIT_ERROR;
  }
  compile_specs.imports = (ok_import_bindings){bindings.data, bindings.count};
  compile_specs.is_module = true;
  compile_specs.module_path = ok_create_string_view(p_canonical, (uint32_t)strlen(p_canonical), true);
  compile_result compiled = compiler_compile(&okay->compiler, compile_specs);
  top_level_declarations_deinit(&compile_specs.top_level_declarations, &okay->alloc);
  ok_import_binding_array_deinit(&bindings, &okay->alloc);
  parser_deinit(&parser);
  if (compiled.status != COMPILE_OK) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    return OK_COMPILE_ERROR;
  }

  // the compiled module function rides unrooted from compiler_compile until
  // this guard: vm_begin_source below allocates, and under aggressive gc that
  // collection must not sweep the tree ok_stamp_source_ids is about to walk.
  if (!gc_guard_up(&okay->gc, OBJECT_AS_VALUE(compiled.function))) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    module_report(p_from_source, p_site, "out of memory while running the module's top level.");
    return OK_INIT_ERROR;
  }
  // run: one source registration per module (stack traces stay honest in
  // multi-file runs), seeds staged like any <main>.
  const uint32_t source_id = vm_begin_source(&okay->vm, p_resolved->source.code, p_resolved->source.path);
  if (source_id == VM_SOURCE_UNSTAMPED) {
    gc_guard_down(&okay->gc);
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    return OK_INIT_ERROR;
  }
  ok_stamp_source_ids(compiled.function, source_id);
  err = false;
  interpret_specs interpret_specs;
  interpret_specs.function = compiled.function;
  interpret_specs.source_id = source_id;
  interpret_specs.top_level_values = ok_get_top_level_values(okay, &err);
  const interpret_result ran = err ? (interpret_result){0} : vm_interpret(&okay->vm, interpret_specs);
  gc_guard_down(&okay->gc);
  value_array_deinit(&interpret_specs.top_level_values, &okay->alloc);
  vm_end_source(&okay->vm, source_id);
  if (err || ran.status != RUNTIME_OK) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    // the runtime error already printed with the module's own path and
    // trace; this note ties it to the import that pulled it in.
    module_note(p_from_source, p_site, "while loading this module (its top level failed).");
    return OK_RUNTIME_ERROR;
  }

  // harvest: hidden globals -> the sealed namespace instance -> the registry
  value namespace = NULL_AS_VALUE();
  if (!build_namespace(p_modules, p_canonical, &compiled.exports, &namespace)) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    module_report(p_from_source, p_site, "out of memory while building the module's namespace.");
    return OK_INIT_ERROR;
  }
  ok_module_record record;
  record.canonical = mod_strdup(a, p_canonical);
  record.name = VALUE_AS_INSTANCE(namespace)->klass->name;
  record.namespace_class = VALUE_AS_INSTANCE(namespace)->klass;
  record.namespace = namespace;
  record.imports = imports_owned;
  record.imports_count = imports_owned_count;
  record.is_native = false;
  record.descriptor = NULL;
  if (!gc_guard_up(&okay->gc, namespace)) {
    ok_module_record_deinit(&record, &okay->alloc);
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    resolved_consume_code(a, p_resolved);
    module_report(p_from_source, p_site, "out of memory while registering the module.");
    return OK_INIT_ERROR;
  }
  const bool registered = record.canonical != NULL && ok_module_records_append(&p_modules->loaded, record);
  gc_guard_down(&okay->gc);
  parse_result_deinit(&parsed);
  module_exports_deinit(&compiled.exports, &okay->alloc);
  resolved_consume_code(a, p_resolved);
  if (!registered) {
    ok_module_record_deinit(&record, &okay->alloc);
    module_report(p_from_source, p_site, "out of memory while registering the module.");
    return OK_INIT_ERROR;
  }
  return OK;
}

// ---------------------------------------------------------------- the std modules (OR-2)

// the built-in std registry. the std root resolves against this table ONLY
// (M8): never the filesystem, never OKPATH, nothing can shadow it. the
// members are the same native function objects the flat spellings use,
// harvested from their hidden globals into the same sealed-namespace shape
// file modules get, so a std module value IS a module value: same freezes,
// same print shape, same from-import probe. math and str are importable
// today and open empty, the stdlib growth version fills them, and the
// resolution lane does not change when it does.
typedef struct {
  const char* export_name;
  const char* hidden_global;
} std_member_def;

typedef struct {
  const char* stem;
  const std_member_def* members;
  uint32_t member_count;
} std_module_def;

static const std_member_def std_array_members[] = {
    {"len", " len"},
    {"push", " push"},
    {"pop", " pop"},
    {"remove", " remove"},
};
static const std_member_def std_map_members[] = {
    {"len", " len"},
    {"remove", " remove"},
    {"get", " get"},
};
static const std_member_def std_io_members[] = {
    {"print", " print"},
};
static const std_module_def std_modules[] = {
    {"array", std_array_members, sizeof(std_array_members) / sizeof(std_array_members[0])},
    {"io", std_io_members, sizeof(std_io_members) / sizeof(std_io_members[0])},
    {"map", std_map_members, sizeof(std_map_members) / sizeof(std_map_members[0])},
    {"math", NULL, 0},
    {"str", NULL, 0},
};

static const std_module_def* std_module_find(const ok_string_view p_stem) {
  for (uint32_t i = 0; i < sizeof(std_modules) / sizeof(std_modules[0]); ++i) {
    if (view_equals(p_stem, std_modules[i].stem)) {
      return &std_modules[i];
    }
  }
  return NULL;
}

// the registry key and display name of one std module: "std/<stem>". no
// leading slash, so it can never collide with a realpath'd file key (those
// are absolute, always).
static bool std_canonical_of(const std_module_def* p_def, char* p_buffer, size_t p_capacity) {
  const int written = snprintf(p_buffer, p_capacity, "std/%s", p_def->stem);
  return written > 0 && (size_t)written < p_capacity;
}

// builds and registers one std module, idempotent through the registry.
// built lazily on first import: an embedder that never touches std never
// pays for it.
static ok_result std_build_module(module_system* p_modules, const std_module_def* p_def) {
  ok* okay = p_modules->ok;
  const ok_allocators* a = &okay->raw_alloc;
  char canonical[32];
  if (!std_canonical_of(p_def, canonical, sizeof(canonical))) {
    return OK_INIT_ERROR;
  }
  if (registry_find(p_modules, canonical) != NULL) {
    return OK;
  }

  module_exports exports;
  module_exports_init(&exports, &okay->alloc);
  ok_result result = OK;
  for (uint32_t i = 0; i < p_def->member_count; ++i) {
    module_export entry;
    entry.name = ok_create_string_view(p_def->members[i].export_name,
                                       (uint32_t)strlen(p_def->members[i].export_name),
                                       true);
    entry.local = 0; // unused: only the compiler's epilogue reads it
    entry.hidden = globals_store_get(&okay->globals_store,
                                     ok_create_string_view(p_def->members[i].hidden_global,
                                                           (uint32_t)strlen(p_def->members[i].hidden_global),
                                                           true));
    if (!IS_GLOBAL_VALID(entry.hidden) || !module_exports_append(&exports, entry)) {
      result = OK_INIT_ERROR;
      break;
    }
  }

  if (result == OK) {
    value namespace = NULL_AS_VALUE();
    if (!build_namespace(p_modules, canonical, &exports, &namespace)) {
      result = OK_INIT_ERROR;
    } else {
      ok_module_record record;
      record.canonical = mod_strdup(a, canonical);
      record.name = VALUE_AS_INSTANCE(namespace)->klass->name;
      record.namespace_class = VALUE_AS_INSTANCE(namespace)->klass;
      record.namespace = namespace;
      record.imports = NULL;
      record.imports_count = 0;
      record.is_native = false;
      record.descriptor = NULL;
      if (!gc_guard_up(&okay->gc, namespace)) {
        ok_module_record_deinit(&record, &okay->alloc);
        result = OK_INIT_ERROR;
      } else {
        const bool registered = record.canonical != NULL && ok_module_records_append(&p_modules->loaded, record);
        gc_guard_down(&okay->gc);
        if (!registered) {
          ok_module_record_deinit(&record, &okay->alloc);
          result = OK_INIT_ERROR;
        }
      }
    }
  }
  module_exports_deinit(&exports, &okay->alloc);
  return result;
}

// ---------------------------------------------------------------- resolution

// resolve one import path from one importing file. produces the resolved
// module (canonical + the loader's read, or the native lane's descriptor),
// or prints the teaching 404 with the full trace of locations and forms
// tried (and returns false).
static project_lane_result resolve_in_project(module_system* p_modules,
                                              const ok_project* p_project,
                                              const ok_string_view p_path,
                                              str_list* p_tried,
                                              char* p_native_error,
                                              size_t p_native_error_capacity,
                                              const ok_source* p_source,
                                              const token* p_site,
                                              const char* p_where,
                                              uint32_t p_depth,
                                              resolved_module* p_out);

static bool resolve_import(module_system* p_modules,
                           const ok_string_view p_path,
                           const char* p_from_dir,
                           const ok_source* p_source,
                           const token* p_site,
                           const char* p_where,
                           resolved_module* p_out) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;
  str_list tried;
  str_list_init(&tried);
  char native_error[256];
  native_error[0] = '\0';
  bool resolved = false;

  if (view_starts_with(p_path, "./") || view_starts_with(p_path, "../")) {
    // the file-relative lane (M3): raw ok files and natives, no project
    // needed.
    char* joined = join_normalize(a, p_from_dir, p_path);
    if (joined != NULL) {
      resolved = probe_ladder(p_modules, joined, &tried, native_error, sizeof(native_error), p_where != NULL, p_out);
      mod_free(a, joined);
    }
  } else if (view_equals(p_path, "std")) {
    // the bare root imports nothing: the std library is a tree of modules.
    module_report(p_source,
                  p_site,
                  "the std root is a namespace: import a module under it, like \"std/array\".");
    str_list_deinit(&tried, a);
    return false;
  } else if (view_starts_with(p_path, "std/")) {
    // the std lane (M8, OR-2): the built-in registry only. it never falls
    // through to OKPATH, so no directory on disk can shadow the standard
    // library.
    const ok_string_view stem = ok_create_string_view(p_path.chars + 4,
                                                      ok_string_view_get_length(&p_path) - 4,
                                                      false);
    const std_module_def* def = std_module_find(stem);
    if (def == NULL) {
      string message = asprint(&p_modules->ok->alloc,
                               "there is no std module '%.*s': the std modules are array, io, map, math, str.",
                               (int)ok_string_view_get_length(&stem),
                               stem.chars);
      module_report(p_source, p_site, message.chars != NULL ? message.chars : "no such std module.");
      string_deinit(&message, &p_modules->ok->alloc);
      str_list_deinit(&tried, a);
      return false;
    }
    if (p_where == NULL && std_build_module(p_modules, def) != OK) {
      module_report(p_source, p_site, "out of memory while building the module's namespace.");
      str_list_deinit(&tried, a);
      return false;
    }
    char canonical[32];
    if (!std_canonical_of(def, canonical, sizeof(canonical))) {
      str_list_deinit(&tried, a);
      return false;
    }
    p_out->canonical = mod_strdup(a, canonical);
    if (p_out->canonical == NULL) {
      str_list_deinit(&tried, a);
      return false;
    }
    str_list_deinit(&tried, a);
    return true;
  } else {
    // a bare name: a force-loaded native answers first (its registry key is
    // its declared name, ok_native_force_load's lane). the copy is
    // length-bounded: the import literal is a lexer slice, not a C string.
    if (registry_find_native(p_modules, p_path) != NULL) {
      const uint64_t length = ok_string_view_get_length(&p_path);
      char* owned = (char*)mod_alloc(a, length + 1);
      if (owned != NULL) {
        memcpy(owned, p_path.chars, length);
        owned[length] = '\0';
      }
      p_out->canonical = owned;
      p_out->is_native = true;
      str_list_deinit(&tried, a);
      return p_out->canonical != NULL;
    }
    // the project lane (OR-4, the draft's section 5 step 3): the governing
    // project's module roots, then its native targets, then its deps by
    // name, recursing into project deps. the governing project is the
    // deepest installed root containing the importing file's directory.
    if (!resolved && p_modules->projects_count > 0) {
      // canonicalize_path DESTROYS its input (the ladder's ownership
      // discipline): the from_dir is the caller's, a copy goes in
      char* from_dir_copy = mod_strdup(a, p_from_dir);
      char* canon_dir = from_dir_copy != NULL ? canonicalize_path(a, from_dir_copy) : NULL;
      if (canon_dir != NULL) {
        const ok_project* governor = NULL;
        size_t governor_length = 0;
        for (uint32_t i = 0; i < p_modules->projects_count; ++i) {
          const ok_project* candidate = &p_modules->projects[i];
          const size_t root_length = strlen(candidate->root);
          const size_t dir_length = strlen(canon_dir);
          const bool under = dir_length == root_length ||
                             (dir_length > root_length && canon_dir[root_length] == '/' &&
                              memcmp(canon_dir, candidate->root, root_length) == 0);
          if (under && root_length >= governor_length) {
            governor = candidate;
            governor_length = root_length;
          }
        }
        mod_free(a, canon_dir);
        if (governor != NULL) {
          const project_lane_result lane = resolve_in_project(p_modules, governor, p_path, &tried, native_error,
                                                              sizeof(native_error), p_source, p_site, p_where, 0,
                                                              p_out);
          if (lane == PROJECT_FOUND) {
            resolved = true;
          } else if (lane == PROJECT_ERROR) {
            // the hard error already printed (a refused capability, a
            // refused build, a project dep imported bare): no 404 on top
            str_list_deinit(&tried, a);
            return false;
          }
        }
      }
    }
    // the OKPATH lane: colon-separated roots, the same ladder at each.
    const char* okpath = getenv("OKPATH");
    if (okpath != NULL && *okpath != '\0') {
      char* roots = mod_strdup(a, okpath);
      if (roots != NULL) {
        char* cursor = roots;
        while (cursor != NULL && *cursor != '\0' && !resolved) {
          char* colon = strchr(cursor, ':');
          if (colon != NULL) {
            *colon = '\0';
          }
          if (*cursor != '\0') {
            char* joined = join_normalize(a, cursor, p_path);
            if (joined != NULL) {
              resolved = probe_ladder(p_modules, joined, &tried, native_error, sizeof(native_error), p_where != NULL,
                                      p_out);
              mod_free(a, joined);
            }
          }
          cursor = colon != NULL ? colon + 1 : NULL;
        }
        mod_free(a, roots);
      }
    }
  }

  if (!resolved) {
    // a native candidate existed but is broken: the hard error, its own
    // message is the note (a text file named .so, a corrupt image, a
    // missing ok_native_export)
    if (native_error[0] != '\0') {
      lane_report(p_where, p_source, p_site, "the native module could not be loaded.");
      lane_note(p_where, p_source, p_site, native_error);
      str_list_deinit(&tried, a);
      return false;
    }
    // the 404 must teach (M3): every location and form tried, plus the hint.
    lane_report(p_where, p_source, p_site, "module not found.");
    for (uint32_t i = 0; i < tried.count; ++i) {
      lane_note(p_where, p_source, p_site, tried.items[i]);
    }
    if (p_where == NULL && (p_modules->ok->caps_dropped & OK_CAP_NATIVE_LOAD) != 0) {
      lane_note(p_where, p_source, p_site,
                "native modules are not searched: OK_CAP_NATIVE_LOAD is dropped in this ok instance.");
    }
    if (tried.count == 0) {
      lane_note(p_where, p_source, p_site, "no locations were searched: file-relative paths need './' or '../', "
                                           "bare names search the project roots and OKPATH (unset).");
    } else {
      lane_note(p_where, p_source, p_site, "add the module's directory to OKPATH, or fix the path.");
    }
    str_list_deinit(&tried, a);
    return false;
  }
  str_list_deinit(&tried, a);
  return true;
}

// the binding segments (M5): the import literal's own segments, '.' skipped,
// '..' popped where possible and dropped at the front (they climb toward the
// importer's directory - the NAME is the remainder), a trailing '.ok' or the
// native extension stripped. "a/b" -> [a, b]; "./util.ok" -> [util];
// "../lib/h" -> [lib, h]; "./x.so" -> [x].
static bool binding_segments(const ok_allocators* p_a,
                              const ok_string_view p_path,
                              char*** p_out,
                              uint32_t* p_out_count) {
  const uint64_t length = ok_string_view_get_length(&p_path);
  const char* ext = ok_dylib_extension();
  const uint64_t ext_length = (uint64_t)strlen(ext);
  char** segments = NULL;
  uint32_t count = 0;
  uint32_t capacity = 0;
  uint64_t i = 0;
  while (i < length) {
    uint64_t j = i;
    while (j < length && p_path.chars[j] != '/') {
      ++j;
    }
    const uint64_t seg_length = j - i;
    const bool is_dot = seg_length == 1 && p_path.chars[i] == '.';
    const bool is_dotdot = seg_length == 2 && p_path.chars[i] == '.' && p_path.chars[i + 1] == '.';
    if (is_dotdot && count > 0) {
      mod_free(p_a, segments[--count]);
    } else if (!is_dot && !is_dotdot && seg_length > 0) {
      if (count == capacity) {
        const uint32_t grown = capacity == 0 ? 4 : capacity * 2;
        char** grown_segments = (char**)mod_realloc(p_a, segments, sizeof(char*) * grown);
        if (grown_segments == NULL) {
          goto oom;
        }
        segments = grown_segments;
        capacity = grown;
      }
      uint64_t name_length = seg_length;
      if (name_length > 3 && memcmp(p_path.chars + i + name_length - 3, ".ok", 3) == 0) {
        name_length -= 3;
      } else if (name_length > ext_length && memcmp(p_path.chars + i + name_length - ext_length, ext, ext_length) == 0) {
        name_length -= ext_length;
      }
      if (name_length == 0) {
        i = j + 1;
        continue;
      }
      char* segment = (char*)mod_alloc(p_a, name_length + 1);
      if (segment == NULL) {
        goto oom;
      }
      memcpy(segment, p_path.chars + i, name_length);
      segment[name_length] = '\0';
      segments[count++] = segment;
    }
    i = j + 1;
  }
  if (count == 0) {
    goto oom; // a path of only dots has nothing to bind
  }
  *p_out = segments;
  *p_out_count = count;
  return true;
oom:
  for (uint32_t k = 0; k < count; ++k) {
    mod_free(p_a, segments[k]);
  }
  if (segments != NULL) {
    mod_free(p_a, segments);
  }
  return false;
}

static void free_segments(const ok_allocators* p_a, char** p_segments, uint32_t p_count) {
  for (uint32_t i = 0; i < p_count; ++i) {
    mod_free(p_a, p_segments[i]);
  }
  if (p_segments != NULL) {
    mod_free(p_a, p_segments);
  }
}

// ---------------------------------------------------------------- prepare (the graph phase)

ok_result ok_modules_prepare(module_system* p_modules,
                             ast_root* p_root,
                             const ok_source* p_source,
                             ok_import_binding_array* p_bindings,
                             char*** p_out_imports,
                             uint32_t* p_out_imports_count) {
  ok* okay = p_modules->ok;
  const ok_allocators* a = &okay->raw_alloc;
  char* from_dir = directory_of(a, p_source->path);
  if (from_dir == NULL) {
    return OK_INIT_ERROR;
  }
  ok_result result = OK;

  // the TU's resolved imports (the graph edges): deduped canonical paths,
  // load order. ownership moves to the caller on success (the record, or
  // the entry run's discard), stays here on failure.
  char** imports = NULL;
  uint32_t imports_count = 0;
  uint32_t imports_capacity = 0;

  // the TU's root forest + the deferred bindings awaiting their roots
  pending_ns forest;
  pending_ns_init(a, &forest, "");
  uint32_t* deferred = NULL;      // binding indices (raw user allocators)
  char** deferred_roots = NULL; // parallel: the arena'd root name each lands under
  uint32_t deferred_count = 0;
  uint32_t deferred_capacity = 0;
  ok_string_view* bound_roots = NULL; // root names already declared in this TU (M5 merging)
  uint32_t bound_count = 0;
  uint32_t bound_capacity = 0;

  for (uint32_t s = 0; s < p_root->statements.count && result == OK; ++s) {
    ast_statement* statement = p_root->statements.data[s];
    if (statement->node.node_type != AST_IMPORT_STATEMENT) {
      continue;
    }
    ast_import_statement* import = (ast_import_statement*)statement;
    const ok_string_view path = ast_import_statement_path(import);
    const token* site = &import->statement.node.token;

    char* canonical = NULL;
    resolved_module resolved;
    resolved_init(&resolved);
    if (!resolve_import(p_modules, path, from_dir, p_source, site, NULL, &resolved)) {
      result = OK_COMPILE_ERROR;
      break;
    }
    canonical = resolved.canonical; // borrowed view for the edge bookkeeping
    const ok_result loaded = load_module(p_modules, &resolved, p_source, site);
    if (loaded != OK) {
      resolved_deinit(a, &resolved);
      result = loaded;
      break;
    }
    ok_module_record* record = registry_find(p_modules, canonical);
    // cannot be NULL: load_module just registered it (or it was there)
    if (record == NULL) {
      resolved_deinit(a, &resolved);
      module_report(p_source, site, "internal error: the loaded module did not register.");
      result = OK_INIT_ERROR;
      break;
    }
    // record the edge (dedup: the same module imported twice lists once)
    bool edge_new = true;
    for (uint32_t e = 0; e < imports_count; ++e) {
      if (strcmp(imports[e], canonical) == 0) {
        edge_new = false;
        break;
      }
    }
    if (edge_new) {
      if (imports_count == imports_capacity) {
        const uint32_t grown = imports_capacity == 0 ? 8 : imports_capacity * 2;
        char** grown_imports = (char**)mod_realloc(a, imports, sizeof(char*) * grown);
        if (grown_imports == NULL) {
          resolved_deinit(a, &resolved);
          result = OK_INIT_ERROR;
          break;
        }
        imports = grown_imports;
        imports_capacity = grown;
      }
      imports[imports_count] = mod_strdup(a, canonical);
      if (imports[imports_count] == NULL) {
        resolved_deinit(a, &resolved);
        result = OK_INIT_ERROR;
        break;
      }
      imports_count++;
    }
    resolved_deinit(a, &resolved);
    canonical = NULL;

    if (import->symbols.count == 0) {
      // the path form: `import "a/b";` binds the root; `import "a/b" as c;`
      // binds the module itself.
      if (import->has_alias) {
        const ok_import_binding binding = {
            .name = ok_create_string_view(import->alias.start, import->alias.length, false),
            .value = record->namespace,
            .merge = false,
        };
        if (!ok_import_binding_array_append(p_bindings, binding)) {
          result = OK_INIT_ERROR;
          break;
        }
        continue;
      }
      char** segments = NULL;
      uint32_t segment_count = 0;
      if (!binding_segments(a, path, &segments, &segment_count)) {
        module_report(p_source, site, "the import path has no name to bind.");
        result = OK_COMPILE_ERROR;
        break;
      }
      // the root name goes to the per-run arena: the binding table and the
      // bound-roots check view it past this statement (the segment buffer
      // dies at the end of it).
      char* root_owned = arena_strdup(p_modules, segments[0]);
      if (root_owned == NULL) {
        free_segments(a, segments, segment_count);
        result = OK_INIT_ERROR;
        break;
      }
      const ok_string_view root_name =
          ok_create_string_view(root_owned, (uint32_t)strlen(root_owned), true);
      // the reserved root (M8): only the std/ lane may bind `std`. a user
      // file or directory named std (any relative or OKPATH path whose
      // first segment is std) still cannot.
      if (view_equals(root_name, "std") && !view_starts_with(path, "std/")) {
        module_report(p_source,
                      site,
                      "the name 'std' is reserved for the standard library's namespace.");
        free_segments(a, segments, segment_count);
        result = OK_COMPILE_ERROR;
        break;
      }
      // already bound in this TU? the merge lane (M5): no redeclaration, no
      // code - the root namespace gains the new stem below.
      bool already_bound = false;
      for (uint32_t b = 0; b < bound_count; ++b) {
        if (ok_string_view_get_length(&bound_roots[b]) == ok_string_view_get_length(&root_name) &&
            memcmp(bound_roots[b].chars, root_name.chars, ok_string_view_get_length(&root_name)) == 0) {
          already_bound = true;
          break;
        }
      }
      if (!already_bound) {
        if (bound_count == bound_capacity) {
          const uint32_t grown = bound_capacity == 0 ? 4 : bound_capacity * 2;
          ok_string_view* grown_roots =
              (ok_string_view*)mod_realloc(a, bound_roots, sizeof(ok_string_view) * grown);
          if (grown_roots == NULL) {
            free_segments(a, segments, segment_count);
            result = OK_INIT_ERROR;
            break;
          }
          bound_roots = grown_roots;
          bound_capacity = grown;
        }
        bound_roots[bound_count++] = root_name;
      }
      // walk/create the node chain, leaf = the module value. the nodes own
      // their names, so the segment buffer can die with the statement.
      pending_ns* node = &forest;
      bool conflict = false;
      for (uint32_t i = 0; i < segment_count; ++i) {
        pending_ns* child = pending_ns_child(a, node, segments[i]);
        if (child == NULL) {
          free_segments(a, segments, segment_count);
          result = OK_INIT_ERROR;
          break;
        }
        node = child;
        if (node->has_leaf && i + 1 < segment_count) {
          // a path cannot bind a module and a namespace at once (M13 doctrine)
          module_report(p_source,
                        site,
                        "import path conflict: this path binds a namespace under a path that is already a "
                        "module.");
          conflict = true;
          break;
        }
        if (node->children_count > 0 && i + 1 == segment_count) {
          module_report(p_source,
                        site,
                        "import path conflict: this path binds a module where a namespace already lives.");
          conflict = true;
          break;
        }
      }
      if (result != OK || conflict) {
        if (conflict) {
          result = OK_COMPILE_ERROR;
        }
        free_segments(a, segments, segment_count);
        break;
      }
      node->module_leaf = record->namespace;
      node->has_leaf = true;
      free_segments(a, segments, segment_count);

      const ok_import_binding binding = {
          .name = root_name,
          .value = NULL_AS_VALUE(), // filled when the roots materialize
          .merge = already_bound,
      };
      if (!ok_import_binding_array_append(p_bindings, binding)) {
        result = OK_INIT_ERROR;
        break;
      }
      if (!already_bound) {
        // defer: after the walk, this binding gets the materialized root
        // (matched by name - the tree's arrays move as they grow)
        if (deferred_count == deferred_capacity) {
          const uint32_t grown = deferred_capacity == 0 ? 4 : deferred_capacity * 2;
          uint32_t* grown_deferred = (uint32_t*)mod_realloc(a, deferred, sizeof(uint32_t) * grown);
          char** grown_roots = (char**)mod_realloc(a, deferred_roots, sizeof(char*) * grown);
          if (grown_deferred == NULL || grown_roots == NULL) {
            mod_free(a, grown_deferred != NULL ? grown_deferred : deferred);
            if (grown_roots != NULL) {
              deferred_roots = grown_roots; // keep the bigger one; freed below
            }
            result = OK_INIT_ERROR;
            break;
          }
          deferred = grown_deferred;
          deferred_roots = grown_roots;
          deferred_capacity = grown;
        }
        deferred[deferred_count] = p_bindings->count - 1;
        deferred_roots[deferred_count] = root_owned;
        deferred_count++;
      }
      continue;
    }

    // the from form: bind exported symbols directly (M9: pub only).
    for (uint32_t i = 0; i < import->symbols.count && result == OK; ++i) {
      const ast_import_specifier* spec = &import->symbols.data[i];
      const token* name_site = spec->alias.type == TOKEN_IDENTIFIER ? &spec->alias : &spec->name;
      const ok_string_view symbol =
          ok_create_string_view(spec->name.start, spec->name.length, false);
      if (spec->alias.type == TOKEN_IDENTIFIER
              ? view_equals(ok_create_string_view(spec->alias.start, spec->alias.length, false), "std")
              : view_equals(symbol, "std")) {
        module_report(p_source, name_site, "the name 'std' is reserved for the standard library's namespace.");
        result = OK_COMPILE_ERROR;
        break;
      }
      // the export probe: the frozen namespace's field map, by interned name
      object_specs obj_specs = {&okay->alloc, &okay->objects_store};
      object_string* probe = string_interner_intern(&okay->interner, symbol, &obj_specs);
      if (probe == NULL) {
        result = OK_INIT_ERROR;
        break;
      }
      const field_entry* field = object_class_find_field(record->namespace_class, probe);
      if (field == NULL) {
        string message = asprint(&okay->alloc,
                                 "module '%s' has no pub export '%.*s'.",
                                 record->canonical,
                                 (int)ok_string_view_get_length(&symbol),
                                 symbol.chars);
        module_report(p_source, name_site, message.chars != NULL ? message.chars : "no such export.");
        string_deinit(&message, &okay->alloc);
        result = OK_COMPILE_ERROR;
        break;
      }
      const ok_import_binding binding = {
          .name = spec->alias.type == TOKEN_IDENTIFIER
                      ? ok_create_string_view(spec->alias.start, spec->alias.length, false)
                      : symbol,
          .value = VALUE_AS_INSTANCE(record->namespace)->fields[field->slot],
          .merge = false,
      };
      if (!ok_import_binding_array_append(p_bindings, binding)) {
        result = OK_INIT_ERROR;
        break;
      }
    }
  }

  // materialize the roots and fill the deferred bindings
  if (result == OK) {
    for (uint32_t i = 0; i < forest.children_count && result == OK; ++i) {
      pending_ns* root_node = &forest.children[i];
      value root_value;
      if (root_node->children_count == 0) {
        // a single-segment path binds the module itself (M5: the module lands
        // AT the root); the registry roots the value already
        root_value = root_node->module_leaf;
      } else if (!materialize_ns(p_modules, root_node, &root_value)) {
        module_report(p_source, &p_root->statements.data[0]->node.token, "out of memory while building the root namespace.");
        result = OK_INIT_ERROR;
        break;
      }
      // the pending-roots window: marked until the importer's constants adopt
      if (!gc_guard_up(&okay->gc, root_value)) {
        module_report(p_source, &p_root->statements.data[0]->node.token, "out of memory while building the root namespace.");
        result = OK_INIT_ERROR;
        break;
      }
      const bool pushed = value_array_append(&p_modules->pending_roots, root_value);
      gc_guard_down(&okay->gc);
      if (!pushed) {
        result = OK_INIT_ERROR;
        break;
      }
      for (uint32_t d = 0; d < deferred_count; ++d) {
        if (deferred_roots[d] != NULL && strcmp(deferred_roots[d], root_node->name) == 0) {
          p_bindings->data[deferred[d]].value = root_value;
        }
      }
    }
  }

  pending_ns_deinit(a, &forest);
  if (deferred != NULL) {
    mod_free(a, deferred);
  }
  if (deferred_roots != NULL) {
    mod_free(a, deferred_roots);
  }
  if (bound_roots != NULL) {
    mod_free(a, bound_roots);
  }
  mod_free(a, from_dir);
  if (result != OK) {
    for (uint32_t i = 0; i < imports_count; ++i) {
      mod_free(a, imports[i]);
    }
    if (imports != NULL) {
      mod_free(a, imports);
    }
    return result;
  }
  *p_out_imports = imports;
  *p_out_imports_count = imports_count;
  return result;
}


// ---------------------------------------------------------------- projects (OR-4)

// one path split at its first segment: "mathx/vec" -> ("mathx", "vec"),
// "app" -> ("app", ""). the views borrow p_path's chars.
static void split_first_segment(const ok_string_view p_path, ok_string_view* p_head, ok_string_view* p_rest) {
  const uint64_t length = ok_string_view_get_length(&p_path);
  uint64_t slash = 0;
  while (slash < length && p_path.chars[slash] != '/') {
    ++slash;
  }
  *p_head = ok_create_string_view(p_path.chars, slash, false);
  if (slash < length) {
    *p_rest = ok_create_string_view(p_path.chars + slash + 1, length - slash - 1, false);
  } else {
    *p_rest = ok_create_string_view(NULL, 0, false);
  }
}

static bool view_seg_equals(const ok_string_view p_view, const char* p_text) {
  const size_t length = strlen(p_text);
  return ok_string_view_get_length(&p_view) == length && memcmp(p_view.chars, p_text, length) == 0;
}

// join <dir>/<sub>/<rel> with normalization, one allocation out. p_sub may
// be NULL (skipped). every join is a view join: join_normalize takes views.
static char* join_three(const ok_allocators* p_a, const char* p_dir, const char* p_sub, const ok_string_view p_rel) {
  const ok_string_view sub_view =
      p_sub != NULL ? ok_create_string_view(p_sub, (uint32_t)strlen(p_sub), true)
                    : ok_create_string_view(".", 1, true);
  char* base = join_normalize(p_a, p_dir, sub_view);
  if (base == NULL) {
    return NULL;
  }
  char* joined = join_normalize(p_a, base, p_rel);
  mod_free(p_a, base);
  return joined;
}

// the native-target lane inside one project: the import names a declared
// target. caps first (the doctrine: LOAD gates the load, BUILD gates the
// ensure hook), the hook second (staleness and consent are host data), the
// load third. p_where short-circuits to reporting the artifact.
static project_lane_result resolve_native_target(module_system* p_modules,
                                  const ok_project* p_project,
                                  const ok_project_native* p_target,
                                  str_list* p_tried,
                                  char* p_native_error,
                                  size_t p_native_error_capacity,
                                  const ok_source* p_source,
                                  const token* p_site,
                                  const char* p_where,
                                  resolved_module* p_out) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;

  if (p_where != NULL) {
    // report without ensuring, opening, or building
    char* artifact_copy = mod_strdup(a, p_target->artifact);
    char* canonical = artifact_copy != NULL ? canonicalize_path(a, artifact_copy) : NULL;
    if (canonical == NULL) {
      return PROJECT_ERROR;
    }
    p_out->canonical = canonical;
    p_out->is_native = true;
    return PROJECT_FOUND;
  }

  if ((p_modules->ok->caps_dropped & OK_CAP_NATIVE_LOAD) != 0) {
    char message[320];
    snprintf(message, sizeof(message), "native target '%s' refused: OK_CAP_NATIVE_LOAD is dropped in this ok instance.",
             p_target->name);
    lane_report(p_where, p_source, p_site, message);
    return PROJECT_ERROR;
  }
  if ((p_modules->ok->caps_dropped & OK_CAP_NATIVE_BUILD) == 0 && p_modules->project_hooks.ensure != NULL) {
    char err[512];
    err[0] = '\0';
    if (!p_modules->project_hooks.ensure(p_modules->project_hooks.p_ctx, p_project, p_target, err, sizeof(err))) {
      char message[640];
      snprintf(message, sizeof(message), "native target '%s' could not be built: %s", p_target->name,
               err[0] != '\0' ? err : "the build hook refused it.");
      lane_report(p_where, p_source, p_site, message);
      return PROJECT_ERROR;
    }
  } else if ((p_modules->ok->caps_dropped & OK_CAP_NATIVE_BUILD) != 0) {
    // no hook ran (the cap is dropped): the artifact must already exist.
    // the loader's read is the existence probe, the where lane's trick.
    ok_source probe;
    if (p_modules->loader.read_source == NULL ||
        !p_modules->loader.read_source(p_modules->loader.p_ctx, p_target->artifact, &probe)) {
      char message[512];
      snprintf(message,
               sizeof(message),
               "native target '%s' has no artifact at %s, and OK_CAP_NATIVE_BUILD is dropped: no build was attempted.",
               p_target->name,
               p_target->artifact);
      lane_report(p_where, p_source, p_site, message);
      return PROJECT_ERROR;
    }
    mod_free(a, (void*)probe.code);
  }
  if (p_modules->loader.load_native == NULL) {
    lane_report(p_where, p_source, p_site, "this ok instance's loader has no native lane.");
    return PROJECT_ERROR;
  }
  // the artifact string is the host's (borrowed, const): a copy goes into
  // the destroying canonicalizer, the canonical is this lane's to spend
  char* artifact_copy = mod_strdup(a, p_target->artifact);
  char* canonical = artifact_copy != NULL ? canonicalize_path(a, artifact_copy) : NULL;
  if (canonical == NULL) {
    return PROJECT_ERROR;
  }
  // already loaded answers from the registry (one load per instance, ever)
  if (registry_find(p_modules, canonical) != NULL) {
    p_out->canonical = canonical;
    p_out->is_native = true;
    return PROJECT_FOUND;
  }
  const ok_native_module* descriptor = NULL;
  void* handle = NULL;
  char err[256];
  err[0] = '\0';
  const bool loaded =
      p_modules->loader.load_native(p_modules->loader.p_ctx, canonical, ok_api_table(), &descriptor, &handle, err,
                                    sizeof(err));
  if (!loaded) {
    if (err[0] != '\0') {
      snprintf(p_native_error, p_native_error_capacity, "%s", err);
    }
    str_list_append(p_tried, a, canonical); // best effort: the trace wants it
    mod_free(a, canonical);
    return PROJECT_MISS; // unreadable or broken: the caller's lanes go on
  }
  p_out->canonical = canonical;
  p_out->is_native = true;
  p_out->descriptor = descriptor;
  p_out->handle = handle;
  return PROJECT_FOUND;
}

// resolve one import path inside one project (the draft's section 5 step 3):
// the module roots, then the native targets, then the deps by name. project
// deps recurse with their own tables. depth caps the (host-supplied, maybe
// cyclic) dep graph. false = not found in this project's lanes (the caller
// falls through), hard errors print and return false with p_out untouched.
static project_lane_result resolve_in_project(module_system* p_modules,
                                              const ok_project* p_project,
                                              const ok_string_view p_path,
                                              str_list* p_tried,
                                              char* p_native_error,
                                              size_t p_native_error_capacity,
                                              const ok_source* p_source,
                                              const token* p_site,
                                              const char* p_where,
                                              uint32_t p_depth,
                                              resolved_module* p_out) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;

  // the module roots, the full ladder at each (native candidates included)
  for (uint32_t r = 0; r < p_project->module_roots_count; ++r) {
    char* candidate = join_three(a, p_project->root, p_project->module_roots[r], p_path);
    if (candidate == NULL) {
      return PROJECT_ERROR;
    }
    const bool found =
        probe_ladder(p_modules, candidate, p_tried, p_native_error, p_native_error_capacity, p_where != NULL, p_out);
    mod_free(a, candidate);
    if (found) {
      return PROJECT_FOUND;
    }
  }

  // the native targets: the import names one (single-segment paths only,
  // a native is one module, not a namespace)
  const uint64_t path_length = ok_string_view_get_length(&p_path);
  bool has_slash = false;
  for (uint64_t i = 0; i < path_length; ++i) {
    if (p_path.chars[i] == '/') {
      has_slash = true;
      break;
    }
  }
  if (!has_slash) {
    for (uint32_t n = 0; n < p_project->natives_count; ++n) {
      if (view_seg_equals(p_path, p_project->natives[n].name)) {
        return resolve_native_target(p_modules, p_project, &p_project->natives[n], p_tried, p_native_error,
                                     p_native_error_capacity, p_source, p_site, p_where, p_out);
      }
    }
  }

  // the deps by name: the first segment names the dep, the rest resolves
  // inside it (a project dep's own roots/natives/deps, a raw dir's ladder)
  if (p_depth >= 32) {
    lane_report(p_where, p_source, p_site, "project dependency nesting is too deep: the limit is 32.");
    return PROJECT_ERROR;
  }
  ok_string_view head;
  ok_string_view rest;
  split_first_segment(p_path, &head, &rest);
  for (uint32_t d = 0; d < p_project->deps_count; ++d) {
    const ok_project_dep* dep = &p_project->deps[d];
    if (!view_seg_equals(head, dep->name)) {
      continue;
    }
    if (dep->is_project) {
      // the dep's own project table: the installed project at dep->path
      const ok_project* dep_project = NULL;
      for (uint32_t i = 0; i < p_modules->projects_count; ++i) {
        if (strcmp(p_modules->projects[i].root, dep->path) == 0) {
          dep_project = &p_modules->projects[i];
          break;
        }
      }
      if (dep_project == NULL) {
        // the host's graph is inconsistent: fall back to the raw-dir lanes
        dep_project = NULL;
      }
      if (dep_project != NULL) {
        if (ok_string_view_get_length(&rest) == 0) {
          char message[320];
          snprintf(message,
                   sizeof(message),
                   "'%s' is a project dependency: import a module under it, like \"%s/module\".",
                   dep->name,
                   dep->name);
          lane_report(p_where, p_source, p_site, message);
          return PROJECT_ERROR;
        }
        return resolve_in_project(p_modules, dep_project, rest, p_tried, p_native_error, p_native_error_capacity,
                                  p_source, p_site, p_where, p_depth + 1, p_out);
      }
    }
    // the raw-dir lane (also the fallback for an unresolved project dep):
    // the dep directory is an import root, the full ladder under it. a bare
    // dep name probes the directory itself: D/index.ok, D.ok, D.so
    char* candidate = join_three(a, dep->path, NULL, ok_string_view_get_length(&rest) == 0
                                                        ? ok_create_string_view(".", 1, true)
                                                        : rest);
    if (candidate == NULL) {
      return PROJECT_ERROR;
    }
    const bool found =
        probe_ladder(p_modules, candidate, p_tried, p_native_error, p_native_error_capacity, p_where != NULL, p_out);
    mod_free(a, candidate);
    if (found) {
      return PROJECT_FOUND;
    }
  }
  return PROJECT_MISS;
}

// ok.h's ok_set_projects: the borrowed graph moves in wholesale.
void ok_modules_set_projects(module_system* p_modules,
                             const ok_project* p_projects,
                             uint32_t p_count,
                             ok_project_build_hooks p_hooks) {
  p_modules->projects = p_projects;
  p_modules->projects_count = p_count;
  p_modules->project_hooks = p_hooks;
}

// ok.h's ok_module_where: the resolution question, asked directly. nothing
// loads, nothing runs, nothing builds: the same ladder, read probes only.
bool ok_modules_where(module_system* p_modules, const char* p_import, const char* p_from_dir,
                      const char** p_out_canonical) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;
  const ok_string_view view = ok_create_string_view(p_import, (uint32_t)strlen(p_import), false);
  resolved_module resolved;
  resolved_init(&resolved);
  if (!resolve_import(p_modules, view, p_from_dir, NULL, NULL, "ok where", &resolved)) {
    return false;
  }
  // the answer buffer: one slot, the previous answer dies with the call
  if (p_modules->where_canonical != NULL) {
    mod_free(a, p_modules->where_canonical);
  }
  p_modules->where_canonical = resolved.canonical;
  resolved.canonical = NULL;
  resolved_deinit(a, &resolved);
  if (p_modules->where_canonical == NULL) {
    return false;
  }
  if (p_out_canonical != NULL) {
    *p_out_canonical = p_modules->where_canonical;
  }
  return true;
}

// ok.h's ok_module_evaluate: one source as a standalone module, the
// manifest lane. imports are refused (a manifest is a script), the pub
// surface becomes a namespace value, nothing registers.
ok_value ok_modules_evaluate(module_system* p_modules, ok_source* p_source) {
  ok* okay = p_modules->ok;

  // parse: the manifest's own diagnostics (syntax errors carry its path)
  parser parser;
  parser_specs parser_specs = {.alloc = &okay->alloc, .source = p_source};
  parser_init(&parser, parser_specs);
  parse_result parsed = parser_parse(&parser);
  if (parsed.status != PARSE_OK) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    return NULL_AS_VALUE();
  }

  // imports are refused with the import site's own location
  for (uint32_t i = 0; i < parsed.root->statements.count; ++i) {
    if (parsed.root->statements.data[i]->node.node_type == AST_IMPORT_STATEMENT) {
      ast_import_statement* import = (ast_import_statement*)parsed.root->statements.data[i];
      module_report(p_source,
                    &import->statement.node.token,
                    "the manifest may not import: it is evaluated standalone, with no import lane.");
      parser_deinit(&parser);
      parse_result_deinit(&parsed);
      return NULL_AS_VALUE();
    }
  }

  // compile in module mode: pub top-levels recorded, the export epilogue
  // emitted (the same lane every imported module compiles through)
  compile_specs compile_specs;
  compile_specs.source = p_source;
  compile_specs.root = parsed.root;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(okay, &err);
  if (err) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    return NULL_AS_VALUE();
  }
  compile_specs.imports = (ok_import_bindings){NULL, 0};
  compile_specs.is_module = true;
  compile_specs.module_path = ok_create_string_view(p_source->path, (uint32_t)strlen(p_source->path), true);
  compile_result compiled = compiler_compile(&okay->compiler, compile_specs);
  top_level_declarations_deinit(&compile_specs.top_level_declarations, &okay->alloc);
  parser_deinit(&parser);
  if (compiled.status != COMPILE_OK) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    return NULL_AS_VALUE();
  }

  // run the top level: one source registration so runtime failures carry
  // the manifest's own path and trace
  if (!gc_guard_up(&okay->gc, OBJECT_AS_VALUE(compiled.function))) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    return NULL_AS_VALUE();
  }
  const uint32_t source_id = vm_begin_source(&okay->vm, p_source->code, p_source->path);
  if (source_id == VM_SOURCE_UNSTAMPED) {
    gc_guard_down(&okay->gc);
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    return NULL_AS_VALUE();
  }
  ok_stamp_source_ids(compiled.function, source_id);
  err = false;
  interpret_specs interpret_specs;
  interpret_specs.function = compiled.function;
  interpret_specs.source_id = source_id;
  interpret_specs.top_level_values = ok_get_top_level_values(okay, &err);
  const interpret_result ran = err ? (interpret_result){0} : vm_interpret(&okay->vm, interpret_specs);
  gc_guard_down(&okay->gc);
  value_array_deinit(&interpret_specs.top_level_values, &okay->alloc);
  vm_end_source(&okay->vm, source_id);
  if (err || ran.status != RUNTIME_OK) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    return NULL_AS_VALUE(); // the runtime error printed with the manifest's path
  }

  // harvest into the sealed namespace; NOT registered (the host owns it)
  value namespace = NULL_AS_VALUE();
  if (!build_namespace(p_modules, p_source->path, &compiled.exports, &namespace)) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    return NULL_AS_VALUE();
  }
  parse_result_deinit(&parsed);
  module_exports_deinit(&compiled.exports, &okay->alloc);
  return namespace;
}

// ---------------------------------------------------------------- lifecycle

void ok_modules_init(module_system* p_modules, ok* p_ok, allocators* p_alloc) {
  p_modules->ok = p_ok;
  ok_module_records_init(&p_modules->loaded, p_alloc);
  p_modules->chain_depth = 0;
  for (uint32_t i = 0; i < OK_MODULES_MAX_DEPTH; ++i) {
    p_modules->chain[i] = NULL;
  }
  value_array_init(&p_modules->pending_roots, p_alloc);
  p_modules->name_arena = NULL;
  p_modules->name_arena_count = 0;
  p_modules->name_arena_capacity = 0;
  p_modules->mounting = NULL;
  p_modules->native_handles = NULL;
  p_modules->native_handles_count = 0;
  p_modules->native_handles_capacity = 0;
  p_modules->projects = NULL;
  p_modules->projects_count = 0;
  p_modules->project_hooks.ensure = NULL;
  p_modules->project_hooks.p_ctx = NULL;
  p_modules->where_canonical = NULL;
  // the CLI default loader: fs reads (this file) + the dylib-based native
  // lane (okdylib.c). both halves reach the instance through the shared ctx.
  ok_loader loader;
  loader.read_source = fs_read_source;
  loader.load_native = ok_load_native_thunk;
  loader.p_ctx = p_modules;
  p_modules->loader = loader;
}

void ok_modules_deinit(module_system* p_modules) {
  const ok_allocators* a = &p_modules->ok->raw_alloc;
  // the native lifecycle first, while the vm is fully alive: on_unload is
  // for releasing the module's own C resources (the lua lesson: never
  // mid-run, only here). the default lane's handles close after, so the
  // unload code is still mapped while it runs.
  for (uint32_t i = 0; i < p_modules->loaded.count; ++i) {
    const ok_module_record* record = &p_modules->loaded.data[i];
    if (record->is_native && record->descriptor != NULL && record->descriptor->on_unload != NULL) {
      record->descriptor->on_unload(p_modules->ok);
    }
  }
  ok_module_records_deinit(&p_modules->loaded, p_modules->loaded.alloc);
  value_array_deinit(&p_modules->pending_roots, p_modules->pending_roots.alloc);
  for (uint32_t i = 0; i < p_modules->name_arena_count; ++i) {
    mod_free(a, p_modules->name_arena[i]);
  }
  if (p_modules->name_arena != NULL) {
    mod_free(a, p_modules->name_arena);
  }
  p_modules->name_arena = NULL;
  p_modules->name_arena_count = 0;
  p_modules->name_arena_capacity = 0;
  // the default lane's handles (a host load_native owns its own)
  if (p_modules->ok->dylib != NULL) {
    for (uint32_t i = 0; i < p_modules->native_handles_count; ++i) {
      p_modules->ok->dylib->close(p_modules->ok->dylib->p_ctx, p_modules->native_handles[i]);
    }
  }
  if (p_modules->native_handles != NULL) {
    mod_free(a, p_modules->native_handles);
  }
  p_modules->native_handles = NULL;
  p_modules->native_handles_count = 0;
  p_modules->native_handles_capacity = 0;
  // OR-4: the where answer buffer (the project graph itself is borrowed)
  if (p_modules->where_canonical != NULL) {
    mod_free(a, p_modules->where_canonical);
    p_modules->where_canonical = NULL;
  }
  p_modules->projects = NULL;
  p_modules->projects_count = 0;
  p_modules->ok = NULL;
}

void ok_modules_set_loader(module_system* p_modules, ok_loader p_loader) {
  p_modules->loader = p_loader;
}

// ---------------------------------------------------------------- force-load (OR-3)

// `ok run --native m.so app.ok` (M3's last resort): load a native outside
// any project, register it under its declared name (or p_name), so bare
// imports resolve to it. the capability gate applies, failures print their
// own diagnostic (no import site exists yet).
bool ok_native_force_load(ok* p_ok, const char* p_path, const char* p_name) {
  module_system* modules = &p_ok->modules;
  const ok_allocators* a = &p_ok->raw_alloc;

  if ((p_ok->caps_dropped & OK_CAP_NATIVE_LOAD) != 0) {
    plain_report(p_path, "native loading is refused: OK_CAP_NATIVE_LOAD is dropped in this ok instance.");
    return false;
  }
  if (modules->loader.load_native == NULL) {
    plain_report(p_path, "this ok instance's loader has no native lane.");
    return false;
  }
  const ok_native_module* descriptor = NULL;
  void* handle = NULL;
  char err[256];
  err[0] = '\0';
  if (!modules->loader.load_native(modules->loader.p_ctx, p_path, ok_api_table(), &descriptor, &handle, err,
                                   sizeof(err))) {
    if (err[0] != '\0') {
      plain_report(p_path, "the native module could not be loaded.");
      fprintf(stderr, "note: %s\n", err);
    } else {
      plain_report(p_path, "no native module at this path.");
    }
    return false;
  }
  const char* key = p_name != NULL ? p_name : descriptor->name;
  if (registry_find(modules, key) != NULL) {
    string message = asprint(&p_ok->alloc, "a module is already registered under the name '%s'.", key);
    plain_report(p_path, message.chars != NULL ? message.chars : "a module is already registered under that name.");
    string_deinit(&message, &p_ok->alloc);
    return false;
  }
  char* key_owned = mod_strdup(a, key);
  if (key_owned == NULL) {
    plain_report(p_path, "out of memory while registering the module.");
    return false;
  }
  const ok_result registered = register_native(modules, descriptor, handle, key_owned, p_path, NULL, NULL, NULL);
  mod_free(a, key_owned);
  return registered == OK;
}
