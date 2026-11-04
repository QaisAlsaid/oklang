#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okvm.h"
#include "ok/ok.h"
#if defined(OK_PARANOID)
#define OK_TRACE_EXECUTION
#endif // defined(OK_PARANOID)
#if defined(OK_TRACE_EXECUTION)
#include "okdebug.h"
#endif // defined(OK_TRACE_EXECUTION)

#include "okarray.h"
#include "okdiag.h"
#include "okgc.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okutils.h"

#define STACK_SIZE 256

static void runtime_error(vm* p_vm, ok_error_kind p_kind, const char* p_fmt, ...);
static void runtime_oom(vm* p_vm, const char* p_fmt, ...);
static interpret_result vm_report_error(vm* p_vm) __attribute__((cold));
static value materialize_error_value(vm* p_vm);
static void reset_error_state(vm* p_vm);

static const char* s_if_plural(uint32_t p_n) {
  return p_n == 1 ? "" : "s";
}

const char* ok_error_kind_name(ok_error_kind p_kind) {
  switch (p_kind) {
  case OK_EK_TYPE:
    return "type";
  case OK_EK_ARITY:
    return "arity";
  case OK_EK_INDEX:
    return "index";
  case OK_EK_KEY:
    return "key";
  case OK_EK_VALUE:
    return "value";
  case OK_EK_CONVERSION:
    return "conversion";
  case OK_EK_NATIVE:
    return "native";
  case OK_EK_INTERNAL:
    return "internal";
  }
  return "internal";
}
static bool call_spread(vm* p_vm,
                        execution_state* p_exe,
                        register_file* p_reg,
                        uint32_t p_base,
                        uint16_t p_callee,
                        uint16_t p_fixed,
                        uint32_t p_retc);
static bool call_value(vm* p_vm,
                       execution_state* p_exe,
                       register_file* p_reg,
                       uint32_t p_base,
                       uint16_t p_callee,
                       uint16_t p_argc,
                       uint32_t p_retc);
static object_upvalue* capture_upvalue(vm* p_vm, execution_state* p_exe, uint32_t p_register);
static void close_upvalues(execution_state* p_exe, uint32_t p_uptill);
static inline value* upvalue_cell(vm* p_vm, execution_state* p_exe, object_upvalue* p_upvalue);
static interpret_result run_until(vm* p_vm, uint32_t p_call_depth);
static inline bool call(vm* p_vm,
                        execution_state* p_exe,
                        register_file* p_reg,
                        object_closure* p_closure,
                        uint32_t p_callee,
                        uint16_t p_argc,
                        uint32_t p_retc);
static bool stage_defer_thunk(vm* p_vm, execution_state* p_exe, register_file* p_reg, object_closure* p_thunk);
static bool run_pending_defers(vm* p_vm, uint32_t p_from, uint32_t p_uptill);
static interpret_result create_closure(
    vm* p_vm, execution_state* p_exe, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, uint16_t p_src);
void interpret_result_deinit(interpret_result* p_interpret_result) {
}

// a runtime error fired somewhere under the current C entry point. callers
// that resume interpretation after the error point
// (call_native returning to an OP_CALL handler) must check this and fail instead:
// the error lane is set, and continuing would run bytecode against a reported then reset state.
// the outermost C entry point (depth == 0) reports if unreported then consumes.
static interpret_result vm_error(vm* p_vm, bool p_report) {
  // no-op inside a native. at depth 0: the ok_run lane reports (the
  // uncaught-to-top contract), the ok_call lane does not.
  // host-caught errors stop auto-printing:
  // the embedder holds the value through ok_last_error, printing it is their call.
  if (p_vm->current_execution_state->native_call_depth == 0) {
    if (p_report) {
      vm_report_error(p_vm);
    }
    reset_error_state(p_vm);
    p_vm->current_execution_state->error_pending = false;
  }
  interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
  return result;
}

void vm_init(vm* p_vm, vm_specs p_specs) {
  p_vm->alloc = p_specs.alloc;
  p_vm->objects_store = p_specs.objects_store;
  p_vm->globals_store = p_specs.globals_store;
  p_vm->interner = p_specs.interner;
  vm_sources_init(&p_vm->sources, p_vm->alloc);
  p_vm->retain_source = false;
  p_vm->ok = p_specs.ok;
  p_vm->print_cb = NULL;
  p_vm->print_ctx = NULL;
  p_vm->object_singleton = NULL;
  p_vm->class_singleton = NULL;
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    p_vm->builtin_classes[i] = NULL;
  }
  p_vm->static_call_name = NULL;
  p_vm->init_method_name = NULL;
  for (uint32_t i = 0; i < CLASS_OP_COUNT; ++i) {
    p_vm->op_names[i] = NULL;
  }
  // TODO: temp
  p_vm->current_execution_state = p_vm->alloc->allocate(p_vm->alloc, sizeof(execution_state));
  p_vm->main_state = p_vm->current_execution_state;
  p_vm->resume_parked = NULL;
  p_vm->current_execution_state->open_upvalues = NULL;
  p_vm->current_execution_state->native_has_error = false;
  p_vm->current_execution_state->error_pending = false;
  p_vm->current_execution_state->error_reported = false;
  p_vm->current_execution_state->error_hard = false;
  p_vm->current_execution_state->error_is_script = false;
  p_vm->current_execution_state->error_value = NULL_AS_VALUE();
  p_vm->current_execution_state->error_kind = OK_EK_INTERNAL;
  p_vm->current_execution_state->has_error = false;
  p_vm->current_execution_state->error_what[0] = '\0';
  p_vm->current_execution_state->native_call_depth = 0;
  p_vm->current_execution_state->native_suspend_pending = false;
  p_vm->current_execution_state->owner_coro = NULL; // the vm's own state: no coro owns it
  p_vm->current_execution_state->yield_barrier = 0;
  p_vm->current_execution_state->yield_barrier_kind = 0;
  p_vm->current_execution_state->next_parked = NULL;
  handler_stack_init(&p_vm->current_execution_state->handlers, p_vm->alloc);
  string_init(&p_vm->current_execution_state->native_error_message, NULL, 0, false, p_vm->alloc);
  register_file_init_warm(&p_vm->current_execution_state->registers, STACK_SIZE, p_specs.alloc);
  call_stack_init(&p_vm->current_execution_state->call_stack, p_specs.alloc);
}

void vm_deinit(vm* p_vm) {
  for (uint32_t i = 0; i < p_vm->sources.count; ++i) {
    vm_source_entry* entry = &p_vm->sources.data[i];
    p_vm->alloc->release(p_vm->alloc, entry->path);
    if (entry->owns_code && entry->code != NULL) {
      p_vm->alloc->release(p_vm->alloc, (void*)entry->code);
    }
    entry->path = NULL;
    entry->code = NULL;
    entry->owns_code = false;
  }
  vm_sources_deinit(&p_vm->sources, p_vm->alloc);
  p_vm->globals_store = NULL;
  p_vm->objects_store = NULL;
  p_vm->ok = NULL;
  // TODO: temp
  call_stack_deinit(&p_vm->current_execution_state->call_stack, p_vm->alloc);
  register_file_deinit(&p_vm->current_execution_state->registers);
  p_vm->current_execution_state->open_upvalues = NULL;
  p_vm->current_execution_state->native_call_depth = 0;
  p_vm->current_execution_state->native_has_error = false;
  p_vm->current_execution_state->error_pending = false;
  p_vm->current_execution_state->error_reported = false;
  p_vm->current_execution_state->error_hard = false;
  p_vm->current_execution_state->error_is_script = false;
  handler_stack_deinit(&p_vm->current_execution_state->handlers, p_vm->alloc);
  string_deinit(&p_vm->current_execution_state->native_error_message, p_vm->alloc);
  p_vm->alloc->release(p_vm->alloc, p_vm->current_execution_state);
}

// the hierarchy singletons, created only after the gc watches the vm (their
// only root is mark_vm). in-flight pieces ride the guard stack across the
// allocations that could collect mid-sequence: aggressive mode collects on
// every allocation, and an unrooted, unassigned object is exactly what a
// collect would sweep.
//
// the guard discipline here is PER-OBJECT BALANCED: a name rides the stack
// only across the class creation that adopts it, and drops the moment the
// class assignment roots the pair (mark_vm marks the singleton, its trace
// marks the name). the original shape held every name on the stack at once,
// which silently overflowed OK_GC_GUARD_CAPACITY (16) once the builtin
// singletons joined the sequence - gc_guard_up fails quietly past capacity,
// the str singleton's name was swept mid-startup, its memory got reused by
// the num name, and `x as str` probed "operator num". found under
// OK_AGGRESSIVE_GC by conversion_private_error.ok; guard pushes are checked
// now, and the cached names below need no guard at all (their assignment is
// the rooting - nothing allocates between the two).
// the canonical spelling for a c-literal name, interned: matches the
// compiler's constants by pointer (the probe fast paths hit on ==).
static object_string* intern_vm_string(vm* p_vm, const char* p_chars) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  return string_interner_intern(
      p_vm->interner, ok_create_string_view(p_chars, OK_STRING_VIEW_CALCULATE_LENGTH, true), &specs);
}

bool vm_init_singletons(vm* p_vm) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  gc* gc = p_vm->alloc->gc;
  object_string* object_name = intern_vm_string(p_vm, "object");
  if (object_name == NULL) {
    return false;
  }
  if (!gc_guard_up(gc, OBJECT_AS_VALUE(object_name))) {
    return false;
  }
  p_vm->object_singleton = create_object_class(object_name, NULL, &specs);
  gc_guard_down(gc); // the assignment above rooted class + name for good
  if (p_vm->object_singleton == NULL) {
    return false;
  }
  // the root is born complete: no fields, no methods, frozen empty.
  object_class_freeze(p_vm->object_singleton);
  object_string* class_name = intern_vm_string(p_vm, "type");
  if (class_name == NULL) {
    return false;
  }
  if (!gc_guard_up(gc, OBJECT_AS_VALUE(class_name))) {
    return false;
  }
  p_vm->class_singleton = create_object_class(class_name, p_vm->object_singleton, &specs);
  gc_guard_down(gc); // same rooting as above
  if (p_vm->class_singleton == NULL) {
    return false;
  }
  object_class_freeze(p_vm->class_singleton);
  // the cached lookup names: no guard window exists for any of them - the
  // assignment into the vm IS the rooting (mark_vm scans these slots, and
  // nothing allocates between assignment and the next possible collect).
  p_vm->static_call_name = intern_vm_string(p_vm, "operator ()");
  p_vm->init_method_name = intern_vm_string(p_vm, "init");
  // the operator probe names: one per class_operator, from the canonical
  // spelling table. [CALL] aliases static_call_name (same spelling), so it is
  // skipped here and patched in after the loop.
  for (uint32_t i = 0; i < CLASS_OP_COUNT; ++i) {
    if (i == (uint32_t)CLASS_OP_CALL) {
      continue;
    }
    if (p_vm->static_call_name == NULL || p_vm->init_method_name == NULL) {
      break;
    }
    p_vm->op_names[i] = intern_vm_string(p_vm, class_operator_spelling((class_operator)i));
  }
  p_vm->op_names[CLASS_OP_CALL] = p_vm->static_call_name;
  // the seven builtin singletons (C10): under object, created UNFROZEN here -
  // the native method surface is mounted by ok_define_builtin_surfaces (the
  // C-side step that owns their freeze, so the tables fill before the seal).
  // the name rides the guard stack only across the class creation that
  // adopts it.
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    object_string* builtin_name = intern_vm_string(p_vm, builtin_class_name((builtin_class_id)i));
    if (builtin_name == NULL) {
      break;
    }
    if (!gc_guard_up(gc, OBJECT_AS_VALUE(builtin_name))) {
      break;
    }
    p_vm->builtin_classes[i] = create_object_class(builtin_name, p_vm->object_singleton, &specs);
    gc_guard_down(gc); // the class assignment roots the pair
    if (p_vm->builtin_classes[i] == NULL) {
      break;
    }
  }
  for (uint32_t i = 0; i < CLASS_OP_COUNT; ++i) {
    if (p_vm->op_names[i] == NULL) {
      return false;
    }
  }
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    if (p_vm->builtin_classes[i] == NULL) {
      return false;
    }
  }
  return p_vm->static_call_name != NULL && p_vm->init_method_name != NULL;
}

bool vm_native_error(vm* p_vm, ok_string_view p_message) {
  execution_state* exe = p_vm->current_execution_state;
  if (exe->native_call_depth == 0) {
    return false;
  }
  exe->native_has_error = true;
  exe->native_error_message = create_string_from_string_view(p_message, p_vm->alloc);
  return true;
}

uint32_t vm_begin_source(vm* p_vm, const char* p_code, const char* p_path) {
  // the path is always ok's: chunks name their source after the run ends.
  // the code is the caller's for the run, or ok's copy when retention is on.
  allocators* alloc = p_vm->alloc;
  const size_t path_len = strlen(p_path) + 1;
  char* path_copy = alloc->allocate(alloc, path_len);
  if (path_copy == NULL) {
    return VM_SOURCE_UNSTAMPED;
  }
  memcpy(path_copy, p_path, path_len);
  const char* code = p_code;
  bool owns_code = false;
  if (p_vm->retain_source) {
    const size_t code_len = strlen(p_code) + 1;
    char* code_copy = alloc->allocate(alloc, code_len);
    if (code_copy == NULL) {
      alloc->release(alloc, path_copy);
      return VM_SOURCE_UNSTAMPED;
    }
    memcpy(code_copy, p_code, code_len);
    code = code_copy;
    owns_code = true;
  }
  vm_source_entry entry = {path_copy, code, owns_code};
  if (!vm_sources_append(&p_vm->sources, entry)) {
    if (owns_code) {
      alloc->release(alloc, (void*)code);
    }
    alloc->release(alloc, path_copy);
    return VM_SOURCE_UNSTAMPED;
  }
  return p_vm->sources.count - 1;
}

void vm_end_source(vm* p_vm, uint32_t p_source_id) {
  if (p_source_id == VM_SOURCE_UNSTAMPED || p_source_id >= p_vm->sources.count) {
    return;
  }
  vm_source_entry* entry = &p_vm->sources.data[p_source_id];
  if (!entry->owns_code) {
    // the borrow expired with the ok_run call: nothing may excerpt from it
    // anymore (the caller's buffer is theirs again). the path stays.
    entry->code = NULL;
  }
}

const vm_source_entry* vm_source_of(const vm* p_vm, uint32_t p_source_id) {
  if (p_source_id == VM_SOURCE_UNSTAMPED || p_source_id >= p_vm->sources.count) {
    return NULL;
  }
  return &p_vm->sources.data[p_source_id];
}

value vm_last_error(vm* p_vm) {
  execution_state* exe = p_vm->current_execution_state;
  if (!exe->has_error) {
    return NULL_AS_VALUE();
  }
  // a script-thrown value is handed back whole; a runtime-raised error
  // materializes its {:kind:, :what:} map fresh (the lane text is the truth,
  // the map is a projection, and a host that wants it past the next
  // allocation roots it, same contract as ok_array_new).
  return materialize_error_value(p_vm);
}

void vm_clear_error(vm* p_vm) {
  execution_state* exe = p_vm->current_execution_state;
  exe->has_error = false;
  exe->error_pending = false;
  exe->error_reported = false;
  exe->error_is_script = false;
  exe->error_hard = false;
  exe->error_value = NULL_AS_VALUE();
}

bool vm_native_throw(vm* p_vm, ok_value p_value) {
  execution_state* exe = p_vm->current_execution_state;
  if (exe->native_call_depth == 0) {
    return false; // like ok_native_error: only meaningful inside a native
  }
  // the value-throwing lane: set the error lane directly. call_native's
  // error_pending check fails the OP_CALL without touching the lane, and the
  // dispatch loop's unwind parks THE VALUE at the script's catch binder,
  // a host throw crosses into script catch as itself, zero extra machinery.
  exe->error_pending = true;
  exe->error_reported = false;
  exe->error_hard = false;
  exe->error_is_script = true;
  exe->error_value = p_value;
  exe->error_what[0] = '\0';
  exe->error_kind = OK_EK_NATIVE;
  exe->has_error = true;
  return true;
}

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  value vfn = OBJECT_AS_VALUE(p_specs.function);
  gc_guard_up(p_vm->alloc->gc, vfn);
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_closure* closure = create_object_closure(p_specs.function, &s);
  if (closure == NULL) {
    interpret_result res = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    runtime_oom(p_vm, "out of memory: failed to allocate to level function.");
    return res;
  }

  if (p_specs.top_level_values.count > STACK_SIZE - 2) { // otherwise call handles it
    if (!register_array_grow(&reg->array, closure->function->registers)) {
      interpret_result res = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return res;
    }
  }
  reg->array.data[reg->top] = OBJECT_AS_VALUE(closure); // call register

  // mark_vm roots registers in [0, top): the slot we just wrote is at top, so
  // until call() raises top to frame_top the closure would be unrooted and any
  // allocation inside call() (e.g. register file growth) could sweep it.
  reg->top++;
  const uint32_t hidden_start = reg->top;
  gc_guard_down(p_vm->alloc->gc);
  if (!call_value(p_vm, p_vm->current_execution_state, reg, 0, 0, 0, 0)) {
    return vm_error(p_vm, true);
  }
  for (uint16_t i = 0; i < p_specs.top_level_values.count; ++i) {
    reg->array.data[hidden_start + i] = p_specs.top_level_values.data[i];
  }
  interpret_result interpret_result = run_until(p_vm, 0);
  if (interpret_result.status != RUNTIME_OK) {
    // uncaught at the outermost level: FAIL_PATH already printed the report;
    // this is the final reset + consume (the ok_run lane keeps its print).
    vm_report_error(p_vm);
    reset_error_state(p_vm);
    exe->error_pending = false;
  }
  return interpret_result;
}

interpret_result vm_call(vm* p_vm, value p_callee, uint32_t p_argc) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  const uint32_t call_depth = exe->call_stack.count;
  const uint32_t base = reg->top - p_argc - 1;

  // p_callee is already at an index relative p_base (compare OP_CALL: frame base + register index).
  // passing base here double based the callee slot:
  // with a non-empty call stack (i.e. a native calling back into the vm).
  // while base > 0 and call_value read data[base + base], which is a garbage register, instead of the closure staged at
  // data[base].
  //
  // ok_call requests the dynamic lane. take everything, expose the
  // first ("first of span") instead of tripping the arity check.
  // single-result callees are byte-for-byte the old behavior.
  if (!call_value(p_vm, exe, reg, base, 0, p_argc, RETURN_COUNT_DYNAMIC)) {
    return vm_error(p_vm, false);
  }
  if (exe->call_stack.count == call_depth) {
    interpret_result result = {.status = RUNTIME_OK,
                               .top_level_return = reg->array.data[base]}; // TODO multiple returns
    return result;
  }
  interpret_result result = run_until(p_vm, call_depth);
  if (result.status != RUNTIME_OK) {
    // only the outermost C entry point may finalize: inner ok_calls (from inside a native)
    // leave the error pending and the frame alive so an outer script level can still catch it,
    // and the enclosing call_native can fail the OP_CALL instead of resuming.
    // at depth 0 this is the host-caught lane:
    // reset + consume, NO print (embedder reads ok_last_error).
    if (exe->native_call_depth == 0) {
      reset_error_state(p_vm);
      exe->error_pending = false;
    }
  }
  return result;
}

static inline bool is_falsy(value p_value) {
  return (IS_VALUE_STRICTLY_BOOL(p_value) && !VALUE_AS_BOOL(p_value)) || IS_VALUE_STRICTLY_NULL(p_value);
}

// p_is_constant selects between constant pool (k[index]) and frame register (r[index])
static inline value resolve_operand(call_frame* p_frame, register_file* p_reg, uint16_t p_index, bool p_is_constant) {
  if (p_is_constant) {
    return p_frame->closure->function->chunk.constants.data[p_index];
  }
  return p_reg->array.data[p_frame->registers_base + p_index];
}

// p_is_global selects between globals store (g[index]) and frame register (r[index])
static inline value
resolve_goperand(vm* p_vm, call_frame* p_frame, register_file* p_reg, uint16_t p_index, bool p_is_global) {
  if (p_is_global) {
    return p_vm->globals_store->global_values.data[p_index];
  }
  return p_reg->array.data[p_frame->registers_base + p_index];
}

// ============================== native methods ==============================
// (the natives-as-classes patch: one flight, two staging shapes)

// the shared native flight: run one C entry point with a staged argv and
// hand its result back through p_result. error lanes (ok_native_error,
// ok_native_throw, a failed ok_call inside) are reported here exactly like
// call_native always did. the caller owns where the result lands and the
// register watermark. p_argc is bounds-checked against the 8-bit argc of
// ok_native_fn itself - the old call_native would truncate and then overrun
// its stash on an ANY-arity native called with 256+ arguments (a landmine
// the method path would have inherited; now it is everyone's guard).
static inline bool
run_native(vm* p_vm, execution_state* p_exe, ok_native_fn p_fn, uint16_t p_argc, const value* p_argv, value* p_result) {
  if (p_argc > UINT8_MAX) {
    runtime_error(p_vm, OK_EK_ARITY, "too many arguments to a native call.");
    return false;
  }
  p_exe->native_call_depth++;
  value result = p_fn(p_vm->ok, (uint8_t)p_argc, p_argv);
  p_exe->native_call_depth--;
  if (p_exe->native_has_error) {
    runtime_error(p_vm, OK_EK_NATIVE, "%s", p_exe->native_error_message.chars);
    p_exe->native_has_error = false;
    string_deinit(&p_exe->native_error_message, p_vm->alloc);
    return false;
  }
  if (p_exe->error_pending) {
    // a runtime error fired inside the native (e.g. its ok_call on a script
    // closure failed). runtime_error already reset the state and printed the
    // report; resuming would reload frame state that no longer exists.
    return false;
  }
  p_exe->native_has_error = false;
  string_deinit(&p_exe->native_error_message, p_vm->alloc);
  *p_result = result;
  return true;
}

// a native METHOD entry: the receiver rides argv[0]. p_window is the call
// window base - the receiver's own register, arguments above it (the INVOKE
// shape, the bound-method shape, and the operator-dispatch staging all share
// it, which is the whole reason the door was pre-built this way). the
// declared arity COUNTS the receiver: the existing builtin handlers were
// already receiver-first functions (`len(x)` always took its receiver as
// argv[0]), so one C entry serves both spellings - `len(a)` and `a.len()`.
// no frame is pushed; the result lands at the window base like OP_CALL's
// r[a] = r[a](...) and the watermark returns to the caller's frame top.
static inline bool call_native_method(vm* p_vm,
                                      execution_state* p_exe,
                                      register_file* p_reg,
                                      const method_entry* p_entry,
                                      uint32_t p_window,
                                      uint16_t p_argc) {
  const uint32_t total = (uint32_t)p_argc + 1u;
  if (p_entry->arity != OK_NATIVE_ARITY_ANY && total != (uint32_t)p_entry->arity) {
    runtime_error(p_vm,
                  OK_EK_ARITY,
                  "invalid call to method '%s' of class '%s': expected %u argument%s, got %u.",
                  p_entry->name->string.chars,
                  p_entry->owner->name->string.chars,
                  (unsigned)p_entry->arity - 1u,
                  p_entry->arity - 1u == 1 ? "" : "s",
                  p_argc);
    return false;
  }
  if (total > UINT8_MAX) {
    runtime_error(p_vm, OK_EK_ARITY, "too many arguments to method '%s'.", p_entry->name->string.chars);
    return false;
  }
  // stage a copy: the native may re-enter the vm and grow (realloc) the
  // register file out from under a window pointer - call_native's discipline.
  value stash[UINT8_MAX];
  memcpy(stash, p_reg->array.data + p_window, total * sizeof(value));
  value result;
  if (!run_native(p_vm, p_exe, p_entry->native, (uint16_t)total, stash, &result)) {
    return false;
  }
  gc_guard_up(p_vm->alloc->gc, result);
  register_file* reg = &p_exe->registers;
  if (p_exe->call_stack.count > 0) {
    call_frame* caller = &p_exe->call_stack.data[p_exe->call_stack.count - 1];
    reg->top = caller->registers_base + caller->closure->function->registers;
  } else {
    reg->top = p_window;
  }
  reg->array.data[p_window] = result;
  gc_guard_down(p_vm->alloc->gc);
  return true;
}

// ============================== coroutines (R20-R24) ==============================
//
// a coro is an object that owns an execution_state: the same register file /
// call stack / handler stack shape the vm's own state has. resume SWAPS the
// vm's current state for the coro's, parks the resumer on vm.resume_parked
// (the collector walks every parked state like the current one - their
// registers and frames are live values), and drives the coro with a plain
// run_until(0). OP_YIELD parks the top frame in place (no pops, no upvalue
// closes, no defers) and returns RUNTIME_SUSPENDED straight out of THAT
// run_until - the yield barrier guarantees no C-driven nested run_until sits
// between the two, so suspension never has to cross a C frame (the discipline
// lua calls "not yielding across a C-call boundary", enforced as an error).
//
// death: a returned body (or one that errored) frees the state - a dead coro
// costs its object and nothing else. errors transplant to the resumer's lane
// (after the R22 death sweep ran the body's defers on the coro's own state),
// so the resume call site is where a catch sees them, and an uncaught one
// propagates outward through the resumer's own levels like any native
// failure. no scheduler, no transfer: a coro is driven by whoever holds it.

typedef enum {
  YIELD_BARRIER_CONSTRUCTOR = 0,
  YIELD_BARRIER_OPERATOR,
  YIELD_BARRIER_DEFER,
} yield_barrier_kind;

static const char* coro_status_names[4] = {"ready", "suspended", "running", "dead"};

const char* coro_status_name(uint8_t p_status) {
  return p_status <= (uint8_t)CORO_DEAD ? coro_status_names[p_status] : "dead";
}

// a fresh, all-empty state owned by p_coro. the register file warms at a
// small window; the body's own call() grows it on demand. every slot in a
// warm file reads null, which is the invariant mark_execution_state walks on.
static execution_state* coro_state_create(vm* p_vm, object_coro* p_coro) {
  allocators* alloc = p_vm->alloc;
  execution_state* state = (execution_state*)alloc->allocate(alloc, sizeof(execution_state));
  if (state == NULL) {
    return NULL;
  }
  state->open_upvalues = NULL;
  state->native_has_error = false;
  state->error_pending = false;
  state->error_reported = false;
  state->error_hard = false;
  state->error_is_script = false;
  state->error_value = NULL_AS_VALUE();
  state->error_kind = OK_EK_INTERNAL;
  state->has_error = false;
  state->error_what[0] = '\0';
  state->native_call_depth = 0;
  state->native_suspend_pending = false;
  state->owner_coro = p_coro;
  state->yield_barrier = 0;
  state->yield_barrier_kind = 0;
  state->next_parked = NULL;
  handler_stack_init(&state->handlers, alloc);
  string_init(&state->native_error_message, NULL, 0, false, alloc);
  register_file_init_warm(&state->registers, 16, alloc);
  call_stack_init(&state->call_stack, alloc);
  return state;
}

// the state teardown, shared by death (coro_drive) and the object sweep
// (object_dispatch_deinit's OBJ_CORO arm, through the header declaration).
void okvm_execution_state_dispose(execution_state* p_state, allocators* p_alloc) {
  call_stack_deinit(&p_state->call_stack, p_alloc);
  register_file_deinit(&p_state->registers);
  handler_stack_deinit(&p_state->handlers, p_alloc);
  string_deinit(&p_state->native_error_message, p_alloc);
  p_alloc->release(p_alloc, p_state);
}

// move the dying coro's error lane onto the resumer's. the resumer is
// executing a native call with a clean lane (nothing in flight), so the
// overwrite is total: kind, text, thrown value, the hard flag, and the
// pending/reported bookkeeping all cross, and the source clears - its state
// dies right after and nothing may read a moved lane.
static void transplant_error_lane(execution_state* p_from, execution_state* p_to) {
  memcpy(p_to->error_what, p_from->error_what, sizeof(p_to->error_what));
  p_to->error_kind = p_from->error_kind;
  p_to->error_value = p_from->error_value;
  p_to->error_is_script = p_from->error_is_script;
  p_to->error_hard = p_from->error_hard;
  p_to->error_pending = p_from->error_pending;
  p_to->error_reported = p_from->error_reported;
  p_to->has_error = p_from->has_error;
  p_from->error_pending = false;
  p_from->error_hard = false;
  p_from->has_error = false;
  p_from->error_reported = false;
  p_from->error_value = NULL_AS_VALUE();
}

// the resume driver. returns false with the error transplanted to the
// resumer's lane (the resume call itself fails there - catchable at the call
// site); true with *p_out as the yielded value (suspension) or the body's
// final value (completion). a dead coro answers null (R21), a running one
// errors (re-entrancy, however the chain reached it).
static bool coro_drive(vm* p_vm, object_coro* p_coro, bool p_has_arg, value p_arg, value* p_out) {
  execution_state* resumer = p_vm->current_execution_state;
  switch ((coro_status)p_coro->status) {
  case CORO_DEAD:
    *p_out = NULL_AS_VALUE(); // R21: the iterator-exhausted shape
    return true;
  case CORO_RUNNING:
    runtime_error(p_vm, OK_EK_VALUE, "resume of a running coroutine.");
    return false;
  case CORO_READY:
  case CORO_SUSPENDED:
    break;
  }
  if (p_coro->state == NULL) {
    p_coro->state = coro_state_create(p_vm, p_coro);
    if (p_coro->state == NULL) {
      runtime_oom(p_vm, "out of memory: failed to allocate a coroutine state.");
      return false;
    }
  }
  execution_state* cs = p_coro->state;

  // park the resumer, swap in the coro. from here until the swap-back, the
  // collector roots the resumer through vm.resume_parked (its parked
  // registers and frames are live - the coro reads them again when it yields
  // back) and the coro through current_execution_state, like any state.
  resumer->next_parked = p_vm->resume_parked;
  p_vm->resume_parked = resumer;
  p_vm->current_execution_state = cs;

  bool staged_ok = true;
  if (p_coro->status == CORO_READY) {
    // the first resume: stage the body closure at window slot 0, the resume
    // argument (when given) above it - the body's own parameters, exactly a
    // call's shape - then let call_value do arity/defaults/varargs. the
    // window rides the gc guard until the pushed frame roots the closure
    // (call's own discipline), and reg->top covers both slots meanwhile.
    register_file* reg = &cs->registers;
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(p_coro->body));
    reg->array.data[0] = OBJECT_AS_VALUE(p_coro->body);
    reg->array.data[1] = p_has_arg ? p_arg : NULL_AS_VALUE();
    reg->top = 2;
    staged_ok = call_value(p_vm, cs, reg, 0, 0, p_has_arg ? 1u : 0u, 1u);
    gc_guard_down(p_vm->alloc->gc);
  } else {
    // a suspended coro: the top frame is the yielder (nothing ran since the
    // park), and the argument lands in the register the parked OP_YIELD
    // named - the yield expression's own result slot.
    call_frame* yielder = &cs->call_stack.data[cs->call_stack.count - 1];
    cs->registers.array.data[yielder->registers_base + p_coro->resume_target] = p_arg;
  }

  interpret_result result = {.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  if (staged_ok) {
    p_coro->status = CORO_RUNNING;
    result = run_until(p_vm, 0);
  } else {
    // the staging itself failed (the body's arity contract, an oom): the
    // error is already on the coro's state and no frame was ever pushed -
    // the same death lane takes it (sweep is a no-op, the transplant is the
    // resume call's failure)
    result.status = RUNTIME_ERROR;
  }

  if (result.status == RUNTIME_ERROR) {
    // the coro died by error. the R22 death sweep runs FIRST, on the coro's
    // own state (still current): the propagation lane left the frames
    // unpopped, and reset_error_state runs their defers exactly like an
    // uncaught error at top level - innermost first, chains continue past a
    // failing defer, the first failure replaces the lane. the resumer stays
    // parked while the sweep's thunks allocate.
    reset_error_state(p_vm);
    // then the lane crosses (before the state dies - a moved lane, not a
    // shared one), the state goes away, and the resume call fails in the
    // resumer exactly where a catch can take it
    transplant_error_lane(cs, resumer);
    p_vm->current_execution_state = resumer;
    p_vm->resume_parked = resumer->next_parked;
    resumer->next_parked = NULL;
    p_coro->status = CORO_DEAD;
    p_coro->state = NULL;
    okvm_execution_state_dispose(cs, p_vm->alloc);
    return false;
  }

  // swap back + unpark, always
  p_vm->current_execution_state = resumer;
  p_vm->resume_parked = resumer->next_parked;
  resumer->next_parked = NULL;

  if (result.status == RUNTIME_SUSPENDED) {
    // the OP_YIELD handler parked the frame and set the status
    *p_out = result.top_level_return;
    return true;
  }

  // RUNTIME_OK: the body returned. the defers already ran in bytecode (the
  // compiler's scope exits), the state tears down, the value crosses.
  p_coro->status = CORO_DEAD;
  p_coro->state = NULL;
  okvm_execution_state_dispose(cs, p_vm->alloc);
  *p_out = result.top_level_return;
  return true;
}

