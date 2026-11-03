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

#include "ok/ok_fwd.h"
#include "ok/ok.h"
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

typedef struct ok_loader {
  // reads the source at an absolute path into *p_out (code: a malloc'd,
  // NUL-terminated buffer the CALLER frees; path: the canonical path).
  // false = unreadable (the driver turns it into the 404's IO note).
  bool (*read_source)(void* p_ctx, const char* p_path, ok_source* p_out);
  // load_native arrives with OR-3 (sandbox + the ok_api vtable). NULL today.
  void* p_ctx;
} ok_loader;

// one loaded module. the registry is a plain array probed by strcmp on the
// canonical path - module counts are tens, not thousands, and the load path
// is not hot; the okmap upgrade can come when a profile asks for it.
typedef struct {
  char* canonical;             // the resolved absolute path (owned)
  object_string* name;         // the canonical, interned (the namespace class's name)
  object_class* namespace_class;
  value namespace;             // the sealed namespace instance
} ok_module_record;

ARRAY_DECLARE_DEFAULT(ok_module_records, ok_module_record)
ARRAY_DECLARE_DEFAULT(ok_import_binding_array, ok_import_binding)

typedef struct module_system {
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
  ok_loader loader;            // the host's loader (init installs the fs default)
} module_system;

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
ok_result ok_modules_prepare(module_system* p_modules,
                             ast_root* p_root,
                             const ok_source* p_source,
                             ok_import_binding_array* p_bindings);

// the gc's walk (called from okgc.c's mark): the registry's namespaces and
// the pending-roots window. canonical/name/class strings ride the class
// (reachable through the namespace instance), so only the values need walks.
bool ok_modules_mark(gc* p_gc, module_system* p_modules);

#endif // OK_MODULES_H
