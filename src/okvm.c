#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okvm.h"
#if defined(OK_PARANOID)
#define OK_TRACE_EXECUTION
#endif // defined(OK_PARANOID)
#if defined(OK_TRACE_EXECUTION)
#include "okdebug.h"
#endif // defined(OK_TRACE_EXECUTION)

#include "okarray.h"
#include "okgc.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okutils.h"

#define STACK_SIZE 256

static void runtime_error(vm* p_vm, const char* p_fmt, ...);
static bool call_value(vm* p_vm,
                       execution_state* p_exe,
                       register_file* p_reg,
                       uint32_t p_base,
                       uint16_t p_callee,
                       uint16_t p_argc,
                       uint32_t p_retc);
static object_upvalue* capture_upvalue(vm* p_vm, execution_state* p_exe, uint32_t p_register);
static void close_upvalues(execution_state* p_exe, uint32_t p_uptill);
static interpret_result run_until(vm* p_vm, uint32_t p_call_depth);
static interpret_result create_closure(
    vm* p_vm, execution_state* p_exe, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, uint16_t p_src);
void interpret_result_deinit(interpret_result* p_interpret_result) {
}

// the flag must stay visible to enclosing call_native frames while a native
// is still unwinding (native_call_depth > 0). only the outermost C entry
// point (depth == 0) consumes it so a subsequent ok_call starts clean.
static inline void vm_consume_pending_error(execution_state* p_exe) {
  if (p_exe->native_call_depth == 0) {
    p_exe->error_pending = false;
  }
}

// a runtime error fired somewhere under the current C entry point. callers
// that resume interpretation after the error point (call_native returning to
// an OP_CALL handler) must check this and fail instead: runtime_error already
// reset the call stack + watermark, and continuing would run bytecode against
// a wild frame pointer.
static interpret_result vm_error(execution_state* p_exe) {
  vm_consume_pending_error(p_exe);
  interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
  return result;
}

void vm_init(vm* p_vm, vm_specs p_specs) {
  p_vm->alloc = p_specs.alloc;
  p_vm->objects_store = p_specs.objects_store;
  p_vm->globals_store = p_specs.globals_store;
  p_vm->source = NULL;
  p_vm->ok = p_specs.ok;
  // TODO: temp
  p_vm->current_execution_state = p_vm->alloc->allocate(p_vm->alloc, sizeof(execution_state));
  p_vm->current_execution_state->open_upvalues = NULL;
  p_vm->current_execution_state->native_has_error = false;
  p_vm->current_execution_state->error_pending = false;
  p_vm->current_execution_state->native_call_depth = 0;
  string_init(&p_vm->current_execution_state->native_error_message, NULL, 0, false, p_vm->alloc);
  register_file_init_warm(&p_vm->current_execution_state->registers, STACK_SIZE, p_specs.alloc);
  call_stack_init(&p_vm->current_execution_state->call_stack, p_specs.alloc);
}