// `coro(fu)` - the overridden static operator (): argv[0] is the class value
// itself, argv[1] the body. the body must be a plain closure (a fu or
// lambda); a bound method or a native is refused with the honest message.
static ok_value native_coro_construct(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  vm* p_vm = ok_vm(p_ok);
  if (p_argc != 2 || !IS_VALUE_CLOSURE(p_argv[1])) {
    runtime_error(p_vm, OK_EK_TYPE, "the coro body must be a fu.");
    return NULL_AS_VALUE();
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_closure* body = VALUE_AS_CLOSURE(p_argv[1]);
  gc_guard_up(p_vm->alloc->gc, p_argv[1]); // the coro rides the guard until the caller's window adopts it
  object_coro* coro = create_object_coro(body, &specs);
  gc_guard_down(p_vm->alloc->gc);
  if (coro == NULL) {
    runtime_oom(p_vm, "out of memory: failed to allocate a coroutine.");
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(coro);
}

// `c.resume([v])` - the drive call. the first resume's argument (when given)
// becomes the body's own argument; a suspended resume's argument lands in the
// parked yield expression. resume returns the yielded value, or the body's
// final value when it completes.
static ok_value native_coro_resume(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  vm* p_vm = ok_vm(p_ok);
  if (p_argc < 1 || p_argc > 2) {
    runtime_error(p_vm,
                  OK_EK_ARITY,
                  "invalid call to method 'resume' of class 'coro': expected at most 1 argument, got %u.",
                  (unsigned)(p_argc - 1u));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_CORO(p_argv[0])) {
    runtime_error(p_vm, OK_EK_TYPE, "resume: the receiver is not a coro.");
    return NULL_AS_VALUE();
  }
  object_coro* coro = VALUE_AS_CORO(p_argv[0]);
  value out;
  if (!coro_drive(p_vm, coro, p_argc == 2, p_argc == 2 ? p_argv[1] : NULL_AS_VALUE(), &out)) {
    return NULL_AS_VALUE(); // the lane (transplanted to the resumer) carries the error
  }
  return out;
}

// `c.status()` - "ready" | "suspended" | "running" | "dead". interned
// spellings: canonical objects shared with any other holder of the same
// content (=== identity), and a poll loop reuses the live entry without
// allocating. the interner is a weak set (D20): a status name nothing holds
// dies at the next sweep and the next poll simply re-interns it.
static ok_value native_coro_status(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  vm* p_vm = ok_vm(p_ok);
  if (p_argc != 1 || !IS_VALUE_CORO(p_argv[0])) {
    runtime_error(p_vm, OK_EK_TYPE, "status: the receiver is not a coro.");
    return NULL_AS_VALUE();
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* name = string_interner_intern(
      p_vm->interner,
      ok_create_string_view(coro_status_name(VALUE_AS_CORO(p_argv[0])->status), OK_STRING_VIEW_CALCULATE_LENGTH, true),
      &specs);
  if (name == NULL) {
    runtime_oom(p_vm, "out of memory: failed to intern a status name.");
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(name);
}

bool vm_define_coro_surface(vm* p_vm) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_class* klass = p_vm->builtin_classes[OK_BUILTIN_CORO];
  bool status = true;
  status &= object_class_define_native_static_method(
      klass, ok_create_string_view("operator ()", 11, true), 2, native_coro_construct, /*p_pub*/ true, &specs);
  status &= object_class_define_native_method(
      klass, ok_create_string_view("resume", 6, true), OK_NATIVE_ARITY_ANY, native_coro_resume, true, &specs);
  status &=
      object_class_define_native_method(klass, ok_create_string_view("status", 6, true), 1, native_coro_status, true, &specs);
  return status;
}

// =========================== the coro C API (patch 42) ===========================
//
// ok.h's embedder surface over the same engine the natives drive: create,
// drive, inspect, and the one piece of machinery a script cannot express -
// a native parking its caller. the wrappers keep ok_call's C boundary
// discipline: errors land in the boundary's lane (ok_last_error at depth
// 0, the enclosing call's failure inside a native), values cross as plain
// words, rooting past each call is the host's job.

// the ok.h status enum and the internal coro_status must stay index-parallel
// (ok_coro_status returns the internal word unchecked).
typedef char ok_coro_status_is_index_parallel[(OK_CORO_READY == (int)CORO_READY &&
                                                OK_CORO_SUSPENDED == (int)CORO_SUSPENDED &&
                                                OK_CORO_RUNNING == (int)CORO_RUNNING &&
                                                OK_CORO_DEAD == (int)CORO_DEAD)
                                                   ? 1
                                                   : -1];

// the park half of ok_native_suspend, shared by every call-completion arm
// that can finish a suspend-requesting native (OP_CALL x4, OP_INVOKE,
// OP_SUPER_INVOKE - the operator / constructor / unwind lanes are refused
// at request time by the barrier, so they never reach a park). the native's
// result already sits in the window at p_result (a frame-relative register
// index); it goes OUT to the resumer, the coro parks in place, and the next
// resume's argument lands in that same register - the yield contract, one
// boundary later.
static interpret_result native_suspend_park(execution_state* p_exe,
                                             register_file* p_reg,
                                             call_frame* p_frame,
                                             uint16_t p_result) {
  p_exe->native_suspend_pending = false;
  object_coro* coro = p_exe->owner_coro;
  coro->resume_target = p_result;
  coro->status = CORO_SUSPENDED;
  return (interpret_result){.status = RUNTIME_SUSPENDED,
                            .top_level_return = p_reg->array.data[p_frame->registers_base + p_result]};
}

ok_value ok_coro_new(ok* p_ok, ok_value p_body) {
  vm* p_vm = ok_vm(p_ok);
  if (!IS_VALUE_CLOSURE(p_body)) {
    return NULL_AS_VALUE();
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  gc_guard_up(p_vm->alloc->gc, p_body); // the coro rides the guard until the caller roots it
  object_coro* coro = create_object_coro(VALUE_AS_CLOSURE(p_body), &specs);
  gc_guard_down(p_vm->alloc->gc);
  if (coro == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(coro);
}

bool ok_coro_resume(ok* p_ok, ok_value p_coro, ok_value p_arg, bool p_has_arg, ok_value* p_out) {
  vm* p_vm = ok_vm(p_ok);
  if (!IS_VALUE_CORO(p_coro)) {
    *p_out = NULL_AS_VALUE();
    return false;
  }
  // the coro may be unrooted host state for the whole drive: the guard
  // covers exactly the window in which the host holds no reference the
  // collector can see (the drive itself swaps the states, the parked
  // resumer's registers keep the script-lane receiver alive the same way).
  gc_guard_up(p_vm->alloc->gc, p_coro);
  value out;
  const bool drove = coro_drive(p_vm, VALUE_AS_CORO(p_coro), p_has_arg, p_arg, &out);
  gc_guard_down(p_vm->alloc->gc);
  if (!drove) {
    // the lane (transplanted to this boundary's state) carries the error:
    // consumed here at depth 0 for ok_last_error, left pending inside a
    // native so the enclosing call fails - vm_call's exact discipline.
    if (p_vm->current_execution_state->native_call_depth == 0) {
      vm_error(p_vm, false);
    }
    *p_out = NULL_AS_VALUE();
    return false;
  }
  *p_out = out;
  return true;
}

uint32_t ok_coro_status(ok* p_ok, ok_value p_coro) {
  (void)p_ok;
  if (!IS_VALUE_CORO(p_coro)) {
    return UINT32_MAX;
  }
  return VALUE_AS_CORO(p_coro)->status;
}

ok_value ok_coro_current(ok* p_ok) {
  vm* p_vm = ok_vm(p_ok);
  object_coro* coro = p_vm->current_execution_state->owner_coro;
  if (coro == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(coro);
}

bool ok_native_suspend(ok* p_ok) {
  vm* p_vm = ok_vm(p_ok);
  execution_state* exe = p_vm->current_execution_state;
  if (exe->native_call_depth == 0) {
    return false; // not inside a native: there is no return boundary to park at
  }
  if (exe->owner_coro == NULL) {
    runtime_error(p_vm, OK_EK_VALUE, "cannot suspend outside a coroutine.");
    return false;
  }
  if (exe->native_call_depth > 1) {
    runtime_error(p_vm, OK_EK_VALUE, "cannot suspend across a native call.");
    return false;
  }
  if (exe->yield_barrier > 0) {
    switch ((yield_barrier_kind)exe->yield_barrier_kind) {
    case YIELD_BARRIER_CONSTRUCTOR:
      runtime_error(p_vm, OK_EK_VALUE, "cannot suspend from inside a constructor.");
      break;
    case YIELD_BARRIER_OPERATOR:
      runtime_error(p_vm, OK_EK_VALUE, "cannot suspend from inside an operator method.");
      break;
    case YIELD_BARRIER_DEFER:
    default:
      runtime_error(p_vm, OK_EK_VALUE, "cannot suspend from inside a defer.");
      break;
    }
    return false;
  }
  exe->native_suspend_pending = true;
  return true;
}
static bool entry_visible_from(const method_entry* p_entry, const call_frame* p_frame);
static interpret_result error_interpret_result(void);
static interpret_result dispatch_operator_method(vm* p_vm,
                                                 register_file* p_reg,
                                                 call_frame* p_frame,
                                                 uint16_t p_dest,
                                                 const method_entry* p_entry,
                                                 const object_string* p_name,
                                                 value p_receiver,
                                                 const value* p_args,
                                                 uint32_t p_argc);

// the instance arm of every perform ladder (C14): when the left operand is an
// instance whose chain carries the operator, run it. returns true when the
// call HAPPENED (p_out carries its result or its error); false when the chain
// has no such operator and the caller continues to its own error/fallback.
// privacy rides the running frame like every method call. the cold path by
// construction: the number and string lanes return before this arm ever runs.
static bool try_instance_operator(vm* p_vm,
                                  register_file* p_reg,
                                  call_frame* p_frame,
                                  uint16_t p_dest,
                                  value p_lhs,
                                  const value* p_args,
                                  uint32_t p_argc,
                                  class_operator p_op,
                                  interpret_result* p_out) {
  if (!IS_VALUE_INSTANCE(p_lhs)) {
    return false;
  }
  object_class* klass = VALUE_AS_INSTANCE(p_lhs)->klass;
  if (p_op >= CLASS_OP_COUNT || p_vm->op_names[p_op] == NULL) {
    return false;
  }
  const method_entry* entry = object_class_find_operator(klass, p_op, p_vm->op_names[p_op]);
  if (entry == NULL) {
    return false;
  }
  if (!entry_visible_from(entry, p_frame)) {
    runtime_error(p_vm,
                  OK_EK_TYPE,
                  "operator '%s' of class '%s' is private.",
                  p_vm->op_names[p_op]->string.chars,
                  entry->owner->name->string.chars);
    *p_out = error_interpret_result();
    return true;
  }
  *p_out = dispatch_operator_method(
      p_vm, p_reg, p_frame, p_dest, entry, p_vm->op_names[p_op], p_lhs, p_args, p_argc);
  return true;
}

static interpret_result perform_not(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_on) {
  const uint32_t base = p_frame->registers_base;
  value on = p_on;
  if (IS_VALUE_NUMBER(on)) {
    goto invalid;
  }
  if (IS_VALUE_BOOL(on)) {
    on = BOOL_AS_VALUE(!VALUE_AS_BOOL(on));
  } else if (IS_VALUE_OBJECT(on)) {
    goto invalid;
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, OK_EK_TYPE, "invalid operand for unary '!'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  p_reg->array.data[base + p_dest] = on;
  interpret_result res;
  res.status = RUNTIME_OK;
  return res;
}

static interpret_result
perform_negate(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_on) {
  const uint32_t base = p_frame->registers_base;
  value on = p_on;
  if (IS_VALUE_NUMBER(on)) {
    on ^= SIGN_BIT; // ultra fast
  } else if (IS_VALUE_INT(on)) {
    on = INT_AS_VALUE(-VALUE_AS_INT(on));
  } else if (IS_VALUE_OBJECT(on)) {
    // the instance arm (C14): unary minus shares the "operator -" name with
    // the binary; the method's own arity is the discriminator.
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, on, NULL, 0, CLASS_OP_NEGATE, &out)) {
      return out;
    }
    goto invalid;
  } else {
  invalid: {
    runtime_error(p_vm, OK_EK_TYPE, "invalid operand for unary '-'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  p_reg->array.data[base + p_dest] = on;
  interpret_result res;
  res.status = RUNTIME_OK;
  return res;
}

// the numeric union: doubles and the int48 lane, one comparison point. the
// int48 payload converts to double exactly (2^48 < 2^53), so mixed compares
// through f64 lose nothing.
static inline bool value_is_numeric(value p_v) {
  return IS_VALUE_NUMBER(p_v) || IS_VALUE_STRICTLY_INT(p_v);
}

// strictly-int first: raw doubles can alias the int tag (the _STRICT
// doctrine - the tag check alone is not a discriminator), and a double that
// aliases must take the VALUE_AS_NUMBER read.
static inline double value_as_f64(value p_v) {
  return IS_VALUE_STRICTLY_INT(p_v) ? (double)VALUE_AS_INT(p_v) : VALUE_AS_NUMBER(p_v);
}

static interpret_result
perform_add(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  // the number lane first: the common case, and nothing below it can ever
  // intercept it (the builtin classes are sealed).
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    if (IS_VALUE_STRICTLY_INT(lhs) && IS_VALUE_STRICTLY_INT(p_rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) + VALUE_AS_INT(p_rhs));
    } else {
      lhs = NUMBER_AS_VALUE(value_as_f64(lhs) + value_as_f64(p_rhs));
    }
    p_reg->array.data[base + p_dest] = lhs;
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  switch (VALUE_TAG(lhs)) {
  case TAG_OBJECT_MASK: {
    // string concatenation: both operands must be strings, no implicit
    // coercion ("a" + 1 = error). build the combined bytes over a raw scratch
    // buffer first: it is plain memory, invisible to the gc, so allocations
    // inside the object creation can collect freely.
    if (!IS_VALUE_STRING(lhs) || !IS_VALUE_STRING(p_rhs)) {
      break;
    }
    const object_string* left = VALUE_AS_STRING(lhs);
    const object_string* right = VALUE_AS_STRING(p_rhs);
    const uint64_t left_length = string_get_length(&left->string);
    const uint64_t right_length = string_get_length(&right->string);
    char* scratch = (char*)p_vm->alloc->allocate(p_vm->alloc, left_length + right_length);
    if (scratch == NULL) {
      runtime_oom(p_vm, "out of memory: failed to concatenate strings.");
      return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    }
    memcpy(scratch, left->string.chars, left_length);
    memcpy(scratch + left_length, right->string.chars, right_length);
    object_specs specs = {p_vm->alloc, p_vm->objects_store};
    object_string* concatenated =
        create_object_string(ok_create_string_view(scratch, left_length + right_length, false), &specs);
    p_vm->alloc->release(p_vm->alloc, scratch);
    if (concatenated == NULL) {
      runtime_oom(p_vm, "out of memory: failed to concatenate strings.");
      return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    }
    p_reg->array.data[base + p_dest] = OBJECT_AS_VALUE(concatenated);
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  default:
    break;
  }
  // the instance arm (C14): a + b dispatches on a's class only.
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_ADD, &out)) {
      return out;
    }
  }
  runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '+'.");
  return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_subtract(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    if (IS_VALUE_STRICTLY_INT(lhs) && IS_VALUE_STRICTLY_INT(p_rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) - VALUE_AS_INT(p_rhs));
    } else {
      lhs = NUMBER_AS_VALUE(value_as_f64(lhs) - value_as_f64(p_rhs));
    }
    p_reg->array.data[base + p_dest] = lhs;
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  // the instance arm (C14): a - b dispatches on a's class only. unary minus
  // shares the "operator -" name and splits by arity at the method itself.
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_SUBTRACT, &out)) {
      return out;
    }
  }
  runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '-'.");
  return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_multiply(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    if (IS_VALUE_STRICTLY_INT(lhs) && IS_VALUE_STRICTLY_INT(p_rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) * VALUE_AS_INT(p_rhs));
    } else {
      lhs = NUMBER_AS_VALUE(value_as_f64(lhs) * value_as_f64(p_rhs));
    }
    p_reg->array.data[base + p_dest] = lhs;
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  // the instance arm (C14).
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_MULTIPLY, &out)) {
      return out;
    }
  }
  runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '*'.");
  return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_divide(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    if (IS_VALUE_STRICTLY_INT(lhs) && IS_VALUE_STRICTLY_INT(p_rhs)) {
      if (VALUE_AS_INT(p_rhs) == 0) {
        runtime_error(p_vm, OK_EK_VALUE, "division by zero.");
        return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      }
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) / VALUE_AS_INT(p_rhs));
    } else {
      if (value_as_f64(p_rhs) == 0.0) {
        runtime_error(p_vm, OK_EK_VALUE, "division by zero.");
        return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      }
      lhs = NUMBER_AS_VALUE(value_as_f64(lhs) / value_as_f64(p_rhs));
    }
    p_reg->array.data[base + p_dest] = lhs;
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  // the instance arm (C14).
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_DIVIDE, &out)) {
      return out;
    }
  }
  runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '/'.");
  return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
}

static double floor_mod(double p_lhs, double p_rhs) {
  const double remainder = fmod(p_lhs, p_rhs);
  if (remainder != 0.0 && ((remainder < 0.0) != (p_rhs < 0.0))) {
    return remainder + p_rhs;
  }
  return remainder;
}

static interpret_result
perform_modulo(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    if (IS_VALUE_STRICTLY_INT(lhs) && IS_VALUE_STRICTLY_INT(p_rhs)) {
      if (VALUE_AS_INT(p_rhs) == 0) {
        runtime_error(p_vm, OK_EK_VALUE, "modulo by zero.");
        return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      }
      int64_t remainder = VALUE_AS_INT(lhs) % VALUE_AS_INT(p_rhs);
      if (remainder != 0 && ((remainder < 0) != (VALUE_AS_INT(p_rhs) < 0))) {
        remainder += VALUE_AS_INT(p_rhs); // |remainder| < |rhs|: cannot leave the int48 domain
      }
      lhs = INT_AS_VALUE(remainder);
    } else {
      if (value_as_f64(p_rhs) == 0.0) {
        runtime_error(p_vm, OK_EK_VALUE, "modulo by zero.");
        return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      }
      lhs = NUMBER_AS_VALUE(floor_mod(value_as_f64(lhs), value_as_f64(p_rhs)));
    }
    p_reg->array.data[base + p_dest] = lhs;
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  // the instance arm (C14).
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_MODULUS, &out)) {
      return out;
    }
  }
  runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '%%'.");
  return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(value_as_f64(lhs) == value_as_f64(p_rhs));
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  switch (VALUE_TAG(lhs)) {
  case TAG_OBJECT_MASK: {
    // value equality for strings: equal content, not common prefix. interned
    // operands (literals, compiler names) settle on the pointer compare first.
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(p_rhs)) {
      if (lhs == p_rhs) {
        p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(true);
        return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
      }
      const object_string* left = VALUE_AS_STRING(lhs);
      const object_string* right = VALUE_AS_STRING(p_rhs);
      const uint64_t left_length = string_get_length(&left->string);
      p_reg->array.data[base + p_dest] =
          BOOL_AS_VALUE(left_length == string_get_length(&right->string) &&
                        memcmp(left->string.chars, right->string.chars, left_length) == 0);
      return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
    }
    break;
  }
  default:
    break;
  }
  // mixed type equality intentionally falls back to a raw identity check
  // (null/bool vs anything): turning this into a type error is a semantic
  // decision deferred to when we design the error model.
  // the instance arm (C11/C14): with operator == the class's method decides;
  // without it the identity fallback below is the shipped default.
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_EQUALS, &out)) {
      return out;
    }
  }
  p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(lhs == p_rhs);
  return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_not_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  if (value_is_numeric(lhs) && value_is_numeric(p_rhs)) {
    p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(value_as_f64(lhs) != value_as_f64(p_rhs));
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  switch (VALUE_TAG(lhs)) {
  case TAG_OBJECT_MASK: {
    // value equality for strings, negated - same pointer/content shape.
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(p_rhs)) {
      if (lhs == p_rhs) {
        p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(false);
        return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
      }
      const object_string* left = VALUE_AS_STRING(lhs);
      const object_string* right = VALUE_AS_STRING(p_rhs);
      const uint64_t left_length = string_get_length(&left->string);
      p_reg->array.data[base + p_dest] =
          BOOL_AS_VALUE(!(left_length == string_get_length(&right->string) &&
                          memcmp(left->string.chars, right->string.chars, left_length) == 0));
      return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
    }
    break;
  }
  default:
    break;
  }
  // mixed type: the identity fallback, negated (see perform_equal). the
  // instance arm dispatches operator == and negates its result's truthiness
  // - `a != b` is exactly `!(a == b)`, no separate operator.
  {
    interpret_result out;
    if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, lhs, &p_rhs, 1, CLASS_OP_EQUALS, &out)) {
      if (out.status == RUNTIME_OK) {
        p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(is_falsy(p_reg->array.data[base + p_dest]));
      }
      return out;
    }
  }
  p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(lhs != p_rhs);
  return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
}

// the four comparisons share one shape: numbers compare through the numeric
// union, instances dispatch on the left operand's class (C14), everything
// else is a type error (no coercion, no cross-type order).
#define PERFORM_ORDERED(name, op, spelling, class_op)                                                                    \
  static interpret_result perform_##name(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest,          \
                                          value p_lhs, value p_rhs) {                                                   \
    const uint32_t base = p_frame->registers_base;                                                                      \
    if (value_is_numeric(p_lhs) && value_is_numeric(p_rhs)) {                                                           \
      p_reg->array.data[base + p_dest] = BOOL_AS_VALUE(value_as_f64(p_lhs) op value_as_f64(p_rhs));                     \
      return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};                             \
    }                                                                                                                   \
    {                                                                                                                   \
      interpret_result out;                                                                                             \
      if (try_instance_operator(p_vm, p_reg, p_frame, p_dest, p_lhs, &p_rhs, 1, class_op, &out)) {                      \
        return out;                                                                                                     \
      }                                                                                                                 \
    }                                                                                                                   \
    runtime_error(p_vm, OK_EK_TYPE, "invalid operands for binary '" spelling "'.");                                     \
    return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};                            \
  }

PERFORM_ORDERED(greater, >, ">", CLASS_OP_GREATER)
PERFORM_ORDERED(less, <, "<", CLASS_OP_LESS)
PERFORM_ORDERED(greater_equal, >=, ">=", CLASS_OP_GREATER_EQUAL)
PERFORM_ORDERED(less_equal, <=, "<=", CLASS_OP_LESS_EQUAL)

static inline bool as_str_rest(vm* p_vm, ok_string_view p_str, value* p_result) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* str = create_object_string(p_str, &specs);
  if (str == NULL) {
    runtime_oom(p_vm, "out of memory: failed to allocate string object.");
    return false;
  }
  *p_result = OBJECT_AS_VALUE(str);
  return true;
}

// asprint + as_str_rest + the release of the printed intermediate. the bare
// pattern leaked the middle buffer on every call (lsan found it the day
// `instance as str` first ran in the e2e corpus - the object arms had simply
// never been reached before). the result value is written before the deinit
// and the deinit never collects, so no rooting window opens.
static inline bool as_str_printed(vm* p_vm, string p_printed, value* p_result) {
  if (p_printed.chars == NULL) {
    return false; // the caller reported its own failure flavor already
  }
  const bool ok = as_str_rest(p_vm, ok_create_string_view_from_string(p_printed), p_result);
  string_deinit(&p_printed, p_vm->alloc);
  return ok;
}

