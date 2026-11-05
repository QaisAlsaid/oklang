#ifndef OK_STATE_H
#define OK_STATE_H

// the ok instance's full layout, shared by ok.c and the module system
// (okmodules.c). it lived private to ok.c until OR-1: the module driver needs
// the same reach (compiler, vm, globals, interner, gc guards) ok_run_inner
// has, and routing it all through accessors would be ceremony - the struct
// stays an internal affair of the library, just visible to its own files.

#include "ok/ok_fwd.h"
#include "ok/ok_source.h"
#include "ok/ok_string.h"

#include "okcompiler.h"
#include "okglobals_store.h"
#include "okintern.h"
#include "okmodules.h"
#include "okobject_store.h"

// struct ok carries the gc BY VALUE, so every okstate.h consumer needs the
// full layout - the expose guard is this header's business, exactly like it
// is okgc.c's. an identical redefinition is legal C, so files that define it
// first are unaffected.
#ifndef OK_GC_EXPOSE
#define OK_GC_EXPOSE
#endif
#include "okgc.h"

#include "okvm.h"

struct ok {
  object_store objects_store;
  globals_store globals_store;
  string_interner interner;
  compiler compiler;
  vm vm;
  gc gc;
  // OR-1: the module system - the registry (canonical path -> module), the
  // active load chain, and the pending-roots window. a gc root (watched).
  module_system modules;
  ok_source source; // the CURRENT run's source (code borrowed for the run)
  allocators alloc; // gc-hooked wrapper
  ok_allocators raw_alloc;
  // OR-3: the capability ratchet's state (granted = OK_CAP_ALL & ~caps_dropped,
  // only ever shrinking after ok_create) and the resolved dylib table
  // (ok_specs.dylib, else the platform wrapper, else NULL = no loading).
  uint32_t caps_dropped;
  const ok_dylib* dylib;
  bool dump_bytecode;
  bool dump_skip_run;
  // P4: sys.exit's request. the native sets both; the exit unwinds like
  // an uncaught error (no catch may take it), and ok_run maps the run's
  // end to OK_EXITED with the code readable through ok_exit_code. every
  // ok_run starts with the lane reset.
  bool exit_requested;
  int32_t exit_code;
};

// the seeds (ok.c): the builtin surface every TU's <main> pre-declares in its
// hidden scope. the module driver stages them for module mains identically.
top_level_declarations ok_get_top_level_declarations(ok* p_ok, bool* p_error);
value_array ok_get_top_level_values(ok* p_ok, bool* p_error);

// stamp a freshly compiled tree with its source id (main + every nested proto
// in the constants arrays). the module runner stamps module trees with it.
void ok_stamp_source_ids(object_function* p_function, uint32_t p_source_id);

#endif // OK_STATE_H
