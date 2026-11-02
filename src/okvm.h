#ifndef OK_VM_H
#define OK_VM_H

#include "okarray.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okobject.h"

ARRAY_DECLARE_DEFAULT(register_array, value)

// one live `try` region. records live on one vm-wide stack
// (tagged with their frame index) so scripts that never use try pay nothing: the stack
// stays empty, zero allocations. the frame tags stay ordered (frames push
// and pop lifo, their records with them), and every frame pop drops its record
// a stale record addressing a reused frame index is the one bug this layout cannot express.
typedef struct {
  uint32_t frame;     // index into the call stack
  uint32_t* catch_ip; // resume point inside that frame's chunk
  uint16_t park;      // frame-relative register the error value lands in
  uint16_t floor;     // upvalue floor: close everything at/above on unwind
} handler_record;

ARRAY_DECLARE_DEFAULT(handler_stack, handler_record)

// the closed set of runtime-error kinds: what `catch` code branches on
// and the `kind` field of the synthesized error map. consumer-driven
typedef enum {
  OK_EK_TYPE,
  OK_EK_ARITY,
  OK_EK_INDEX,
  OK_EK_KEY,
  OK_EK_VALUE,
  OK_EK_CONVERSION,
  OK_EK_NATIVE,
  OK_EK_INTERNAL,
} ok_error_kind;

const char* ok_error_kind_name(ok_error_kind p_kind);

// one per ok_run, appended for the run's lifetime. the path is always ok's (copied):
// a chunk can name its source after the run that compiled it ended.
// the code is the caller's buffer for the duration of the run and NULL after,
// unless retention is on (ok_retain_source). then it's an ok-owned copy,
// alive until ok_free. this is the v1 of the multi-source:
// right path + right lines always, source references when the text is alive,
// never a. the 0.7.0 registry (keyed, deduped) will replaces it.
typedef struct {
  char* path;
  const char* code;
  bool owns_code;
} vm_source_entry;

ARRAY_DECLARE_DEFAULT(vm_sources, vm_source_entry)

// chunks reference their source by index into the vm's table. see okchunk.h
// (VM_SOURCE_UNSTAMPED lives next to the chunk that carries the index).

typedef struct {
  uint32_t top;
  register_array array;
} register_file;

void register_file_init(register_file* p_registers, allocators* p_alloc);
bool register_file_init_warm(register_file* p_registers, uint32_t p_initial_capacity, allocators* p_alloc);
void register_file_deinit(register_file* p_registers);

enum { RETURN_COUNT_DYNAMIC = UINT32_MAX };
typedef struct {
  object_closure* closure;
  uint32_t* ip;
  uint32_t registers_base;
  uint32_t return_base;
  uint32_t return_count;
  uint16_t argc; // set by call(): the callee's own prologue (defaults ladder) reads it
  // set by stage_defer_thunk: this frame runs a defer thunk. the unwinder
  // reads it to decide the cascade. an error that passed through a defer
  // invocation (raised in the thunk or anything under it, uncaught on the way)
  // stops the defer chain: subsequent defers in the dying region are nilled.
  bool defer_thunk;
} call_frame;

ARRAY_DECLARE_DEFAULT(call_stack, call_frame)

typedef struct {
  call_stack call_stack;
  register_file registers;
  object_upvalue* open_upvalues;
  string native_error_message;
  bool native_has_error;

  // the error lane. runtime_error RAISES into it. the dispatch loop's error
  // exit (FAIL_PATH) attempts the unwind to a handler first, and only then
  // reports. the one place where the live ip and the call stack still exist
  // together. the outermost C boundary (vm_error at native_call_depth == 0)
  // is the no-frames lane's report point, and the final reset.
  //
  // two flavors: a script `throw v` carries the VALUE whole,
  // a runtime-raised error carries kind + text and
  // materializes into a {:kind:, :what:} map only when a catch (or
  // ok_last_error) actually consumes it. error_hard is the oom lane.
  char error_what[512];
  ok_error_kind error_kind;
  value error_value;
  bool error_is_script;
  bool error_hard;
  bool error_reported;
  // the read flag, distinct from error_pending (the propagation flag): a
  // host-caught error is consumed for propagation but stays readable for
  // ok_last_error until cleared or the next ok_run.
  bool has_error;

  // set by runtime_error, and checked by call_native
  // (a native returning after an inner error must fail its OP_CALL)
  // and consumed at the outermost C entry point (vm_call/vm_interpret with native_call_depth == 0).
  bool error_pending;
  uint32_t native_call_depth;

  // the defer cascade: once a defer fails during an unwind scan, subsequent
  // defers in the dying region are nilled, not run
  // (one failure stops the cleanup chain. the failing defer's error replaces the in-flight one).
  // cleared when a catch takes an error, when the lane is cleared, and at the final reset.
  bool defer_cascade;

  // the live try regions, vm-wide, frame-tagged (see handler_record).
  handler_stack handlers;
} execution_state;

struct vm {
  execution_state* current_execution_state;
  // the source table: one entry per ok_run, chunks carry the index. reports read per-frame.
  vm_sources sources;
  bool retain_source;
  object_store* objects_store;
  globals_store* globals_store;
  allocators* alloc;
  ok* ok;
  // print routing (ok_set_print): NULL keeps the CLI's formatted stdout.
  ok_print_fn print_cb;
  void* print_ctx;
};

typedef enum {
  RUNTIME_OK,
  RUNTIME_ERROR,
} runtime_status;

typedef struct {
  runtime_status status;
  value top_level_return;
} interpret_result;

void interpret_result_deinit(interpret_result* p_interpret_result);

typedef struct {
  object_store* objects_store;
  globals_store* globals_store;
  allocators* alloc;
  ok* ok;
} vm_specs;

typedef struct {
  uint32_t source_id;
  object_function* function;
  value_array top_level_values;
} interpret_specs;

void vm_init(vm* p_vm, vm_specs p_specs);
void vm_deinit(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs);
interpret_result vm_call(vm* p_vm, value p_callee, uint32_t p_argc);
bool vm_native_error(vm* p_vm, ok_string_view p_message);

// the source table. vm_begin_source registers the run's source
// (path always copied, code copied when retention is on, borrowed for the run otherwise)
// and returns its id, or VM_SOURCE_UNSTAMPED on allocation failure.
// vm_end_source expires the borrow when the entry doesn't own its code.
// called at every ok_run exit. vm_source_of reads an entry for diagnostics.
uint32_t vm_begin_source(vm* p_vm, const char* p_code, const char* p_path);
void vm_end_source(vm* p_vm, uint32_t p_source_id);
const vm_source_entry* vm_source_of(const vm* p_vm, uint32_t p_source_id);

// the host-caught lane: the most recent error's value (ok_last_error), the
// clear (ok_clear_error), and the native's value-throw
// (ok_native_throw. the sibling of ok_native_error, which stays as message sugar).
value vm_last_error(vm* p_vm);
void vm_clear_error(vm* p_vm);
bool vm_native_throw(vm* p_vm, ok_value p_value);

typedef enum { TT_STR, TT_NUM, TT_BOOL } temporary_type_tags;

#endif // OK_VM_H