static inline bool num_as_str(vm* p_vm, double p_value, value* p_result) {
  char buff[64];
  if (snprintf(buff, 64, "%g", p_value) < 0) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert number to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view(buff, OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
}

static inline bool int_as_str(vm* p_vm, int64_t p_value, value* p_result) {
  char buff[64];
  if (snprintf(buff, 64, "%li", p_value) < 0) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert number to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view(buff, OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
}

static inline bool as_str(vm* p_vm, value p_value, value* p_result);

// `{k: v, ...}` in slot order (iteration order is unspecified v1 printing follows it).
// string keys print unquoted, same display form array elements use.
// the empty map prints `{:}`, its literal. the key's bytes are copied
// into the scratch buffer before the value converts: each as_str allocates (collects),
// and an unrooted intermediate would be swept otherwise.
static inline bool map_as_str(vm* p_vm, object* p_obj, value* p_result) {
  object_map* map = (object_map*)p_obj;
  char* chars = NULL;
  size_t len = 0;
  for (uint32_t i = 0; i < map->entries.capacity; ++i) {
    if (IS_VALUE_STRICTLY_NULL(map->entries.keys[i])) {
      continue;
    }
    const uint8_t dellen = len == 0 ? 0 : 2;
    value kres;
    if (!as_str(p_vm, map->entries.keys[i], &kres)) {
      return false;
    }
    string* kstr = &VALUE_AS_STRING(kres)->string;
    const uint32_t klen = (uint32_t)string_get_length(kstr);
    if (kstr->chars != NULL) {
      gc_guard_up(p_vm->alloc->gc, kres);
      chars = p_vm->alloc->reallocate(p_vm->alloc, chars, len, len + dellen + klen + 2);
      gc_guard_down(p_vm->alloc->gc);
      if (chars == NULL) {
        runtime_oom(p_vm, "out of memory: failed to convert map to string.");
        return false;
      }
      if (dellen != 0) {
        memcpy(chars + len, ", ", dellen);
      }
      memcpy(chars + len + dellen, kstr->chars, klen);
      memcpy(chars + len + dellen + klen, ": ", 2);
      len += dellen + klen + 2;
    }
    value vres;
    if (!as_str(p_vm, map->entries.values[i], &vres)) {
      return false;
    }
    string* vstr = &VALUE_AS_STRING(vres)->string;
    const uint32_t vlen = (uint32_t)string_get_length(vstr);
    if (vstr->chars != NULL) {
      gc_guard_up(p_vm->alloc->gc, vres);
      chars = p_vm->alloc->reallocate(p_vm->alloc, chars, len, len + vlen);
      gc_guard_down(p_vm->alloc->gc);
      if (chars == NULL) {
        runtime_oom(p_vm, "out of memory: failed to convert map to string.");
        return false;
      }
      memcpy(chars + len, vstr->chars, vlen);
      len += vlen;
    }
  }
  string fstr = len == 0 ? asprint(p_vm->alloc, "{:}") : asprint(p_vm->alloc, "{%.*s}", len, chars);
  if (chars != NULL) {
    p_vm->alloc->release(p_vm->alloc, chars);
  }
  if (fstr.chars == NULL) {
    runtime_oom(p_vm, "out of memory: failed to convert map to string.");
    return false;
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* str = create_object_string(ok_create_string_view_from_string(fstr), &specs);
  if (str == NULL) {
    runtime_oom(p_vm, "out of memory: failed to convert map to string.");
    return false;
  }
  *p_result = OBJECT_AS_VALUE(str);
  string_deinit(&fstr, p_vm->alloc); // here so obj str is rooted
  return true;
}

static inline bool array_as_str(vm* p_vm, object* p_arr, value* p_result) {
  object_array* arr = (object_array*)p_arr;
  char* chars = NULL;
  size_t len = 0;
  for (uint32_t i = 0; i < arr->values.count; ++i) {
    // ", " after each element except the last. (the original placed it before
    // each element except the last, printing "[, 1, 23]" for [1, 2, 3]
    // no e2e test ever converted a multi-element array, so it survived v0.1.0.)
    const uint8_t delemlen = i + 1 < arr->values.count ? 2 : 0;
    value res;
    if (!as_str(p_vm, arr->values.data[i], &res)) {
      return false;
    }
    string* str = &VALUE_AS_STRING(res)->string;
    uint32_t reslen = string_get_length(str);
    if (str->chars != NULL) {
      gc_guard_up(p_vm->alloc->gc, res);
      chars = p_vm->alloc->reallocate(p_vm->alloc, chars, len, len + reslen + delemlen);
      gc_guard_down(p_vm->alloc->gc);
      if (chars == NULL) {
        runtime_oom(p_vm, "out of memory: failed to convert array to string.");
        return false;
      }
      memcpy(chars + len, str->chars, reslen);
      if (delemlen != 0) {
        memcpy(chars + len + reslen, ", ", delemlen);
      }
      len += reslen + delemlen;
    }
  }
  string fstr = asprint(p_vm->alloc, "[%.*s]", len, chars);
  if (chars != NULL) {
    p_vm->alloc->release(p_vm->alloc,
                         chars); // release only after asprint copied: it could be NULL for an all-empty array
  }
  if (fstr.chars == NULL) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert array to string.");
    return false;
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* str = create_object_string(ok_create_string_view_from_string(fstr), &specs);
  if (str == NULL) {
    runtime_oom(p_vm, "out of memory: failed to convert array to string.");
    return false;
  }
  *p_result = OBJECT_AS_VALUE(str);
  string_deinit(&fstr, p_vm->alloc); // here so obj str is rooted
  return true;
}

static inline bool closure_as_str(vm* p_vm, object* p_closure, value* p_result) {
  object_closure* closure = (object_closure*)p_closure;
  string str = asprint(p_vm->alloc, "fu %s |%i|", closure->function->name, closure->function->arity);
  if (str.chars == NULL) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert function to string.");
    return false;
  }
  return as_str_printed(p_vm, str, p_result);
}

static inline bool native_function_as_str(vm* p_vm, object* p_native_function, value* p_result) {
  object_native_function* nativefu = (object_native_function*)p_native_function;
  char arity[16];
  if (nativefu->arity == OK_NATIVE_ARITY_ANY) {
    snprintf(arity, sizeof(arity), "any");
  } else {
    snprintf(arity, sizeof(arity), "%u", nativefu->arity);
  }
  string str = asprint(p_vm->alloc, "native fu %s |%s| (@%p)", nativefu->name, arity, nativefu->function);
  if (str.chars == NULL) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert native function to string.");
    return false;
  }
  return as_str_printed(p_vm, str, p_result);
}

static inline bool native_value_as_str(vm* p_vm, object* p_native_value, value* p_result) {
  object_native_value* nativev = (object_native_value*)p_native_value;
  string str = asprint(p_vm->alloc, "native value (@%p) (dtor @%p)", nativev->user_data, nativev->destructor);
  if (str.chars == NULL) {
    runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert native value to string.");
    return false;
  }
  return as_str_printed(p_vm, str, p_result);
}

static inline bool object_as_str(vm* p_vm, object* p_obj, value* p_result) {
  switch (object_get_type(p_obj)) {
  case OBJ_STRING: {
    *p_result = OBJECT_AS_VALUE(p_obj);
    return true;
  }
  case OBJ_ARRAY: {
    return array_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_MAP: {
    return map_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_CLOSURE: {
    return closure_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_NATIVE_FUNCTION: {
    return native_function_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_NATIVE_VALUE: {
    return native_value_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_CLASS: {
    object_class* klass = (object_class*)p_obj;
    string str = asprint(p_vm->alloc, "<class %s>", klass->name->string.chars);
    if (str.chars == NULL) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert class to string.");
      return false;
    }
    return as_str_printed(p_vm, str, p_result);
  }
  case OBJ_INSTANCE: {
    object_instance* inst = (object_instance*)p_obj;
    // OR-1: a module namespace prints as what it is. the class name is the
    // canonical path for modules, the root segment for root namespaces.
    string str = asprint(p_vm->alloc,
                         inst->klass->module_namespace ? "<module %s>" : "<instance %s>",
                         inst->klass->name->string.chars);
    if (str.chars == NULL) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert instance to string.");
      return false;
    }
    return as_str_printed(p_vm, str, p_result);
  }
  case OBJ_BOUND_METHOD: {
    object_bound_method* bound = (object_bound_method*)p_obj;
    string str = asprint(p_vm->alloc, "<method %s>", bound->entry->name->string.chars);
    if (str.chars == NULL) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert method to string.");
      return false;
    }
    return as_str_printed(p_vm, str, p_result);
  }
  case OBJ_CORO: {
    object_coro* coro = (object_coro*)p_obj;
    string str = asprint(p_vm->alloc, "<coro %s>", coro_status_name(coro->status));
    if (str.chars == NULL) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert coro to string.");
      return false;
    }
    return as_str_printed(p_vm, str, p_result);
  }
  }
  runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert unknown object to string.");
  return false;
}

static inline bool as_str(vm* p_vm, value p_value, value* p_result) {
  if (IS_VALUE_NUMBER(p_value)) {
    return num_as_str(p_vm, VALUE_AS_NUMBER(p_value), p_result);
  }

  const uint64_t tag = VALUE_TAG(p_value);
  switch (tag) {
  case TAG_INT_MASK: {
    return int_as_str(p_vm, VALUE_AS_INT(p_value), p_result);
  }
  case TAG_NULL_MASK: {
    return as_str_rest(p_vm, ok_create_string_view("null", OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
  }
  case TAG_TRUE_MASK: {

    return as_str_rest(p_vm, ok_create_string_view("true", OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
  }
  case TAG_FALSE_MASK: {
    return as_str_rest(p_vm, ok_create_string_view("false", OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
  }
  case TAG_OBJECT_MASK: {
    return object_as_str(p_vm, VALUE_AS_OBJECT(p_value), p_result);
  }
  }
  runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert unknown value to string.");
  return false;
}

static inline bool object_as_num(vm* p_vm, object* p_obj, value* p_result) {
  switch (object_get_type(p_obj)) {
  case OBJ_STRING: {
    char buff[64];
    string* str = &((object_string*)p_obj)->string;
    uint32_t len = string_get_length(str);
    if (len >= sizeof(buff)) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert string to number: string too long. max is 64.");
      return false;
    }
    memcpy(buff, str->chars, len);
    buff[len] = '\0';

    errno = 0;
    char* endptr;
    const double value = strtod(buff, &endptr);
    if (endptr == buff) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert string to number.");
      return false;
    }
    if (*endptr != '\0') {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert string to number.");
      return false;
    }
    if (errno == ERANGE) {
      runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert string to number: out of range.");
      return false;
    }
    *p_result = NUMBER_AS_VALUE(value);
    return true;
  }
  case OBJ_ARRAY: {
    runtime_error(p_vm, OK_EK_CONVERSION, "array is not convertible to number.");
    return false;
  }
  case OBJ_MAP: {
    runtime_error(p_vm, OK_EK_CONVERSION, "map is not convertible to number.");
    return false;
  }
  case OBJ_CLOSURE: {
    runtime_error(p_vm, OK_EK_CONVERSION, "function is not convertible to number.");
    return false;
  }
  case OBJ_NATIVE_FUNCTION: {
    runtime_error(p_vm, OK_EK_CONVERSION, "native function is not convertible to number.");
    return false;
  }
  case OBJ_NATIVE_VALUE: {
    // scrr idk if we should allow this!?
    runtime_error(p_vm, OK_EK_CONVERSION, "native value is not convertible to number.");
    return false;
  }
  case OBJ_CLASS: {
    runtime_error(p_vm, OK_EK_CONVERSION, "class is not convertible to number.");
    return false;
  }
  case OBJ_INSTANCE: {
    // the operator lane already had its chance in perform_as; an instance
    // with no `operator num` has no numeric form, full stop
    runtime_error(p_vm, OK_EK_CONVERSION, "instance is not convertible to number.");
    return false;
  }
  case OBJ_BOUND_METHOD: {
    runtime_error(p_vm, OK_EK_CONVERSION, "method is not convertible to number.");
    return false;
  }
  }
  runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert unknown object to number.");
  return false;
}

static ok_value as_num(vm* p_vm, ok_value p_value, value* p_result) {
  if (IS_VALUE_NUMBER(p_value)) {
    *p_result = p_value;
    return true;
  }

  const uint64_t tag = VALUE_TAG(p_value);
  switch (tag) {
  case TAG_INT_MASK: {
    *p_result = NUMBER_AS_VALUE(VALUE_AS_INT(p_value));
    return true;
  }
  case TAG_NULL_MASK: {
    runtime_error(p_vm, OK_EK_CONVERSION, "null is not convertible to number.");
    return false;
  }
  case TAG_TRUE_MASK:
  case TAG_FALSE_MASK: {
    runtime_error(p_vm, OK_EK_CONVERSION, "boolean is not convertible to number.");
    return false;
  }
  case TAG_OBJECT_MASK: {
    return object_as_num(p_vm, VALUE_AS_OBJECT(p_value), p_result);
  }
  }
  runtime_error(p_vm, OK_EK_CONVERSION, "failed to convert unknown value to number.");
  return false;
}

static ok_value as_bool(vm* p_vm, value p_value, value* p_result) {
  if (IS_VALUE_STRICTLY_BOOL(p_value)) {
    *p_result = p_value;
  } else if (IS_VALUE_STRICTLY_NULL(p_value)) {
    *p_result = BOOL_AS_VALUE(false);
  } else {
    *p_result = BOOL_AS_VALUE(true);
  }
  return true;
}

// classof (C10): every value is an instance of exactly one class. instances
// carry theirs, class values are all type, every other object maps to its
// lane's builtin singleton. native values (opaque embedder payloads) sit
// directly under object, and bound methods count as function - both are
// recorded in the classes draft as the two judgment calls.
object_class* vm_classof(vm* p_vm, ok_value p_value) {
  if (IS_VALUE_NUMBER(p_value) || IS_VALUE_INT(p_value)) {
    return p_vm->builtin_classes[OK_BUILTIN_NUM];
  }
  switch (VALUE_TAG(p_value)) {
  case TAG_INT_MASK:
    return p_vm->builtin_classes[OK_BUILTIN_NUM];
  case TAG_NULL_MASK:
    return p_vm->builtin_classes[OK_BUILTIN_NIL];
  case TAG_TRUE_MASK:
  case TAG_FALSE_MASK:
    return p_vm->builtin_classes[OK_BUILTIN_BOOL];
  case TAG_OBJECT_MASK:
    break;
  default:
    return p_vm->object_singleton;
  }
  if (IS_VALUE_INSTANCE(p_value)) {
    return VALUE_AS_INSTANCE(p_value)->klass;
  }
  if (IS_VALUE_CLASS(p_value)) {
    return p_vm->class_singleton;
  }
  switch (OBJECT_TYPE(p_value)) {
  case OBJ_STRING:
    return p_vm->builtin_classes[OK_BUILTIN_STR];
  case OBJ_ARRAY:
    return p_vm->builtin_classes[OK_BUILTIN_ARRAY];
  case OBJ_MAP:
    return p_vm->builtin_classes[OK_BUILTIN_MAP];
  case OBJ_CORO:
    return p_vm->builtin_classes[OK_BUILTIN_CORO];
  case OBJ_CLOSURE:
  case OBJ_NATIVE_FUNCTION:
  case OBJ_BOUND_METHOD:
    return p_vm->builtin_classes[OK_BUILTIN_FUNCTION];
  default:
    // native values are opaque payloads: object directly. raw protos and
    // upvalues never surface as values; if one ever does, object is the
    // honest total answer.
    return p_vm->object_singleton;
  }
}

bool vm_is_instance(vm* p_vm, ok_value p_value, const object_class* p_target) {
  return object_class_is_a(vm_classof(p_vm, p_value), p_target);
}

// the mangled conversion name "operator <Target>": the same string the
// parser synthesizes at the declaration site, rebuilt here because the
// target is a runtime value. the raw buffer dies inside (create_object_string
// copies the bytes), so the probe owns its own storage.
static object_string* make_conversion_name(vm* p_vm, const object_class* p_target) {
  enum { PREFIX_LEN = 9 }; // "operator " - keyword, one space, per the canonical spelling
  const uint32_t target_len = string_get_length(&p_target->name->string);
  char* buffer = p_vm->alloc->allocate(p_vm->alloc, PREFIX_LEN + target_len + 1u);
  if (buffer == NULL) {
    return NULL;
  }
  memcpy(buffer, "operator ", PREFIX_LEN);
  memcpy(buffer + PREFIX_LEN, p_target->name->string.chars, target_len);
  buffer[PREFIX_LEN + target_len] = '\0';
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* probe =
      create_object_string(ok_create_string_view(buffer, PREFIX_LEN + target_len, true), &specs);
  p_vm->alloc->release(p_vm->alloc, buffer);
  return probe;
}

// the conversion contract (C13): an `operator <Target>` must deliver a value
// of the target. the builtin targets check their lane, a class target
// accepts the target or any descendant (an operator Vec2 returning a Vec3 : Vec2
// is a legal narrowing source, not a lie).
static bool conversion_result_matches(vm* p_vm, const object_class* p_target, value p_result) {
  if (p_target == p_vm->builtin_classes[OK_BUILTIN_STR]) {
    return IS_VALUE_STRING(p_result);
  }
  if (p_target == p_vm->builtin_classes[OK_BUILTIN_NUM]) {
    return value_is_numeric(p_result);
  }
  if (p_target == p_vm->builtin_classes[OK_BUILTIN_BOOL]) {
    return IS_VALUE_STRICTLY_BOOL(p_result);
  }
  return IS_VALUE_INSTANCE(p_result) && object_class_is_a(VALUE_AS_INSTANCE(p_result)->klass, p_target);
}

static interpret_result
perform_strict_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  (void)p_vm;
  // raw word identity (C11): total over every value, never dispatchable.
  // same bits for immediates (1 === 1.0 is false - different boxes), same
  // allocation for heap values. bit-identical NaNs compare equal here; that
  // is the box's truth and the operator promises nothing else.
  p_reg->array.data[p_frame->registers_base + p_dest] = BOOL_AS_VALUE(p_lhs == p_rhs);
  return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_is(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  if (!IS_VALUE_CLASS(p_rhs)) {
    runtime_error(p_vm, OK_EK_TYPE, "invalid type: 'is' requires a class on the right.");
    return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
  }
  // classof + identity, fused (C12's ruling): the exact-class probe. the
  // chain-walk lives in isinstance; `is` answers "what box", not "what family".
  p_reg->array.data[p_frame->registers_base + p_dest] =
      BOOL_AS_VALUE(vm_classof(p_vm, p_lhs) == VALUE_AS_CLASS(p_rhs));
  return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
}

static interpret_result
perform_as(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  interpret_result res;
  res.status = RUNTIME_ERROR;
  res.top_level_return = NULL_AS_VALUE();

  if (!IS_VALUE_CLASS(p_rhs)) {
    runtime_error(p_vm, OK_EK_CONVERSION, "invalid type: expected a type in 'as' conversion.");
    return res;
  }
  object_class* target = VALUE_AS_CLASS(p_rhs);
  value* dest = &p_reg->array.data[base + p_dest];

  // the user lane (C13): an instance's chain may carry `operator <Target>`.
  // builtin classes are sealed and empty, so no other receiver shape can
  // define one - the probe is skipped entirely for numbers, strings, arrays,
  // maps, functions and null, keeping the builtin lanes below allocation-free.
  if (IS_VALUE_INSTANCE(p_lhs)) {
    object_class* klass = VALUE_AS_INSTANCE(p_lhs)->klass;
    object_string* probe = make_conversion_name(p_vm, target);
    if (probe == NULL) {
      runtime_oom(p_vm, "out of memory: failed to allocate conversion name.");
      return res;
    }
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(probe));
    const method_entry* entry = object_class_find_method(klass, probe);
    if (entry != NULL) {
      if (!entry_visible_from(entry, p_frame)) {
        gc_guard_down(p_vm->alloc->gc);
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "operator '%s' of class '%s' is private.",
                      probe->string.chars,
                      entry->owner->name->string.chars);
        return res;
      }
      interpret_result out =
          dispatch_operator_method(p_vm, p_reg, p_frame, p_dest, entry, probe, p_lhs, NULL, 0);
      gc_guard_down(p_vm->alloc->gc);
      if (out.status != RUNTIME_OK) {
        return out;
      }
      if (!conversion_result_matches(p_vm, target, p_reg->array.data[base + p_dest])) {
        runtime_error(p_vm,
                      OK_EK_INTERNAL,
                      "operator '%s' of class '%s' returned the wrong type.",
                      probe->string.chars,
                      klass->name->string.chars);
        return (interpret_result){.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      }
      return out;
    }
    gc_guard_down(p_vm->alloc->gc);
  }

  // the builtin lanes: same targets as always, now reached through the
  // class values instead of the retired hidden tag globals.
  if (target == p_vm->builtin_classes[OK_BUILTIN_STR]) {
    if (as_str(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    return res;
  }
  if (target == p_vm->builtin_classes[OK_BUILTIN_NUM]) {
    if (as_num(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    return res;
  }
  if (target == p_vm->builtin_classes[OK_BUILTIN_BOOL]) {
    if (as_bool(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    return res;
  }

  runtime_error(p_vm,
                OK_EK_CONVERSION,
                "no conversion from '%s' to '%s'.",
                vm_classof(p_vm, p_lhs)->name->string.chars,
                target->name->string.chars);
  return res;
}

static inline void cleanup_frame(execution_state* p_exe, register_file* p_registers, call_frame* p_frame) {
  close_upvalues(p_exe, p_frame->registers_base);
  call_stack_remove(&p_exe->call_stack, p_exe->call_stack.count - 1, p_exe->call_stack.count - 1);
  // the dying frame's try regions die with it: a stale record would address a
  // reused frame index and hijack a later error into a dead window
  while (p_exe->handlers.count > 0 &&
         p_exe->handlers.data[p_exe->handlers.count - 1].frame >= p_exe->call_stack.count) {
    p_exe->handlers.count--;
  }
  // the register watermark must go back to the calller's frame top
  if (p_exe->call_stack.count > 0) {
    call_frame* caller = &p_exe->call_stack.data[p_exe->call_stack.count - 1];
    p_registers->top = caller->registers_base + caller->closure->function->registers;
  } else {
    p_registers->top = 0;
  }
}

static inline uint16_t
return_n_from_frame(register_file* p_registers, call_frame* p_frame, uint16_t p_base, uint32_t p_count) {
  const uint32_t base = p_frame->registers_base + p_base;
  if (p_count == RETURN_COUNT_DYNAMIC) {
    p_count = p_registers->top - base;
  }
  uint16_t i = 0;
  uint32_t count = p_count;

  if (p_frame->return_count != RETURN_COUNT_DYNAMIC && count > p_frame->return_count) {
    count = p_frame->return_count;
  }
  for (; i < count; ++i) {
    p_registers->array.data[p_frame->return_base + i] = p_registers->array.data[base + i];
  }
  return i;
}

static inline uint16_t
return_1_from_frame(vm* p_vm, execution_state* p_exe, call_frame* p_frame, uint16_t p_index, returned p_returned) {
  const uint32_t base = p_frame->registers_base + p_index;
  register_file* registers = &p_exe->registers;
  if (p_frame->return_count < 1) {
    cleanup_frame(p_exe, registers, p_frame);
    return 0;
  }
  // the frame's upvalues close BEFORE the result lands: return_base is the
  // call window base, which for a method is its receiver's own slot (r0),
  // and an open cell there must snapshot the receiver - not the return
  // value that is about to overwrite it.
  close_upvalues(p_exe, p_frame->registers_base);
  switch (p_returned) {
  case RETURNED_NULL: {
    registers->array.data[p_frame->return_base] = NULL_AS_VALUE();
    break;
  }
  case RETURNED_KTRUE: {
    registers->array.data[p_frame->return_base] = BOOL_AS_VALUE(true);
    break;
  }
  case RETURNED_KFALSE: {
    registers->array.data[p_frame->return_base] = BOOL_AS_VALUE(false);
    break;
  }
  case RETURNED_CONSTANT: {
    registers->array.data[p_frame->return_base] = p_frame->closure->function->chunk.constants.data[p_index];
    break;
  }
  case RETURNED_GLOBAL: {
    registers->array.data[p_frame->return_base] = p_vm->globals_store->global_values.data[p_index];
    break;
  }
  case RETURNED_UPVALUE: {
    registers->array.data[p_frame->return_base] = *upvalue_cell(p_vm, p_exe, p_frame->closure->upvalues.data[p_index]);
    break;
  }
  case RETURNED_REGISTER: {
    registers->array.data[p_frame->return_base] = registers->array.data[base];
    break;
  }
  }
  cleanup_frame(p_exe, registers, p_frame);
  return 1;
}

static uint16_t
return_from_frame(execution_state* p_exe, register_file* p_registers, call_frame* p_frame, uint16_t p_a, uint16_t p_b) {
  const uint32_t count = p_b == 0 ? RETURN_COUNT_DYNAMIC : p_b;
  // close before the results land in the window (see return_1_from_frame).
  close_upvalues(p_exe, p_frame->registers_base);
  const uint16_t result = return_n_from_frame(p_registers, p_frame, p_a, count);
  cleanup_frame(p_exe, p_registers, p_frame);
  return result;
}

static inline interpret_result top_level_return(register_file* p_registers, call_frame* p_frame, uint16_t p_returned) {
#if defined(OK_TRACE_EXECUTION)
  printf("[ ");
  for (value* reg = p_registers->array.data; reg < p_registers->array.data + p_registers->top; ++reg) {
    printf("[");
    value_debug_print(*reg);
    printf("]");
  }
  printf("]\n");
#endif // defined(OK_TRACE_EXECUTION)
  interpret_result result;
  // compiler doesn't allow more than 1 return from top level
  result.top_level_return = p_returned > 0 ? p_registers->array.data[p_frame->return_base] : NULL_AS_VALUE();
  result.status = RUNTIME_OK;
  return result;
}

// array index resolution shared by OP_INDEX_GET / OP_INDEX_SET
static bool resolve_array_index(vm* p_vm, value p_index, uint32_t p_count, uint32_t* p_out) {
  if (IS_VALUE_NUMBER(p_index)) {
    const double d = VALUE_AS_NUMBER(p_index);
    if (!(d >= MIN_SAFE_INTEGER && d <= MAX_SAFE_INTEGER) || d != (double)(int64_t)d) {
      runtime_error(p_vm, OK_EK_INDEX, "array index must be a whole number, got %g.", d);
      return false;
    }
    if (d < 0 || d >= (double)p_count) {
      runtime_error(p_vm, OK_EK_INDEX, "index out of bounds: %g (len %u).", d, p_count);
      return false;
    }
    *p_out = (uint32_t)d;
    return true;
  }
  if (IS_VALUE_INT(p_index)) {
    const int64_t i = VALUE_AS_INT(p_index);
    if (i < 0 || (uint64_t)i >= (uint64_t)p_count) {
      runtime_error(p_vm, OK_EK_INDEX, "index out of bounds: %lld (len %u).", (long long)i, p_count);
      return false;
    }
    *p_out = (uint32_t)i;
    return true;
  }
  runtime_error(p_vm, OK_EK_INDEX, "array index must be a number.");
  return false;
}

static bool for_operand_as_number(vm* p_vm, value p_value, const char* p_which, double* p_out) {
  if (IS_VALUE_NUMBER(p_value)) {
    *p_out = VALUE_AS_NUMBER(p_value);
    return true;
  }
  if (IS_VALUE_INT(p_value)) {
    *p_out = (double)VALUE_AS_INT(p_value);
    return true;
  }
  runtime_error(p_vm, OK_EK_TYPE, "%s must be a number.", p_which);
  return false;
}

static object_array* resolve_array_base(vm* p_vm, value p_base) {
  if (!value_is_object_type(p_base, OBJ_ARRAY)) {
    runtime_error(p_vm, OK_EK_TYPE, "invalid operand for '[]': expected an array or a map.");
    return NULL;
  }
  return VALUE_AS_ARRAY(p_base);
}

// map index paths (shared by OP_INDEX_GET / OP_INDEX_SET)

// the key gate every map write/read runs: null is the empty-slot encoding, and
// only strings, numbers (int-tagged included) and booleans hash.
static bool map_key_check(vm* p_vm, value p_key) {
  if (IS_VALUE_STRICTLY_NULL(p_key)) {
    runtime_error(p_vm, OK_EK_KEY, "map key cannot be null.");
    return false;
  }
  if (!map_key_is_legal(p_key)) {
    runtime_error(p_vm, OK_EK_KEY, "invalid map key: only strings, numbers and booleans can be keys.");
    return false;
  }
  return true;
}

// the strict read: a missing key is a hard error, get() is the lax path.
// read-only and allocation-free (probes never allocate).
static bool map_read(vm* p_vm, value p_map_value, value p_key, value* p_out) {
  if (!map_key_check(p_vm, p_key)) {
    return false;
  }
  value* slot = value_map_get(&VALUE_AS_MAP(p_map_value)->entries, p_key);
  if (slot == NULL) {
    runtime_error(p_vm, OK_EK_KEY, "map key not found.");
    return false;
  }
  *p_out = *slot;
  return true;
}

// insert-or-overwrite. the map lives in a register (rooted). key/value operands
// are registers or constants (rooted via the frame / the function's constant
// array), so a grow-triggered collection mid-set is safe: the tracer walks the
// still-attached old arrays, and the new arrays are plain invisible memory.
static bool map_write(vm* p_vm, value p_map_value, value p_key, value p_value) {
  if (!map_key_check(p_vm, p_key)) {
    return false;
  }
  if (!object_map_set(VALUE_AS_MAP(p_map_value), p_key, p_value)) {
    runtime_oom(p_vm, "out of memory: failed to grow map.");
    return false;
  }
  return true;
}

// a script `throw v` hands the value back whole. a runtime-raised error materializes into
// a {:kind: "...", :what: "..."} map. branchable, and the exact two fields the
// 0.5.0 error object will wrap, so `e["what"]` code carries over. built only
// when a consumer asks (a catch binder, ok_last_error): an uncaught error never pays for the map.
static value materialize_error_value(vm* p_vm) {
  execution_state* exe = p_vm->current_execution_state;
  if (exe->error_is_script) {
    return exe->error_value;
  }
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_map* map = create_object_map(&s);
  if (map == NULL) {
    return NULL_AS_VALUE(); // oom while building the error value: degrade to null, never recurse
  }
  gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(map));
  object_string* key_kind = create_object_string(ok_create_string_view("kind", 4, true), &s);
  if (key_kind != NULL) {
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(key_kind));
  }
  object_string* value_kind =
      key_kind != NULL
          ? create_object_string(
                ok_create_string_view(ok_error_kind_name(exe->error_kind), OK_STRING_VIEW_CALCULATE_LENGTH, true), &s)
          : NULL;
  if (value_kind != NULL) {
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(value_kind));
  }
  object_string* key_what = create_object_string(ok_create_string_view("what", 4, true), &s);
  if (key_what != NULL) {
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(key_what));
  }
  object_string* value_what =
      key_what != NULL
          ? create_object_string(ok_create_string_view(exe->error_what, OK_STRING_VIEW_CALCULATE_LENGTH, true), &s)
          : NULL;
  if (value_what != NULL) {
    gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(value_what));
  }
  if (key_kind != NULL && value_kind != NULL) {
    object_map_set(map, OBJECT_AS_VALUE(key_kind), OBJECT_AS_VALUE(value_kind));
  }
  if (key_what != NULL && value_what != NULL) {
    object_map_set(map, OBJECT_AS_VALUE(key_what), OBJECT_AS_VALUE(value_what));
  }
  gc_guard_down(p_vm->alloc->gc);
  gc_guard_down(p_vm->alloc->gc);
  gc_guard_down(p_vm->alloc->gc);
  gc_guard_down(p_vm->alloc->gc);
  gc_guard_down(p_vm->alloc->gc);
  return OBJECT_AS_VALUE(map);
}

// the defer machinery.
// stage (put a thunk closure in a fresh frame at the register file top, above every compile-time register, so
// nothing live is ever clobbered), scan (find tagged slots in a register
// range and run them, lifo, descending), and the unwind integration below.
// the two entry points that run script from C: OP_DEFER_RUN's arm (the
// dispatch loop itself resumes into the thunk, OP_CALL-style) and the scan
// (a nested run_until that returns when the thunk's frame pops).

// stage a defer thunk at the register file top and call it, discard lane.
// arity cannot fail (the compiler built the thunk). register-file growth
// can oom (the hard lane). the slot was nil'd by the caller, so the closure
// is otherwise unreferenced between that nil and top rising.
// guarded here, the vm_interpret staging discipline.
static bool stage_defer_thunk(vm* p_vm, execution_state* p_exe, register_file* p_reg, object_closure* p_thunk) {
  const uint32_t staged = p_reg->top;
  if (staged + 1u + p_thunk->function->registers > UINT16_MAX + 1u) {
    runtime_error(p_vm, OK_EK_INTERNAL, "defer: register file exhausted.");
    return false;
  }
  // the closure is rooted by the guard until call() appends its frame (the
  // frame's own closure marking takes over), and never touches the register
  // file as a plain value: the staging slot gets a NULL. this matters.
  // top is the deepest live frame's window, which can sit below an outer frame's
  // compile-time window (a callee reuses the caller's temp region). a thunk
  // pointer parked at top would be swept while uncovered and then resurrected
  // as a dangling mark when the outer frame's window re-covers the slot.
  gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(p_thunk));
  if (staged + 1u > p_reg->array.capacity) {
    // same contract as call()'s growth: fresh slots must read as null
    const uint32_t target = staged + 1u + ((staged + 1u) >> 1u);
    const uint32_t new_capacity = target > UINT16_MAX + 1u ? UINT16_MAX + 1u : target;
    const uint32_t initialized_uptill = p_reg->array.capacity;
    if (!register_array_grow(&p_reg->array, new_capacity)) {
      gc_guard_down(p_vm->alloc->gc);
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return false;
    }
    for (uint32_t i = initialized_uptill; i < new_capacity; ++i) {
      p_reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  p_reg->array.data[staged] = NULL_AS_VALUE(); // the thunk's r0: written by its body or dead
  p_reg->top = staged + 1;                     // call() raises this to the thunk's full window
  const bool called = call(p_vm, p_exe, p_reg, p_thunk, staged, 0, 0);
  gc_guard_down(p_vm->alloc->gc);
  if (!called) {
    return false;
  }
  // flag the pushed frame (call() clears it): both entry points. OP_DEFER_RUN's
  // arm and the unwind scan. the flag is the chronology marker the unwinder
  // reads to tell "the in-flight error IS a defer failure" from "an original
  // error a defer may replace"
  p_exe->call_stack.data[p_exe->call_stack.count - 1].defer_thunk = true;
  return true;
}

// the first failure of a defer chain is the error that propagates when the
// chain is done: a failing defer's error replaces the in-flight one, and
// LATER failures in the same chain are dropped in the first's favor (the
// first error is the reportable one - zig/go doctrine, continuation plus a
// deterministic winner). the value rides a gc guard between the failure and
// the chain's end: the lane itself carries the later thunk's error in that
// window, so the first failure's value would otherwise be unrooted.
typedef struct {
  char what[512];
  ok_error_kind kind;
  value value;
  bool is_script;
  bool reported;
  bool has_error;
} defer_chain_failure;

static void defer_chain_failure_capture(const execution_state* p_exe, defer_chain_failure* p_out) {
  memcpy(p_out->what, p_exe->error_what, sizeof(p_out->what));
  p_out->kind = p_exe->error_kind;
  p_out->value = p_exe->error_value;
  p_out->is_script = p_exe->error_is_script;
  p_out->reported = p_exe->error_reported;
  p_out->has_error = p_exe->has_error;
}

static void defer_chain_failure_restore(execution_state* p_exe, const defer_chain_failure* p_failure) {
  memcpy(p_exe->error_what, p_failure->what, sizeof(p_exe->error_what));
  p_exe->error_kind = p_failure->kind;
  p_exe->error_value = p_failure->value;
  p_exe->error_is_script = p_failure->is_script;
  p_exe->error_reported = p_failure->reported;
  p_exe->has_error = p_failure->has_error;
  p_exe->error_pending = true; // a failure in flight, by construction
}

// run every pending defer in [p_from, p_uptill), descending, lifo, because
// later declarations hold higher registers (hidden locals allocate in order,
// the slot last). a defer that fails (or anything it calls failing uncaught)
// DOES NOT stop the chain: the remaining defers still run (zig/go doctrine -
// cleanup must not be lost to a cleanup error), the failing thunk's own
// callees are torn down with THEIR defers run, and the FIRST failure's error
// is what the caller sees when the scan returns false. oom is the one
// stop-everything lane (error_hard: no script may run after it).
// p_in_flight_is_defer: the in-flight error was raised by a defer (the
// chronology marker) - it is the chain's first by definition, later failures
// drop against it instead of replacing it.
static bool run_pending_defers_ex(vm* p_vm, uint32_t p_from, uint32_t p_uptill, bool p_in_flight_is_defer) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  if (exe->error_hard) {
    return true; // oom: no script may run, the hard lane owns the teardown
  }
  // root the whole scanned range for the scan's duration: top is the
  // throw-point's watermark, which can sit below the scanned frame's full
  // compile-time window (the walk restores it only at the park). a tagged
  // slot above top would be sweepable by the very collections the scan's
  // own thunks trigger. every slot in a frame's window is null-or-valid
  // (call()'s null-fill discipline), so raising top only ever roots more.
  if (reg->top < p_uptill) {
    reg->top = p_uptill;
  }
  defer_chain_failure first;
  bool first_captured = false; // the accounting: the chain's first is known
  bool chain_failed = false;   // the control: a NEW failure happened in THIS call
  bool first_guarded = false;
  if (p_in_flight_is_defer && exe->error_pending) {
    // the in-flight is the chain's first by chronology - captured for the
    // drop accounting only: it propagates through the walk that is already
    // delivering it, this scan must not report it as its own failure (the
    // dying-frame loop would restart without popping, a livelock)
    first_captured = true;
    defer_chain_failure_capture(exe, &first);
    first_guarded = gc_guard_up(p_vm->alloc->gc, first.value);
  }
  for (uint32_t at = p_uptill; at-- > p_from;) {
    if (!IS_VALUE_STRICTLY_DEFER(reg->array.data[at])) {
      continue;
    }
    object_closure* thunk = VALUE_AS_DEFER(reg->array.data[at]);
    reg->array.data[at] = NULL_AS_VALUE(); // consumed: exactly once per scope instance
    const uint32_t depth = exe->call_stack.count;
    const uint32_t saved_top = reg->top;
    bool thunk_failed = false;
    if (stage_defer_thunk(p_vm, exe, reg, thunk)) {
      // the thunk must run with a clean propagation lane: the in-flight error
      // is parked here in C, and call_native's post-flight error_pending check
      // cannot tell "raised inside the native" from "pending since before the
      // native", without this, every deferred native call fails spuriously.
      // has_error and error_value stay set: the in-flight value must keep
      // rooting across the thunk's allocations either way.
      const bool saved_pending = exe->error_pending;
      exe->error_pending = false;
      // the yield barrier: a defer thunk is a C-driven region (the chain scan
      // holds the rest of the cascade and the first-failure bookkeeping in
      // this C frame) - a yield from inside it could never resume honestly,
      // so OP_YIELD refuses it
      const uint8_t saved_barrier = exe->yield_barrier;
      if (exe->yield_barrier == 0) {
        exe->yield_barrier_kind = YIELD_BARRIER_DEFER;
      }
      exe->yield_barrier++;
      const interpret_result result = run_until(p_vm, depth);
      exe->yield_barrier = saved_barrier;
      if (result.status == RUNTIME_OK) {
        // the thunk survived (its own catches, if any, consumed their errors):
        // the in-flight error continues outward exactly as it was
        exe->error_pending = saved_pending;
      } else {
        thunk_failed = true; // nothing inside the thunk caught it
      }
    } else {
      thunk_failed = true; // the staging itself failed (register exhaustion, oom)
    }
    reg->top = saved_top;
    if (!thunk_failed) {
      continue;
    }
    if (exe->error_hard) {
      break; // oom inside the thunk: the hard lane owns the rest, stop here
    }
    // tear the failing thunk's leftover frames down exactly like the
    // unwinder's dying-frame loop, except their defers RUN (the doctrine:
    // cleanup on the way out, whatever failed - a failing defer must not
    // orphan the cleanup of the frames it was unwinding through)
    while (exe->call_stack.count > depth) {
      call_frame* dying = &exe->call_stack.data[exe->call_stack.count - 1];
      (void)run_pending_defers_ex(p_vm, dying->registers_base, dying->registers_base + dying->closure->function->registers, true);
      // the in-flight at this point is the failing thunk's own error: the
      // nested chain treats it as the first (nested failures drop against
      // it, and the outer first - captured before or below - wins over
      // whatever it left)
      close_upvalues(exe, dying->registers_base);
      exe->call_stack.count--;
      while (exe->handlers.count > 0 && exe->handlers.data[exe->handlers.count - 1].frame >= exe->call_stack.count) {
        exe->handlers.count--;
      }
    }
    if (!first_captured) {
      first_captured = true;
      defer_chain_failure_capture(exe, &first);
      // the guard may be full at pathological chain recursion depth; the
      // chain still continues (stopping cleanup is the bug being fixed), the
      // unguarded first value is the accepted cost of that corner
      first_guarded = gc_guard_up(p_vm->alloc->gc, first.value);
    } else {
      // a later failure: its error is dropped, the first goes back in the
      // lane so the next thunk's park/restore and the final propagation
      // keep seeing the first
      defer_chain_failure_restore(exe, &first);
    }
    chain_failed = true; // a new failure in this call, whatever the accounting
  }
  if (chain_failed) {
    if (first_guarded) {
      gc_guard_down(p_vm->alloc->gc);
    }
    return false; // the lane holds the first failure
  }
  return true;
}

static bool run_pending_defers(vm* p_vm, uint32_t p_from, uint32_t p_uptill) {
  return run_pending_defers_ex(p_vm, p_from, p_uptill, false);
}

// the unwind. walk the handler
// stack top-down, the first live record above this run_until's depth wins:
// pop the frames above it (running their defers first, cleanup on the way
// out is defer's whole job), consume the record, run the handler frame's
// defers at/above the record's floor (before the catch, close upvalues above the floor, restore the
// watermark to the handler frame's full window, materialize + park the
// error value at the record's register, jump. records below the depth
// belong to outer C levels. theirs to take, not ours.
//
// a defer failing anywhere in here returns false from its scan AFTER
// running the whole chain (continuation doctrine): the walk restarts for
// the first failure's error with the bookkeeping intact (consumed slots
// re-scan as no-ops, the restart is idempotent).
// the restarted walk can only find records the failing defer's own scope did not consume.
// the try whose catch was about to run never sees a defer's failure
// (its record was consumed before the scan), but outer trys in the same frame can.
// the resume state, returned BY VALUE: taking &frame/&ip in the FAIL_PATH
// macro would make the dispatch loop's hottest locals address-taken (forced
// to memory, measured +15% on numeric_for_sum). the struct keeps them register-promotable.
typedef struct {
  call_frame* frame;
  uint32_t* ip;
  bool caught;
} unwind_resume;

static unwind_resume __attribute__((noinline, cold)) attempt_unwind(vm* p_vm, uint32_t p_depth) {
  execution_state* exe = p_vm->current_execution_state;
  unwind_resume resume = {NULL, NULL, false};
  if (exe->error_hard) {
    return resume; // oom: not a value, not catchable. the hard lane already reported
  }
  while (exe->handlers.count > 0) {
    // copied by value: the defer scans below run script, which can grow (and
    // thus move) the handler array. a held pointer would dangle.
    const handler_record taken = exe->handlers.data[exe->handlers.count - 1];
    if (taken.frame < p_depth) {
      break; // an outer C level's try: leave it for the outer walk
    }
    // the chronology marker: did the in-flight error pass through a defer
    // invocation on its way here (raised in a thunk, or in anything a thunk
    // called, uncaught on the way)? then it IS a defer failure - the chain's
    // first by definition, and the scans below drop later failures against
    // it instead of letting them replace it. an error from a SIBLING ordinary
    // call never sees a live thunk frame and stays replaceable.
    bool in_flight_is_defer = false;
    for (uint32_t f = exe->call_stack.count; f-- > taken.frame + 1;) {
      if (exe->call_stack.data[f].defer_thunk) {
        in_flight_is_defer = true;
        break;
      }
    }
    // pop the frames above the handler's frame, defers first (their upvalues
    // close after. one step of live code before the usual cleanup)
    bool defer_failed = false;
    while (exe->call_stack.count > taken.frame + 1) {
      call_frame* dying = &exe->call_stack.data[exe->call_stack.count - 1];
      if (!run_pending_defers_ex(
              p_vm, dying->registers_base, dying->registers_base + dying->closure->function->registers, in_flight_is_defer)) {
        defer_failed = true;
        break;
      }
      close_upvalues(exe, dying->registers_base);
      exe->call_stack.count--;
      while (exe->handlers.count > 0 && exe->handlers.data[exe->handlers.count - 1].frame >= exe->call_stack.count) {
        exe->handlers.count--;
      }
    }
    if (defer_failed) {
      continue; // restart the walk for the defer's error (its chain already ran)
    }
    // consume the record and restore the handler frame
    exe->handlers.count--;
    call_frame* fr = &exe->call_stack.data[taken.frame];
    // the handler frame's own region: defers at/above the floor run before
    // the catch. a failure here restarts the walk. the consumed record (the
    // catch this frame was about to run) never sees it, outer records may.
    if (!run_pending_defers_ex(
            p_vm, fr->registers_base + taken.floor, fr->registers_base + fr->closure->function->registers, in_flight_is_defer)) {
      continue;
    }
    close_upvalues(exe, fr->registers_base + taken.floor);
    exe->registers.top = fr->registers_base + fr->closure->function->registers;
    const value error = materialize_error_value(p_vm);
    exe->registers.array.data[fr->registers_base + taken.park] = error;
    fr->ip = taken.catch_ip;
    resume.frame = fr;
    resume.ip = fr->ip;
    resume.caught = true;
    // consumed: the catch owns it now (and the run recovered: last_error
    // reads nothing once a catch took it)
    exe->error_pending = false;
    exe->error_reported = false;
    exe->has_error = false;
    return resume;
  }
  return resume;
}

// ============================== classes =================================

// the segment of p_class's frozen layout that declares p_slot. chains are
// short and this only runs on the guarded (obj.field) paths.
static const object_class* field_defining_segment(const object_class* p_class, uint16_t p_slot) {
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    const uint16_t base = c->parent != NULL ? c->parent->nfields : 0;
    if (p_slot >= base && p_slot < base + c->nown) {
      return c;
    }
  }
  return NULL;
}

// private methods are callable from the defining class and its subclasses
// only; the caller's lexical class rides the running frame's closure.
static bool entry_visible_from(const method_entry* p_entry, const call_frame* p_frame) {
  if (p_entry->pub) {
    return true;
  }
  if (p_frame == NULL) {
    // a call with no bytecode caller (ok_call from C): the top of the world
    // is outside every hierarchy ring.
    return false;
  }
  const object_class* caller = p_frame->closure->owner;
  if (caller == NULL) {
    return false;
  }
  return object_class_is_a(caller, p_entry->owner);
}

// stage an operator-method call at the register file top and run it to
// completion (the construction/defer lane): the receiver and arguments are
// copied ABOVE every live register (a binop's (dest, lhs, rhs) shape carries
// no call-window guarantee, so the frame's own registers are never touched),
// the method runs in a nested run_until, and the single result lands in
// p_dest. the cold path by construction - the builtin lanes returned first.
//
// the one hazard of running script from inside a perform: the nested call may
// GROW (and realloc) the register file and the call stack, which dangles the
// dispatch loop's cached exe/reg/frame locals. the CALLER (the dispatch case)
// refreshes them after a successful perform - the same dance the construct
// path already does.
static interpret_result dispatch_operator_method(vm* p_vm,
                                                 register_file* p_reg,
                                                 call_frame* p_frame,
                                                 uint16_t p_dest,
                                                 const method_entry* p_entry,
                                                 const object_string* p_name,
                                                 value p_receiver,
                                                 const value* p_args,
                                                 uint32_t p_argc) {
  execution_state* exe = p_vm->current_execution_state;
  // arity: the entry counts parameters, the receiver rides separately. the
  // pre-check is the ERROR MESSAGE lane only - call() re-asserts the real
  // contract (defaults ladder included), so it stays deliberately simple:
  // exact match, the defaulted window, or varargs deferred to call() itself.
  // native entries check their own arity inside call_native_method (theirs
  // counts the receiver).
  if (!p_entry->is_native && !p_entry->closure->function->has_varargs &&
      p_argc > p_entry->closure->function->arity) {
    runtime_error(p_vm,
                  OK_EK_ARITY,
                  "operator '%s' of class '%s' expects at most %u argument%s.",
                  p_name->string.chars,
                  p_entry->owner->name->string.chars,
                  (unsigned)p_entry->closure->function->arity,
                  p_entry->closure->function->arity == 1 ? "" : "s");
    return error_interpret_result();
  }
  const uint32_t depth = exe->call_stack.count;
  const uint32_t saved_top = p_reg->top;
  const uint32_t need = 1u + p_argc + (p_entry->is_native ? 0u : p_entry->closure->function->registers);
  if (saved_top + need > UINT16_MAX + 1u) {
    runtime_error(p_vm, OK_EK_INTERNAL, "operator dispatch: register file exhausted.");
    return error_interpret_result();
  }
  if (saved_top + need > p_reg->array.capacity) {
    // same contract as call()'s growth: fresh slots must read as null
    const uint32_t target = saved_top + need + ((saved_top + need) >> 1u);
    const uint32_t new_capacity = target > UINT16_MAX + 1u ? UINT16_MAX + 1u : target;
    const uint32_t initialized_uptill = p_reg->array.capacity;
    if (!register_array_grow(&p_reg->array, new_capacity)) {
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return error_interpret_result();
    }
    for (uint32_t i = initialized_uptill; i < new_capacity; ++i) {
      p_reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  const uint32_t staged = p_reg->top;
  p_reg->array.data[staged] = p_receiver;
  for (uint32_t i = 0; i < p_argc; ++i) {
    p_reg->array.data[staged + 1u + i] = p_args[i];
  }
  p_reg->top = staged + need;
  if (p_entry->is_native) {
    // the native flight reads the staged window (receiver at [staged], the
    // arguments above), lands its result back at [staged]. no frame, no
    // nested run_until - but the register file may still have grown above.
    const bool called = call_native_method(p_vm, exe, p_reg, p_entry, staged, (uint16_t)p_argc);
    p_reg->top = saved_top;
    if (!called) {
      return error_interpret_result();
    }
    p_reg->array.data[p_frame->registers_base + p_dest] = p_reg->array.data[staged];
    return (interpret_result){.status = RUNTIME_OK, .top_level_return = NULL_AS_VALUE()};
  }
  gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(p_entry->closure));
  const bool called = call(p_vm, exe, p_reg, p_entry->closure, staged, (uint16_t)p_argc, 1u);
  gc_guard_down(p_vm->alloc->gc);
  if (!called) {
    p_reg->top = saved_top;
    return error_interpret_result();
  }
  // the yield barrier: an operator method is a C-driven region (the result
  // copy back into the caller's register happens in THIS C frame after the
  // nested run_until returns) - a yield from under it could never resume
  // honestly, so OP_YIELD refuses it
  const uint8_t saved_barrier = exe->yield_barrier;
  if (exe->yield_barrier == 0) {
    exe->yield_barrier_kind = YIELD_BARRIER_OPERATOR;
  }
  exe->yield_barrier++;
  const interpret_result result = run_until(p_vm, depth);
  exe->yield_barrier = saved_barrier;
  p_reg->top = saved_top;
  if (result.status != RUNTIME_OK) {
    return result;
  }
  p_reg->array.data[p_frame->registers_base + p_dest] = p_reg->array.data[staged];
  return result;
}

// rewrite one chunk's field NAME ops in place to baked slots (or globals).
// width-stable by construction: every NAME form rewrites to an op of the same
// word count with the same operand roles (the b'/C payload swaps name-const
// for slot / global id; the global store rebuilds its word because the
// operands trade places).
// rewrite one 2w instruction in place: low word first, high second
// (chunk_write_word2's split, JOIN_WORD's contract).
static void rewrite_word2(chunk* p_chunk, uint32_t p_at, word2 p_words) {
  p_chunk->code.data[p_at] = (word)(p_words & UINT64_C(0xffffffff));
  p_chunk->code.data[p_at + 1] = (word)(p_words >> 32u);
}

static bool patch_chunk_fields(vm* p_vm, object_class* p_class, chunk* p_chunk, bool p_construction) {
  for (uint32_t i = 0; i < p_chunk->code.count;) {
    const word instruction = p_chunk->code.data[i];
    const opcode op = op_of(instruction);
    const uint32_t words = opcode_word_count(op);
    switch (op) {
    case OP_GET_FIELD_NAME:
    case OP_GET_FIELD_NAME_LONG: {
      const bool is_long = op == OP_GET_FIELD_NAME_LONG;
      const word2 joined = is_long ? join_words(p_chunk->code.data[i + 1], instruction) : 0;
      const uint16_t dst = is_long ? A_of(joined) : aprime_of(instruction);
      const uint16_t name_at = is_long ? B_of(joined) : bprime_of(instruction);
      const object_string* name = VALUE_AS_STRING(p_chunk->constants.data[name_at]);
      const field_entry* field = object_class_find_field(p_class, name);
      if (field != NULL) {
        if (is_long) {
          rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_GET_FIELD_LONG, false, dst, field->slot, 0, false));
        } else {
          p_chunk->code.data[i] = encode_oppabprime(OP_GET_FIELD, false, dst, field->slot);
        }
      } else {
        // the method lane (R1): a bare name that is a method of this chain
        // bakes to a bind of `this` - the same operand roles, width-stable.
        // fields win over methods, methods over globals (the GET_MEMBER
        // order, mirrored for bare names).
        const method_entry* method = object_class_find_method(p_class, name);
        if (method != NULL) {
          if (is_long) {
            rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_GET_METHOD_LONG, false, dst, name_at, 0, false));
          } else {
            p_chunk->code.data[i] = encode_oppabprime(OP_GET_METHOD, false, dst, name_at);
          }
          break;
        }
        const global glob = globals_store_get(p_vm->globals_store, ok_create_string_view(
                                                                       name->string.chars,
                                                                       string_get_length(&name->string),
                                                                       false));
        if (!IS_GLOBAL_VALID(glob)) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "undefined variable: %s (class %s has no field of this name).",
                        name->string.chars,
                        p_class->name->string.chars);
          return false;
        }
        if (is_long) {
          rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_LOAD_GLOBAL_LONG, false, dst, glob.index, 0, false));
        } else {
          p_chunk->code.data[i] = encode_oppabprime(OP_LOAD_GLOBAL, false, dst, glob.index);
        }
      }
      break;
    }
    case OP_STORE_FIELD_NAME:
    case OP_STORE_FIELD_NAME_LONG: {
      const bool is_long = op == OP_STORE_FIELD_NAME_LONG;
      const bool value_is_constant = p_of(instruction);
      const word2 joined = is_long ? join_words(p_chunk->code.data[i + 1], instruction) : 0;
      const uint16_t value_at = is_long ? A_of(joined) : aprime_of(instruction);
      const uint16_t name_at = is_long ? B_of(joined) : bprime_of(instruction);
      const object_string* name = VALUE_AS_STRING(p_chunk->constants.data[name_at]);
      const field_entry* field = object_class_find_field(p_class, name);
      if (field != NULL) {
        // mutability is a post-construction contract: the initializer thunks
        // and the init method itself assign non-mut fields (that is the
        // point of construction). everything else bakes the trap.
        if (field->mut || p_construction) {
          if (is_long) {
            rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_STORE_FIELD_LONG, value_is_constant, value_at, field->slot, 0, false));
          } else {
            p_chunk->code.data[i] =
                encode_oppabprime(OP_STORE_FIELD, value_is_constant, value_at, field->slot);
          }
        } else {
          p_chunk->code.data[i] = encode_oppabprime(OP_FIELD_IMMUTABLE, false, 0, name_at);
        }
      } else {
        // the method lane: storing to a bare method name is the trap (methods
        // are not assignable). fields win, then this, then the global store.
        if (object_class_find_method(p_class, name) != NULL) {
          if (is_long) {
            rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_METHOD_NOT_ASSIGNABLE_LONG, false, name_at, 0, 0, false));
          } else {
            p_chunk->code.data[i] = encode_oppabprime(OP_METHOD_NOT_ASSIGNABLE, false, 0, name_at);
          }
          break;
        }
        const global glob = globals_store_get(p_vm->globals_store, ok_create_string_view(
                                                                       name->string.chars,
                                                                       string_get_length(&name->string),
                                                                       false));
        if (!IS_GLOBAL_VALID(glob)) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "undefined variable: %s (class %s has no field of this name).",
                        name->string.chars,
                          p_class->name->string.chars);
          return false;
        }
        if (is_long) {
          // STORE_GLOBAL_LONG (p, A=gid, B=value): the operands trade places
          rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_STORE_GLOBAL_LONG, value_is_constant, glob.index, value_at, 0, false));
        } else {
          // STORE_GLOBAL (p, a'=gid, b'=value): the operands trade places
          p_chunk->code.data[i] =
              encode_oppabprime(OP_STORE_GLOBAL, value_is_constant, glob.index, value_at);
        }
      }
      break;
    }
    case OP_STORE_FIELD_G_NAME: {
      const word2 joined = join_words(p_chunk->code.data[i + 1], instruction);
      const uint16_t obj_at = A_of(joined);
      const uint16_t value_at = B_of(joined);
      const uint16_t name_at = C_of(joined);
      const object_string* name = VALUE_AS_STRING(p_chunk->constants.data[name_at]);
      const field_entry* field = object_class_find_field(p_class, name);
      if (field == NULL) {
        // not an instance field of this class: leave the name form alone.
        // the receiver may be a class value (a static store) - that is a
        // runtime truth, and OP_STORE_FIELD_G_NAME resolves it itself.
        break;
      }
      if (!field->mut) {
        rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_FIELD_IMMUTABLE_G, false, 0, 0, name_at, false));
        break;
      }
      rewrite_word2(p_chunk, i, encode_OPPABCQ(OP_STORE_FIELD_G, false, obj_at, value_at, field->slot, false));
      break;
    }
    default:
      break;
    }
    i += words;
  }
  return true;
}