void vm_deinit(vm* p_vm) {
  p_vm->source = NULL;
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
  string_deinit(&p_vm->current_execution_state->native_error_message, p_vm->alloc);
  p_vm->alloc->release(p_vm->alloc, p_vm->current_execution_state);
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

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  p_vm->source = p_specs.source; // TODO: having it this way means only one source per vm. but it's ok will fix soon.
  value vfn = OBJECT_AS_VALUE(p_specs.function);
  gc_guard_up(p_vm->alloc->gc, vfn); // TODO array based gc guard
  // reg->array.data[exe->registers.top] = vfn;
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_closure* closure = create_object_closure(p_specs.function, &s);
  if (closure == NULL) {
    interpret_result res = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    runtime_error(p_vm, "out of memory: failed to allocate to level function.");
    return res;
  }

  if (p_specs.top_level_values.count > STACK_SIZE - 2) { // otherwise call handles it
    if (!register_array_grow(&reg->array, closure->function->registers)) {
      interpret_result res = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
      runtime_error(p_vm, "out of memory: failed to grow register file.");
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
    return vm_error(p_vm->current_execution_state);
  }
  for (uint16_t i = 0; i < p_specs.top_level_values.count; ++i) {
    reg->array.data[hidden_start + i] = p_specs.top_level_values.data[i];
  }
  interpret_result interpret_result = run_until(p_vm, 0);
  if (interpret_result.status != RUNTIME_OK) {
    vm_consume_pending_error(p_vm->current_execution_state);
  }
  return interpret_result;
}

interpret_result vm_call(vm* p_vm, value p_callee, uint32_t p_argc) {
  execution_state* exe = p_vm->current_execution_state;
  register_file* reg = &exe->registers;
  const uint32_t call_depth = exe->call_stack.count;
  const uint32_t base = reg->top - p_argc - 1;

  // p_callee is already at an index relative p_base (compare OP_CALL: frame base +
  // register index). passing base here double based the callee slot: with a
  // non-empty call stack (i.e. a native calling back into the vm) (e.g. dorg3's
  // array_foreach -> ok_call(closure) mid-frame) while base > 0 and call_value read
  // data[base + base], which is a garbage register, instead of the closure staged at
  // data[base]. every tested ok_call until now ran from C on an empty call stack
  // (base == 0), where 0 + 0 was accidentally correct. a regression test was written for this very case.
  if (!call_value(p_vm, exe, reg, base, 0, p_argc, 1)) {
    return vm_error(exe);
  }
  if (exe->call_stack.count == call_depth) {
    interpret_result result = {.status = RUNTIME_OK,
                               .top_level_return = reg->array.data[base]}; // TODO multiple returns
    return result;
  }
  interpret_result result = run_until(p_vm, call_depth);
  if (result.status != RUNTIME_OK) {
    // only the outermost C entry point may consume a pending error: inner
    // ok_calls (from inside a native) must leave it visible so the enclosing
    // call_native can fail the OP_CALL instead of resuming on an erroneous reset state.
    vm_consume_pending_error(exe);
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
    runtime_error(p_vm, "invalid operand for unary '!'.");
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
    goto invalid;
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operand for unary '-'.");
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
perform_add(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) + VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) + VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) + VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_INT(lhs) + VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      // string concatenation: both operands must be strings, no implicit
      // coercion ("a" + 1 = error)
      // build the combined bytes over a raw scratch buffer first:
      // it is plain memory, invisible to the gc, so allocations inside the
      // object creation can collect freely. create_object_string copies the
      // view into its own dynamic buffer and the result is published into the
      // destination register with no allocation in between. the
      // post-store-append window that killed array NEW does not exist here (no
      // append_n phase, nothing allocates between creation and publication).
      const object_string* left = VALUE_AS_STRING(p_lhs);
      const object_string* right = VALUE_AS_STRING(p_rhs);
      const uint64_t left_length = string_get_length(&left->string);
      const uint64_t right_length = string_get_length(&right->string);
      char* scratch = (char*)p_vm->alloc->allocate(p_vm->alloc, left_length + right_length);
      if (scratch == NULL) {
        runtime_error(p_vm, "out of memory: failed to concatenate strings.");
        interpret_result res;
        res.status = RUNTIME_ERROR;
        res.top_level_return = NULL_AS_VALUE();
        return res;
      }
      memcpy(scratch, left->string.chars, left_length);
      memcpy(scratch + left_length, right->string.chars, right_length);
      object_specs specs = {p_vm->alloc, p_vm->objects_store};
      object_string* concatenated =
          create_object_string(ok_create_string_view(scratch, left_length + right_length, false), &specs);
      p_vm->alloc->release(p_vm->alloc, scratch);
      if (concatenated == NULL) {
        runtime_error(p_vm, "out of memory: failed to concatenate strings.");
        interpret_result res;
        res.status = RUNTIME_ERROR;
        res.top_level_return = NULL_AS_VALUE();
        return res;
      }
      lhs = OBJECT_AS_VALUE(concatenated);
    } else {
      goto invalid;
    }
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '+'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  p_reg->array.data[base + p_dest] = lhs;
  interpret_result res;
  res.status = RUNTIME_OK;
  return res;
}

static interpret_result
perform_subtract(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) - VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) - VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) - VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_INT(lhs) - VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    goto invalid;
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '-'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  p_reg->array.data[base + p_dest] = lhs;
  interpret_result res;
  res.status = RUNTIME_OK;
  return res;
}

static interpret_result
perform_multiply(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) * VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) * VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) * VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = NUMBER_AS_VALUE(VALUE_AS_INT(lhs) * VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    goto invalid;
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '*'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  p_reg->array.data[base + p_dest] = lhs;
  interpret_result res;
  res.status = RUNTIME_OK;
  return res;
}

