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
// a registry record owns its canonical path (raw strdup); the deinit hook is
// the array macro's per-element call.
static void ok_module_record_deinit(ok_module_record* p_record, allocators* p_alloc) {
  (void)p_alloc;
  free(p_record->canonical);
  p_record->canonical = NULL;
}

ARRAY_DEFINE_DEFAULT(ok_module_records, ok_module_record, ok_module_record_deinit)
ARRAY_DEFINE_DEFAULT(ok_import_binding_array, ok_import_binding, ARRAY_DEFAULT_TYPE_DEINIT)

// ---------------------------------------------------------------- the fs loader

// the CLI default. an embedder replaces it through ok_modules_set_loader
// (OR-3 exposes that door publicly); the language never guesses a filesystem.
static bool fs_read_source(void* p_ctx, const char* p_path, ok_source* p_out) {
  (void)p_ctx;
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
  if (size < 0) {
    fclose(file);
    return false;
  }
  char* buff = (char*)malloc((size_t)size + 1);
  if (buff == NULL) {
    fclose(file);
    return false;
  }
  const size_t read = fread(buff, sizeof(char), (size_t)size, file);
  fclose(file);
  if (read != (size_t)size) {
    free(buff);
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

// a growable list of malloc'd strings (the 404's trace). raw malloc: these
// live only inside one resolve call.
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

static void str_list_deinit(str_list* p_list) {
  for (uint32_t i = 0; i < p_list->count; ++i) {
    free(p_list->items[i]);
  }
  free(p_list->items);
  p_list->items = NULL;
  p_list->count = 0;
  p_list->capacity = 0;
}

static bool str_list_append(str_list* p_list, const char* p_text) {
  if (p_list->count == p_list->capacity) {
    const uint32_t grown = p_list->capacity == 0 ? 8 : p_list->capacity * 2;
    char** items = (char**)realloc(p_list->items, sizeof(char*) * grown);
    if (items == NULL) {
      return false;
    }
    p_list->items = items;
    p_list->capacity = grown;
  }
  char* copy = strdup(p_text);
  if (copy == NULL) {
    return false;
  }
  p_list->items[p_list->count++] = copy;
  return true;
}

// a module source is a regular file: fopen alone happily "opens" a directory,
// which would let the ladder bind a directory as a module and die at the read.
static bool file_readable(const char* p_path) {
  struct stat info;
  if (stat(p_path, &info) != 0) {
    return false;
  }
  if (!S_ISREG(info.st_mode)) {
    return false;
  }
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return false;
  }
  fclose(file);
  return true;
}

// lexical join + normalize: dir's segments, then rel's, with '.' skipped and
// '..' popping. rel never starts with '/' (the parser rejects absolute
// paths). the result is malloc'd; NULL = OOM.
static char* join_normalize(const char* p_dir, const ok_string_view p_rel) {
  const size_t dir_length = strlen(p_dir);
  const uint64_t rel_length = ok_string_view_get_length(&p_rel);
  char* out = (char*)malloc(dir_length + rel_length + 4);
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
// then + ".ok", then + "/index.ok". a native candidate (.so today - the
// platform table widens with OR-3) is detected but not loaded: natives land
// with OR-3's sandboxed loader, and the error says so.
static char* ladder_at(const char* p_base, str_list* p_tried, bool* p_native_found) {
  static const char* const forms[] = {"", ".ok", "/index.ok"};
  for (uint32_t f = 0; f < 3; ++f) {
    const size_t base_length = strlen(p_base);
    char* candidate = (char*)malloc(base_length + strlen(forms[f]) + 1);
    if (candidate == NULL) {
      return NULL;
    }
    memcpy(candidate, p_base, base_length);
    strcpy(candidate + base_length, forms[f]);
    if (file_readable(candidate)) {
      return candidate;
    }
    str_list_append(p_tried, candidate); // best effort: the trace wants it
    free(candidate);
  }
  const size_t base_length = strlen(p_base);
  char* native_candidate = (char*)malloc(base_length + 4);
  if (native_candidate != NULL) {
    memcpy(native_candidate, p_base, base_length);
    strcpy(native_candidate + base_length, ".so");
    if (file_readable(native_candidate)) {
      *p_native_found = true;
    }
    free(native_candidate);
  }
  return NULL;
}

// the importing file's directory ("" -> "."). returns a malloc'd copy.
static char* directory_of(const char* p_path) {
  const char* last = strrchr(p_path, '/');
  if (last == NULL) {
    return strdup(".");
  }
  if (last == p_path) {
    return strdup("/");
  }
  const size_t length = (size_t)(last - p_path);
  char* dir = (char*)malloc(length + 1);
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

// the per-run name arena (root names the binding table views). raw malloc,
// freed at the next ok_run's reset (ok.c) and at deinit.
static char* arena_strdup(module_system* p_modules, const char* p_text) {
  if (p_modules->name_arena_count == p_modules->name_arena_capacity) {
    const uint32_t grown = p_modules->name_arena_capacity == 0 ? 16 : p_modules->name_arena_capacity * 2;
    char** arena = (char**)realloc(p_modules->name_arena, sizeof(char*) * grown);
    if (arena == NULL) {
      return NULL;
    }
    p_modules->name_arena = arena;
    p_modules->name_arena_capacity = grown;
  }
  char* copy = strdup(p_text);
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
        object_class_add_field(klass, field_name, false /* mut */, NULL /* init thunk */, &previous_owner);
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
  char* name;               // the segment, owned
  value module_leaf;        // set when a module lands exactly here
  bool has_leaf;
  struct pending_ns* children;
  uint32_t children_count;
  uint32_t children_capacity;
} pending_ns;

static void pending_ns_init(pending_ns* p_node, const char* p_name) {
  p_node->name = strdup(p_name);
  p_node->module_leaf = NULL_AS_VALUE();
  p_node->has_leaf = false;
  p_node->children = NULL;
  p_node->children_count = 0;
  p_node->children_capacity = 0;
}

static void pending_ns_deinit(pending_ns* p_node) {
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    pending_ns_deinit(&p_node->children[i]);
  }
  free(p_node->children);
  free(p_node->name);
  p_node->name = NULL;
  p_node->children = NULL;
  p_node->children_count = 0;
  p_node->children_capacity = 0;
}

static pending_ns* pending_ns_child(pending_ns* p_node, const char* p_name) {
  for (uint32_t i = 0; i < p_node->children_count; ++i) {
    if (strcmp(p_node->children[i].name, p_name) == 0) {
      return &p_node->children[i];
    }
  }
  if (p_node->children_count == p_node->children_capacity) {
    const uint32_t grown = p_node->children_capacity == 0 ? 4 : p_node->children_capacity * 2;
    pending_ns* children = (pending_ns*)realloc(p_node->children, sizeof(pending_ns) * grown);
    if (children == NULL) {
      return NULL;
    }
    p_node->children = children;
    p_node->children_capacity = grown;
  }
  pending_ns* child = &p_node->children[p_node->children_count++];
  pending_ns_init(child, p_name);
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
        object_class_add_field(klass, field_name, false /* mut */, NULL /* init thunk */, &previous_owner);
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

// load (or find) one module by canonical path, depth-first. the site + the
// importing source position every diagnostic this can raise.
static ok_result load_module(module_system* p_modules,
                            const char* p_canonical,
                            const ok_source* p_from_source,
                            const token* p_site) {
  ok* okay = p_modules->ok;

  // loaded already? one TU per module, ever (the registry is the cache).
  ok_module_record* found = registry_find(p_modules, p_canonical);
  if (found != NULL) {
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

  // read + parse
  ok_source source;
  source.code = NULL;
  source.path = p_canonical;
  if (p_modules->loader.read_source == NULL || !p_modules->loader.read_source(p_modules->loader.p_ctx, p_canonical, &source)) {
    module_report(p_from_source, p_site, "module source is unreadable (the resolver found it, the read failed).");
    module_note(p_from_source, p_site, p_canonical);
    return OK_COMPILE_ERROR;
  }
  parser parser;
  parser_specs parser_specs = {.alloc = &okay->alloc, .source = &source};
  parser_init(&parser, parser_specs);
  parse_result parsed = parser_parse(&parser);
  if (parsed.status != PARSE_OK) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    free((void*)source.code);
    return OK_PARSE_ERROR;
  }

  // the module's own imports, depth-first (the chain guards the recursion)
  p_modules->chain[p_modules->chain_depth++] = p_canonical;
  ok_import_binding_array bindings;
  ok_import_binding_array_init(&bindings, &okay->alloc);
  const ok_result prepared = ok_modules_prepare(p_modules, parsed.root, &source, &bindings);
  p_modules->chain_depth--;
  if (prepared != OK) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    ok_import_binding_array_deinit(&bindings, &okay->alloc);
    free((void*)source.code);
    return prepared;
  }

  // compile in module mode: pub top-levels recorded, the export epilogue
  // emitted, an early top-level return refused.
  compile_specs compile_specs;
  compile_specs.source = &source;
  compile_specs.root = parsed.root;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(okay, &err);
  if (err) {
    parser_deinit(&parser);
    parse_result_deinit(&parsed);
    ok_import_binding_array_deinit(&bindings, &okay->alloc);
    free((void*)source.code);
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
    free((void*)source.code);
    return OK_COMPILE_ERROR;
  }

  // the compiled module function rides unrooted from compiler_compile until
  // this guard: vm_begin_source below allocates, and under aggressive gc that
  // collection must not sweep the tree ok_stamp_source_ids is about to walk.
  if (!gc_guard_up(&okay->gc, OBJECT_AS_VALUE(compiled.function))) {
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    free((void*)source.code);
    module_report(p_from_source, p_site, "out of memory while running the module's top level.");
    return OK_INIT_ERROR;
  }
  // run: one source registration per module (stack traces stay honest in
  // multi-file runs), seeds staged like any <main>.
  const uint32_t source_id = vm_begin_source(&okay->vm, source.code, source.path);
  if (source_id == VM_SOURCE_UNSTAMPED) {
    gc_guard_down(&okay->gc);
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    free((void*)source.code);
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
    free((void*)source.code);
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
    free((void*)source.code);
    module_report(p_from_source, p_site, "out of memory while building the module's namespace.");
    return OK_INIT_ERROR;
  }
  ok_module_record record;
  record.canonical = strdup(p_canonical);
  record.name = VALUE_AS_INSTANCE(namespace)->klass->name;
  record.namespace_class = VALUE_AS_INSTANCE(namespace)->klass;
  record.namespace = namespace;
  if (!gc_guard_up(&okay->gc, namespace)) {
    free(record.canonical);
    parse_result_deinit(&parsed);
    module_exports_deinit(&compiled.exports, &okay->alloc);
    free((void*)source.code);
    module_report(p_from_source, p_site, "out of memory while registering the module.");
    return OK_INIT_ERROR;
  }
  const bool registered = record.canonical != NULL && ok_module_records_append(&p_modules->loaded, record);
  gc_guard_down(&okay->gc);
  parse_result_deinit(&parsed);
  module_exports_deinit(&compiled.exports, &okay->alloc);
  free((void*)source.code);
  if (!registered) {
    free(record.canonical);
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
      record.canonical = strdup(canonical);
      record.name = VALUE_AS_INSTANCE(namespace)->klass->name;
      record.namespace_class = VALUE_AS_INSTANCE(namespace)->klass;
      record.namespace = namespace;
      if (!gc_guard_up(&okay->gc, namespace)) {
        free(record.canonical);
        result = OK_INIT_ERROR;
      } else {
        const bool registered = record.canonical != NULL && ok_module_records_append(&p_modules->loaded, record);
        gc_guard_down(&okay->gc);
        if (!registered) {
          free(record.canonical);
          result = OK_INIT_ERROR;
        }
      }
    }
  }
  module_exports_deinit(&exports, &okay->alloc);
  return result;
}

// ---------------------------------------------------------------- resolution

// resolve one import path from one importing file. produces the canonical
// (realpath'd, malloc'd) path, or prints the teaching 404 with the full
// trace of locations and forms tried (and returns false).
static bool resolve_import(module_system* p_modules,
                           const ok_string_view p_path,
                           const char* p_from_dir,
                           const ok_source* p_source,
                           const token* p_site,
                           char** p_out_canonical) {
  str_list tried;
  str_list_init(&tried);
  char* found = NULL;
  bool native_found = false;

  if (view_starts_with(p_path, "./") || view_starts_with(p_path, "../")) {
    // the file-relative lane (M3): raw ok files, no project needed.
    char* joined = join_normalize(p_from_dir, p_path);
    if (joined != NULL) {
      found = ladder_at(joined, &tried, &native_found);
      free(joined);
    }
  } else if (view_equals(p_path, "std")) {
    // the bare root imports nothing: the std library is a tree of modules.
    module_report(p_source,
                  p_site,
                  "the std root is a namespace: import a module under it, like \"std/array\".");
    str_list_deinit(&tried);
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
      str_list_deinit(&tried);
      return false;
    }
    if (std_build_module(p_modules, def) != OK) {
      module_report(p_source, p_site, "out of memory while building the module's namespace.");
      str_list_deinit(&tried);
      return false;
    }
    char canonical[32];
    if (!std_canonical_of(def, canonical, sizeof(canonical))) {
      str_list_deinit(&tried);
      return false;
    }
    char* owned = strdup(canonical);
    if (owned == NULL) {
      str_list_deinit(&tried);
      return false;
    }
    str_list_deinit(&tried);
    *p_out_canonical = owned;
    return true;
  } else {
    // the OKPATH lane: colon-separated roots, the same ladder at each.
    const char* okpath = getenv("OKPATH");
    if (okpath != NULL && *okpath != '\0') {
      char* roots = strdup(okpath);
      if (roots != NULL) {
        char* cursor = roots;
        while (cursor != NULL && *cursor != '\0' && found == NULL) {
          char* colon = strchr(cursor, ':');
          if (colon != NULL) {
            *colon = '\0';
          }
          if (*cursor != '\0') {
            char* joined = join_normalize(cursor, p_path);
            if (joined != NULL) {
              found = ladder_at(joined, &tried, &native_found);
              free(joined);
            }
          }
          cursor = colon != NULL ? colon + 1 : NULL;
        }
        free(roots);
      }
    }
  }

  if (found == NULL) {
    if (native_found) {
      module_report(p_source,
                    p_site,
                    "a native module was found, but native loading is not available yet.");
      str_list_deinit(&tried);
      return false;
    }
    // the 404 must teach (M3): every location and form tried, plus the hint.
    module_report(p_source, p_site, "module not found.");
    for (uint32_t i = 0; i < tried.count; ++i) {
      module_note(p_source, p_site, tried.items[i]);
    }
    if (tried.count == 0) {
      module_note(p_source, p_site, "no locations were searched: file-relative paths need './' or '../', "
                                    "bare names search OKPATH (unset).");
    } else {
      module_note(p_source, p_site, "add the module's directory to OKPATH, or fix the path.");
    }
    str_list_deinit(&tried);
    return false;
  }

  // canonicalize (symlinks resolved): the registry key, the class name, and
  // the stack-trace path all use this one spelling.
  char* canonical = realpath(found, NULL);
  if (canonical == NULL) {
    canonical = found; // realpath refused (exotic fs): the found path is the key
  } else {
    free(found);
  }
  *p_out_canonical = canonical;
  str_list_deinit(&tried);
  if (canonical == NULL) {
    module_report(p_source, p_site, "out of memory while resolving the module path.");
    return false;
  }
  return true;
}