static bool freeze_walk_chunk(vm* p_vm, object_class* p_class, chunk* p_chunk, bool p_construction);

// fill a fresh proto from its source: code, line table, identifiers, upvalue
// descriptors, mic slots (empty), and the constants. constant indices are
// load-bearing (the code addresses them), so order and count are preserved
// exactly. values are shared; nested non-member protos are copied so the new
// definition owns its own bake. protos of a class declared inside this body
// are shared - their own definition owns them.
//
// p_dest must already be rooted (guard stack or a reachable parent's
// constants) before this runs: every allocation below can collect. children
// are created, appended, THEN filled - a node is safe the moment it lands in
// a safe parent's constants, so the guard stack never holds more than the
// tree root plus the child being appended.
static bool copy_proto_fill(vm* p_vm, object_function* p_dest, object_function* p_src, object_specs* p_specs) {
  chunk* dest = &p_dest->chunk;
  chunk* src = &p_src->chunk;
  if (!code_append_n(&dest->code, src->code.data, src->code.count) ||
      !source_info_append_n(&dest->source_info, src->source_info.data, src->source_info.count)) {
    return false;
  }
  for (uint32_t i = 0; i < src->identifiers.count; ++i) {
    const string* id = &src->identifiers.data[i];
    string copy;
    if (!string_init(&copy, id->chars, string_get_length(id), string_is_dynamic(id), p_specs->alloc) ||
        !identifiers_append(&dest->identifiers, copy)) {
      return false;
    }
  }
  if (!upvalue_descriptor_array_append_n(&p_dest->upvalues, p_src->upvalues.data, p_src->upvalues.count)) {
    return false;
  }
  // fresh mic slots for every site: each generation's caches start cold
  // (R18). every way empty, hand rewound.
  const mic_cache empty_cache = {{{NULL_AS_VALUE(), NULL, 0, 0, 0}}, 0};
  for (uint32_t i = 0; i < src->mic.count; ++i) {
    if (!mic_caches_append(&dest->mic, empty_cache)) {
      return false;
    }
  }
  p_dest->ndefaults = p_src->ndefaults;
  p_dest->registers = p_src->registers;
  p_dest->results = p_src->results;
  p_dest->has_varargs = p_src->has_varargs;
  p_dest->is_class_member = p_src->is_class_member;
  p_dest->calls_super_init = p_src->calls_super_init;
  dest->source_id = src->source_id;
  for (uint32_t i = 0; i < src->constants.count; ++i) {
    const value v = src->constants.data[i];
    if (IS_VALUE_FUNCTION(v) && !VALUE_AS_FUNCTION(v)->is_class_member) {
      object_function* nested_src = VALUE_AS_FUNCTION(v);
      object_function* nested = create_object_function(nested_src->name, nested_src->arity, p_specs);
      if (nested == NULL) {
        return false;
      }
      // the guard covers exactly the window between creation and the append
      // that roots the child (the append itself can collect).
      if (!gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(nested))) {
        return false;
      }
      const bool appended = value_array_append(&dest->constants, OBJECT_AS_VALUE(nested));
      gc_guard_down(p_vm->alloc->gc);
      if (!appended) {
        return false;
      }
      if (!copy_proto_fill(p_vm, nested, nested_src, p_specs)) {
        return false;
      }
    } else {
      if (!value_array_append(&dest->constants, v)) {
        return false;
      }
    }
  }
  return true;
}

// deep-copy a compiled body (every nested non-member proto included) so a
// second definition can own its own bake. the source's code is already
// freeze-patched for its class; the copy starts from that state, which the
// caller guarantees to be valid (same parent, identical flattened layout).
// a failed copy leaves the partial tree unreachable - the collector reclaims
// it.
static object_function* copy_proto_tree(vm* p_vm, object_function* p_src) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_function* top = create_object_function(p_src->name, p_src->arity, &specs);
  if (top == NULL) {
    return NULL;
  }
  // create_object_function makes one allocation (its collect runs before the
  // object exists) and object_store_append does not allocate, so nothing can
  // collect between creation and this guard.
  if (!gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(top))) {
    return NULL;
  }
  const bool filled = copy_proto_fill(p_vm, top, p_src, &specs);
  gc_guard_down(p_vm->alloc->gc);
  return filled ? top : NULL;
}

// patch one proto (once): the patched_by marker is the single-walk guard.
// redefinitions are handled one level up, at the closures that own member
// protos - every proto reaching this function is fresh or already this
// class's, so a foreign marker here is an internal error.
static bool freeze_patch_proto(vm* p_vm, object_class* p_class, object_function* p_proto, bool p_construction) {
  if (p_proto->patched_by == p_class) {
    return true;
  }
  if (p_proto->patched_by != NULL) {
    runtime_error(p_vm,
                  OK_EK_INTERNAL,
                  "a class body reached the freeze walk without a per-definition copy.");
    return false;
  }
  p_proto->patched_by = p_class;
  return freeze_walk_chunk(p_vm, p_class, &p_proto->chunk, p_construction);
}

// freeze one member closure for p_class. a proto already patched for another
// class is a redefinition: the same parent bakes to the identical layout, so
// the new definition takes a copy of the body and freezes that. a different
// parent would need a different bake from pre-freeze code the proto no
// longer carries, so it is refused.
static bool freeze_patch_closure(vm* p_vm, object_class* p_class, object_closure* p_closure, bool p_construction) {
  object_function* proto = p_closure->function;
  if (proto->patched_by == p_class) {
    return true;
  }
  if (proto->patched_by != NULL) {
    if (proto->patched_by->parent != p_class->parent) {
      runtime_error(p_vm,
                    OK_EK_TYPE,
                    "this class body is already defined with a different base: "
                    "a local class can only be redefined with the same parent.");
      return false;
    }
    object_function* copy = copy_proto_tree(p_vm, proto);
    if (copy == NULL) {
      runtime_oom(p_vm, "out of memory: failed to copy a redefined class body.");
      return false;
    }
    p_closure->function = copy;
    proto = copy;
  }
  proto->patched_by = p_class;
  return freeze_walk_chunk(p_vm, p_class, &proto->chunk, p_construction);
}

static bool freeze_walk_chunk(vm* p_vm, object_class* p_class, chunk* p_chunk, bool p_construction) {
  if (!patch_chunk_fields(p_vm, p_class, p_chunk, p_construction)) {
    return false;
  }
  // proto trees grow downward only (a proto's constants are built before it
  // is finished), so the recursion terminates; shared protos short-circuit
  // through the patched_by guard. nested protos do NOT inherit the
  // construction flag: a closure inside init is patched like any other
  // method body, so deferred mutation of a non-mut field stays trapped.
  // MEMBER protos of another class are the boundary: a class declared inside
  // one of this class's methods compiles its members as constants of that
  // method's chunk, and this class's freeze must leave them for their own
  // OP_CLASS_END (patching them here would bake this class's layout into
  // their bodies and break their definition with a spurious "defined once"
  // error - or worse, a wrong-slot bake).
  for (uint32_t i = 0; i < p_chunk->constants.count; ++i) {
    if (IS_VALUE_FUNCTION(p_chunk->constants.data[i])) {
      object_function* nested = VALUE_AS_FUNCTION(p_chunk->constants.data[i]);
      if (nested->is_class_member) {
        continue;
      }
      if (!freeze_patch_proto(p_vm, p_class, nested, false)) {
        return false;
      }
    }
  }
  return true;
}

// the definition end: freeze the layout, then patch every member body (the
// method closures, the field initializer thunks, and every proto nested under
// them - lambdas included).
static bool class_freeze(vm* p_vm, object_class* p_class) {
  if (!object_class_freeze(p_class)) {
    if (p_class->nfields > aprime_MAX || p_class->nown > aprime_MAX) {
      runtime_error(p_vm, OK_EK_TYPE, "too many fields in class %s.", p_class->name->string.chars);
    } else {
      runtime_oom(p_vm, "out of memory: failed to freeze class.");
    }
    return false;
  }
  for (uint32_t i = 0; i < p_class->methods.capacity; ++i) {
    if (p_class->methods.keys[i] != NULL && p_class->methods.values[i].closure != NULL) {
      const object_string* mname = p_class->methods.keys[i];
      const bool is_init = string_get_length(&mname->string) == 4 && memcmp(mname->string.chars, "init", 4) == 0;
      if (!freeze_patch_closure(p_vm, p_class, p_class->methods.values[i].closure, is_init)) {
        return false;
      }
    }
  }
  for (uint32_t i = 0; i < p_class->own_inits.count; ++i) {
    if (p_class->own_inits.data[i] != NULL) {
      if (!freeze_patch_closure(p_vm, p_class, p_class->own_inits.data[i], true)) {
        return false;
      }
    }
  }
  return true;
}

