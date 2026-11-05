#ifndef OK_VM_H
#define OK_VM_H

#include "okarray.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okintern.h"
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
  // sys.exit rides the error lane (the only control-flow escape a native
  // has) but is a host request, not a script error: no catch may take it,
  // the run unwinds to the top and answers OK_EXITED.
  OK_EK_EXIT,
} ok_error_kind;

const char* ok_error_kind_name(ok_error_kind p_kind);

// raise the exit request (P4): sys.exit's lane. uncatchable by design -
// see OK_EK_EXIT above; the natives call this through ok_vm().
void vm_raise_exit(vm* p_vm);

// one entry per compiled TU: the entry file's run, plus one per loaded
// module (OR-1) - appended at each load, alive until ok_free. the path is
// always ok's (copied): a chunk can name its source after the run that
// compiled it ended. the code is the caller's buffer for the duration of the
// run and NULL after, unless retention is on (ok_retain_source). then it's
// an ok-owned copy, alive until ok_free. right path + right lines always,
// source references when the text is alive. the keyed, deduped registry
// lives in the module system (okmodules.c, canonical paths) - this table
// stays the flat per-TU source record.
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
  // reads it as the chronology marker: an in-flight error that passed
  // through a flagged frame was raised by a defer, which makes it the
  // chain's FIRST failure - subsequent defer failures drop against it
  // instead of replacing it.
  bool defer_thunk;
} call_frame;

ARRAY_DECLARE_DEFAULT(call_stack, call_frame)

typedef struct execution_state {
  call_stack call_stack;
  register_file registers;
  object_upvalue* open_upvalues;
  string native_error_message;
  bool native_has_error;

  // the coroutine ownership tag: NULL on the vm's own state, the running
  // coro on a state a resume swapped in. OP_YIELD reads it to know whether
  // the executing frame belongs to a coroutine at all (a plain function
  // called from main that yields is the "yield outside a coroutine" error).
  struct object_coro* owner_coro;

  // the yield barrier: >0 while script runs inside a C-driven region whose
  // C frame holds result bookkeeping after the nested run_until returns (an
  // operator method dispatch, a construction lane, a defer thunk). a yield
  // from under such a region can never resume honestly - the C code that
  // would receive the suspended result is gone - so OP_YIELD refuses it,
  // same discipline as lua's C-call boundary. the kind names the outermost
  // region for the error message.
  uint8_t yield_barrier;
  uint8_t yield_barrier_kind;

  // set by ok_native_suspend inside a native, consumed by the dispatch
  // loop's native call-completion arms (OP_CALL / OP_CALL_SPREAD /
  // OP_INVOKE / OP_SUPER_INVOKE): the requesting native's result goes out
  // to the resumer, the coro parks exactly like OP_YIELD would, and the
  // next resume's argument lands in the call's result register. the
  // request-time validation (coro owner, yield barrier, native depth)
  // guarantees no completion arm outside those four can ever see it set,
  // so the flag never outlives the native flight that raised it.
  bool native_suspend_pending;

  // the parked-resumer chain head, see vm.resume_parked below.
  struct execution_state* next_parked;

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

  // the live try regions, vm-wide, frame-tagged (see handler_record).
  handler_stack handlers;
} execution_state;

struct vm {
  execution_state* current_execution_state;
  // the vm's own state (the one vm_init allocated), kept by handle: upvalue
  // cells captured there resolve their window through it even while a resume
  // has swapped current to a coro's state
  execution_state* main_state;
  // the resumer chain: while a resume runs a coroutine's state, the states it
  // suspended (its resumer, and the resumer's resumer, ...) root here - the
  // collector walks every one of them like the current state (their parked
  // registers and frames are live values the coro may still read when it
  // yields back). LIFO push/pop inside coro_drive.
  execution_state* resume_parked;
  // the string interner: canonical compiler-made names. rooted via the gc's
  // interner watch; the vm interns its startup spellings here so they
  // pointer-match the compiler's constants (member probes hit on ==).
  string_interner* interner;
  // the two singletons every hierarchy funnels into: object (the root, the
  // default parent) and type (the class of every class value - there is
  // exactly one metaclass, no tower). created at vm_init, traced via mark_vm.
  struct object_class* object_singleton;
  struct object_class* class_singleton;
  // the seven builtin class singletons (C10), index-parallel to
  // builtin_class_id: str/num/bool/array/map/function/nil, all under object,
  // sealed by ok_define_builtin_surfaces after their native method tables
  // are mounted. every value's classof() lands on one of these (or on
  // type, or on a user class). traced via mark_vm like the two above.
  struct object_class* builtin_classes[OK_BUILTIN_COUNT];
  // the cached spelling of the static call operator, so constructing through
  // a class value never allocates its lookup name. rooted like the singletons.
  object_string* static_call_name;
  // the cached spelling of the initializer method, for the construction path.
  object_string* init_method_name;
  // the canonical operator spellings ("operator +" etc.), one per
  // class_operator, created once at init so the dispatch arms never allocate
  // their probe names. rooted like the singletons. [SUBTRACT] and [NEGATE]
  // share a spelling; the cache slots stay index-keyed.
  object_string* op_names[CLASS_OP_COUNT];
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
  // a coroutine parked at a yield: only the resume driver's run_until ever
  // produces it (the yield barrier keeps every nested run_until from ever
  // seeing a suspension), and only coro_drive consumes it - the yielded
  // value rides top_level_return.
  RUNTIME_SUSPENDED,
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
  string_interner* interner;
} vm_specs;

typedef struct {
  uint32_t source_id;
  object_function* function;
  value_array top_level_values;
} interpret_specs;

void vm_init(vm* p_vm, vm_specs p_specs);

// the ok* -> vm* reach-through for natives defined in the vm's own
// translation unit (the coro surface: the native ABI hands them an ok*, and
// the ok struct is private to ok.c). defined there.
vm* ok_vm(ok* p_ok);
// create object/type and the cached lookup names. call exactly once, after
// the gc watches the vm (see the comment on the implementation).
bool vm_init_singletons(vm* p_vm);
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

// classof (C10): the one class value every value is an instance of.
// instances -> their class, class values -> type, everything else -> its
// lane's builtin singleton (native values -> object, bound methods ->
// function; both noted in the classes record). total: never NULL.
object_class* vm_classof(vm* p_vm, ok_value p_value);

// mount the coro surface (R20-R24): the native static `operator ()`
// (construction, `coro(fu)`) and the `resume`/`status` methods on the coro
// builtin class. call before the builtin seal (ok_define_builtin_surfaces
// freezes the tables right after).
bool vm_define_coro_surface(vm* p_vm);

// the chain-walk half of the family (C12): is p_value an instance of
// p_target or one of its descendants (object at the root matches everything).
bool vm_is_instance(vm* p_vm, ok_value p_value, const object_class* p_target);

#endif // OK_VM_H