// the binding segments (M5): the import literal's own segments, '.' skipped,
// '..' popped where possible and dropped at the front (they climb toward the
// importer's directory - the NAME is the remainder), a trailing '.ok'
// stripped. "a/b" -> [a, b]; "./util.ok" -> [util]; "../lib/h" -> [lib, h].
static bool binding_segments(const ok_string_view p_path, char*** p_out, uint32_t* p_out_count) {
  const uint64_t length = ok_string_view_get_length(&p_path);
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
      free(segments[--count]);
    } else if (!is_dot && !is_dotdot && seg_length > 0) {
      if (count == capacity) {
        const uint32_t grown = capacity == 0 ? 4 : capacity * 2;
        char** grown_segments = (char**)realloc(segments, sizeof(char*) * grown);
        if (grown_segments == NULL) {
          goto oom;
        }
        segments = grown_segments;
        capacity = grown;
      }
      uint64_t name_length = seg_length;
      if (name_length > 3 && memcmp(p_path.chars + i + name_length - 3, ".ok", 3) == 0) {
        name_length -= 3;
      }
      if (name_length == 0) {
        i = j + 1;
        continue;
      }
      char* segment = (char*)malloc(name_length + 1);
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
    free(segments[k]);
  }
  free(segments);
  return false;
}