// stage a method call at the register file top and run it to completion: the
// defer machinery's lane (stage, call, nested run_until, restore watermark).
// the receiver lands at the window base, the arguments above it; the caller's
// window is never touched. used by construction only - the dispatch loop's
// own calls resume through it instead.
static bool run_method_to_completion(vm* p_vm, object_closure* p_closure, value p_receiver, uint32_t p_argc,
                                     const value* p_args, value* p_result) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  const uint32_t depth = exe->call_stack.count;
  const uint32_t saved_top = reg->top;
  const uint32_t need = 1u + p_argc + p_closure->function->registers;
  if (reg->top + need > UINT16_MAX + 1u) {
    runtime_error(p_vm, OK_EK_INTERNAL, "construction: register file exhausted.");
    return false;
  }
  if (reg->top + need > reg->array.capacity) {
    // same contract as call()'s growth: fresh slots must read as null
    const uint32_t target = reg->top + need + ((reg->top + need) >> 1u);
    const uint32_t new_capacity = target > UINT16_MAX + 1u ? UINT16_MAX + 1u : target;
    const uint32_t initialized_uptill = reg->array.capacity;
    if (!register_array_grow(&reg->array, new_capacity)) {
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return false;
    }
    for (uint32_t i = initialized_uptill; i < new_capacity; ++i) {
      reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  const uint32_t staged = reg->top;
  reg->array.data[staged] = p_receiver;
  for (uint32_t i = 0; i < p_argc; ++i) {
    reg->array.data[staged + 1u + i] = p_args[i];
  }
  reg->top = staged + need;
  gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(p_closure));
  const bool called = call(p_vm, exe, reg, p_closure, staged, (uint16_t)p_argc, p_result != NULL ? 1u : 0u);
  gc_guard_down(p_vm->alloc->gc);
  if (!called) {
    reg->top = saved_top;
    return false;
  }
  // the yield barrier: the construction lane (field initializer thunks,
  // init methods) is a C-driven region - construct_instance publishes the
  // instance and restores the watermark in ITS C frame after this run_until
  // returns, so a yield from inside a constructor could never resume
  // honestly. OP_YIELD refuses it.
  const uint8_t saved_barrier = exe->yield_barrier;
  if (exe->yield_barrier == 0) {
    exe->yield_barrier_kind = YIELD_BARRIER_CONSTRUCTOR;
  }
  exe->yield_barrier++;
  const interpret_result result = run_until(p_vm, depth);
  exe->yield_barrier = saved_barrier;
  reg->top = saved_top;
  if (result.status != RUNTIME_OK) {
    return false;
  }
  if (p_result != NULL) {
    *p_result = reg->array.data[staged];
  }
  return true;
}

// run one class's field initializer thunks against a fresh instance, then
// recurse up the chain: parent segments initialize before own (declaration
// order within a segment, root-first across segments).
static bool run_field_inits(vm* p_vm, object_class* p_class, value p_instance) {
  if (p_class == NULL) {
    return true;
  }
  if (!run_field_inits(p_vm, p_class->parent, p_instance)) {
    return false;
  }
  for (uint32_t i = 0; i < p_class->own_inits.count; ++i) {
    object_closure* thunk = p_class->own_inits.data[i];
    if (thunk == NULL) {
      continue;
    }
    if (!run_method_to_completion(p_vm, thunk, p_instance, 0, NULL, NULL)) {
      return false;
    }
  }
  return true;
}

// the default construction: allocate the instance, run the field initializer
// thunks root-first, call the nearest init with the caller's arguments, and
// publish the instance at the call window. an overridden static operator ()
// never reaches here - the caller dispatched to it instead.
static bool construct_instance(vm* p_vm, object_class* p_class, uint32_t p_window, uint16_t p_argc) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  if (p_argc > UINT8_MAX) {
    runtime_error(p_vm, OK_EK_ARITY, "too many arguments to construct class %s.", p_class->name->string.chars);
    return false;
  }
  // the arguments are staged at the call window; the register file may grow
  // (and realloc) while the initializer thunks run, so they are stashed in C
  // first - the same discipline call_native applies to its own window.
  value args_stash[UINT8_MAX];
  if (p_argc > 0) {
    memcpy(args_stash, reg->array.data + p_window + 1u, p_argc * sizeof(value));
  }
  gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(p_class));
  object_instance* inst = create_object_instance(p_class, &specs);
  gc_guard_down(p_vm->alloc->gc);
  if (inst == NULL) {
    runtime_oom(p_vm, "out of memory: failed to allocate an instance.");
    return false;
  }
  const value instance_value = OBJECT_AS_VALUE(inst);
  gc_guard_up(p_vm->alloc->gc, instance_value);
  bool ok = run_field_inits(p_vm, p_class, instance_value);
  if (ok) {
    // the nearest init wins (standard method lookup); its return value is
    // discarded - the construction's value is the instance.
    const method_entry* init =
        p_vm->init_method_name != NULL ? object_class_find_method(p_class, p_vm->init_method_name) : NULL;
    if (init != NULL) {
      if (init->is_native) {
        // a C-defined initializer: the receiver-first flight over the stashed
        // arguments, the result discarded - the construction's value is the
        // instance, same as the closure lane.
        value init_stash[UINT8_MAX];
        init_stash[0] = instance_value;
        if (p_argc > 0) {
          memcpy(init_stash + 1, args_stash, p_argc * sizeof(value));
        }
        value discarded;
        ok = run_native(p_vm, exe, init->native, (uint16_t)(p_argc + 1u), init_stash, &discarded);
      } else if (!run_method_to_completion(p_vm, init->closure, instance_value, p_argc, args_stash, NULL)) {
        ok = false;
      }
    }
  }
  gc_guard_down(p_vm->alloc->gc);
  if (!ok) {
    return false;
  }
  // per OP_CALL semantics the result lands at the call window start; the
  // watermark returns to the caller's frame top (call_native's contract).
  if (exe->call_stack.count > 0) {
    call_frame* caller = &exe->call_stack.data[exe->call_stack.count - 1];
    reg->top = caller->registers_base + caller->closure->function->registers;
  } else {
    reg->top = p_window;
  }
  reg->array.data[p_window] = instance_value;
  return true;
}

static interpret_result error_interpret_result(void) {
  interpret_result result;
  result.status = RUNTIME_ERROR;
  result.top_level_return = NULL_AS_VALUE();
  return result;
}

// fill one way of a site's probe chain: the first empty way, else the
// round-robin victim. callers arrive here only on a probe miss, so the key
// is absent by construction. eviction is sound forever: entries are
// read-only snapshots of frozen tables.
static void mic_fill(mic_cache* p_cache, const mic_entry* p_entry) {
  for (uint32_t w = 0; w < OK_MIC_WAYS; ++w) {
    if (p_cache->ways[w].klass == NULL_AS_VALUE()) {
      p_cache->ways[w] = *p_entry;
      return;
    }
  }
  p_cache->ways[p_cache->hand % OK_MIC_WAYS] = *p_entry;
  p_cache->hand = (uint8_t)((p_cache->hand + 1u) % OK_MIC_WAYS);
}

// the fused compare's comparator lane: dispatches to the SAME perform the
// value binops use, by comparator id. lives outside the dispatch loop so its
// switch cannot be mistaken for dispatch control flow by transform tooling
// (and reads better): every arm returns directly.
static interpret_result perform_comparison(
    vm* p_vm, register_file* p_reg, call_frame* p_frame, uint8_t p_cmp, uint16_t p_dest, value p_lhs, value p_rhs) {
  switch (p_cmp) {
  case CMP_EQUALS:
    return perform_equal(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_NOT_EQUAL:
    return perform_not_equal(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_LESS:
    return perform_less(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_LESS_EQUAL:
    return perform_less_equal(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_GREATER:
    return perform_greater(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_GREATER_EQUAL:
    return perform_greater_equal(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_STRICT_EQUAL:
    return perform_strict_equal(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  case CMP_IS:
  default:
    return perform_is(p_vm, p_reg, p_frame, p_dest, p_lhs, p_rhs);
  }
}

// threaded dispatch (the labels-as-values GNU extension): each handler ends
// in its own indirect jump through the opcode table, so the cpu's branch
// predictor learns per-site targets instead of fighting over one shared
// switch branch. enabled by OK_THREADED_DISPATCH - cmake's GNU auto-detect
// sets it unless OK_PURE is on, and any -D hand-build can too. the trace
// build keeps the canonical switch loop (OK_TRACE_EXECUTION wants the loop
// top on every instruction, and perf is not its concern); anything without
// the extension compiles the switch exactly as before. the two modes share
// one handler body: CASE() spells the label, DISPATCH() leaves the handler.
#if defined(OK_THREADED_DISPATCH) && defined(__GNUC__) && !defined(OK_TRACE_EXECUTION)
#define OK_DISPATCH_THREADED 1
#endif

#ifdef OK_DISPATCH_THREADED
#define CASE(op) L_##op
#define DISPATCH()                                                                                                       \
  do {                                                                                                                   \
    instruction = READ_WORD();                                                                                           \
    goto *ok_dispatch_table[op_of(instruction)];                                                                         \
  } while (0)
#else
#define CASE(op) case op
#define DISPATCH() break
#endif

interpret_result run_until(vm* p_vm, uint32_t p_call_depth) {
  execution_state* exe = p_vm->current_execution_state;
  call_frame* frame = &exe->call_stack.data[exe->call_stack.count - 1];
  register_file* reg = &exe->registers;
#define REG_AT(n) exe->registers.array.data[frame->registers_base + n]
  register word* ip = frame->ip;
#define READ_WORD() (*ip++)
#define JOIN_WORD(instruction) (((word2)READ_WORD() << 32u) | (word2)(instruction))
  // the error exit. the loop owns the live ip. runtime_error
  // only stashes (no print, no reset), so this is the one place where the
  // live ip and the call stack still exist together. first unwind:
  // a live handler record above this level's depth takes the error (frames pop,
  // the value parks, the loop resumes at the catch). otherwise report (the
  // innermost level prints; outer levels propagate silently) and fail.
  // no reset, an outer C level may still catch. (before this, frame->ip was only
  // synced in the four CALL arms, so every BIN_OP/INDEX/... error read the
  // frame's ENTRY pointer and the report printed with no location at all.
  // lua's lvm.c keeps the same invariant via savestate/Protect.
  // a per-instruction store was measured at +10-18% on numeric_for_sum and declined.)
#define FAIL_PATH()                                                                                                    \
  do {                                                                                                                 \
    frame->ip = ip;                                                                                                    \
    if (exe->error_hard) {                                                                                             \
      /* oom: uncatchable, prints from the live state with the synced ip */                                            \
      return vm_report_error(p_vm);                                                                                    \
    }                                                                                                                  \
    const unwind_resume resume = attempt_unwind(p_vm, p_call_depth);                                                   \
    if (resume.caught) {                                                                                               \
      frame = resume.frame;                                                                                            \
      ip = resume.ip;                                                                                                  \
      goto next_instruction;                                                                                           \
    }                                                                                                                  \
    /* propagation: an outer script level may still catch, and the print belongs */                                    \
    /* to the outermost C lane (ok_run prints) */                                                                      \
    return error_interpret_result();                                                                                   \
  } while (0)

// the operator-dispatch lanes run script from inside a perform (the nested
// run_until in dispatch_operator_method): the nested call may grow (and
// realloc) the register file and the call stack, which dangles this loop's
// cached exe/reg/frame locals. every case whose perform can dispatch refreshes
// after a success - three loads and one store, nothing when nothing grew.
#define REFRESH_AFTER_NESTED()                                                                                          \
  do {                                                                                                                  \
    exe = p_vm->current_execution_state;                                                                                \
    reg = &exe->registers;                                                                                              \
    frame = &exe->call_stack.data[exe->call_stack.count - 1];                                                           \
    frame->ip = ip;                                                                                                     \
  } while (0)

#ifdef OK_DISPATCH_THREADED
// one label per opcode, designated by enum value so the mapping cannot rot
// with the enum's order (a hole stays NULL and fails loudly on first dispatch;
// OK_PARANOID checks the whole table up front).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // &&label is the extension being opted into
static void* ok_dispatch_table[OP__COUNT] = {
  [OP_NOP] = &&L_OP_NOP,
  [OP_MOVE] = &&L_OP_MOVE,
  [OP_MOVE_LONG] = &&L_OP_MOVE_LONG,
  [OP_LOAD_CONSTANT] = &&L_OP_LOAD_CONSTANT,
  [OP_LOAD_CONSTANT_LONG] = &&L_OP_LOAD_CONSTANT_LONG,
  [OP_JUMP] = &&L_OP_JUMP,
  [OP_LOOP] = &&L_OP_LOOP,
  [OP_JUMP_IF] = &&L_OP_JUMP_IF,
  [OP_JUMP_IF_SET] = &&L_OP_JUMP_IF_SET,
  [OP_LOAD_NULL] = &&L_OP_LOAD_NULL,
  [OP_LOAD_FALSE] = &&L_OP_LOAD_FALSE,
  [OP_LOAD_TRUE] = &&L_OP_LOAD_TRUE,
  [OP_LOAD_GLOBAL] = &&L_OP_LOAD_GLOBAL,
  [OP_LOAD_GLOBAL_LONG] = &&L_OP_LOAD_GLOBAL_LONG,
  [OP_STORE_GLOBAL] = &&L_OP_STORE_GLOBAL,
  [OP_STORE_GLOBAL_LONG] = &&L_OP_STORE_GLOBAL_LONG,
  [OP_LOAD_UPVALUE] = &&L_OP_LOAD_UPVALUE,
  [OP_LOAD_UPVALUE_LONG] = &&L_OP_LOAD_UPVALUE_LONG,
  [OP_STORE_UPVALUE] = &&L_OP_STORE_UPVALUE,
  [OP_STORE_UPVALUE_LONG] = &&L_OP_STORE_UPVALUE_LONG,
  [OP_CLOSE_UPVALUES] = &&L_OP_CLOSE_UPVALUES,
  [OP_CALL] = &&L_OP_CALL,
  [OP_CALL_LONG] = &&L_OP_CALL_LONG,
  [OP_RETURN] = &&L_OP_RETURN,
  [OP_RETURN_LONG] = &&L_OP_RETURN_LONG,
  [OP_RETURN_1] = &&L_OP_RETURN_1,
  [OP_CLOSURE] = &&L_OP_CLOSURE,
  [OP_CLOSURE_LONG] = &&L_OP_CLOSURE_LONG,
  [OP_NOT] = &&L_OP_NOT,
  [OP_NOT_LONG] = &&L_OP_NOT_LONG,
  [OP_NEGATE] = &&L_OP_NEGATE,
  [OP_NEGATE_LONG] = &&L_OP_NEGATE_LONG,
  [OP_ADD] = &&L_OP_ADD,
  [OP_ADD_LONG] = &&L_OP_ADD_LONG,
  [OP_SUBTRACT] = &&L_OP_SUBTRACT,
  [OP_SUBTRACT_LONG] = &&L_OP_SUBTRACT_LONG,
  [OP_MULTIPLY] = &&L_OP_MULTIPLY,
  [OP_MULTIPLY_LONG] = &&L_OP_MULTIPLY_LONG,
  [OP_DIVIDE] = &&L_OP_DIVIDE,
  [OP_DIVIDE_LONG] = &&L_OP_DIVIDE_LONG,
  [OP_MODULUS] = &&L_OP_MODULUS,
  [OP_AS] = &&L_OP_AS,
  [OP_AS_LONG] = &&L_OP_AS_LONG,
  [OP_MODULUS_LONG] = &&L_OP_MODULUS_LONG,
  [OP_EQUALS] = &&L_OP_EQUALS,
  [OP_EQUALS_LONG] = &&L_OP_EQUALS_LONG,
  [OP_NOT_EQUAL] = &&L_OP_NOT_EQUAL,
  [OP_NOT_EQUAL_LONG] = &&L_OP_NOT_EQUAL_LONG,
  [OP_GREATER] = &&L_OP_GREATER,
  [OP_GREATER_LONG] = &&L_OP_GREATER_LONG,
  [OP_LESS] = &&L_OP_LESS,
  [OP_LESS_LONG] = &&L_OP_LESS_LONG,
  [OP_GREATER_EQUAL] = &&L_OP_GREATER_EQUAL,
  [OP_GREATER_EQUAL_LONG] = &&L_OP_GREATER_EQUAL_LONG,
  [OP_LESS_EQUAL] = &&L_OP_LESS_EQUAL,
  [OP_LESS_EQUAL_LONG] = &&L_OP_LESS_EQUAL_LONG,
  [OP_PRINT] = &&L_OP_PRINT,
  [OP_ARRAY_NEW] = &&L_OP_ARRAY_NEW,
  [OP_ARRAY_NEW_LONG] = &&L_OP_ARRAY_NEW_LONG,
  [OP_MAP_NEW] = &&L_OP_MAP_NEW,
  [OP_MAP_NEW_LONG] = &&L_OP_MAP_NEW_LONG,
  [OP_INDEX_GET] = &&L_OP_INDEX_GET,
  [OP_INDEX_GET_LONG] = &&L_OP_INDEX_GET_LONG,
  [OP_INDEX_SET] = &&L_OP_INDEX_SET,
  [OP_INDEX_SET_LONG] = &&L_OP_INDEX_SET_LONG,
  [OP_JUMP_IF_ARGC] = &&L_OP_JUMP_IF_ARGC,
  [OP_FOR_PREP] = &&L_OP_FOR_PREP,
  [OP_FOR_LOOP] = &&L_OP_FOR_LOOP,
  [OP_ITER_PREP] = &&L_OP_ITER_PREP,
  [OP_ITER_GET] = &&L_OP_ITER_GET,
  [OP_ITER_LOOP] = &&L_OP_ITER_LOOP,
  [OP_VA_PACK] = &&L_OP_VA_PACK,
  [OP_CALL_SPREAD] = &&L_OP_CALL_SPREAD,
  [OP_CALL_SPREAD_LONG] = &&L_OP_CALL_SPREAD_LONG,
  [OP_TRY_ENTER] = &&L_OP_TRY_ENTER,
  [OP_TRY_EXIT] = &&L_OP_TRY_EXIT,
  [OP_THROW] = &&L_OP_THROW,
  [OP_DEFER_MARK] = &&L_OP_DEFER_MARK,
  [OP_DEFER_RUN] = &&L_OP_DEFER_RUN,
  [OP_CLASS] = &&L_OP_CLASS,
  [OP_DEFINE_FIELD] = &&L_OP_DEFINE_FIELD,
  [OP_DEFINE_METHOD] = &&L_OP_DEFINE_METHOD,
  [OP_CLASS_END] = &&L_OP_CLASS_END,
  [OP_GET_FIELD] = &&L_OP_GET_FIELD,
  [OP_GET_FIELD_LONG] = &&L_OP_GET_FIELD_LONG,
  [OP_STORE_FIELD] = &&L_OP_STORE_FIELD,
  [OP_STORE_FIELD_LONG] = &&L_OP_STORE_FIELD_LONG,
  [OP_STORE_FIELD_G] = &&L_OP_STORE_FIELD_G,
  [OP_STORE_FIELD_G_NAME] = &&L_OP_STORE_FIELD_G_NAME,
  [OP_FIELD_IMMUTABLE] = &&L_OP_FIELD_IMMUTABLE,
  [OP_FIELD_IMMUTABLE_G] = &&L_OP_FIELD_IMMUTABLE_G,
  [OP_GET_MEMBER] = &&L_OP_GET_MEMBER,
  [OP_INVOKE] = &&L_OP_INVOKE,
  [OP_SUPER_INVOKE] = &&L_OP_SUPER_INVOKE,
  [OP_CLASS_END_LONG] = &&L_OP_CLASS_END_LONG,
  [OP_DEFINE_STATIC_METHOD] = &&L_OP_DEFINE_STATIC_METHOD,
  [OP_DEFINE_STATIC_FIELD] = &&L_OP_DEFINE_STATIC_FIELD,
  [OP_GET_STATIC_NAME] = &&L_OP_GET_STATIC_NAME,
  [OP_STORE_STATIC_NAME] = &&L_OP_STORE_STATIC_NAME,
  [OP_GET_METHOD] = &&L_OP_GET_METHOD,
  [OP_GET_METHOD_LONG] = &&L_OP_GET_METHOD_LONG,
  [OP_METHOD_NOT_ASSIGNABLE] = &&L_OP_METHOD_NOT_ASSIGNABLE,
  [OP_METHOD_NOT_ASSIGNABLE_LONG] = &&L_OP_METHOD_NOT_ASSIGNABLE_LONG,
  [OP_STRICT_EQUAL] = &&L_OP_STRICT_EQUAL,
  [OP_STRICT_EQUAL_LONG] = &&L_OP_STRICT_EQUAL_LONG,
  [OP_IS] = &&L_OP_IS,
  [OP_IS_LONG] = &&L_OP_IS_LONG,
  [OP_JUMP_IF_CMP] = &&L_OP_JUMP_IF_CMP,
  [OP_YIELD] = &&L_OP_YIELD,
  [OP_YIELD_LONG] = &&L_OP_YIELD_LONG,
  // the four pre-patch NAME forms never dispatch: the freeze pass rewrites
  // every one before member code can run. the switch build silently skips an
  // un-rewritten stray (no case label, no default); aliasing them to NOP
  // keeps the two dispatch modes byte-identical in that (impossible) case.
  [OP_GET_FIELD_NAME] = &&L_OP_NOP,
  [OP_GET_FIELD_NAME_LONG] = &&L_OP_NOP,
  [OP_STORE_FIELD_NAME] = &&L_OP_NOP,
  [OP_STORE_FIELD_NAME_LONG] = &&L_OP_NOP,
};
#ifdef OK_PARANOID
  for (int ok_di = 0; ok_di < (int)OP__COUNT; ++ok_di) {
    if (ok_dispatch_table[ok_di] == NULL) {
      fprintf(stderr, "ok: dispatch table hole at opcode %d\n", ok_di);
      exit(70);
    }
  }
#endif
#pragma GCC diagnostic pop
#endif

  for (;;) {
#if defined(OK_TRACE_EXECUTION)
    disassembler disassembler;
    disassembler_specs specs = {.chunk = &frame->closure->function->chunk, .globals_store = p_vm->globals_store};
    disassembler_init(&disassembler, specs);

    printf("[ ");
    for (value* r = exe->registers.array.data; r < exe->registers.array.data + exe->registers.top; ++r) {
      printf("[");
      value_debug_print(*r);
      printf("]");
    }
    printf("]\n");
    debug_disassemble_instruction(&disassembler, (uint32_t)(ip - frame->closure->function->chunk.code.data));
#endif // defined(OK_TRACE_EXECUTION)

    word instruction = READ_WORD();
#if defined(OK_DISPATCH_THREADED)
    goto *ok_dispatch_table[op_of(instruction)];
#else
    switch (op_of(instruction)) {
#endif
    CASE(OP_NOP): {
      DISPATCH();
    }
    CASE(OP_MOVE): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = REG_AT(bp);
      DISPATCH();
    }
    CASE(OP_MOVE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = REG_AT(B);
      DISPATCH();
    }
    CASE(OP_LOAD_CONSTANT): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = frame->closure->function->chunk.constants.data[bp];
      DISPATCH();
    }
    CASE(OP_LOAD_CONSTANT_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = frame->closure->function->chunk.constants.data[B];
      DISPATCH();
    }
    CASE(OP_JUMP): {
      ip += Alarge_of(instruction);
      DISPATCH();
    }
    CASE(OP_LOOP): {
      ip -= Alarge_of(instruction);
      DISPATCH();
    }
    CASE(OP_JUMP_IF): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const bool p = p_of(words);
      const bool jump = p ^ is_falsy(REG_AT(B));
      ip += jump * A;
      DISPATCH();
    }
    CASE(OP_JUMP_IF_SET): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const bool p = p_of(words);
      const bool jump = p ^ is_falsy(REG_AT(B));
      if (jump) {
        ip += A;
        REG_AT(C) = REG_AT(B);
      }
      DISPATCH();
    }
    CASE(OP_JUMP_IF_CMP): {
      // the fused condition: one instruction where the generic path spends
      // two (the binop's bool materialization + OP_JUMP_IF's re-read). the
      // comparison runs through the SAME perform the binop uses (the helper
      // above), landing its result in the lhs register - which the compiler
      // may point straight at a local (the `while i < n` shape fuses with
      // zero staging), so the operand is saved and restored around the
      // result read. operators keep dispatching, the ordered four keep their
      // shared shape, and the truthiness test consumes the result exactly as
      // OP_JUMP_IF would.
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const uint8_t cmp = cmp_of(words);
      if (!comparison_is_valid(cmp)) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt fused comparison id.");
        FAIL_PATH();
        DISPATCH();
      }
      const value lhs = REG_AT(B);
      const value rhs = Q_of(words) ? frame->closure->function->chunk.constants.data[C] : REG_AT(C);
      const interpret_result res = perform_comparison(p_vm, reg, frame, cmp, B, lhs, rhs);
      if (res.status != RUNTIME_OK) {
        // the error paths never wrote the destination (performs write their
        // result only on success), so the operand needs no restore here.
        FAIL_PATH();
        DISPATCH();
      }
      // a dispatched operator method may have grown the register file / call
      // stack: re-cache before the result read.
      REFRESH_AFTER_NESTED();
      const bool jump = p ^ is_falsy(REG_AT(B));
      REG_AT(B) = lhs; // the operand's register survives the fused compare
      if (jump) {
        ip += A;
      }
      DISPATCH();
    }
    CASE(OP_LOAD_NULL): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      for (uint32_t i = A; i < (uint32_t)A + (uint32_t)B; ++i) {
        REG_AT(i) = NULL_AS_VALUE();
      }
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr,
              "[tg] NULL r[%u..r[%u) in %s\n",
              A,
              (uint32_t)A + B,
              frame->closure->function->name ? frame->closure->function->name->string.chars : "?");
#endif
      DISPATCH();
    }
    CASE(OP_LOAD_FALSE): {
      const uint16_t A = Alarge_of(instruction);
      REG_AT(A) = BOOL_AS_VALUE(false);
      DISPATCH();
    }
    CASE(OP_LOAD_TRUE): {
      const uint16_t A = Alarge_of(instruction);
      REG_AT(A) = BOOL_AS_VALUE(true);
      DISPATCH();
    }
    CASE(OP_LOAD_GLOBAL): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = p_vm->globals_store->global_values.data[bp];
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr, "[tg] L  g[%u] -> r[%u]  bits=0x%016llx\n", bp, ap, (unsigned long long)REG_AT(ap));
#endif
      DISPATCH();
    }
    CASE(OP_LOAD_GLOBAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = p_vm->globals_store->global_values.data[B];
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr, "[tg] L  g[%u] -> r[%u] (long)  bits=0x%016llx\n", B, A, (unsigned long long)REG_AT(A));
#endif
      DISPATCH();
    }
    CASE(OP_STORE_GLOBAL): {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      p_vm->globals_store->global_values.data[ap] = resolve_operand(frame, reg, bp, p);
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr,
              "[tg] S  g[%u] <- %s[%u]  bits=0x%016llx\n",
              ap,
              p ? "k" : "r",
              bp,
              (unsigned long long)p_vm->globals_store->global_values.data[ap]);
#endif
      DISPATCH();
    }
    CASE(OP_STORE_GLOBAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      p_vm->globals_store->global_values.data[A] = resolve_operand(frame, reg, B, p);
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr,
              "[tg] S  g[%u] <- %s[%u] (long)  bits=0x%016llx\n",
              A,
              p ? "k" : "r",
              B,
              (unsigned long long)p_vm->globals_store->global_values.data[A]);
