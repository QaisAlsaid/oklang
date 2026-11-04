#ifndef OK_MODULES_H
#define OK_MODULES_H

// OR-1 "O Resolution" - the compile-time module spine (M1-M14).
//
// the VM and the compiler never touch the filesystem for imports: all loading
// goes through the loader vtable the host installs (M14: "the vm only cares
// about its vtable"). the CLI installs the fs default; an embedder can mount
// memory-backed sources (the OR-3 test will do exactly that). load_native is
// OR-3's half - a native candidate found by the ladder today produces the
// teaching "lands with OR-3" error instead of a load.
//
// semantics (the MODULES draft, section 3): the compiler resolves the import
// graph depth-first; each module is one TU compiled once, keyed by resolved
// canonical path; top-level code runs once, on first import, before the
// importer's top level; a cycle is a hard compile error (M13); the module
// value is a sealed namespace instance (frozen class, public fields).

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok.h"
#include "ok/ok_module.h"
#include "ok/ok_source.h"
#include "ok/ok_string.h"

#include "okarray.h"
#include "okast.h"
#include "okcompiler.h"
#include "okobject.h"
#include "okvalue.h"

// the import chain depth cap: the load recursion shares the C stack with the
// parser and the compiler, so a pathological graph errors instead of
// blowing it (the draft's depth guard - there was no existing one to share).
#define OK_MODULES_MAX_DEPTH 128

// the loader vtable is the PUBLIC one (ok_module.h, OR-3): read_source for
// ok modules, load_native for the native lane. the CLI default (installed
// by init) is fs reads + the dylib-based native loader; an embedder
// replaces the whole table through ok_set_loader.

// one loaded module. the registry is a plain array probed by strcmp on the
// canonical path - module counts are tens, not thousands, and the load path
// is not hot; the okmap upgrade can come when a profile asks for it.
typedef struct {
  char* canonical;             // the registry key: resolved absolute path, "std/<stem>", or a
                               // force-loaded native's declared name (owned, user allocators)
  object_string* name;         // the canonical, interned (the namespace class's name)
  object_class* namespace_class;
  value namespace;             // the sealed namespace instance
  // the module's own imports, the graph edge list (deduped canonical paths
  // in load order). owned, user allocators. the read side is
  // ok_module_import_count / ok_module_import_path (ok.h).
  char** imports;
  uint32_t imports_count;
  // OR-3: a native module. the descriptor is borrowed (the dylib handle
  // keeps it alive until ok_free's close sweep); on_unload fires there.
  bool is_native;
  const ok_native_module* descriptor;
} ok_module_record;

// the mount window (OR-3): a native module's non-function exports, staged
// between mount_classes' start and the namespace freeze. api->module_export
// (okapi.c) appends here; the loader turns the list into namespace fields.
// the values ride UNROOTED between export and freeze, so mark_modules walks
// the window and every append is a gc-safe moment (the guard stack is not
// deep enough for a mount's worth of values).
typedef struct {
  char** names;                // owned copies, user allocators
  value_array values;
  uint32_t count;
  uint32_t capacity;
  const char* failure;         // NULL, or the reason a mount export was refused
} native_mount;

ARRAY_DECLARE_DEFAULT(ok_module_records, ok_module_record)
ARRAY_DECLARE_DEFAULT(ok_import_binding_array, ok_import_binding)