static void free_segments(char** p_segments, uint32_t p_count) {
  for (uint32_t i = 0; i < p_count; ++i) {
    free(p_segments[i]);
  }
  free(p_segments);
}

// ---------------------------------------------------------------- prepare (the graph phase)

ok_result ok_modules_prepare(module_system* p_modules,
                             ast_root* p_root,
                             const ok_source* p_source,
                             ok_import_binding_array* p_bindings) {
  ok* okay = p_modules->ok;
  char* from_dir = directory_of(p_source->path);
  if (from_dir == NULL) {
    return OK_INIT_ERROR;
  }
  ok_result result = OK;

  // the TU's root forest + the deferred bindings awaiting their roots
  pending_ns forest;
  pending_ns_init(&forest, "");
  uint32_t* deferred = NULL;      // binding indices (raw malloc)
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
    if (!resolve_import(p_modules, path, from_dir, p_source, site, &canonical)) {
      result = OK_COMPILE_ERROR;
      break;
    }
    const ok_result loaded = load_module(p_modules, canonical, p_source, site);
    if (loaded != OK) {
      free(canonical);
      result = loaded;
      break;
    }
    ok_module_record* record = registry_find(p_modules, canonical);
    // cannot be NULL: load_module just registered it (or it was there)
    free(canonical);
    canonical = NULL;
    if (record == NULL) {
      module_report(p_source, site, "internal error: the loaded module did not register.");
      result = OK_INIT_ERROR;
      break;
    }

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
      if (!binding_segments(path, &segments, &segment_count)) {
        module_report(p_source, site, "the import path has no name to bind.");
        result = OK_COMPILE_ERROR;
        break;
      }
      // the root name goes to the per-run arena: the binding table and the
      // bound-roots check view it past this statement (the segment buffer
      // dies at the end of it).
      char* root_owned = arena_strdup(p_modules, segments[0]);
      if (root_owned == NULL) {
        free_segments(segments, segment_count);
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
        free_segments(segments, segment_count);
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
          ok_string_view* grown_roots = (ok_string_view*)realloc(bound_roots, sizeof(ok_string_view) * grown);
          if (grown_roots == NULL) {
            free_segments(segments, segment_count);
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
        pending_ns* child = pending_ns_child(node, segments[i]);
        if (child == NULL) {
          free_segments(segments, segment_count);
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
        free_segments(segments, segment_count);
        break;
      }
      node->module_leaf = record->namespace;
      node->has_leaf = true;
      free_segments(segments, segment_count);

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
          uint32_t* grown_deferred = (uint32_t*)realloc(deferred, sizeof(uint32_t) * grown);
          char** grown_roots = (char**)realloc(deferred_roots, sizeof(char*) * grown);
          if (grown_deferred == NULL || grown_roots == NULL) {
            free(grown_deferred != NULL ? grown_deferred : deferred);
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

  pending_ns_deinit(&forest);
  free(deferred);
  free(deferred_roots);
  free(bound_roots);
  free(from_dir);
  return result;
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
  ok_loader loader;
  loader.read_source = fs_read_source;
  loader.p_ctx = NULL;
  p_modules->loader = loader;
}

void ok_modules_deinit(module_system* p_modules) {
  ok_module_records_deinit(&p_modules->loaded, p_modules->loaded.alloc);
  value_array_deinit(&p_modules->pending_roots, p_modules->pending_roots.alloc);
  for (uint32_t i = 0; i < p_modules->name_arena_count; ++i) {
    free(p_modules->name_arena[i]);
  }
  free(p_modules->name_arena);
  p_modules->name_arena = NULL;
  p_modules->name_arena_count = 0;
  p_modules->name_arena_capacity = 0;
  p_modules->ok = NULL;
}

void ok_modules_set_loader(module_system* p_modules, ok_loader p_loader) {
  p_modules->loader = p_loader;
}