static interpret_result
perform_divide(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      if (VALUE_AS_NUMBER(rhs) == 0.0f) {
        goto division_by_zero;
      }
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) / VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      if (VALUE_AS_INT(rhs) == 0) {
        goto division_by_zero;
      }
      lhs = NUMBER_AS_VALUE(VALUE_AS_NUMBER(lhs) / VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      if (VALUE_AS_INT(rhs) == 0) {
        goto division_by_zero;
      }
      lhs = INT_AS_VALUE(VALUE_AS_INT(lhs) / VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      if (VALUE_AS_NUMBER(rhs) == 0.0f) {
        goto division_by_zero;
      }
      lhs = NUMBER_AS_VALUE(VALUE_AS_INT(lhs) / VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '/'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
division_by_zero: {
  runtime_error(p_vm, "division by zero.");
  interpret_result res;
  res.status = RUNTIME_ERROR;
  res.top_level_return = NULL_AS_VALUE();
  return res;
}
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
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      if (VALUE_AS_NUMBER(rhs) == 0.0f) {
        goto modulo_by_zero;
      }
      lhs = NUMBER_AS_VALUE(floor_mod(VALUE_AS_NUMBER(lhs), VALUE_AS_NUMBER(rhs)));
    } else if (IS_VALUE_INT(rhs)) {
      if (VALUE_AS_INT(rhs) == 0) {
        goto modulo_by_zero;
      }
      lhs = NUMBER_AS_VALUE(floor_mod(VALUE_AS_NUMBER(lhs), (double)VALUE_AS_INT(rhs)));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      if (VALUE_AS_INT(rhs) == 0) {
        goto modulo_by_zero;
      }
      int64_t remainder = VALUE_AS_INT(lhs) % VALUE_AS_INT(rhs);
      if (remainder != 0 && ((remainder < 0) != (VALUE_AS_INT(rhs) < 0))) {
        remainder += VALUE_AS_INT(rhs); // |remainder| < |rhs|: cannot leave the int48 domain
      }
      lhs = INT_AS_VALUE(remainder);
    } else if (IS_VALUE_NUMBER(rhs)) {
      if (VALUE_AS_NUMBER(rhs) == 0.0f) {
        goto modulo_by_zero;
      }
      lhs = NUMBER_AS_VALUE(floor_mod((double)VALUE_AS_INT(lhs), VALUE_AS_NUMBER(rhs)));
    } else if (IS_VALUE_OBJECT(rhs)) {
      goto invalid;
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    goto invalid;
    // TODO
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '%%'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
modulo_by_zero: {
  runtime_error(p_vm, "modulo by zero.");
  interpret_result res;
  res.status = RUNTIME_ERROR;
  res.top_level_return = NULL_AS_VALUE();
  return res;
}
}

static interpret_result
perform_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) == VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) == VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) == VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) == VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      // value equality: equal content, not common prefix. strncmp with only
      // the lhs length made "ab" == "abc" true.
      const object_string* left = VALUE_AS_STRING(p_lhs);
      const object_string* right = VALUE_AS_STRING(p_rhs);
      const uint64_t left_length = string_get_length(&left->string);
      lhs = BOOL_AS_VALUE(left_length == string_get_length(&right->string) &&
                          memcmp(left->string.chars, right->string.chars, left_length) == 0);
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid:
    // mixed type equality intentionally falls back to a raw identity check
    // (null/bool vs anything): turning this into a type error is a semantic
    // decision deferred to when we design the error model.
    lhs = BOOL_AS_VALUE(lhs == rhs);
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static interpret_result
perform_not_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) != VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) != VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) != VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) != VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      // value equality, same length+memcmp shape as perform_equal
      const object_string* left = VALUE_AS_STRING(p_lhs);
      const object_string* right = VALUE_AS_STRING(p_rhs);
      const uint64_t left_length = string_get_length(&left->string);
      lhs = BOOL_AS_VALUE(!(left_length == string_get_length(&right->string) &&
                            memcmp(left->string.chars, right->string.chars, left_length) == 0));
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid:
    // mixed type equality intentionally falls back to a raw identity check
    // (null/bool vs anything): turning this into a type error is a semantic
    // decision deferred to when we design the error model.
    lhs = BOOL_AS_VALUE(lhs != rhs);
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static interpret_result
perform_greater(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) > VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) > VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) > VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) > VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      lhs = BOOL_AS_VALUE(strncmp(VALUE_AS_STRING(p_lhs)->string.chars,
                                  VALUE_AS_STRING(p_rhs)->string.chars,
                                  string_get_length(&VALUE_AS_STRING(p_lhs)->string)) > 0);
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '>'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static interpret_result
perform_less(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) < VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) < VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) < VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) < VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      lhs = BOOL_AS_VALUE(strncmp(VALUE_AS_STRING(p_lhs)->string.chars,
                                  VALUE_AS_STRING(p_rhs)->string.chars,
                                  string_get_length(&VALUE_AS_STRING(p_lhs)->string)) < 0);
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '<'.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static interpret_result
perform_greater_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) >= VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) >= VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) >= VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) >= VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      lhs = BOOL_AS_VALUE(strncmp(VALUE_AS_STRING(p_lhs)->string.chars,
                                  VALUE_AS_STRING(p_rhs)->string.chars,
                                  string_get_length(&VALUE_AS_STRING(p_lhs)->string)) >= 0);
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '>='.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static interpret_result
perform_less_equal(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  value lhs = p_lhs;
  value rhs = p_rhs;
  if (IS_VALUE_NUMBER(lhs)) {
    if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) <= VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_NUMBER(lhs) <= VALUE_AS_INT(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_INT(lhs)) {
    if (IS_VALUE_INT(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) <= VALUE_AS_INT(rhs));
    } else if (IS_VALUE_NUMBER(rhs)) {
      lhs = BOOL_AS_VALUE(VALUE_AS_INT(lhs) <= VALUE_AS_NUMBER(rhs));
    } else if (IS_VALUE_OBJECT(rhs)) {
      // TODO
      // not sure if we should allow this for objects or they have to be strictly lhs.
      // TBD after we add classes.
    } else {
      goto invalid;
    }
  } else if (IS_VALUE_OBJECT(lhs)) {
    if (IS_VALUE_STRING(lhs) && IS_VALUE_STRING(rhs)) {
      lhs = BOOL_AS_VALUE(strncmp(VALUE_AS_STRING(p_lhs)->string.chars,
                                  VALUE_AS_STRING(p_rhs)->string.chars,
                                  string_get_length(&VALUE_AS_STRING(p_lhs)->string)) <= 0);
    } else {
      goto invalid;
      // TODO others
    }
  } else {
  invalid: {
    runtime_error(p_vm, "invalid operands for binary '<='.");
    interpret_result res;
    res.status = RUNTIME_ERROR;
    res.top_level_return = NULL_AS_VALUE();
    return res;
  }
  }
  {
    p_reg->array.data[base + p_dest] = lhs;
    interpret_result res;
    res.status = RUNTIME_OK;
    return res;
  }
}