#endif
      DISPATCH();
    }
    CASE(OP_LOAD_UPVALUE): {
      uint16_t ap = aprime_of(instruction);
      uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = *upvalue_cell(p_vm, exe,
                                 frame->closure->upvalues.data[bp]); // upvalue locations index the OWNER's register array
      DISPATCH();
    }
    CASE(OP_LOAD_UPVALUE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = *upvalue_cell(p_vm, exe,
                               frame->closure->upvalues.data[B]); // upvalue locations index the OWNER's register array
      DISPATCH();
    }
    CASE(OP_STORE_UPVALUE): {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      *upvalue_cell(p_vm, exe, frame->closure->upvalues.data[ap]) = resolve_operand(frame, reg, bp, p);
      DISPATCH();
    }
    CASE(OP_STORE_UPVALUE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      *upvalue_cell(p_vm, exe, frame->closure->upvalues.data[A]) = resolve_operand(frame, reg, B, p);
      DISPATCH();
    }
    CASE(OP_CLOSE_UPVALUES): {
      const uint16_t A = Alarge_of(instruction);
      close_upvalues(exe, frame->registers_base + A);
      DISPATCH();
    }
    CASE(OP_CALL): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      frame->ip = ip;
      if (!call_value(p_vm,
                      exe,
                      reg,
                      frame->registers_base,
                      a,
                      p ? reg->top - (frame->registers_base + a + 1) : b,
                      c == 0 ? RETURN_COUNT_DYNAMIC : c - 1)) {
        FAIL_PATH();
      }
      // not sure if exe is gonna be modified here or not (resume??) but we set it anyways
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      if (exe->native_suspend_pending) {
        return native_suspend_park(exe, reg, frame, a);
      }
      DISPATCH();
    }
    CASE(OP_CALL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      frame->ip = ip;
      if (!call_value(p_vm,
                      exe,
                      reg,
                      frame->registers_base,
                      A,
                      p ? reg->top - (frame->registers_base + A + 1) : B,
                      C == 0 ? RETURN_COUNT_DYNAMIC : C - 1)) {
        FAIL_PATH();
      }
      // not sure if exe is gonna be modified here or not (resume??) but we set it anyways
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      if (exe->native_suspend_pending) {
        return native_suspend_park(exe, reg, frame, A);
      }
      DISPATCH();
    }
    CASE(OP_RETURN): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      const uint16_t returned = return_from_frame(exe, reg, frame, ap, bp);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      DISPATCH();
    }
    CASE(OP_RETURN_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t returned = return_from_frame(exe, reg, frame, A, B);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      DISPATCH();
    }
    CASE(OP_RETURN_1): {
      const uint16_t A = Alarge_of(instruction);
      const uint8_t e = elarge_of(instruction);
      const uint16_t returned = return_1_from_frame(p_vm, exe, frame, A, e);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      DISPATCH();
    }
    // the coroutine suspend point. the guards first (outside a coro, or
    // under a C-driven region / native hop: the yields that could never
    // resume honestly), then the park: the value goes out, the frame's ip
    // stays synced past this word, the resume-target register index rides
    // the coro, the status flips, and THIS run_until - the coro's resume
    // driver, the barriers guarantee no other - returns RUNTIME_SUSPENDED
    // with the value. no pops, no upvalue closes, no defers: a parked coro
    // is its frames, exactly as they were.
    CASE(OP_YIELD): {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      object_coro* coro = exe->owner_coro;
      frame->ip = ip;
      if (coro == NULL) {
        runtime_error(p_vm, OK_EK_VALUE, "yield outside a coroutine.");
        FAIL_PATH();
        DISPATCH();
      }
      if (exe->yield_barrier > 0) {
        switch ((yield_barrier_kind)exe->yield_barrier_kind) {
        case YIELD_BARRIER_CONSTRUCTOR:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside a constructor.");
          break;
        case YIELD_BARRIER_OPERATOR:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside an operator method.");
          break;
        case YIELD_BARRIER_DEFER:
        default:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside a defer.");
          break;
        }
        FAIL_PATH();
        DISPATCH();
      }
      if (exe->native_call_depth > 0) {
        runtime_error(p_vm, OK_EK_VALUE, "cannot yield across a native call.");
        FAIL_PATH();
        DISPATCH();
      }
      const value out = resolve_operand(frame, reg, bp, p);
      coro->resume_target = ap;
      coro->status = CORO_SUSPENDED;
      return (interpret_result){.status = RUNTIME_SUSPENDED, .top_level_return = out};
    }
    CASE(OP_YIELD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      object_coro* coro = exe->owner_coro;
      frame->ip = ip;
      if (coro == NULL) {
        runtime_error(p_vm, OK_EK_VALUE, "yield outside a coroutine.");
        FAIL_PATH();
        DISPATCH();
      }
      if (exe->yield_barrier > 0) {
        switch ((yield_barrier_kind)exe->yield_barrier_kind) {
        case YIELD_BARRIER_CONSTRUCTOR:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside a constructor.");
          break;
        case YIELD_BARRIER_OPERATOR:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside an operator method.");
          break;
        case YIELD_BARRIER_DEFER:
        default:
          runtime_error(p_vm, OK_EK_VALUE, "cannot yield from inside a defer.");
          break;
        }
        FAIL_PATH();
        DISPATCH();
      }
      if (exe->native_call_depth > 0) {
        runtime_error(p_vm, OK_EK_VALUE, "cannot yield across a native call.");
        FAIL_PATH();
        DISPATCH();
      }
      const value out = resolve_operand(frame, reg, B, p);
      coro->resume_target = A;
      coro->status = CORO_SUSPENDED;
      return (interpret_result){.status = RUNTIME_SUSPENDED, .top_level_return = out};
    }
    CASE(OP_CLOSURE): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result result = create_closure(p_vm, exe, reg, frame, ap, bp);
      if (result.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_CLOSURE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result result = create_closure(p_vm, exe, reg, frame, A, B);
      if (result.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_NOT): {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result res = perform_not(p_vm, reg, frame, ap, resolve_operand(frame, reg, bp, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_NOT_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result res = perform_not(p_vm, reg, frame, A, resolve_operand(frame, reg, B, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_NEGATE): {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result res = perform_negate(p_vm, reg, frame, ap, resolve_operand(frame, reg, bp, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_NEGATE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result res = perform_negate(p_vm, reg, frame, A, resolve_operand(frame, reg, B, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_ADD): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_add(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_ADD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_add(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_SUBTRACT): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_subtract(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_SUBTRACT_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_subtract(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_MULTIPLY): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_multiply(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_MULTIPLY_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_multiply(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_DIVIDE): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_divide(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_DIVIDE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_divide(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_MODULUS): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_modulo(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_MODULUS_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_modulo(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_EQUALS): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_EQUALS_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_NOT_EQUAL): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_not_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_STRICT_EQUAL): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      // never nests: raw word compare, no dispatch, no allocation
      interpret_result res =
          perform_strict_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_STRICT_EQUAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_strict_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_IS): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      // never nests: classof + pointer compare
      interpret_result res = perform_is(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_IS_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_is(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_NOT_EQUAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_not_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_GREATER): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_greater(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_GREATER_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_greater(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_LESS): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_less(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_LESS_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_less(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_GREATER_EQUAL): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_greater_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_GREATER_EQUAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res = perform_greater_equal(
          p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_LESS_EQUAL): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_less_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_LESS_EQUAL_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_less_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_AS): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      // an `operator <Target>` conversion runs script from inside the
      // perform: the register file and call stack may realloc under us
      interpret_result res = perform_as(p_vm, reg, frame, a, REG_AT(b), resolve_goperand(p_vm, frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }
    CASE(OP_AS_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_as(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_goperand(p_vm, frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        FAIL_PATH();
      }
      REFRESH_AFTER_NESTED();
      DISPATCH();
    }

    CASE(OP_ARRAY_NEW): {
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      // elements are live frame registers ([0, top) rooted). but value_array_grow
      // reallocs through the gc-aware allocator, which does collect on growth:
      // between creation and publication the array is linked in the store yet
      // unreachable, so a collect inside append_n would sweep it (aggressive gc
      // found this on every array test). so guard the window.
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_array* arr = create_object_array(&s);
      if (arr == NULL) {
        goto oom_new;
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(arr));
      const bool appended = c == 0 || value_array_append_n(&arr->values, &REG_AT(b), c);
      gc_guard_down(p_vm->alloc->gc);
      if (!appended) {
        goto oom_new;
      }
      REG_AT(a) = OBJECT_AS_VALUE(arr);
      DISPATCH();
    oom_new:
      runtime_oom(p_vm, "out of memory: failed to allocate array.");
      FAIL_PATH();
    }
    CASE(OP_ARRAY_NEW_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_array* arr = create_object_array(&s);
      if (arr == NULL) {
        goto oom_new_long;
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(arr));
      const bool appended = C == 0 || value_array_append_n(&arr->values, &REG_AT(B), C);
      gc_guard_down(p_vm->alloc->gc);
      if (!appended) {
        goto oom_new_long;
      }
      REG_AT(A) = OBJECT_AS_VALUE(arr);
      DISPATCH();
    oom_new_long:
      runtime_oom(p_vm, "out of memory: failed to allocate array.");
      FAIL_PATH();
    }
    CASE(OP_MAP_NEW): {
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      // pairs are live frame registers ([0, top) rooted). like ARRAY_NEW, the
      // map is linked in the store yet unreachable until it lands in r[a], and
      // every insert may grow through the gc-aware allocator (collects): guard
      // the whole fill. duplicate keys in a literal: the later insert
      // overwrites, len counts the unique keys.
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_map* map = create_object_map(&s);
      if (map == NULL) {
        goto oom_map_new;
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(map));
      bool filled = true;
      for (uint32_t i = 0; i < c && filled; ++i) {
        filled = map_write(p_vm, OBJECT_AS_VALUE(map), REG_AT(b + 2 * i), REG_AT(b + 2 * i + 1));
      }
      gc_guard_down(p_vm->alloc->gc);
      if (!filled) {
        FAIL_PATH(); // map_write already reported
      }
      REG_AT(a) = OBJECT_AS_VALUE(map);
      DISPATCH();
    oom_map_new:
      runtime_oom(p_vm, "out of memory: failed to allocate map.");
      FAIL_PATH();
    }
    CASE(OP_MAP_NEW_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_map* map = create_object_map(&s);
      if (map == NULL) {
        goto oom_map_new_long;
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(map));
      bool filled = true;
      for (uint32_t i = 0; i < C && filled; ++i) {
        filled = map_write(p_vm, OBJECT_AS_VALUE(map), REG_AT(B + 2 * i), REG_AT(B + 2 * i + 1));
      }
      gc_guard_down(p_vm->alloc->gc);
      if (!filled) {
        FAIL_PATH(); // map_write already reported
      }
      REG_AT(A) = OBJECT_AS_VALUE(map);
      DISPATCH();
    oom_map_new_long:
      runtime_oom(p_vm, "out of memory: failed to allocate map.");
      FAIL_PATH();
    }
    CASE(OP_INDEX_GET): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      const value base = REG_AT(b);
      const value key = resolve_operand(frame, reg, c, p);
      if (value_is_object_type(base, OBJ_MAP)) {
        value read;
        if (!map_read(p_vm, base, key, &read)) {
          FAIL_PATH();
        }
        REG_AT(a) = read;
        DISPATCH();
      }
      // the instance arm: operator [] on the left operand's class.
      if (IS_VALUE_INSTANCE(base)) {
        interpret_result out;
        if (try_instance_operator(p_vm, reg, frame, a, base, &key, 1, CLASS_OP_SUBSCRIPT_GET, &out)) {
          if (out.status != RUNTIME_OK) {
            FAIL_PATH();
          }
          REFRESH_AFTER_NESTED();
          DISPATCH();
        }
      }
      const object_array* arr = resolve_array_base(p_vm, base);
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, key, arr->values.count, &index)) {
        FAIL_PATH();
      }
      REG_AT(a) = arr->values.data[index];
      DISPATCH();
    }
    CASE(OP_INDEX_GET_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const value base = REG_AT(B);
      const value key = resolve_operand(frame, reg, C, p);
      if (value_is_object_type(base, OBJ_MAP)) {
        value read;
        if (!map_read(p_vm, base, key, &read)) {
          FAIL_PATH();
        }
        REG_AT(A) = read;
        DISPATCH();
      }
      // the instance arm: operator [] on the left operand's class.
      if (IS_VALUE_INSTANCE(base)) {
        interpret_result out;
        if (try_instance_operator(p_vm, reg, frame, A, base, &key, 1, CLASS_OP_SUBSCRIPT_GET, &out)) {
          if (out.status != RUNTIME_OK) {
            FAIL_PATH();
          }
          REFRESH_AFTER_NESTED();
          DISPATCH();
        }
      }
      const object_array* arr = resolve_array_base(p_vm, base);
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, key, arr->values.count, &index)) {
        FAIL_PATH();
      }
      REG_AT(A) = arr->values.data[index];
      DISPATCH();
    }
    CASE(OP_INDEX_SET): {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      const value base = REG_AT(a);
      const value key = resolve_operand(frame, reg, b, p);
      if (value_is_object_type(base, OBJ_MAP)) {
        // narrow SET has no room for a value-const flag (p is spent on the key):
        // the compiler materializes constant values, c is always a register here.
        if (!map_write(p_vm, base, key, REG_AT(c))) {
          FAIL_PATH();
        }
        DISPATCH();
      }
      // the instance arm: operator []= (index, value); the operator runs for
      // effect - the assignment expression's value is the stored value, same
      // as the array/map lanes.
      if (IS_VALUE_INSTANCE(base)) {
        const value args[2] = {key, REG_AT(c)};
        interpret_result out;
        if (try_instance_operator(p_vm, reg, frame, 0, base, args, 2, CLASS_OP_SUBSCRIPT_SET, &out)) {
          if (out.status != RUNTIME_OK) {
            FAIL_PATH();
          }
          REFRESH_AFTER_NESTED();
          DISPATCH();
        }
      }
      const object_array* arr = resolve_array_base(p_vm, base);
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, key, arr->values.count, &index)) {
        FAIL_PATH();
      }
      ((object_array*)arr)->values.data[index] = REG_AT(c);
      DISPATCH();
    }
    CASE(OP_INDEX_SET_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(words);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const value base = REG_AT(A);
      const value key = resolve_operand(frame, reg, B, p);
      if (value_is_object_type(base, OBJ_MAP)) {
        if (!map_write(p_vm, base, key, resolve_operand(frame, reg, C, q))) {
          FAIL_PATH();
        }
        DISPATCH();
      }
      // the instance arm: operator []= (index, value), long form.
      if (IS_VALUE_INSTANCE(base)) {
        const value args[2] = {key, resolve_operand(frame, reg, C, q)};
        interpret_result out;
        if (try_instance_operator(p_vm, reg, frame, 0, base, args, 2, CLASS_OP_SUBSCRIPT_SET, &out)) {
          if (out.status != RUNTIME_OK) {
            FAIL_PATH();
          }
          REFRESH_AFTER_NESTED();
          DISPATCH();
        }
      }
      const object_array* arr = resolve_array_base(p_vm, base);
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, key, arr->values.count, &index)) {
        FAIL_PATH();
      }
      ((object_array*)arr)->values.data[index] = resolve_operand(frame, reg, C, q);
      DISPATCH();
    }
    CASE(OP_JUMP_IF_ARGC): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const bool p = p_of(words);
      const bool jump = (p ? 1u : 0u) == (frame->argc > B ? 1u : 0u);
      ip += jump * A;
      DISPATCH();
    }
    CASE(OP_FOR_PREP): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words); // forward offset to the loop exit
      const uint16_t B = B_of(words); // counter. limit at B + 1, step at B + 2
      double start, limit, step;
      if (!for_operand_as_number(p_vm, REG_AT(B), "numeric for start", &start) ||
          !for_operand_as_number(p_vm, REG_AT(B + 1), "numeric for limit", &limit) ||
          !for_operand_as_number(p_vm, REG_AT(B + 2), "numeric for step", &step)) {
        FAIL_PATH();
      }
      if (step == 0.0) {
        runtime_error(p_vm, OK_EK_VALUE, "numeric for step is zero.");
        FAIL_PATH();
      }

      REG_AT(B) = NUMBER_AS_VALUE(start);
      REG_AT(B + 1) = NUMBER_AS_VALUE(limit);
      REG_AT(B + 2) = NUMBER_AS_VALUE(step);
      const bool in_range = step > 0.0 ? start < limit : start > limit;
      if (!in_range) {
        ip += A;
      }
      DISPATCH();
    }
    CASE(OP_FOR_LOOP): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words); // backward offset to the body top
      const uint16_t B = B_of(words); // counter. limit at B + 1, step at B + 2
      double counter;
      if (p_of(instruction)) {
        counter = VALUE_AS_NUMBER(REG_AT(B));
      } else if (!for_operand_as_number(p_vm, REG_AT(B), "numeric for counter", &counter)) {
        // mut counter: the body may reseat it to any value, keep the gate.
        FAIL_PATH();
      }
      // only the counter is reachable by name
      const double limit = VALUE_AS_NUMBER(REG_AT(B + 1));
      const double step = VALUE_AS_NUMBER(REG_AT(B + 2));
      counter += step;
      REG_AT(B) = NUMBER_AS_VALUE(counter);
      const bool in_range = step > 0.0 ? counter < limit : counter > limit;
      if (in_range) {
        ip -= A;
      }
      DISPATCH();
    }
    CASE(OP_ITER_PREP): {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(words);     // p == 1: the (k, v) two-binding form
      const uint16_t A = A_of(words); // forward offset to the loop exit
      const uint16_t B = B_of(words); // state: iterable, cursor at B + 1, end at B + 2
      const value iterable = REG_AT(B);
      if (value_is_object_type(iterable, OBJ_ARRAY)) {
        if (p) {
          runtime_error(p_vm, OK_EK_TYPE, "array iteration binds one loop variable.");
          FAIL_PATH();
        }
        // cursor/end are written here and by ITER_LOOP only, always as plain
        // numbers: the back edge reads them raw with no tag gate.
        const uint32_t count = VALUE_AS_ARRAY(iterable)->values.count;
        REG_AT(B + 1) = NUMBER_AS_VALUE(0);
        REG_AT(B + 2) = NUMBER_AS_VALUE((double)count);
        if (count == 0) {
          ip += A;
        }
        DISPATCH();
      }
      if (value_is_object_type(iterable, OBJ_MAP)) {
        // snapshot the version for the back edge's size-change check, then
        // park the cursor on the first occupied slot.
        object_map* map = VALUE_AS_MAP(iterable);
        REG_AT(B + 2) = NUMBER_AS_VALUE((double)map->version);
        uint32_t slot = 0;
        while (slot < map->entries.capacity && IS_VALUE_STRICTLY_NULL(map->entries.keys[slot])) {
          slot++;
        }
        REG_AT(B + 1) = NUMBER_AS_VALUE((double)slot);
        if (slot >= map->entries.capacity) {
          ip += A;
        }
        DISPATCH();
      }
      runtime_error(p_vm, OK_EK_TYPE, "can only iterate over arrays and maps.");
      FAIL_PATH();
    }
    CASE(OP_ITER_GET): {
      const bool p = p_of(instruction); // p == 1: also write the value register
      const word2 words = JOIN_WORD(instruction);
      const uint16_t B = B_of(words); // state base
      const uint16_t C = C_of(words); // the binding: k, and v at C + 1 when p
      const value iterable = REG_AT(B);
      // cursor: written only by PREP/LOOP as a plain number, read raw
      const uint32_t cursor = (uint32_t)VALUE_AS_NUMBER(REG_AT(B + 1));
      if (value_is_object_type(iterable, OBJ_ARRAY)) {
        REG_AT(C) = VALUE_AS_ARRAY(iterable)->values.data[cursor];
      } else {
        // a map: PREP validated the type, cursor is parked on an occupied slot
        object_map* map = VALUE_AS_MAP(iterable);
        REG_AT(C) = map->entries.keys[cursor];
        if (p) {
          REG_AT(C + 1) = map->entries.values[cursor];
        }
      }
      DISPATCH();
    }
    CASE(OP_ITER_LOOP): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words); // backward offset to the body top
      const uint16_t B = B_of(words); // state base
      const value iterable = REG_AT(B);
      if (value_is_object_type(iterable, OBJ_ARRAY)) {
        // the frozen end makes pushes during the body invisible to this trip
        // (lua-style): the loop runs to the entry-time element count.
        const double cursor = VALUE_AS_NUMBER(REG_AT(B + 1)) + 1.0;
        REG_AT(B + 1) = NUMBER_AS_VALUE(cursor);
        if (cursor < VALUE_AS_NUMBER(REG_AT(B + 2))) {
          ip -= A;
        }
        DISPATCH();
      }
      // a map: PREP validated the type. a size change since the snapshot is a
      // hard error (the slot walk would mis-walk: backward shifts move entries
      // under the cursor, grows swap the arrays entirely). value overwrites do
      // not bump the version and stay legal mid-iteration.
      object_map* map = VALUE_AS_MAP(iterable);
      if ((double)map->version != VALUE_AS_NUMBER(REG_AT(B + 2))) {
        runtime_error(p_vm, OK_EK_VALUE, "map changed size during iteration.");
        FAIL_PATH();
      }
      uint32_t slot = (uint32_t)VALUE_AS_NUMBER(REG_AT(B + 1));
      for (++slot; slot < map->entries.capacity && IS_VALUE_STRICTLY_NULL(map->entries.keys[slot]); ++slot) {
      }
      REG_AT(B + 1) = NUMBER_AS_VALUE((double)slot);
      if (slot < map->entries.capacity) {
        ip -= A;
      }
      DISPATCH();
    }
    CASE(OP_VA_PACK): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      // the trailing-argument window: [bp, argc] (bp is the varargs parameter's
      // own register, i.e. the first trailing argument). fewer arguments than
      // that means the fixed/defaulted params swallowed everything: an empty
      // pack. the source slots are live frame registers ([0, top) rooted). the
      // array being built is store-linked yet unreachable until it lands in
      // r[ap]. guard through the append, exactly like OP_ARRAY_NEW.
      const uint32_t count = (uint32_t)frame->argc >= bp ? (uint32_t)frame->argc - bp + 1u : 0u;
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_array* arr = create_object_array(&s);
      if (arr == NULL) {
        runtime_oom(p_vm, "out of memory: failed to allocate the varargs array.");
        FAIL_PATH();
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(arr));
      const bool appended = count == 0 || value_array_append_n(&arr->values, &REG_AT(bp), count);
      gc_guard_down(p_vm->alloc->gc);
      if (!appended) {
        runtime_oom(p_vm, "out of memory: failed to allocate the varargs array.");
        FAIL_PATH();
      }
      REG_AT(ap) = OBJECT_AS_VALUE(arr);
      DISPATCH();
    }
    CASE(OP_CALL_SPREAD): {
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      frame->ip = ip;
      if (!call_spread(p_vm, exe, reg, frame->registers_base, a, b, c == 0 ? RETURN_COUNT_DYNAMIC : (uint32_t)c - 1u)) {
        FAIL_PATH();
      }
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      if (exe->native_suspend_pending) {
        return native_suspend_park(exe, reg, frame, a);
      }
      DISPATCH();
    }
    CASE(OP_CALL_SPREAD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      frame->ip = ip;
      if (!call_spread(p_vm, exe, reg, frame->registers_base, A, B, C == 0 ? RETURN_COUNT_DYNAMIC : (uint32_t)C - 1u)) {
        FAIL_PATH();
      }
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      if (exe->native_suspend_pending) {
        return native_suspend_park(exe, reg, frame, A);
      }
      DISPATCH();
    }
    CASE(OP_PRINT): {
      const bool p = p_of(instruction);
      const value printed = resolve_operand(frame, reg, Alarge_of(instruction), p);
      if (p_vm->print_cb != NULL) {
        p_vm->print_cb(p_vm->print_ctx, printed);
      } else {
        value_debug_print(printed);
        puts("");
      }
      DISPATCH();
    }
    CASE(OP_TRY_ENTER): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      handler_record rec;
      rec.frame = exe->call_stack.count - 1;
      rec.catch_ip = ip + A; // forward offset from the word after this 2w instruction
      rec.park = B;
      rec.floor = C;
      if (!handler_stack_append(&exe->handlers, rec)) {
        runtime_oom(p_vm, "out of memory: failed to grow the handler stack.");
        FAIL_PATH();
      }
      DISPATCH();
    }
    CASE(OP_TRY_EXIT): {
      // leaving the protected region. the top record belongs to this frame
      // by construction (the compiler pairs every ENTER it emits).
      ASSERT(exe->handlers.count > 0 && exe->handlers.data[exe->handlers.count - 1].frame == exe->call_stack.count - 1);
      exe->handlers.count--;
      DISPATCH();
    }
    CASE(OP_THROW): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      exe->error_pending = true;
      exe->error_reported = false;
      exe->error_hard = false;
      exe->error_is_script = true; // the value is the error: any value, whole
      exe->error_value = REG_AT(A);
      exe->error_what[0] = '\0';
      exe->error_kind = OK_EK_INTERNAL;
      exe->has_error = true;
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_DEFER_MARK): {
      const uint16_t A = Alarge_of(instruction);
      value* slot = &reg->array.data[frame->registers_base + A];
      if (!IS_VALUE_STRICTLY_OBJECT(*slot) || object_get_type(VALUE_AS_OBJECT(*slot)) != OBJ_CLOSURE) {
        runtime_error(p_vm, OK_EK_INTERNAL, "internal: defer mark on a non-closure.");
        FAIL_PATH();
        DISPATCH();
      }
      *slot = DEFER_AS_VALUE((object_closure*)VALUE_AS_OBJECT(*slot));
      DISPATCH();
    }
    CASE(OP_DEFER_RUN): {
      const uint16_t A = Alarge_of(instruction);
      value* slot = &reg->array.data[frame->registers_base + A];
      if (IS_VALUE_STRICTLY_DEFER(*slot)) {
        object_closure* thunk = VALUE_AS_DEFER(*slot);
        *slot = NULL_AS_VALUE(); // consumed: exactly once per scope instance
        frame->ip = ip;
        if (!stage_defer_thunk(p_vm, exe, reg, thunk)) {
          FAIL_PATH();
          DISPATCH();
        }
        // the OP_CALL refresh: the thunk's frame returned into this one, or
        // an unwind resumed us elsewhere entirely
        exe = p_vm->current_execution_state;
        reg = &exe->registers;
        frame = &exe->call_stack.data[exe->call_stack.count - 1];
        ip = frame->ip;
      }
      // a null slot is the no-op: the defer statement never executed on
      // this path (conditional defer, a loop iteration that skipped it)
      DISPATCH();
    }
    CASE(OP_CLASS): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      value parent_value = REG_AT(C);
      object_class* parent = NULL;
      if (IS_VALUE_CLASS(parent_value)) {
        parent = VALUE_AS_CLASS(parent_value);
      } else if (!IS_VALUE_STRICTLY_NULL(parent_value)) {
        runtime_error(p_vm, OK_EK_TYPE, "the parent of a class must be a class.");
        FAIL_PATH();
        DISPATCH();
      }
      if (parent == NULL) {
        parent = p_vm->object_singleton;
      }
      if (parent == NULL) {
        runtime_error(p_vm, OK_EK_INTERNAL, "class object is missing.");
        FAIL_PATH();
        DISPATCH();
      }
      if (!IS_VALUE_STRING(frame->closure->function->chunk.constants.data[B])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "class name constant is not a string.");
        FAIL_PATH();
        DISPATCH();
      }
      object_specs specs = {p_vm->alloc, p_vm->objects_store};
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[B]);
      gc_guard_up(p_vm->alloc->gc, parent_value);
      object_class* klass = create_object_class(name, parent, &specs);
      gc_guard_down(p_vm->alloc->gc);
      if (klass == NULL) {
        runtime_oom(p_vm, "out of memory: failed to allocate class.");
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(A) = OBJECT_AS_VALUE(klass);
      DISPATCH();
    }
    CASE(OP_DEFINE_FIELD): {
      const word2 words = JOIN_WORD(instruction);
      const bool has_no_init = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const bool is_mut = Q_of(words);
      const bool is_pub = R_of(words);
      if (!IS_VALUE_CLASS(REG_AT(A)) || !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt class field definition.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(A));
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      object_closure* thunk = NULL;
      if (!has_no_init) {
        if (!IS_VALUE_CLOSURE(REG_AT(B))) {
          runtime_error(p_vm, OK_EK_INTERNAL, "corrupt class field initializer.");
          FAIL_PATH();
          DISPATCH();
        }
        thunk = VALUE_AS_CLOSURE(REG_AT(B));
        thunk->owner = klass;
      }
      const object_string* previous_owner = NULL;
      if (!object_class_add_field(klass, name, is_mut, is_pub, thunk, NULL_AS_VALUE(), &previous_owner)) {
        if (previous_owner != NULL) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "field '%s' is already declared by class '%s'.",
                        name->string.chars,
                        previous_owner->string.chars);
        } else {
          runtime_oom(p_vm, "out of memory: failed to add field.");
        }
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_DEFINE_METHOD): {
      const word2 words = JOIN_WORD(instruction);
      const bool is_pub = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      if (!IS_VALUE_CLASS(REG_AT(A)) || !IS_VALUE_CLOSURE(REG_AT(B)) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt class method definition.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(A));
      object_closure* closure = VALUE_AS_CLOSURE(REG_AT(B));
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      if (klass->frozen) {
        runtime_error(p_vm, OK_EK_TYPE, "class %s is sealed: methods cannot be added.", klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      closure->owner = klass;
      // pub survives overrides: an override of a pub ancestor is pub, and a
      // private override of a pub ancestor is refused (no quiet narrowing).
      bool pub = is_pub;
      {
        const method_entry* inherited = NULL;
        for (const object_class* c = klass->parent; c != NULL; c = c->parent) {
          inherited = method_map_get(&c->methods, name);
          if (inherited != NULL) {
            break; // leave the chain walk (a C break, never a dispatch)
          }
        }
        if (inherited != NULL) {
          if (inherited->pub && !is_pub) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "method '%s' overrides a pub method and must be pub.",
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          if (inherited->pub) {
            pub = true;
          }
          // the signature contract governs virtual dispatch: an override
          // must accept what the parent accepted. init is exempt - it is a
          // construction entry point, never dispatched through a base
          // reference, and the super-forwarding pattern requires the
          // subclass to widen its parameter list.
          const bool is_init_name =
              string_get_length(&name->string) == 4 && memcmp(name->string.chars, "init", 4) == 0;
          const object_function* fu = closure->function;
          if (!is_init_name && (inherited->arity != fu->arity ||
                                inherited->closure->function->ndefaults != fu->ndefaults ||
                                inherited->closure->function->has_varargs != fu->has_varargs)) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "method '%s' overrides a method with a different signature.",
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
        }
      }
      // the super-initializer rule's teeth, at the one point they can exist:
      // the chain is complete here (runtime names resolved), and the body
      // was compiled with the calls-super flag. an init override whose chain
      // carries an init must call super - forgot-super is a definition error.
      if (string_get_length(&name->string) == 4 && memcmp(name->string.chars, "init", 4) == 0) {
        for (const object_class* c = klass->parent; c != NULL; c = c->parent) {
          if (method_map_get(&c->methods, name) != NULL) {
            if (!closure->function->calls_super_init) {
              runtime_error(p_vm,
                            OK_EK_TYPE,
                            "class '%s' defines init without calling super(): an ancestor defines init.",
                            klass->name->string.chars);
              FAIL_PATH();
            }
            break; // the ancestor's init was found (a C break, never a dispatch)
          }
        }
        if (p_vm->current_execution_state->error_pending) {
          DISPATCH();
        }
      }
      method_entry entry;
      entry.closure = closure;
      entry.native = NULL;
      entry.name = name;
      entry.arity = closure->function->arity;
      entry.owner = klass;
      entry.is_native = 0;
      entry.pub = pub;
      // one name, one owner - within a class body too. this is load-bearing
      // for operators: `operator -` (binary) and `operator -()` (unary) share
      // a canonical name, and a silent overwrite would flip the dispatch.
      if (method_map_get(&klass->methods, name) != NULL) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "method '%s' is already declared in the body of class '%s'.",
                      name->string.chars,
                      klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      if (!method_map_set(&klass->methods, name, entry)) {
        runtime_oom(p_vm, "out of memory: failed to add method.");
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_CLASS_END): {
      const uint16_t ap = aprime_of(instruction);
      if (!IS_VALUE_CLASS(REG_AT(ap))) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt class definition end.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(ap));
      if (!class_freeze(p_vm, klass)) {
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_CLASS_END_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      if (!IS_VALUE_CLASS(REG_AT(A))) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt class definition end.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(A));
      if (!class_freeze(p_vm, klass)) {
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_DEFINE_STATIC_METHOD): {
      const word2 words = JOIN_WORD(instruction);
      const bool is_pub = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      if (!IS_VALUE_CLASS(REG_AT(A)) || !IS_VALUE_CLOSURE(REG_AT(B)) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt static method definition.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(A));
      object_closure* closure = VALUE_AS_CLOSURE(REG_AT(B));
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      if (klass->frozen) {
        runtime_error(p_vm, OK_EK_TYPE, "class %s is sealed: statics cannot be added.", klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      closure->owner = klass;
      // pub survival and the signature contract, against the parent's STATICS
      // (the same rules methods carry, one table over).
      bool pub = is_pub;
      {
        const method_entry* inherited = NULL;
        for (const object_class* c = klass->parent; c != NULL; c = c->parent) {
          inherited = method_map_get(&c->statics, name);
          if (inherited != NULL) {
            break; // leave the chain walk (a C break, never a dispatch)
          }
        }
        if (inherited != NULL) {
          if (inherited->pub && !is_pub) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static method '%s' overrides a pub static and must be pub.",
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          if (inherited->pub) {
            pub = true;
          }
          const object_function* fu = closure->function;
          if (inherited->arity != fu->arity || inherited->closure->function->ndefaults != fu->ndefaults ||
              inherited->closure->function->has_varargs != fu->has_varargs) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static method '%s' overrides a static with a different signature.",
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
        }
      }
      method_entry entry;
      entry.closure = closure;
      entry.native = NULL;
      entry.name = name;
      entry.arity = closure->function->arity;
      entry.owner = klass;
      entry.is_native = 0;
      entry.pub = pub;
      if (!method_map_set(&klass->statics, name, entry)) {
        runtime_oom(p_vm, "out of memory: failed to add static method.");
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_DEFINE_STATIC_FIELD): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const bool is_mut = Q_of(words);
      const bool is_pub = R_of(words);
      if (!IS_VALUE_CLASS(REG_AT(A)) || !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt static field definition.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(REG_AT(A));
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      const object_string* previous_owner = NULL;
      if (!object_class_add_static_field(klass, name, is_mut, is_pub, REG_AT(B), &previous_owner)) {
        if (previous_owner != NULL) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "static '%s' is already declared by class '%s'.",
                        name->string.chars,
                        previous_owner->string.chars);
        } else {
          runtime_oom(p_vm, "out of memory: failed to add static field.");
        }
        FAIL_PATH();
        DISPATCH();
      }
      DISPATCH();
    }
    CASE(OP_GET_STATIC_NAME): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      value klass_value = REG_AT(B);
      if (!IS_VALUE_CLASS(klass_value) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt static field read.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(klass_value);
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      const object_class* owner = NULL;
      const field_entry* entry = object_class_find_static(klass, name, &owner);
      if (entry == NULL) {
        // the method lane (R1, statics side): a bare sibling static method
        // binds to the class receiver (`this` in a static is the class).
        // static fields win, then static methods, then globals - the same
        // ladder the instance lane bakes at freeze.
        const method_entry* method = object_class_find_static_method(klass, name);
        if (method != NULL) {
          if (!entry_visible_from(method, frame)) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static method '%s' of class '%s' is private.",
                          name->string.chars,
                          klass->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          gc_guard_up(p_vm->alloc->gc, klass_value);
          object_specs specs = {p_vm->alloc, p_vm->objects_store};
          object_bound_method* bound = create_object_bound_method(klass_value, method, &specs);
          gc_guard_down(p_vm->alloc->gc);
          if (bound == NULL) {
            runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
            FAIL_PATH();
            DISPATCH();
          }
          REG_AT(A) = OBJECT_AS_VALUE(bound);
          DISPATCH();
        }
        // the global lane: bare globals are reachable from static contexts
        // (the instance lane's freeze fallback, mirrored at runtime - the
        // statics lane swallows the name before the compiler's global run).
        const global glob = globals_store_get(p_vm->globals_store, ok_create_string_view(
                                                                         name->string.chars,
                                                                         string_get_length(&name->string),
                                                                         false));
        if (IS_GLOBAL_VALID(glob)) {
          REG_AT(A) = p_vm->globals_store->global_values.data[glob.index];
          DISPATCH();
        }
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "undefined variable: %s (class %s has no static of this name).",
                      name->string.chars,
                      klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(A) = owner->static_values.data[entry->slot];
      DISPATCH();
    }
    CASE(OP_STORE_STATIC_NAME): {
      const word2 words = JOIN_WORD(instruction);
      const bool value_is_constant = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      value klass_value = REG_AT(B);
      if (!IS_VALUE_CLASS(klass_value) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt static field store.");
        FAIL_PATH();
        DISPATCH();
      }
      object_class* klass = VALUE_AS_CLASS(klass_value);
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      const object_class* owner = NULL;
      const field_entry* entry = object_class_find_static(klass, name, &owner);
      if (entry == NULL) {
        // the method lane: storing to a static method name is the trap.
        if (object_class_find_static_method(klass, name) != NULL) {
          runtime_error(p_vm,
                        OK_EK_VALUE,
                        "cannot assign to static method '%s': methods are not assignable.",
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        // the global lane, mirroring the read's fallback.
        const global glob = globals_store_get(p_vm->globals_store, ok_create_string_view(
                                                                         name->string.chars,
                                                                         string_get_length(&name->string),
                                                                         false));
        if (IS_GLOBAL_VALID(glob)) {
          const global_id gid = p_vm->globals_store->global_descriptors.data[glob.index];
          if ((gid.flags & GLOBAL_FLAG_MUTABLE) == 0) {
            runtime_error(p_vm,
                          OK_EK_VALUE,
                          "cannot assign to global '%s': declared without 'mut'.",
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          p_vm->globals_store->global_values.data[glob.index] = value_is_constant
                                                                    ? frame->closure->function->chunk.constants
                                                                          .data[A]
                                                                    : REG_AT(A);
          DISPATCH();
        }
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "undefined variable: %s (class %s has no static of this name).",
                      name->string.chars,
                      klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      if (!entry->mut) {
        runtime_error(p_vm,
                      OK_EK_VALUE,
                      "cannot assign to static '%s': declared without 'mut'.",
                      name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      owner->static_values.data[entry->slot] = value_is_constant
                                                    ? frame->closure->function->chunk.constants.data[A]
                                                    : REG_AT(A);
      DISPATCH();
    }
    CASE(OP_GET_METHOD): {
      // the baked method lane (R1): a bare method name inside a member body,
      // proved at freeze time, resolved here against the running method's
      // DEFINING class (the body was compiled in its context) and bound to
      // `this` at register 0. the slow lane next to INVOKE's fused fast one.
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[bp])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt method read.");
        FAIL_PATH();
        DISPATCH();
      }
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[bp]);
      object_class* owner = frame->closure->owner;
      if (owner == NULL) {
        runtime_error(p_vm, OK_EK_INTERNAL, "method read outside a class member.");
        FAIL_PATH();
        DISPATCH();
      }
      const method_entry* entry = object_class_find_method(owner, name);
      if (entry == NULL) {
        // the tables are frozen; the freeze proved this exists. unreachable
        // unless the bytecode was hand-forged.
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt method read: entry vanished from a frozen table.");
        FAIL_PATH();
        DISPATCH();
      }
      if (!entry_visible_from(entry, frame)) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "method '%s' of class '%s' is private.",
                      name->string.chars,
                      entry->owner->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      gc_guard_up(p_vm->alloc->gc, self);
      object_specs specs = {p_vm->alloc, p_vm->objects_store};
      object_bound_method* bound = create_object_bound_method(self, entry, &specs);
      gc_guard_down(p_vm->alloc->gc);
      if (bound == NULL) {
        runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(ap) = OBJECT_AS_VALUE(bound);
      DISPATCH();
    }
    CASE(OP_GET_METHOD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self) ||
          !IS_VALUE_STRING(frame->closure->function->chunk.constants.data[B])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt method read.");
        FAIL_PATH();
        DISPATCH();
      }
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[B]);
      object_class* owner = frame->closure->owner;
      if (owner == NULL) {
        runtime_error(p_vm, OK_EK_INTERNAL, "method read outside a class member.");
        FAIL_PATH();
        DISPATCH();
      }
      const method_entry* entry = object_class_find_method(owner, name);
      if (entry == NULL) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt method read: entry vanished from a frozen table.");
        FAIL_PATH();
        DISPATCH();
      }
      if (!entry_visible_from(entry, frame)) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "method '%s' of class '%s' is private.",
                      name->string.chars,
                      entry->owner->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      gc_guard_up(p_vm->alloc->gc, self);
      object_specs specs = {p_vm->alloc, p_vm->objects_store};
      object_bound_method* bound = create_object_bound_method(self, entry, &specs);
      gc_guard_down(p_vm->alloc->gc);
      if (bound == NULL) {
        runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(A) = OBJECT_AS_VALUE(bound);
      DISPATCH();
    }
    CASE(OP_METHOD_NOT_ASSIGNABLE): {
      const uint16_t bp = bprime_of(instruction);
      const object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[bp]);
      runtime_error(p_vm,
                    OK_EK_VALUE,
                    "cannot assign to method '%s': methods are not assignable.",
                    name != NULL ? name->string.chars : "?");
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_METHOD_NOT_ASSIGNABLE_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[A]);
      runtime_error(p_vm,
                    OK_EK_VALUE,
                    "cannot assign to method '%s': methods are not assignable.",
                    name != NULL ? name->string.chars : "?");
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_GET_FIELD): {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self)) {
        runtime_error(p_vm, OK_EK_TYPE, "field access outside a method receiver.");
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(ap) = VALUE_AS_INSTANCE(self)->fields[bp];
      DISPATCH();
    }
    CASE(OP_GET_FIELD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self)) {
        runtime_error(p_vm, OK_EK_TYPE, "field access outside a method receiver.");
        FAIL_PATH();
        DISPATCH();
      }
      REG_AT(A) = VALUE_AS_INSTANCE(self)->fields[B];
      DISPATCH();
    }
    CASE(OP_STORE_FIELD): {
      const bool value_is_constant = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self)) {
        runtime_error(p_vm, OK_EK_TYPE, "field access outside a method receiver.");
        FAIL_PATH();
        DISPATCH();
      }
      VALUE_AS_INSTANCE(self)->fields[bp] = value_is_constant
                                                ? frame->closure->function->chunk.constants.data[ap]
                                                : REG_AT(ap);
      DISPATCH();
    }
    CASE(OP_STORE_FIELD_LONG): {
      const word2 words = JOIN_WORD(instruction);
      const bool value_is_constant = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      value self = REG_AT(0);
      if (!IS_VALUE_INSTANCE(self)) {
        runtime_error(p_vm, OK_EK_TYPE, "field access outside a method receiver.");
        FAIL_PATH();
        DISPATCH();
      }
      VALUE_AS_INSTANCE(self)->fields[B] = value_is_constant
                                               ? frame->closure->function->chunk.constants.data[A]
                                               : REG_AT(A);
      DISPATCH();
    }
    CASE(OP_FIELD_IMMUTABLE): {
      const uint16_t bp = bprime_of(instruction);
      const object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[bp]);
      runtime_error(p_vm,
                    OK_EK_VALUE,
                    "cannot assign to field '%s': declared without 'mut'.",
                    name->string.chars);
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_STORE_FIELD_G): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      value recv = REG_AT(A);
      if (!IS_VALUE_INSTANCE(recv)) {
        runtime_error(p_vm, OK_EK_TYPE, "only instances have fields.");
        FAIL_PATH();
        DISPATCH();
      }
      object_instance* inst = VALUE_AS_INSTANCE(recv);
      const object_class* recv_seg = field_defining_segment(inst->klass, C);
      const object_class* owner_seg = frame->closure->owner != NULL
                                          ? field_defining_segment(frame->closure->owner, C)
                                          : NULL;
      if (recv_seg == NULL || recv_seg != owner_seg) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "class '%s' does not share this field with the enclosing hierarchy.",
                      inst->klass->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      inst->fields[C] = REG_AT(B);
      DISPATCH();
    }
    CASE(OP_STORE_FIELD_G_NAME): {
      // the un-baked member store: the receiver is an expression, so it may
      // be an instance (a field the freeze could not bake - or chose not to)
      // or a class value (a static). name-keyed, resolved here, both lanes
      // carry the visibility ring and the mutability contract.
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      value recv = REG_AT(A);
      if (!IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "corrupt member store.");
        FAIL_PATH();
        DISPATCH();
      }
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      if (IS_VALUE_INSTANCE(recv)) {
        object_instance* inst = VALUE_AS_INSTANCE(recv);
        // OR-1: module namespaces are immutable from script - their fields are
        // the exports, sealed by the loader before anyone sees the value.
        if (inst->klass->module_namespace) {
          runtime_error(p_vm,
                        OK_EK_VALUE,
                        "module namespaces are immutable: '%s' is an export, not an assignable field.",
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        const field_entry* field = object_class_find_field(inst->klass, name);
        if (field == NULL) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "class '%s' has no field '%s'.",
                        inst->klass->name->string.chars,
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        const object_class* seg = field_defining_segment(inst->klass, field->slot);
        const object_class* caller = frame->closure->owner;
        if (!field->pub && (seg == NULL || caller == NULL || !object_class_is_a(caller, seg))) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "field '%s' of class '%s' is not visible here.",
                        name->string.chars,
                        inst->klass->name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        if (!field->mut) {
          runtime_error(p_vm,
                        OK_EK_VALUE,
                        "cannot assign to field '%s': declared without 'mut'.",
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        inst->fields[field->slot] = REG_AT(B);
        DISPATCH();
      }
      if (IS_VALUE_CLASS(recv)) {
        object_class* klass = VALUE_AS_CLASS(recv);
        const object_class* owner = NULL;
        const field_entry* statik = object_class_find_static(klass, name, &owner);
        if (statik == NULL) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "class '%s' has no static '%s'.",
                        klass->name->string.chars,
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        const object_class* caller = frame->closure->owner;
        if (!statik->pub && (caller == NULL || !object_class_is_a(caller, owner))) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "static '%s' of class '%s' is not visible here.",
                        name->string.chars,
                        owner->name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        if (!statik->mut) {
          runtime_error(p_vm,
                        OK_EK_VALUE,
                        "cannot assign to static '%s': declared without 'mut'.",
                        name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        owner->static_values.data[statik->slot] = REG_AT(B);
        DISPATCH();
      }
      runtime_error(p_vm, OK_EK_TYPE, "only instances and classes have assignable members.");
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_FIELD_IMMUTABLE_G): {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t C = C_of(words);
      const object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      runtime_error(p_vm,
                    OK_EK_VALUE,
                    "cannot assign to field '%s': declared without 'mut'.",
                    name->string.chars);
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_GET_MEMBER): {
      const word2 words = JOIN_WORD(instruction);
      const word mic_word = READ_WORD();
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const uint16_t mic_at = (uint16_t)mic_word;
      value recv = REG_AT(B);
      object_instance* inst = NULL;
      object_class* klass = NULL;
      const bool recv_is_class = IS_VALUE_CLASS(recv);
      if (IS_VALUE_INSTANCE(recv)) {
        inst = VALUE_AS_INSTANCE(recv);
        klass = inst->klass;
      } else if (recv_is_class) {
        klass = VALUE_AS_CLASS(recv);
      } else {
        // the native lane (§11): a builtin receiver dispatches on its class
        // singleton - `a.len()` finds the native entry on the array class
        klass = vm_classof(p_vm, recv);
      }
      chunk* chunk = &frame->closure->function->chunk;
      if (mic_at >= chunk->mic.count) {
        runtime_error(p_vm, OK_EK_INTERNAL, "member cache index out of range.");
        FAIL_PATH();
        DISPATCH();
      }
      mic_cache* cache = &chunk->mic.data[mic_at];
      mic_entry* hit = NULL;
      for (uint32_t w = 0; w < OK_MIC_WAYS; ++w) {
        if (cache->ways[w].klass == OBJECT_AS_VALUE(klass) &&
            cache->ways[w].recv_is_class == (recv_is_class ? 1u : 0u)) {
          hit = &cache->ways[w];
          break; // leave the probe loop (a C break, never a dispatch)
        }
      }
      if (hit != NULL) {
        if (hit->is_field) {
          if (hit->recv_is_class) {
            // static fields never cache: a stale lane flip means corruption.
            runtime_error(p_vm, OK_EK_INTERNAL, "corrupt member cache lane.");
            FAIL_PATH();
            DISPATCH();
          }
          REG_AT(A) = inst->fields[hit->field_slot];
          DISPATCH();
        }
        // a cached method entry: the bind
        gc_guard_up(p_vm->alloc->gc, recv);
        object_specs specs = {p_vm->alloc, p_vm->objects_store};
        object_bound_method* bound = create_object_bound_method(recv, hit->method, &specs);
        gc_guard_down(p_vm->alloc->gc);
        if (bound == NULL) {
          runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
          FAIL_PATH();
          DISPATCH();
        }
        REG_AT(A) = OBJECT_AS_VALUE(bound);
        DISPATCH();
      }
      if (!IS_VALUE_STRING(chunk->constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "member name constant is not a string.");
        FAIL_PATH();
        DISPATCH();
      }
      object_string* name = VALUE_AS_STRING(chunk->constants.data[C]);
      if (recv_is_class) {
        // the statics lane. static fields walk uncached: the slot indexes the
        // DEFINING class's storage, which this mic slot cannot carry (baking
        // is the ladder step, when a profile asks for it).
        const object_class* owner = NULL;
        const field_entry* sf = object_class_find_static(klass, name, &owner);
        if (sf != NULL) {
          // the field doctrine: statics are hierarchy-internal unless pub
          // (patch 51), the ring is the defining class and everything below it.
          const object_class* caller = frame->closure->owner;
          if (!sf->pub && (caller == NULL || !object_class_is_a(caller, owner))) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static '%s' of class '%s' is not visible here.",
                          name->string.chars,
                          owner->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          REG_AT(A) = owner->static_values.data[sf->slot];
          DISPATCH();
        }
        const method_entry* entry = object_class_find_static_method(klass, name);
        if (entry != NULL) {
          if (!entry_visible_from(entry, frame)) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static method '%s' of class '%s' is private.",
                          name->string.chars,
                          klass->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          const mic_entry filled = {recv, entry, 0, 0, 1};
          mic_fill(cache, &filled);
          gc_guard_up(p_vm->alloc->gc, recv);
          object_specs specs = {p_vm->alloc, p_vm->objects_store};
          object_bound_method* bound = create_object_bound_method(recv, entry, &specs);
          gc_guard_down(p_vm->alloc->gc);
          if (bound == NULL) {
            runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
            FAIL_PATH();
            DISPATCH();
          }
          REG_AT(A) = OBJECT_AS_VALUE(bound);
          DISPATCH();
        }
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "class '%s' has no static '%s'.",
                      klass->name->string.chars,
                      name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      const field_entry* field = object_class_find_field(klass, name);
      if (field != NULL) {
        // OR-1: a module namespace's fields are its exports - public by
        // doctrine (M9); the visibility ring does not apply.
        if (!klass->module_namespace) {
          // visibility: pub fields are the surface (patch 51); everything
          // else needs the calling hierarchy to contain the field's defining
          // segment (class-wide privacy, the C++ ring)
          const object_class* seg = field_defining_segment(klass, field->slot);
          const object_class* caller = frame->closure->owner;
          if (!field->pub && (seg == NULL || caller == NULL || !object_class_is_a(caller, seg))) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "field '%s' of class '%s' is not visible here.",
                          name->string.chars,
                          klass->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
        }
        const mic_entry filled = {OBJECT_AS_VALUE(klass), NULL, field->slot, 1, 0};
        mic_fill(cache, &filled);
        REG_AT(A) = inst->fields[field->slot];
        DISPATCH();
      }
      const method_entry* entry = object_class_find_method(klass, name);
      if (entry != NULL) {
        if (!entry_visible_from(entry, frame)) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "method '%s' of class '%s' is private.",
                        name->string.chars,
                        klass->name->string.chars);
          FAIL_PATH();
          DISPATCH();
        }
        const mic_entry filled = {OBJECT_AS_VALUE(klass), entry, 0, 0, 0};
        mic_fill(cache, &filled);
        gc_guard_up(p_vm->alloc->gc, recv);
        object_specs specs = {p_vm->alloc, p_vm->objects_store};
        object_bound_method* bound = create_object_bound_method(recv, entry, &specs);
        gc_guard_down(p_vm->alloc->gc);
        if (bound == NULL) {
          runtime_oom(p_vm, "out of memory: failed to allocate a bound method.");
          FAIL_PATH();
          DISPATCH();
        }
        REG_AT(A) = OBJECT_AS_VALUE(bound);
        DISPATCH();
      }
      if (klass->module_namespace) {
        // OR-1: the teaching miss for module namespaces.
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "module '%s' has no export '%s'.",
                      klass->name->string.chars,
                      name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      runtime_error(p_vm,
                    OK_EK_TYPE,
                    "class '%s' has no field or method '%s'.",
                    klass->name->string.chars,
                    name->string.chars);
      FAIL_PATH();
      DISPATCH();
    }
    CASE(OP_INVOKE): {
      const word2 words = JOIN_WORD(instruction);
      const bool takes_one = p_of(instruction);
      const word mic_word = READ_WORD();
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const uint16_t mic_at = (uint16_t)mic_word;
      value recv = REG_AT(A);
      const method_entry* entry = NULL;
      object_class* klass = NULL;
      object_instance* inst = NULL;
      const bool recv_is_class = IS_VALUE_CLASS(recv);
      if (IS_VALUE_INSTANCE(recv)) {
        inst = VALUE_AS_INSTANCE(recv);
        klass = inst->klass;
      } else if (recv_is_class) {
        // the statics lane: Foo.m(args) is the same INVOKE with the class
        // value as its receiver, and Foo(args) rides OP_CALL's class arm.
        klass = VALUE_AS_CLASS(recv);
      } else {
        // the native lane (§11): a builtin receiver dispatches on its class
        // singleton. the builtin tables are sealed (C15), so the mic below
        // stays sound; a value whose class carries no such method gets the
        // ordinary "has no method" error, naming the class.
        klass = vm_classof(p_vm, recv);
      }
      chunk* chunk = &frame->closure->function->chunk;
      if (mic_at >= chunk->mic.count) {
        runtime_error(p_vm, OK_EK_INTERNAL, "invoke cache index out of range.");
        FAIL_PATH();
        DISPATCH();
      }
      mic_cache* cache = &chunk->mic.data[mic_at];
      mic_entry* hit = NULL;
      for (uint32_t w = 0; w < OK_MIC_WAYS; ++w) {
        if (cache->ways[w].klass == OBJECT_AS_VALUE(klass) &&
            cache->ways[w].recv_is_class == (recv_is_class ? 1u : 0u)) {
          hit = &cache->ways[w];
          break; // leave the probe loop (a C break, never a dispatch)
        }
      }
      if (hit != NULL) {
        if (hit->is_field) {
          if (hit->recv_is_class) {
            runtime_error(p_vm, OK_EK_INTERNAL, "corrupt invoke cache lane.");
            FAIL_PATH();
            DISPATCH();
          }
          // a cached field slot holding a callable: the other thing
          // obj.name(args) can mean. re-read and re-check every time - the
          // value in a mut field can change between calls.
          entry = NULL;
          value callable = inst->fields[hit->field_slot];
          if (IS_VALUE_CLOSURE(callable)) {
            REG_AT(A) = callable;
            frame->ip = ip;
            if (!call(p_vm, exe, reg, VALUE_AS_CLOSURE(callable), frame->registers_base + A, B,
                      takes_one ? 1u : 0u)) {
              FAIL_PATH();
              DISPATCH();
            }
            exe = p_vm->current_execution_state;
            reg = &exe->registers;
            frame = &exe->call_stack.data[exe->call_stack.count - 1];
            ip = frame->ip;
            DISPATCH();
          }
          // OR-1: a module namespace's export can be any callable value - a
          // class (instantiation), later a native (OR-3). the general call
          // lane takes them; closures took the fast path above.
          if (inst->klass->module_namespace) {
            REG_AT(A) = callable;
            frame->ip = ip;
            if (!call_value(p_vm, exe, reg, frame->registers_base, A, B, takes_one ? 1u : 0u)) {
              FAIL_PATH();
              DISPATCH();
            }
            exe = p_vm->current_execution_state;
            reg = &exe->registers;
            frame = &exe->call_stack.data[exe->call_stack.count - 1];
            ip = frame->ip;
            DISPATCH();
          }
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "field holds a value that is not callable in this position.");
          FAIL_PATH();
          DISPATCH();
        }
        entry = hit->method;
      } else {
        if (!IS_VALUE_STRING(chunk->constants.data[C])) {
          runtime_error(p_vm, OK_EK_INTERNAL, "method name constant is not a string.");
          FAIL_PATH();
          DISPATCH();
        }
        object_string* name = VALUE_AS_STRING(chunk->constants.data[C]);
        if (recv_is_class) {
          entry = object_class_find_static_method(klass, name);
          if (entry == NULL) {
            // the call operator on a class with no override is the default
            // construction - the one name that means "build me one".
            if (p_vm->static_call_name != NULL &&
                string_get_length(&name->string) == string_get_length(&p_vm->static_call_name->string) &&
                memcmp(name->string.chars, p_vm->static_call_name->string.chars,
                       string_get_length(&name->string)) == 0) {
              frame->ip = ip;
              if (!construct_instance(p_vm, klass, frame->registers_base + A, B)) {
                FAIL_PATH();
                DISPATCH();
              }
              exe = p_vm->current_execution_state;
              reg = &exe->registers;
              frame = &exe->call_stack.data[exe->call_stack.count - 1];
              ip = frame->ip;
              DISPATCH();
            }
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "class '%s' has no static method '%s'.",
                          klass->name->string.chars,
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          if (!entry_visible_from(entry, frame)) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "static method '%s' of class '%s' is private.",
                          name->string.chars,
                          klass->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          const mic_entry filled = {recv, entry, 0, 0, 1};
          mic_fill(cache, &filled);
        } else {
          entry = object_class_find_method(klass, name);
          if (entry == NULL) {
            // the field fallback: a field holding a callable. visibility
            // follows the field doctrine (the ring), a non-callable value is
            // a type error, and the cache holds the slot.
            const field_entry* field = object_class_find_field(klass, name);
            const object_class* caller = frame->closure->owner;
            const object_class* seg = field != NULL ? field_defining_segment(klass, field->slot) : NULL;
            // OR-1: a module namespace's exports are public (M9) - the ring
            // does not apply; `mod.export(args)` is the hot import path.
            // patch 51: a pub field is the surface everywhere else.
            if (field != NULL &&
                (field->pub || klass->module_namespace ||
                 (seg != NULL && caller != NULL && object_class_is_a(caller, seg)))) {
              value callable = inst->fields[field->slot];
              if (IS_VALUE_CLOSURE(callable)) {
                const mic_entry filled = {OBJECT_AS_VALUE(klass), NULL, field->slot, 1, 0};
                mic_fill(cache, &filled);
                REG_AT(A) = callable;
                frame->ip = ip;
                if (!call(p_vm, exe, reg, VALUE_AS_CLOSURE(callable), frame->registers_base + A, B,
                          takes_one ? 1u : 0u)) {
                  FAIL_PATH();
                  DISPATCH();
                }
                exe = p_vm->current_execution_state;
                reg = &exe->registers;
                frame = &exe->call_stack.data[exe->call_stack.count - 1];
                ip = frame->ip;
                DISPATCH();
              }
              // OR-1: a module namespace's export can be any callable value -
              // a class (instantiation), later a native (OR-3). the general
              // call lane takes them; closures took the fast path above. the
              // cache still holds the slot: the field is immutable, only the
              // value's kind varies per export.
              if (klass->module_namespace) {
                const mic_entry filled = {OBJECT_AS_VALUE(klass), NULL, field->slot, 1, 0};
                mic_fill(cache, &filled);
                REG_AT(A) = callable;
                frame->ip = ip;
                if (!call_value(p_vm, exe, reg, frame->registers_base, A, B, takes_one ? 1u : 0u)) {
                  FAIL_PATH();
                  DISPATCH();
                }
                exe = p_vm->current_execution_state;
                reg = &exe->registers;
                frame = &exe->call_stack.data[exe->call_stack.count - 1];
                ip = frame->ip;
                DISPATCH();
              }
              runtime_error(p_vm,
                            OK_EK_TYPE,
                            "field '%s' of class '%s' holds a value that is not callable.",
                            name->string.chars,
                            klass->name->string.chars);
              FAIL_PATH();
              DISPATCH();
            }
            if (klass->module_namespace) {
              // OR-1: the teaching miss for module namespaces.
              runtime_error(p_vm,
                            OK_EK_TYPE,
                            "module '%s' has no export '%s'.",
                            klass->name->string.chars,
                            name->string.chars);
              FAIL_PATH();
              DISPATCH();
            }
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "class '%s' has no method '%s'.",
                          klass->name->string.chars,
                          name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          if (!entry_visible_from(entry, frame)) {
            runtime_error(p_vm,
                          OK_EK_TYPE,
                          "method '%s' of class '%s' is private.",
                          name->string.chars,
                          klass->name->string.chars);
            FAIL_PATH();
            DISPATCH();
          }
          const mic_entry filled = {OBJECT_AS_VALUE(klass), entry, 0, 0, 0};
          mic_fill(cache, &filled);
        }
      }
      if (entry->is_native) {
        // the native method flight: the window already holds the receiver at
        // r[A] with the arguments above - exactly the argv shape. no frame is
        // pushed, but the native may re-enter the vm and realloc the call
        // stack and register file, so the cached locals refresh after.
        frame->ip = ip;
        if (!call_native_method(p_vm, exe, reg, entry, frame->registers_base + A, B)) {
          FAIL_PATH();
          DISPATCH();
        }
        exe = p_vm->current_execution_state;
        reg = &exe->registers;
        frame = &exe->call_stack.data[exe->call_stack.count - 1];
        ip = frame->ip;
        if (exe->native_suspend_pending) {
          return native_suspend_park(exe, reg, frame, A);
        }
        DISPATCH();
      }
      frame->ip = ip;
      if (!call(p_vm, exe, reg, entry->closure, frame->registers_base + A, B, takes_one ? 1u : 0u)) {
        FAIL_PATH();
        DISPATCH();
      }
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      DISPATCH();
    }
    CASE(OP_SUPER_INVOKE): {
      const word2 words = JOIN_WORD(instruction);
      const bool takes_one = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      object_class* owner = frame->closure->owner;
      if (owner == NULL || owner->parent == NULL) {
        runtime_error(p_vm, OK_EK_TYPE, "'super' outside a class hierarchy.");
        FAIL_PATH();
        DISPATCH();
      }
      if (!IS_VALUE_STRING(frame->closure->function->chunk.constants.data[C])) {
        runtime_error(p_vm, OK_EK_INTERNAL, "method name constant is not a string.");
        FAIL_PATH();
        DISPATCH();
      }
      object_string* name = VALUE_AS_STRING(frame->closure->function->chunk.constants.data[C]);
      // the receiver at register 0 picks the lane: an instance walks the
      // parent's methods, a class value walks the parent's statics.
      const bool static_lane = IS_VALUE_CLASS(REG_AT(0));
      const method_entry* entry =
          static_lane ? object_class_find_static_method(owner->parent, name)
                      : object_class_find_method(owner->parent, name);
      if (entry == NULL) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "no %s '%s' in the super chain of class '%s'.",
                      static_lane ? "static" : "method",
                      name->string.chars,
                      owner->name->string.chars);
        FAIL_PATH();
        DISPATCH();
      }
      if (entry->is_native) {
        // same window shape as the closure lane: receiver (register 0)
        // staged at the call window base, arguments above it.
        REG_AT(A) = REG_AT(0);
        frame->ip = ip;
        if (!call_native_method(p_vm, exe, reg, entry, frame->registers_base + A, B)) {
          FAIL_PATH();
          DISPATCH();
        }
        exe = p_vm->current_execution_state;
        reg = &exe->registers;
        frame = &exe->call_stack.data[exe->call_stack.count - 1];
        ip = frame->ip;
        if (exe->native_suspend_pending) {
          return native_suspend_park(exe, reg, frame, A);
        }
        DISPATCH();
      }
      // stage the receiver (register 0) as the call window base, then the
      // ordinary call machinery owns the rest
      REG_AT(A) = REG_AT(0);
      frame->ip = ip;
      if (!call(p_vm, exe, reg, entry->closure, frame->registers_base + A, B, takes_one ? 1u : 0u)) {
        FAIL_PATH();
        DISPATCH();
      }
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      DISPATCH();
    }
#if !defined(OK_DISPATCH_THREADED)
    } // switch
#endif
  next_instruction:; // FAIL_PATH resumes here: the catch's first instruction is next
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BIN_OP
#undef FAIL_PATH
#undef CASE
#undef DISPATCH
}

// raise (catchable): stash kind + message into the error lane. no printing,
// no reset, no allocation, those happen where the live ip and the call
// stack still exist together (FAIL_PATH in the dispatch loop, which first
// tries the unwind to a handler). a catch may yet consume this error, so
// nothing observable may happen at raise time.
void runtime_error(vm* p_vm, ok_error_kind p_kind, const char* p_fmt, ...) {
  execution_state* exe = p_vm->current_execution_state;
  va_list ap;
  va_start(ap, p_fmt);
  vsnprintf(exe->error_what, sizeof(exe->error_what), p_fmt, ap);
  va_end(ap);
  exe->error_kind = p_kind;
  exe->error_is_script = false;
  exe->error_hard = false;
  exe->error_pending = true;
  exe->error_reported = false;
  exe->has_error = true;
}

// raise (hard): the oom lane. out of memory is not a value and not
// catchable, prints immediately (from the live state, the report
// path itself must not allocate) and resets: nothing may resume.
void runtime_oom(vm* p_vm, const char* p_fmt, ...) {
  execution_state* exe = p_vm->current_execution_state;
  va_list ap;
  va_start(ap, p_fmt);
  vsnprintf(exe->error_what, sizeof(exe->error_what), p_fmt, ap);
  va_end(ap);
  exe->error_kind = OK_EK_INTERNAL;
  exe->error_is_script = false;
  exe->error_hard = true;
  exe->error_pending = true;
  exe->has_error = true;
  // no print here: the raising arm's FAIL_PATH prints hard errors itself,
  // from the live state, with the instruction's synced ip
  // (printing here would read a stale frame->ip, the arm hasn't synced yet).
}

// the report's value half: a script `throw v` prints the value itself, in
// the CLI's print formatting (numbers %g, true/false/null, raw string bytes,
// per-type tags for other objects, the print hook's fallback lane). writes
// into a caller buffer: the report path must not allocate.
static int format_error_value(char* p_out, size_t p_capacity, value p_value) {
  if (IS_VALUE_NUMBER(p_value)) {
    return snprintf(p_out, p_capacity, "%g", VALUE_AS_NUMBER(p_value));
  }
  if (IS_VALUE_BOOL(p_value)) {
    return snprintf(p_out, p_capacity, "%s", VALUE_AS_BOOL(p_value) ? "true" : "false");
  }
  if (IS_VALUE_STRICTLY_NULL(p_value)) {
    return snprintf(p_out, p_capacity, "null");
  }
  if (IS_VALUE_STRICTLY_OBJECT(p_value)) {
    switch (object_get_type(VALUE_AS_OBJECT(p_value))) {
    case OBJ_STRING:
      return snprintf(p_out, p_capacity, "%s", ((object_string*)VALUE_AS_OBJECT(p_value))->string.chars);
    case OBJ_FUNCTION:
      return snprintf(p_out, p_capacity, "<fu>");
    case OBJ_CLOSURE:
      return snprintf(p_out, p_capacity, "<closure>");
    case OBJ_ARRAY:
      return snprintf(p_out, p_capacity, "<array>");
    case OBJ_MAP:
      return snprintf(p_out, p_capacity, "<map>");
    case OBJ_NATIVE_FUNCTION:
      return snprintf(p_out, p_capacity, "<native function>");
    case OBJ_NATIVE_VALUE:
      return snprintf(p_out, p_capacity, "<native value>");
    default:
      return snprintf(p_out, p_capacity, "<object>");
    }
  }
  return snprintf(p_out, p_capacity, "null");
}

// report: the single point a runtime error prints. idempotent
// via error_reported, an error that crossed a native ok_call hop reports
// once (at the innermost FAIL_PATH that still sees the frames) and the
// outer levels propagate silently. resetting closes every open upvalue
// first: an error-path frame's captured locals must not stay open pointing
// into a register file the next run reuses (stale-open-upvalue bug).
static interpret_result __attribute__((cold)) vm_report_error(vm* p_vm) {
  execution_state* exe = p_vm->current_execution_state;
  interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
  if (!exe->error_pending || exe->error_reported) {
    return result;
  }
  exe->error_reported = true;

  {
    bool panic = false;
    bool had_error = false;
    // a script `throw v` reports the value itself ("uncaught error: <value>");
    // a runtime-raised error reports its message text. both render through
    // okdiag so the header and (when the source text is alive) the snippet stay one format.
    char value_text[320];
    const char* text = exe->error_what;
    if (exe->error_is_script) {
      char formatted[256];
      format_error_value(formatted, sizeof(formatted), exe->error_value);
      snprintf(value_text, sizeof(value_text), "uncaught error: %s", formatted);
      text = value_text;
    }
    const ok_string_view message = ok_create_string_view(text, OK_STRING_VIEW_CALCULATE_LENGTH, true);

    if (exe->call_stack.count > 0) {
      call_frame* frame = &exe->call_stack.data[exe->call_stack.count - 1];
      size_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;
      ok_line_info_repeated* info = chunk_find_line_info(&frame->closure->function->chunk, instruction);
      // the source is the frame's (by chunk source id), not "the current
      // run's": an A-compiled function still callable during source B's run
      // reports A's path with A's line numbers. excerpts only when the text
      // is alive (retained, or mid-run borrow).
      const vm_source_entry* entry = vm_source_of(p_vm, frame->closure->function->chunk.source_id);
      if (info != NULL && entry != NULL && entry->code != NULL) {
        // the span is the token that authored the raising instruction: one
        // caret on it. the snippet comes from the source text the renderer
        // reads, the old code printed the token text as the line.
        const ok_span span = {.line = info->line_info.line,
                              .column = info->line_info.column,
                              .offset = info->line_info.offset,
                              .length = 1};
        okdiag_report(&panic, &had_error, entry->path, entry->code, &span, false, REPORT_SEVERITY_ERROR, message, NULL);
      } else if (info != NULL && entry != NULL) {
        // right path + right line:col, no excerpt: the borrowed code expired
        // (degraded, never wrong).
        const ok_span span = {.line = info->line_info.line,
                              .column = info->line_info.column,
                              .offset = info->line_info.offset,
                              .length = 1};
        okdiag_report(&panic, &had_error, entry->path, NULL, &span, false, REPORT_SEVERITY_ERROR, message, NULL);
      } else {
        okdiag_report(&panic, &had_error, "", NULL, NULL, false, REPORT_SEVERITY_ERROR, message, NULL);
      }
    } else {
      okdiag_report(&panic, &had_error, "", NULL, NULL, false, REPORT_SEVERITY_ERROR, message, NULL);
    }

    if (exe->call_stack.count > 0) {
      fputs("\nstack trace (most recent call last):\n", stderr);
      for (uint32_t i = 0; i < exe->call_stack.count; ++i) {
        call_frame* frame = &exe->call_stack.data[i];
        uint32_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;
        ok_line_info_repeated* info = chunk_find_line_info(&frame->closure->function->chunk, instruction);
        const vm_source_entry* entry = vm_source_of(p_vm, frame->closure->function->chunk.source_id);
        if (info != NULL && entry != NULL) {
          fprintf(stderr,
                  "%s:%u:%u in %s\n",
                  entry->path,
                  info->line_info.line,
                  info->line_info.column,
                  frame->closure->function->name->string.chars);
        } else {
          fprintf(stderr,
                  "%s in %s\n",
                  entry != NULL ? entry->path : "<host>",
                  frame->closure->function->name->string.chars);
        }
      }
    }
  }
  return result;
}

// fonal reset: only at the outermost C boundary, when the error is
// uncaught everywhere. closing every open upvalue first, an error-path
// frame's captured locals must not stay open pointing into a register file
// the next run reuses (stale-open-upvalue bug, found while building this lane).
// the handler stack empties with the frames it tags.
// the uncaught defer sweep runs first: every frame's pending defers, innermost
// frame first, descending registers within a frame, cleanup on the way out
// is defer's whole job, and the report already printed (the sweep's stdout
// lands after it). a defer failing during the sweep does not stop it: every
// chain completes, and the lane keeps the FIRST failing defer's error
// (ok_last_error reads the replacement). oom never sweeps (the hard lane).
static void reset_error_state(vm* p_vm) {
  execution_state* exe = p_vm->current_execution_state;
  if (!exe->error_hard && exe->call_stack.count > 0) {
    // the chronology marker, once for the whole sweep: an uncaught error that
    // was raised by a defer (a normal-exit defer failing with no handler
    // above it) is the chain's first - the sweep's own failures drop against it
    bool in_flight_is_defer = false;
    for (uint32_t f = exe->call_stack.count; f-- > 0;) {
      if (exe->call_stack.data[f].defer_thunk) {
        in_flight_is_defer = true;
        break;
      }
    }
    for (uint32_t f = exe->call_stack.count; f-- > 0;) {
      call_frame* fr = &exe->call_stack.data[f];
      (void)run_pending_defers_ex(p_vm, fr->registers_base, fr->registers_base + fr->closure->function->registers, in_flight_is_defer);
      // every frame's chain completes (continuation); a failure's first error
      // stays in the lane for ok_last_error. the teardown below is unconditional
    }
  }
  close_upvalues(exe, 0);
  exe->registers.top = 0;
  exe->call_stack.count = 0;
  exe->handlers.count = 0;
}

static inline bool call(vm* p_vm,
                        execution_state* p_exe,
                        register_file* p_reg,
                        object_closure* p_closure,
                        uint32_t p_callee,
                        uint16_t p_argc,
                        uint32_t p_retc) {
  const object_function* fu = p_closure->function;
  // varargs: `fu f(a, rest...)` the varargs parameter never
  // requires an argument (it defaults to an empty array), so the floor drops
  // it. and there is no ceiling: the trailing arguments are the rest's, as
  // many as the caller staged. non-varargs functions keep the exact old two-sided check.
  if (fu->has_varargs) {
    const uint32_t floor = (uint32_t)(fu->arity - fu->ndefaults) - 1u;
    if (p_argc < floor) {
      runtime_error(p_vm,
                    OK_EK_ARITY,
                    "invalid call to '%s': expected at least %u argument%s, got %u.",
                    fu->name->string.chars,
                    floor,
                    s_if_plural(floor),
                    p_argc);
      return false;
    }
  } else if (p_argc < (uint32_t)(fu->arity - fu->ndefaults) || p_argc > fu->arity) {
    if (fu->ndefaults == 0) {
      runtime_error(p_vm,
                    OK_EK_ARITY,
                    "invalid call to '%s': expected %u argument%s, got %u.",
                    fu->name->string.chars,
                    fu->arity,
                    s_if_plural(fu->arity),
                    p_argc);
    } else if (p_argc < fu->arity - fu->ndefaults) {
      runtime_error(p_vm,
                    OK_EK_ARITY,
                    "invalid call to '%s': expected at least %u argument%s, got %u.",
                    fu->name->string.chars,
                    fu->arity - fu->ndefaults,
                    s_if_plural(fu->arity - fu->ndefaults),
                    p_argc);
    } else {
      runtime_error(p_vm,
                    OK_EK_ARITY,
                    "invalid call to '%s': expected at most %u argument%s, got %u.",
                    fu->name->string.chars,
                    fu->arity,
                    s_if_plural(fu->arity),
                    p_argc);
    }
    return false;
  }
  // no adjust to receiver: a caller that requested a specific
  // count must get exactly the function's inferred result span. destructuring
  // 2 results into 1 binding, or reading a 2-result call in single-expression
  // position, errors here. no lua-style silent take-first or padding. the
  // retc == 0 lane is the discard (statement calls, the top-level entry):
  // no check, the results are dropped. the dynamic lane (ok_call) takes all.
  if (p_retc != RETURN_COUNT_DYNAMIC && p_retc >= 1 && p_retc != (uint32_t)fu->results) {
    runtime_error(p_vm,
                  OK_EK_ARITY,
                  "invalid call to '%s': it returns %u value%s, %u were requested here.",
                  fu->name->string.chars,
                  fu->results,
                  s_if_plural(fu->results),
                  p_retc);
    return false;
  }
  // a varargs frame is as tall as its arguments. the trailing
  // arguments live at [callee + arity, callee + argc], past the compile-time
  // register count. the body's locals overlap them by design: VA_PACK reads
  // the window before any local is written (locals are defined before use,
  // the uninit discipline), and mark_vm roots [0, top) either way.
  uint32_t frame_top = p_callee + p_closure->function->registers;
  if (p_closure->function->has_varargs && (uint32_t)p_argc + 1u > frame_top) {
    frame_top = p_callee + p_argc + 1u;
  }
  if (frame_top > UINT16_MAX + 1u) {
    // the register file is 16-bit-addressable: a varargs call can stage more
    // arguments than the file has slots. hard error, not a clamp. a clamp
    // would silently truncate the window VA_PACK reads.
    runtime_error(p_vm, OK_EK_ARITY, "invalid call to '%s': too many arguments.", fu->name->string.chars);
    return false;
  }
  if (frame_top > p_reg->array.capacity) {
    // grow with headroom so we do not realloc on every nested call
    const uint32_t target = frame_top + (frame_top >> 1u);
    const uint32_t new_capacity = target > UINT16_MAX + 1u ? UINT16_MAX + 1u : target;
    // the register file is written directly (data[i] = ...), never appended to,
    // so array.count stays 0 and is not the initialized watermark. everything in
    // [0, capacity) is: either nulled by register_file_init_warm or by a previous
    // grow. null-filling from 0 here would wipe the caller's live registers.
    const uint32_t initialized_uptill = p_reg->array.capacity;
    if (!register_array_grow(&p_reg->array, new_capacity)) {
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return false;
    }
    // fresh registers must read as null: realloc'd memory is garbage
    for (uint32_t i = initialized_uptill; i < new_capacity; ++i) {
      p_reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  // the callee's window past the staged args is not written by the call: those
  // slots keep bits from earlier frames that used the same region. mark_vm
  // roots [0, top) and top just rose to frame_top, so a collection before the
  // callee assigns its locals would walk those stale pointers as objects.
  // use after free when the object was already swept (aggressive GC / ASan;
  // plain builds read the allocator's free list memory and corrupt the mark).
  // null everything the callee has not received yet so [0, top) only ever
  // holds live values or explicitly tagged nulls.
  {
    uint32_t first_unwritten = p_callee + p_argc + 1;
    if (first_unwritten > frame_top) {
      first_unwritten = frame_top;
    }
    for (uint32_t i = first_unwritten; i < frame_top; ++i) {
      p_reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  p_reg->top = frame_top;
  call_frame frame;
  frame.closure = p_closure;
  frame.ip = p_closure->function->chunk.code.data;
  frame.registers_base = p_callee;
  frame.return_base = p_callee;
  frame.return_count = p_retc;
  frame.argc = p_argc;
  frame.defer_thunk = false;
  return call_stack_append(&p_exe->call_stack, frame);
}

static inline bool call_native(vm* p_vm,
                               execution_state* p_exe,
                               register_file* p_reg,
                               object_native_function* p_native,
                               uint32_t p_callee,
                               uint16_t p_argc,
                               uint32_t p_retc) {
  (void)p_retc; // natives yield exactly one result; the caller's retc only trims closures
  if (p_native->arity != OK_NATIVE_ARITY_ANY && p_argc != p_native->arity) {
    runtime_error(p_vm,
                  OK_EK_ARITY,
                  "invalid call to native '%s': expected %u argument%s, got %u.",
                  p_native->name.chars,
                  p_native->arity,
                  s_if_plural(p_native->arity),
                  p_argc);
    return false;
  }
  // the native receives a pointer into the register file. if it calls back
  // into the vm and the callee's frame needs more registers than the current capacity, call() grows the
  // file with realloc and that pointer would dangle for the rest of the
  // native's execution. thus the native side iteration 2+ would read freed memory
  // when refetching its argument. thus we stage a copy instead.
  value args_stash[UINT8_MAX];
  if (p_argc > 0) {
    memcpy(args_stash, p_reg->array.data + p_callee + 1, p_argc * sizeof(value));
  }
  value result;
  if (!run_native(p_vm, p_exe, p_native->function, p_argc, args_stash, &result)) {
    return false;
  }
  gc_guard_up(p_vm->alloc->gc, result);
  // natives do not push a call frame: when invoked via ok_call the call stack is
  // empty (data[count - 1] would be a wild pointer), and when invoked from
  // bytecode the caller frame's return_base is not where this call's result
  // belongs. per OP_CALL semantics (r[a]... = r[a](r[a+1]...)) the result goes
  // to the call window start, which is the callee slot.
  register_file* reg = &p_exe->registers;
  // restore the caller's watermark, same contract as cleanup_frame: mark_vm
  // roots [0, top) and leaving top at the callee slot would unroot every
  // register above the call window for the rest of the caller's execution.
  // when there is no bytecode caller (ok_call from C on an empty stack) keep
  // the old behavior: top = window base, so the result slot doubles as the
  // staging point for the next ok_call.
  if (p_exe->call_stack.count > 0) {
    call_frame* caller = &p_exe->call_stack.data[p_exe->call_stack.count - 1];
    reg->top = caller->registers_base + caller->closure->function->registers;
  } else {
    reg->top = p_callee;
  }
  reg->array.data[p_callee] = result;
  gc_guard_down(p_vm->alloc->gc);
  return true;
}

// stage the spread array's elements into the argument window, then make the
// call. r[base + callee] is the callable, [base + callee + 1,
// base + callee + fixed] the pre-staged fixed arguments, r[base + callee +
// fixed + 1] the array to spread. the elements land at [array_at, array_at +
// len). overwriting the array's own slot as they go. the object stays alive
// through the C-side pointer under a gc guard while the copy runs.
static bool call_spread(vm* p_vm,
                        execution_state* p_exe,
                        register_file* p_reg,
                        uint32_t p_base,
                        uint16_t p_callee,
                        uint16_t p_fixed,
                        uint32_t p_retc) {
  const uint32_t array_at = p_base + p_callee + p_fixed + 1u;
  const value spread = p_reg->array.data[array_at];
  if (!IS_VALUE_ARRAY(spread)) {
    runtime_error(p_vm, OK_EK_TYPE, "can only spread an array.");
    return false;
  }
  object_array* arr = VALUE_AS_ARRAY(spread);
  const uint32_t len = arr->values.count;
  const uint32_t need = array_at + len;
  if (need > UINT16_MAX + 1u || (uint32_t)p_fixed + len > UINT16_MAX) {
    // the register file is 16-bit-addressable and argc is 16-bit: this is the
    // boundary, before any slot is written.
    runtime_error(p_vm, OK_EK_ARITY, "too many arguments to spread.");
    return false;
  }
  if (need > p_reg->array.capacity) {
    // same contract as call()'s growth: fresh slots must read as null.
    const uint32_t target = need + (need >> 1u);
    const uint32_t new_capacity = target > UINT16_MAX + 1u ? UINT16_MAX + 1u : target;
    const uint32_t initialized_uptill = p_reg->array.capacity;
    if (!register_array_grow(&p_reg->array, new_capacity)) {
      runtime_oom(p_vm, "out of memory: failed to grow register file.");
      return false;
    }
    for (uint32_t i = initialized_uptill; i < new_capacity; ++i) {
      p_reg->array.data[i] = NULL_AS_VALUE();
    }
  }
  // root the element window before anything allocates: the copied elements sit
  // at [array_at, need), possibly above the caller's top, and call() itself
  // allocates (register file growth) before raising top to cover them. every
  // slot we just raised top over holds either a copied element or an old
  // frame's dead-but-valid value. both walk fine under mark_vm.
  if (p_reg->top < need) {
    p_reg->top = need;
  }
  gc_guard_up(p_vm->alloc->gc, spread);
  for (uint32_t i = 0; i < len; ++i) {
    p_reg->array.data[array_at + i] = arr->values.data[i];
  }
  gc_guard_down(p_vm->alloc->gc);
  return call_value(p_vm, p_exe, p_reg, p_base, p_callee, (uint16_t)(p_fixed + len), p_retc);
}

static bool call_value(vm* p_vm,
                       execution_state* p_exe,
                       register_file* p_reg,
                       uint32_t p_base,
                       uint16_t p_callee,
                       uint16_t p_argc,
                       uint32_t p_retc) {
  value callee = p_reg->array.data[p_base + p_callee];
  if (!IS_VALUE_STRICTLY_OBJECT(callee)) {
    goto invalid_call;
  }
  {
    object* obj = VALUE_AS_OBJECT(callee);
    switch (object_get_type(obj)) {
    case OBJ_CLOSURE:
      return call(p_vm, p_exe, p_reg, (object_closure*)obj, p_base + p_callee, p_argc, p_retc);
    case OBJ_NATIVE_FUNCTION:
      return call_native(p_vm, p_exe, p_reg, (object_native_function*)obj, p_base + p_callee, p_argc, p_retc);
    case OBJ_BOUND_METHOD: {
      // the receiver takes the bound value's place at the window base (the
      // OP_CALL shape holds), then the entry calls through the ordinary lane
      object_bound_method* bound = (object_bound_method*)obj;
      p_reg->array.data[p_base + p_callee] = bound->receiver;
      if (bound->entry->is_native) {
        return call_native_method(p_vm, p_exe, p_reg, bound->entry, p_base + p_callee, p_argc);
      }
      return call(p_vm, p_exe, p_reg, bound->entry->closure, p_base + p_callee, p_argc, p_retc);
    }
    case OBJ_INSTANCE: {
      // calling an instance: the instance operator (), staged exactly like a
      // bound method - the instance already sits at the window base (the
      // receiver position), the arguments above it. no override means the
      // instance is not callable.
      object_class* klass = ((object_instance*)obj)->klass;
      const method_entry* entry = p_vm->op_names[CLASS_OP_CALL] != NULL
                                      ? object_class_find_operator(klass, CLASS_OP_CALL, p_vm->op_names[CLASS_OP_CALL])
                                      : NULL;
      if (entry == NULL) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "invalid call: an instance of class '%s' is not callable (no operator ()).",
                      klass->name->string.chars);
        return false;
      }
      const call_frame* caller =
          p_exe->call_stack.count > 0 ? &p_exe->call_stack.data[p_exe->call_stack.count - 1] : NULL;
      if (!entry_visible_from(entry, caller)) {
        runtime_error(p_vm,
                      OK_EK_TYPE,
                      "operator '()' of class '%s' is private.",
                      entry->owner->name->string.chars);
        return false;
      }
      if (entry->is_native) {
        // the instance already sits at the window base: the receiver-first
        // argv shape, ready to fly
        return call_native_method(p_vm, p_exe, p_reg, entry, p_base + p_callee, p_argc);
      }
      return call(p_vm, p_exe, p_reg, entry->closure, p_base + p_callee, p_argc, p_retc);
    }
    case OBJ_CLASS: {
      // calling a class value: the overridden static operator () if the
      // class declares one, otherwise the default construction. the callee
      // slot already holds the class, so a user override stages exactly like
      // any method call with the class as its receiver.
      object_class* klass = (object_class*)obj;
      const method_entry* entry =
          p_vm->static_call_name != NULL ? object_class_find_static_method(klass, p_vm->static_call_name) : NULL;
      if (entry != NULL) {
        const call_frame* caller =
            p_exe->call_stack.count > 0 ? &p_exe->call_stack.data[p_exe->call_stack.count - 1] : NULL;
        if (!entry_visible_from(entry, caller)) {
          runtime_error(p_vm,
                        OK_EK_TYPE,
                        "static method 'operator ()' of class '%s' is private.",
                        klass->name->string.chars);
          return false;
        }
        if (entry->is_native) {
          // the class value is the receiver and already sits at the callee slot
          return call_native_method(p_vm, p_exe, p_reg, entry, p_base + p_callee, p_argc);
        }
        return call(p_vm, p_exe, p_reg, entry->closure, p_base + p_callee, p_argc, p_retc);
      }
      return construct_instance(p_vm, klass, p_base + p_callee, p_argc);
    }
    default:;
    }
  }
invalid_call:
  runtime_error(p_vm, OK_EK_TYPE, "invalid call: value is not callable.");
  return false;
}

static interpret_result create_closure(
    vm* p_vm, execution_state* p_exe, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, uint16_t p_src) {
  object_function* function = VALUE_AS_FUNCTION(p_frame->closure->function->chunk.constants.data[p_src]);
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_closure* closure = create_object_closure(function, &s);
  if (closure == NULL) {
    runtime_oom(p_vm, "out of memory: failed to allocate closure object.");
    interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    return result;
  }
  // a lambda born inside a method belongs to the same class lexically: the
  // privacy checks and super resolution of its body read this at runtime.
  closure->owner = p_frame->closure->owner;
  p_reg->array.data[p_frame->registers_base + p_dest] = OBJECT_AS_VALUE(closure);
  if (function->upvalues.count > 0 && !upvalue_array_grow(&closure->upvalues, function->upvalues.count)) {
    runtime_oom(p_vm, "out of memory: failed to allocate closure upvalues.");
    interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    return result;
  }
  for (uint32_t i = 0; i < function->upvalues.count; ++i) {
    const upvalue_descriptor desc = function->upvalues.data[i];
    bool status = true;
    if (desc.is_local) {
      object_upvalue* upvalue =
          capture_upvalue(p_vm, p_vm->current_execution_state, p_frame->registers_base + desc.index);
      if (upvalue == NULL) {
        interpret_result result = {
            .status = RUNTIME_ERROR,
            .top_level_return = NULL_AS_VALUE(),
        };
        return result;
      }
      status = upvalue_array_append(&closure->upvalues, upvalue);
    } else {
      status = upvalue_array_append(&closure->upvalues, p_frame->closure->upvalues.data[desc.index]);
    }
    if (!status) {
      runtime_oom(p_vm, "out of memory: failed to allocate closure upvalue.");
      interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      return result;
    }
  }
  interpret_result good;
  good.status = RUNTIME_OK;
  return good;
}
static object_upvalue* capture_upvalue(vm* p_vm, execution_state* p_exe, uint32_t p_register) {
  object_upvalue* prev = NULL;
  object_upvalue* upvalue = p_exe->open_upvalues;
  while (upvalue != NULL && object_upvalue_get_location(upvalue) > p_register) {
    prev = upvalue;
    upvalue = upvalue->next;
  }
  if (upvalue != NULL && object_upvalue_get_location(upvalue) == p_register) {
    return upvalue;
  }
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_upvalue* captured = create_object_upvalue(p_register, &s);
  if (captured == NULL) {
    runtime_oom(p_vm, "out of memory: failed to allocate memory for upvalue.");
    return NULL;
  }
  // the owner pin: a cell captured in a coro's state resolves through the
  // coro (traced via the upvalue), a cell captured in the vm's own state
  // stays null-owner (resolved through vm->main_state)
  object_upvalue_set_owner(captured,
                           p_exe->owner_coro != NULL ? OBJECT_AS_VALUE(p_exe->owner_coro) : NULL_AS_VALUE());
  captured->next = upvalue;
  if (prev == NULL) {
    p_exe->open_upvalues = captured;
  } else {
    prev->next = captured;
  }
  return captured;
}

static void close_upvalues(execution_state* p_exe, uint32_t p_uptill) {
  while (p_exe->open_upvalues != NULL && object_upvalue_get_location(p_exe->open_upvalues) >= p_uptill) {
    object_upvalue* upvalue = p_exe->open_upvalues;
    upvalue->closed = p_exe->registers.array.data[object_upvalue_get_location(upvalue)];
    object_upvalue_set_closed(upvalue);
    object_upvalue_set_owner(upvalue, NULL_AS_VALUE()); // a snapshot needs no owner pin
    p_exe->open_upvalues = upvalue->next;
  }
}

// an open upvalue's location indexes its OWNER's register array, never the
// running state's: a coro reads cells captured in its resumer (null owner,
// the vm's own state) and cells it captured itself (the owner coro, pinned
// alive through the upvalue's trace). closed cells never touch a window.
static inline value* upvalue_cell(vm* p_vm, execution_state* p_exe, object_upvalue* p_upvalue) {
  if (object_upvalue_is_closed(p_upvalue)) {
    return &p_upvalue->closed;
  }
  value* window;
  if (IS_VALUE_CORO(p_upvalue->owner)) {
    object_coro* owner_coro = VALUE_AS_CORO(p_upvalue->owner);
    // a dead owner's cells are all closed (death closes everything), so the
    // fallback window is never actually read - it only keeps the resolve total
    window = owner_coro->state != NULL ? owner_coro->state->registers.array.data
                                       : p_vm->main_state->registers.array.data;
  } else if (p_exe == p_vm->main_state) {
    window = p_exe->registers.array.data; // the fast lane: running in the owning state
  } else {
    window = p_vm->main_state->registers.array.data;
  }
  return &window[object_upvalue_get_location(p_upvalue)];
}

ARRAY_DEFINE_DEFAULT(register_array, value, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(vm_sources, vm_source_entry, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(handler_stack, handler_record, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(call_stack, call_frame, ARRAY_DEFAULT_TYPE_DEINIT);

void register_file_init(register_file* p_registers, allocators* p_alloc) {
  p_registers->top = 0;
  register_array_init(&p_registers->array, p_alloc);
}

bool register_file_init_warm(register_file* p_registers, uint32_t p_initial_capacity, allocators* p_alloc) {
  register_file_init(p_registers, p_alloc);
  if (p_initial_capacity > 0) { // so we dont trigger free in the reallocate function.
    if (!register_array_grow(&p_registers->array, p_initial_capacity)) {
      return false;
    }
    // registers must read as null, realloc'd memory is garbage
    for (uint32_t i = 0; i < p_initial_capacity; ++i) {
      p_registers->array.data[i] = NULL_AS_VALUE();
    }
  }
  return false;
}

void register_file_deinit(register_file* p_registers) {
  allocators* alloc = p_registers->array.alloc;
  register_array_deinit(&p_registers->array, alloc);
  register_file_init(p_registers, alloc);
}