// tag-only definition: the name binding lives in okfwd.h's forward typedef
// (the P1 warning sweep - one typedef, not two).
struct module_system {
  ok* ok;                      // the owning instance
  ok_module_records loaded;    // every initialized module, load order
  const char* chain[OK_MODULES_MAX_DEPTH]; // the active load chain's canonical paths (borrowed
                               // from the loaders' buffers - alive through the recursion)
  uint32_t chain_depth;
  // per-run root namespaces: built by prepare(), adopted by the importer's
  // chunk constants when it compiles. marked from here in between (a TU with
  // more roots than the guard stack's 16 slots must not dangle mid-compile).
  // reset at each ok_run entry - the previous run's constants root theirs.
  value_array pending_roots;
  // per-run name arena: the root names the binding table views (they outlive
  // prepare's segment buffers - until the importer's compile consumed them).
  // same reset discipline as pending_roots.
  char** name_arena;
  uint32_t name_arena_count;
  uint32_t name_arena_capacity;
  ok_loader loader;            // the host's loader (init installs the fs + dylib default)
  // OR-3: the active native mount window (NULL outside mount_classes).
  // api->module_export appends here; mark_modules walks it.
  native_mount* mounting;
  // OR-3: handles opened by the DEFAULT native lane (the dylib table lives
  // in ok->dylib). closed at ok_free after the on_unload sweep. host
  // load_native implementations own their own handles.
  void** native_handles;
  uint32_t native_handles_count;
  uint32_t native_handles_capacity;
  // OR-4: the installed project graph (ok_set_projects). all borrowed
  // pointers, replaced wholesale by the next call. the governing project of
  // an importing file is the deepest installed root containing it.
  const ok_project* projects;
  uint32_t projects_count;
  ok_project_build_hooks project_hooks;
  // OR-4: ok_module_where's answer buffer (owned, user allocators). one
  // slot: the latest call owns it, the previous answer dies with the call.
  char* where_canonical;
};

void ok_modules_init(module_system* p_modules, ok* p_ok, allocators* p_alloc);
void ok_modules_deinit(module_system* p_modules);

// OR-3's door (the public ok_module.h will expose it embedder-side); the CLI
// never calls it - init already installed the fs default.
void ok_modules_set_loader(module_system* p_modules, ok_loader p_loader);

// the graph phase for one TU (the entry, or a module being loaded): walks the
// parsed root's import statements in order, resolves+initializes every module
// depth-first (404s, cycles, reserved names and path conflicts are hard
// errors printed with the import site's own file:line:col), builds the TU's
// root namespaces (M5: `import "a/b"` binds `a`, the module lands at `a.b`,
// same-root imports merge), and fills the ordered binding table the compiler
// consumes. p_bindings is caller-initialized and caller-deinitialized; the
// values inside are borrowed (the registry and pending_roots root them).
// p_out_imports/p_out_imports_count receive the TU's own resolved imports
// (deduped canonical paths, load order, user-allocator owned): the caller
// attaches them to the module's registry record, or frees them (the entry
// TU is not a module). NULL/0 on a failed prepare.
ok_result ok_modules_prepare(module_system* p_modules,
                             ast_root* p_root,
                             const ok_source* p_source,
                             ok_import_binding_array* p_bindings,
                             char*** p_out_imports,
                             uint32_t* p_out_imports_count);

// okapi.c: the shared static ok_api table (OR-3). stateless trampolines
// over the ok.h surface, one table per process, every ok instance.
const ok_api* ok_api_table(void);

// OR-4: the project graph install (ok.h's ok_set_projects), the where
// probe (ok.h's ok_module_where), and the standalone manifest evaluator
// (ok.h's ok_module_evaluate). all live here, next to the ladder they
// extend.
void ok_modules_set_projects(module_system* p_modules,
                             const ok_project* p_projects,
                             uint32_t p_count,
                             ok_project_build_hooks p_hooks);
bool ok_modules_where(module_system* p_modules, const char* p_import, const char* p_from_dir,
                      const char** p_out_canonical);
ok_value ok_modules_evaluate(module_system* p_modules, ok_source* p_source);

// the gc's walk (called from okgc.c's mark): the registry's namespaces, the
// pending-roots window, and the native mount window's staged values.
// canonical/name/class strings ride the class (reachable through the
// namespace instance), so only the values need walks.
bool ok_modules_mark(gc* p_gc, module_system* p_modules);

// OR-3: the dylib-based default native lane's vtable thunk (okdylib.c).
// p_ctx is the module system (the default loader pair shares one ctx).
bool ok_load_native_thunk(void* p_ctx,
                          const char* p_path,
                          const ok_api* p_api,
                          const ok_native_module** p_out,
                          void** p_handle,
                          char* p_err,
                          size_t p_err_capacity);

#endif // OK_MODULES_H