static inline bool as_str_rest(vm* p_vm, ok_string_view p_str, value* p_result) {
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* str = create_object_string(p_str, &specs);
  if (str == NULL) {
    runtime_error(p_vm, "out of memory: failed to allocate string object.");
    return false;
  }
  *p_result = OBJECT_AS_VALUE(str);
  return true;
}

static inline bool num_as_str(vm* p_vm, double p_value, value* p_result) {
  char buff[64];
  if (snprintf(buff, 64, "%f", p_value) < 0) {
    runtime_error(p_vm, "failed to convert number to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view(buff, OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
}

static inline bool int_as_str(vm* p_vm, int64_t p_value, value* p_result) {
  char buff[64];
  if (snprintf(buff, 64, "%li", p_value) < 0) {
    runtime_error(p_vm, "failed to convert number to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view(buff, OK_STRING_VIEW_CALCULATE_LENGTH, true), p_result);
}

static inline bool as_str(vm* p_vm, value p_value, value* p_result);

static inline bool array_as_str(vm* p_vm, object* p_arr, value* p_result) {
  object_array* arr = (object_array*)p_arr;
  char* chars = NULL;
  size_t len = 0;
  for (uint32_t i = 0; i < arr->values.count; ++i) {
    const uint8_t delemlen = i < arr->values.count - 1 ? 2 : 0;
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
        runtime_error(p_vm, "out of memory: failed to convert array to string.");
        return false;
      }
      if (delemlen != 0) {
        memcpy(chars + len, ", ", delemlen);
      }
      memcpy(chars + len + delemlen, str->chars, reslen);
      len += reslen + delemlen;
    }
  }
  string fstr = asprint(p_vm->alloc, "[%.*s]", len, chars);
  if (fstr.chars == NULL) {
    runtime_error(p_vm, "failed to convert array to string.");
    return false;
  }
  object_specs specs = {p_vm->alloc, p_vm->objects_store};
  object_string* str = create_object_string(ok_create_string_view_from_string(fstr), &specs);
  if (str == NULL) {
    runtime_error(p_vm, "out of memory: failed to convert array to string.");
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
    runtime_error(p_vm, "failed to convert function to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view_from_string(str), p_result);
}

static inline bool native_function_as_str(vm* p_vm, object* p_native_function, value* p_result) {
  object_native_function* nativefu = (object_native_function*)p_native_function;
  string str = asprint(p_vm->alloc, "native fu %s |%i| (@%p)", nativefu->name, nativefu->arity, nativefu->function);
  if (str.chars == NULL) {
    runtime_error(p_vm, "failed to convert native function to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view_from_string(str), p_result);
}

static inline bool native_value_as_str(vm* p_vm, object* p_native_value, value* p_result) {
  object_native_value* nativev = (object_native_value*)p_native_value;
  string str = asprint(p_vm->alloc, "native value (@%p) (dtor @%p)", nativev->user_data, nativev->destructor);
  if (str.chars == NULL) {
    runtime_error(p_vm, "failed to convert native value to string.");
    return false;
  }
  return as_str_rest(p_vm, ok_create_string_view_from_string(str), p_result);
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
  case OBJ_CLOSURE: {
    return closure_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_NATIVE_FUNCTION: {
    return native_function_as_str(p_vm, p_obj, p_result);
  }
  case OBJ_NATIVE_VALUE: {
    return native_value_as_str(p_vm, p_obj, p_result);
  }
  }
  runtime_error(p_vm, "failed to convert unknown object to string.");
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
  runtime_error(p_vm, "failed to convert unknown value to string.");
  return false;
}

static inline bool object_as_num(vm* p_vm, object* p_obj, value* p_result) {
  switch (object_get_type(p_obj)) {
  case OBJ_STRING: {
    char buff[64];
    string* str = &((object_string*)p_obj)->string;
    uint32_t len = string_get_length(str);
    if (len >= sizeof(buff)) {
      runtime_error(p_vm, "failed to convert string to number: string too long. max is 64.");
      return false;
    }
    memcpy(buff, str->chars, len);
    buff[len] = '\0';

    errno = 0;
    char* endptr;
    const double value = strtod(buff, &endptr);
    if (endptr == buff) {
      runtime_error(p_vm, "failed to convert string to number");
      return false;
    }
    if (*endptr != '\0') {
      runtime_error(p_vm, "failed to convert string to number");
      return false;
    }
    if (errno == ERANGE) {
      runtime_error(p_vm, "failed to convert string to number: out of range.");
      return false;
    }
    *p_result = NUMBER_AS_VALUE(value);
    return true;
  }
  case OBJ_ARRAY: {
    runtime_error(p_vm, "array is not convertable to number.");
    return false;
  }
  case OBJ_CLOSURE: {
    runtime_error(p_vm, "function is not convertable to number.");
    return false;
  }
  case OBJ_NATIVE_FUNCTION: {
    runtime_error(p_vm, "native function is not convertable to number.");
    return false;
  }
  case OBJ_NATIVE_VALUE: {
    // scrr idk if we should allow this!?
    runtime_error(p_vm, "native value is not convertable to number.");
    return false;
  }
  }
  runtime_error(p_vm, "failed to convert unknown object to string.");
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
  }
  case TAG_NULL_MASK: {
    runtime_error(p_vm, "null is not convertable to number.");
    return false;
  }
  case TAG_TRUE_MASK:
  case TAG_FALSE_MASK: {
    runtime_error(p_vm, "boolean is not convertable to number.");
    return false;
  }
  case TAG_OBJECT_MASK: {
    return object_as_num(p_vm, VALUE_AS_OBJECT(p_value), p_result);
  }
  }
  runtime_error(p_vm, "failed to convert unknown value to number.");
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

static interpret_result
perform_as(vm* p_vm, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, value p_lhs, value p_rhs) {
  const uint32_t base = p_frame->registers_base;
  interpret_result res;
  res.status = RUNTIME_ERROR;
  res.top_level_return = NULL_AS_VALUE();

  if (!IS_VALUE_STRICTLY_RESERVED_1(p_rhs)) {
    runtime_error(p_vm, "invalid type: expected a type in 'as' conversion.");
    return res;
  }
  value* dest = &p_reg->array.data[base + p_dest];

  switch (VALUE_AS_RESERVED_1(p_rhs)) {
  case TT_STR: {
    if (as_str(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    break;
  }
  case TT_NUM: {
    if (as_num(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    break;
  }
  case TT_BOOL: {
    if (as_bool(p_vm, p_lhs, dest)) {
      res.status = RUNTIME_OK;
    }
    break;
  }
  }
  return res;
}

static inline void cleanup_frame(execution_state* p_exe, register_file* p_registers, call_frame* p_frame) {
  close_upvalues(p_exe, p_frame->registers_base);
  call_stack_remove(&p_exe->call_stack, p_exe->call_stack.count - 1, p_exe->call_stack.count - 1);
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
    registers->array.data[p_frame->return_base] =
        *object_upvalue_get_value(p_frame->closure->upvalues.data[p_index], registers->array.data);
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

// array index resolution shared by OP_ARRAY_GET / OP_ARRAY_SET
static bool resolve_array_index(vm* p_vm, value p_index, uint32_t p_count, uint32_t* p_out) {
  if (IS_VALUE_NUMBER(p_index)) {
    const double d = VALUE_AS_NUMBER(p_index);
    if (!(d >= MIN_SAFE_INTEGER && d <= MAX_SAFE_INTEGER) || d != (double)(int64_t)d) {
      runtime_error(p_vm, "array index must be a whole number, got %g.", d);
      return false;
    }
    if (d < 0 || d >= (double)p_count) {
      runtime_error(p_vm, "index out of bounds: %g (len %u).", d, p_count);
      return false;
    }
    *p_out = (uint32_t)d;
    return true;
  }
  if (IS_VALUE_INT(p_index)) {
    const int64_t i = VALUE_AS_INT(p_index);
    if (i < 0 || (uint64_t)i >= (uint64_t)p_count) {
      runtime_error(p_vm, "index out of bounds: %lld (len %u).", (long long)i, p_count);
      return false;
    }
    *p_out = (uint32_t)i;
    return true;
  }
  runtime_error(p_vm, "array index must be a number.");
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
  runtime_error(p_vm, "%s must be a number.", p_which);
  return false;
}

static object_array* resolve_array_base(vm* p_vm, value p_base) {
  if (!value_is_object_type(p_base, OBJ_ARRAY)) {
    runtime_error(p_vm, "invalid operand for '[]': expected an array.");
    return NULL;
  }
  return VALUE_AS_ARRAY(p_base);
}

interpret_result run_until(vm* p_vm, uint32_t p_call_depth) {
  execution_state* exe = p_vm->current_execution_state;
  call_frame* frame = &exe->call_stack.data[exe->call_stack.count - 1];
  register_file* reg = &exe->registers;
#define REG_AT(n) exe->registers.array.data[frame->registers_base + n]
  register word* ip = frame->ip;
#define READ_WORD() (*ip++)
#define JOIN_WORD(instruction) (((word2)READ_WORD() << 32u) | (word2)(instruction))

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
    switch (op_of(instruction)) {
    case OP_NOP: {
      break;
    }
    case OP_MOVE: {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = REG_AT(bp);
      break;
    }
    case OP_MOVE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = REG_AT(B);
      break;
    }
    case OP_LOAD_CONSTANT: {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = frame->closure->function->chunk.constants.data[bp];
      break;
    }
    case OP_LOAD_CONSTANT_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = frame->closure->function->chunk.constants.data[B];
      break;
    }
    case OP_JUMP: {
      ip += Alarge_of(instruction);
      break;
    }
    case OP_LOOP: {
      ip -= Alarge_of(instruction);
      break;
    }
    case OP_JUMP_IF: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const bool p = p_of(words);
      const bool jump = p ^ is_falsy(REG_AT(B));
      ip += jump * A;
      break;
    }
    case OP_JUMP_IF_SET: {
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
      break;
    }
    case OP_LOAD_NULL: {
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
      break;
    }
    case OP_LOAD_FALSE: {
      const uint16_t A = Alarge_of(instruction);
      REG_AT(A) = BOOL_AS_VALUE(false);
      break;
    }
    case OP_LOAD_TRUE: {
      const uint16_t A = Alarge_of(instruction);
      REG_AT(A) = BOOL_AS_VALUE(true);
      break;
    }
    case OP_LOAD_GLOBAL: {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = p_vm->globals_store->global_values.data[bp];
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr, "[tg] L  g[%u] -> r[%u]  bits=0x%016llx\n", bp, ap, (unsigned long long)REG_AT(ap));
#endif
      break;
    }
    case OP_LOAD_GLOBAL_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = p_vm->globals_store->global_values.data[B];
#ifdef OK_TRACE_GLOBALS
      fprintf(stderr, "[tg] L  g[%u] -> r[%u] (long)  bits=0x%016llx\n", B, A, (unsigned long long)REG_AT(A));
#endif
      break;
    }
    case OP_STORE_GLOBAL: {
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
      break;
    }
    case OP_STORE_GLOBAL_LONG: {
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
      break;
    }
    case OP_LOAD_UPVALUE: {
      uint16_t ap = aprime_of(instruction);
      uint16_t bp = bprime_of(instruction);
      REG_AT(ap) = *object_upvalue_get_value(
          frame->closure->upvalues.data[bp],
          reg->array.data); // upvalue locations are absolute in the register file, they are not frame relative
      break;
    }
    case OP_LOAD_UPVALUE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      REG_AT(A) = *object_upvalue_get_value(
          frame->closure->upvalues.data[B],
          reg->array.data); // upvalue locations are absolute in the register file, they are not frame relative
      break;
    }
    case OP_STORE_UPVALUE: {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      *object_upvalue_get_value(frame->closure->upvalues.data[ap], reg->array.data) =
          resolve_operand(frame, reg, bp, p);
      break;
    }
    case OP_STORE_UPVALUE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      *object_upvalue_get_value(frame->closure->upvalues.data[A], reg->array.data) = resolve_operand(frame, reg, B, p);
      break;
    }
    case OP_CLOSE_UPVALUES: {
      const uint16_t A = Alarge_of(instruction);
      close_upvalues(exe, frame->registers_base + A);
      break;
    }
    case OP_CALL: {
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
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      // not sure if exe is gonna be modified here or not (resume??) but we set it anyways
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      break;
    }
    case OP_CALL_LONG: {
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
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      // not sure if exe is gonna be modified here or not (resume??) but we set it anyways
      exe = p_vm->current_execution_state;
      reg = &exe->registers;
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      break;
    }
    case OP_RETURN: {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      const uint16_t returned = return_from_frame(exe, reg, frame, ap, bp);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      break;
    }
    case OP_RETURN_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t returned = return_from_frame(exe, reg, frame, A, B);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      break;
    }
    case OP_RETURN_1: {
      const uint16_t A = Alarge_of(instruction);
      const uint8_t e = elarge_of(instruction);
      const uint16_t returned = return_1_from_frame(p_vm, exe, frame, A, e);
      if (exe->call_stack.count == p_call_depth) {
        return top_level_return(reg, frame, returned);
      }
      frame = &exe->call_stack.data[exe->call_stack.count - 1];
      ip = frame->ip;
      break;
    }
    case OP_CLOSURE: {
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result result = create_closure(p_vm, exe, reg, frame, ap, bp);
      if (result.status != RUNTIME_OK) {
        return result;
      }
      break;
    }
    case OP_CLOSURE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result result = create_closure(p_vm, exe, reg, frame, A, B);
      if (result.status != RUNTIME_OK) {
        return result;
      }
      break;
    }
    case OP_NOT: {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result res = perform_not(p_vm, reg, frame, ap, resolve_operand(frame, reg, bp, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_NOT_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result res = perform_not(p_vm, reg, frame, A, resolve_operand(frame, reg, B, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_NEGATE: {
      const bool p = p_of(instruction);
      const uint16_t ap = aprime_of(instruction);
      const uint16_t bp = bprime_of(instruction);
      interpret_result res = perform_negate(p_vm, reg, frame, ap, resolve_operand(frame, reg, bp, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_NEGATE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      interpret_result res = perform_negate(p_vm, reg, frame, A, resolve_operand(frame, reg, B, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_ADD: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_add(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_ADD_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_add(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_SUBTRACT: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_subtract(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_SUBTRACT_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_subtract(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_MULTIPLY: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_multiply(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_MULTIPLY_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_multiply(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_DIVIDE: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_divide(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_DIVIDE_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_divide(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_MODULUS: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_modulo(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_MODULUS_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_modulo(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_EQUALS: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_EQUALS_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_NOT_EQUAL: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_not_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_NOT_EQUAL_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_not_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_GREATER: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_greater(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_GREATER_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_greater(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_LESS: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_less(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_LESS_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_less(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_GREATER_EQUAL: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_greater_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_GREATER_EQUAL_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res = perform_greater_equal(
          p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_LESS_EQUAL: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_less_equal(p_vm, reg, frame, a, REG_AT(b), resolve_operand(frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_LESS_EQUAL_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_less_equal(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_operand(frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_AS: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      interpret_result res = perform_as(p_vm, reg, frame, a, REG_AT(b), resolve_goperand(p_vm, frame, reg, c, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }
    case OP_AS_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(instruction);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      interpret_result res =
          perform_as(p_vm, reg, frame, A, resolve_operand(frame, reg, B, q), resolve_goperand(p_vm, frame, reg, C, p));
      if (res.status != RUNTIME_OK) {
        return res;
      }
      break;
    }

    case OP_ARRAY_NEW: {
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
      break;
    oom_new:
      runtime_error(p_vm, "out of memory: failed to allocate array.");
      interpret_result result;
      result.status = RUNTIME_ERROR;
      result.top_level_return = NULL_AS_VALUE();
      return result;
    }
    case OP_ARRAY_NEW_LONG: {
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
      break;
    oom_new_long:
      runtime_error(p_vm, "out of memory: failed to allocate array.");
      interpret_result result;
      result.status = RUNTIME_ERROR;
      result.top_level_return = NULL_AS_VALUE();
      return result;
    }
    case OP_ARRAY_GET: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      const object_array* arr = resolve_array_base(p_vm, REG_AT(b));
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, resolve_operand(frame, reg, c, p), arr->values.count, &index)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      REG_AT(a) = arr->values.data[index];
      break;
    }
    case OP_ARRAY_GET_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const object_array* arr = resolve_array_base(p_vm, REG_AT(B));
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, resolve_operand(frame, reg, C, p), arr->values.count, &index)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      REG_AT(A) = arr->values.data[index];
      break;
    }
    case OP_ARRAY_SET: {
      const bool p = p_of(instruction);
      const uint8_t a = a_of(instruction);
      const uint8_t b = b_of(instruction);
      const uint8_t c = c_of(instruction);
      const object_array* arr = resolve_array_base(p_vm, REG_AT(a));
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, resolve_operand(frame, reg, b, p), arr->values.count, &index)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      // narrow SET has no room for a value-const flag (p is spent on the index):
      // the compiler materializes constant values, c is always a register here.
      ((object_array*)arr)->values.data[index] = REG_AT(c);
      break;
    }
    case OP_ARRAY_SET_LONG: {
      const word2 words = JOIN_WORD(instruction);
      const bool p = p_of(words);
      const bool q = Q_of(words);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const uint16_t C = C_of(words);
      const object_array* arr = resolve_array_base(p_vm, REG_AT(A));
      uint32_t index = 0;
      if (arr == NULL || !resolve_array_index(p_vm, resolve_operand(frame, reg, B, p), arr->values.count, &index)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      ((object_array*)arr)->values.data[index] = resolve_operand(frame, reg, C, q);
      break;
    }
    case OP_JUMP_IF_ARGC: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words);
      const uint16_t B = B_of(words);
      const bool p = p_of(words);
      const bool jump = (p ? 1u : 0u) == (frame->argc > B ? 1u : 0u);
      ip += jump * A;
      break;
    }
    case OP_FOR_PREP: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words); // forward offset to the loop exit
      const uint16_t B = B_of(words); // counter. limit at B + 1, step at B + 2
      double start, limit, step;
      if (!for_operand_as_number(p_vm, REG_AT(B), "numeric for start", &start) ||
          !for_operand_as_number(p_vm, REG_AT(B + 1), "numeric for limit", &limit) ||
          !for_operand_as_number(p_vm, REG_AT(B + 2), "numeric for step", &step)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      if (step == 0.0) {
        runtime_error(p_vm, "numeric for step is zero.");
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }

      REG_AT(B) = NUMBER_AS_VALUE(start);
      REG_AT(B + 1) = NUMBER_AS_VALUE(limit);
      REG_AT(B + 2) = NUMBER_AS_VALUE(step);
      const bool in_range = step > 0.0 ? start < limit : start > limit;
      if (!in_range) {
        ip += A;
      }
      break;
    }
    case OP_FOR_LOOP: {
      const word2 words = JOIN_WORD(instruction);
      const uint16_t A = A_of(words); // backward offset to the body top
      const uint16_t B = B_of(words); // counter. limit at B + 1, step at B + 2
      double counter;
      if (p_of(instruction)) {
        counter = VALUE_AS_NUMBER(REG_AT(B));
      } else if (!for_operand_as_number(p_vm, REG_AT(B), "numeric for counter", &counter)) {
        // mut counter: the body may reseat it to any value, keep the gate.
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
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
      break;
    }
    case OP_PRINT: {
      const bool p = p_of(instruction);
      value_debug_print(resolve_operand(frame, reg, Alarge_of(instruction), p));
      puts("");
      break;
    }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BIN_OP
}

void runtime_error(vm* p_vm, const char* p_fmt, ...) {
  execution_state* exe = p_vm->current_execution_state;
  va_list ap;

  if (exe->call_stack.count > 0) {
    call_frame* frame = &exe->call_stack.data[exe->call_stack.count - 1];

    size_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;

    ok_line_info_repeated* info = chunk_find_line_info(&frame->closure->function->chunk, instruction);

    if (info != NULL && p_vm->source != NULL) {
      fprintf(stderr, "\n%s:%d:%d ", p_vm->source->path, info->line_info.line, info->line_info.offset);
    }
  }

  va_start(ap, p_fmt);
  vfprintf(stderr, p_fmt, ap);
  va_end(ap);
  // the message must always be newline terminated: with an empty call stack
  // (ok_call from C) no stack trace follows and the next terminal line would
  // glue onto the error. %
  fputc('\n', stderr);

  if (exe->call_stack.count > 0) {
    fputs("stack trace (most recent call last):\n", stderr);

    for (uint32_t i = 0; i < exe->call_stack.count; ++i) {
      call_frame* frame = &exe->call_stack.data[i];

      uint32_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;

      ok_line_info_repeated* info = chunk_find_line_info(&frame->closure->function->chunk, instruction);

      if (info != NULL && p_vm->source != NULL) {
        fprintf(stderr,
                "%s:%d:%d in %s.\n",
                p_vm->source->path,
                info->line_info.line,
                info->line_info.offset,
                frame->closure->function->name->string.chars);
      } else {
        fprintf(stderr,
                "%s in %s.\n",
                p_vm->source != NULL ? p_vm->source->path : "<host>",
                frame->closure->function->name->string.chars);
      }
    }
  }

  exe->registers.top = 0;
  exe->call_stack.count = 0;
  // mark the error as pending: interpretation must not resume after this
  // point without propagating the failure (call_native checks this when a
  // native returns. vm_call/vm_interpret consume it at the outermost C boundary).
  exe->error_pending = true;
}

static inline bool call(vm* p_vm,
                        execution_state* p_exe,
                        register_file* p_reg,
                        object_closure* p_closure,
                        uint32_t p_callee,
                        uint16_t p_argc,
                        uint32_t p_retc) {
  const object_function* fu = p_closure->function;
  if (p_argc < (uint32_t)(fu->arity - fu->ndefaults) || p_argc > fu->arity) {
    if (fu->ndefaults == 0) {
      runtime_error(
          p_vm, "invalid call to '%s': expected %d arguments, got: %d.", fu->name->string.chars, fu->arity, p_argc);
    } else if (p_argc < fu->arity - fu->ndefaults) {
      runtime_error(p_vm,
                    "invalid call to '%s': expected at least %d arguments, got: %d.",
                    fu->name->string.chars,
                    fu->arity - fu->ndefaults,
                    p_argc);
    } else {
      runtime_error(p_vm,
                    "invalid call to '%s': expected at most %d arguments, got: %d.",
                    fu->name->string.chars,
                    fu->arity,
                    p_argc);
    }
    return false;
  }
  const uint32_t frame_top = p_callee + p_closure->function->registers;
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
      runtime_error(p_vm, "out of memory: failed to grow register file.");
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
  return call_stack_append(&p_exe->call_stack, frame);
}

static inline bool call_native(vm* p_vm,
                               execution_state* p_exe,
                               register_file* p_reg,
                               object_native_function* p_native,
                               uint32_t p_callee,
                               uint16_t p_argc,
                               uint32_t p_retc) {
  if (p_argc != p_native->arity) {
    runtime_error(p_vm,
                  "invalid call to native '%s': expected %d arguments, got: %d.",
                  p_native->name.chars,
                  p_native->arity,
                  p_argc);
    return false;
  }
  p_exe->native_call_depth++;
  // the native receives a pointer into the register file. if it calls back
  // into the vm and the callee's frame needs more registers than the current capacity, call() grows the
  // file with realloc and that pointer would dangle for the rest of the
  // native's execution. thus the native side iteration 2+ would read freed memory
  // when refetching its argument. thus we stage a copy instead.
  value args_stash[UINT8_MAX];
  if (p_argc > 0) {
    memcpy(args_stash, p_reg->array.data + p_callee + 1, p_argc * sizeof(value));
  }
  // TODO: allow multiple
  value result = p_native->function(p_vm->ok, p_argc, args_stash);
  p_exe->native_call_depth--;
  if (p_exe->native_has_error) {
    runtime_error(p_vm, "%s", p_exe->native_error_message.chars);
    p_exe->native_has_error = false;
    string_deinit(&p_exe->native_error_message, p_vm->alloc);
    return false;
  }
  if (p_exe->error_pending) {
    // a runtime error fired inside the native (e.g. its ok_call on a script
    // closure failed). runtime_error already reset the state and printed the
    // report. returning true here would make the OP_CALL handler reload
    // frame from an empty call stack (data[count - 1] with count == 0) and segfault.
    // so we fail the call so the error propagates up to the C entry point instead.
    return false;
  }
  gc_guard_up(p_vm->alloc->gc, result);
  p_exe->native_has_error = false;
  string_deinit(&p_exe->native_error_message, p_vm->alloc);
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
    default:;
    }
  }
invalid_call:
  runtime_error(p_vm, "invalid call: value is not callable");
  return false;
}

static interpret_result create_closure(
    vm* p_vm, execution_state* p_exe, register_file* p_reg, call_frame* p_frame, uint16_t p_dest, uint16_t p_src) {
  object_function* function = VALUE_AS_FUNCTION(p_frame->closure->function->chunk.constants.data[p_src]);
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_closure* closure = create_object_closure(function, &s);
  if (closure == NULL) {
    runtime_error(p_vm, "out of memory: failed to allocate closure object.");
    interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    return result;
  }
  p_reg->array.data[p_frame->registers_base + p_dest] = OBJECT_AS_VALUE(closure);
  if (function->upvalues.count > 0 && !upvalue_array_grow(&closure->upvalues, function->upvalues.count)) {
    runtime_error(p_vm, "out of memory: failed to allocate closure upvalues.");
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
      runtime_error(p_vm, "out of memory: failed to allocate closure upvalue.");
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
    runtime_error(p_vm, "out of memory: failed to allocate memory for upvalue.");
    return NULL;
  }
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
    p_exe->open_upvalues = upvalue->next;
  }
}

ARRAY_DEFINE_DEFAULT(register_array, value, ARRAY_DEFAULT_TYPE_DEINIT)
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
