#include <stdarg.h>
#include <stdio.h>
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
static bool is_falsy(value p_value);
static bool values_equal(value p_lhs, value p_rhs);
static value* read_local(vm* p_vm, call_frame* p_frame);
static value* read_local_long(vm* p_vm, call_frame* p_frame);
static bool call_value(vm* p_vm, value p_callee, uint32_t p_argc);
static bool call(vm* p_vm, object_closure* p_closure, uint32_t p_argc);
static bool call_native(vm* p_vm, object_native_function* p_native, uint32_t p_argc);
static object_upvalue* capture_upvalue(vm* p_vm, uint32_t p_local_index);
static void close_upvalues(vm* p_vm, uint32_t p_last);

void interpret_result_deinit(interpret_result* p_interpret_result) {
}

void vm_init(vm* p_vm, vm_specs p_specs) {
  p_vm->alloc = p_specs.alloc;
  p_vm->objects_store = p_specs.objects_store;
  p_vm->globals_store = p_specs.globals_store;
  p_vm->source = NULL;
  p_vm->open_upvalues = NULL;
  p_vm->native_has_error = false;
  p_vm->in_native_call = false;
  p_vm->ok = p_specs.ok;
  string_init(&p_vm->native_error_message, NULL, 0, false, p_vm->alloc);
  stack_init_warm(&p_vm->stack, STACK_SIZE, p_specs.alloc);
  call_stack_init(&p_vm->call_stack, p_specs.alloc);
}

void vm_deinit(vm* p_vm) {
  p_vm->open_upvalues = NULL;
  p_vm->source = NULL;
  p_vm->globals_store = NULL;
  p_vm->objects_store = NULL;
  p_vm->in_native_call = false;
  p_vm->ok = NULL;
  p_vm->native_has_error = false;
  string_deinit(&p_vm->native_error_message, p_vm->alloc);
  call_stack_deinit(&p_vm->call_stack, p_vm->alloc);
  stack_deinit(&p_vm->stack);
}

bool vm_native_error(vm* p_vm, ok_string_view p_message) {
  if (!p_vm->in_native_call) {
    return false;
  }
  p_vm->native_has_error = true;
  p_vm->native_error_message = create_string_from_string_view(p_message, p_vm->alloc);
  return true;
}

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  p_vm->source = p_specs.source; // having it this way means only one source per vm. but it's ok will fix soon.
  value vfn = OBJECT_AS_VALUE(p_specs.function);
  gc_guard_up(p_vm->alloc->gc, vfn);
  stack_push(&p_vm->stack, vfn);
  gc_guard_down(p_vm->alloc->gc);
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_closure* closure = create_object_closure(p_specs.function, &s);
  if (closure == NULL) {
    interpret_result res = {.status = RUNTIME_ERROR, NULL_AS_VALUE()};
    return res;
  }
  *stack_top_ptr(&p_vm->stack, 0) = OBJECT_AS_VALUE(closure);
  if (!call_value(p_vm, stack_top(&p_vm->stack, 0), 0)) {
    interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
    return result;
  }
  interpret_result interpret_result = vm_run(p_vm);
  return interpret_result;
}

interpret_result vm_run(vm* p_vm) {
  call_frame* frame = &p_vm->call_stack.data[p_vm->call_stack.count - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() frame->closure->function->chunk.constants.data[READ_BYTE()]
#define BIN_OP(VALUE, op)                                                                                              \
  do {                                                                                                                 \
    if (!IS_VALUE_NUMBER(stack_top(&p_vm->stack, 0)) || !IS_VALUE_NUMBER(stack_top(&p_vm->stack, 1))) {                \
      runtime_error(p_vm, "'%s' operands must be numbers.", #op);                                                      \
      interpret_result result;                                                                                         \
      result.top_level_return = NULL_AS_VALUE();                                                                       \
      result.status = RUNTIME_ERROR;                                                                                   \
      return result;                                                                                                   \
    }                                                                                                                  \
    double rhs = VALUE_AS_NUMBER(stack_popr(&p_vm->stack));                                                            \
    double lhs = VALUE_AS_NUMBER(stack_popr(&p_vm->stack));                                                            \
    stack_push(&p_vm->stack, VALUE(lhs op rhs));                                                                       \
  } while (0);

  for (;;) {
#if defined(OK_TRACE_EXECUTION)
    disassembler disassembler;
    disassembler_specs specs = {.chunk = &frame->closure->function->chunk, .globals_store = p_vm->globals_store};
    disassembler_init(&disassembler, specs);

    printf("[ ");
    for (value* slot = p_vm->stack.array.data; slot < p_vm->stack.array.data + p_vm->stack.top; ++slot) {
      printf("[");
      value_debug_print(*slot);
      printf("]");
    }
    printf(" ]\n");
    debug_disassemble_instruction(&disassembler, (uint32_t)(frame->ip - frame->closure->function->chunk.code.data));
#endif // defined(OK_TRACE_EXECUTION)

    byte instruction = READ_BYTE();
    switch (instruction) {
    case OP_RETURN: {
      value returned = stack_popr(&p_vm->stack);
      close_upvalues(p_vm, frame->slots);
      stack_resize(&p_vm->stack, frame->slots);
      call_stack_remove(&p_vm->call_stack, p_vm->call_stack.count - 1, p_vm->call_stack.count - 1);
      if (p_vm->call_stack.count == 0) {
#if defined(OK_TRACE_EXECUTION)
        printf("[ ");
        for (value* slot = p_vm->stack.array.data; slot < p_vm->stack.array.data + p_vm->stack.top; ++slot) {
          printf("[ ");
          value_debug_print(*slot);
          printf(" ]");
        }
        printf(" ]\n");
#endif // defined(OK_TRACE_EXECUTION)
        interpret_result result;
        result.top_level_return = returned;
        result.status = RUNTIME_OK;
        return result;
      }
      stack_push(&p_vm->stack, returned);
      frame = &p_vm->call_stack.data[p_vm->call_stack.count - 1];
      break;
    }
    case OP_CONSTANT: {
      value constant = READ_CONSTANT();
      stack_push(&p_vm->stack, constant);
      break;
    }
    case OP_CONSTANT_LONG: {
      value constant =
          frame->closure->function->chunk.constants.data[decode_int(frame->ip, OP_CONSTANT_LONG_OPERANDS_WIDTH)];
      frame->ip += OP_CONSTANT_LONG_OPERANDS_WIDTH;
      stack_push(&p_vm->stack, constant);
      break;
    }
    case OP_POP: {
      stack_pop(&p_vm->stack);
      break;
    }
    case OP_JUMP: {
      const uint32_t jmp = decode_int(frame->ip, OP_JUMP_OPERANDS_WIDTH);
      frame->ip += jmp;
      break;
    }
    case OP_TRUTHY_JUMP: {
      const uint32_t jmp = decode_int(frame->ip, OP_TRUTHY_JUMP_OPERANDS_WIDTH);
      const bool truthy = !is_falsy(stack_top(&p_vm->stack, 0));
      frame->ip += truthy * jmp + (!truthy * OP_TRUTHY_JUMP_OPERANDS_WIDTH);
      break;
    }
    case OP_FALSY_JUMP: {
      const uint32_t jmp = decode_int(frame->ip, OP_FALSY_JUMP_OPERANDS_WIDTH);
      const bool falsy = is_falsy(stack_top(&p_vm->stack, 0));
      frame->ip += falsy * jmp + (!falsy * OP_TRUTHY_JUMP_OPERANDS_WIDTH);
      break;
    }
    case OP_LOOP: {
      const uint32_t loop = decode_int(frame->ip, OP_LOOP_OPERANDS_WIDTH);
      frame->ip -= loop;
      break;
    }
    case OP_NULL: {
      stack_push(&p_vm->stack, NULL_AS_VALUE());
      break;
    }
    case OP_FALSE: {
      stack_push(&p_vm->stack, BOOL_AS_VALUE(false));
      break;
    }
    case OP_TRUE: {
      stack_push(&p_vm->stack, BOOL_AS_VALUE(true));
      break;
    }
    case OP_SET_GLOBAL: {
      byte index = READ_BYTE();
      p_vm->globals_store->global_values.data[index] = stack_top(&p_vm->stack, 0);
      break;
    }
    case OP_SET_GLOBAL_LONG: {
      uint32_t index = decode_int(frame->ip, OP_SET_GLOBAL_LONG_OPERANDS_WIDTH);
      p_vm->globals_store->global_values.data[index] = stack_top(&p_vm->stack, 0);
      frame->ip += OP_SET_GLOBAL_LONG_OPERANDS_WIDTH;
      break;
    }
    case OP_GET_GLOBAL: {
      byte index = READ_BYTE();
      stack_push(&p_vm->stack, p_vm->globals_store->global_values.data[index]);
      break;
    }
    case OP_GET_GLOBAL_LONG: {
      uint32_t index = decode_int(frame->ip, OP_GET_GLOBAL_LONG_OPERANDS_WIDTH);
      frame->ip += OP_GET_GLOBAL_LONG_OPERANDS_WIDTH;
      stack_push(&p_vm->stack, p_vm->globals_store->global_values.data[index]);
      break;
    }
    case OP_GET_LOCAL: {
      stack_push(&p_vm->stack, *read_local(p_vm, frame));
      break;
    }
    case OP_GET_LOCAL_LONG: {
      stack_push(&p_vm->stack, *read_local_long(p_vm, frame));
      break;
    }
    case OP_SET_LOCAL: {
      *read_local(p_vm, frame) = stack_top(&p_vm->stack, 0);
      break;
    }
    case OP_SET_LOCAL_LONG: {
      *read_local_long(p_vm, frame) = stack_top(&p_vm->stack, 0);
      break;
    }
    case OP_GET_UPVALUE: {
      uint32_t index = READ_BYTE();
      stack_push(&p_vm->stack, *object_upvalue_get_value(frame->closure->upvalues.data[index], p_vm->stack.array.data));
      break;
    }
    case OP_GET_UPVALUE_LONG: {
      uint32_t index = decode_int(frame->ip, OP_GET_UPVALUE_LONG_OPERANDS_WIDTH);
      frame->ip += OP_GET_UPVALUE_LONG_OPERANDS_WIDTH;
      stack_push(&p_vm->stack, *object_upvalue_get_value(frame->closure->upvalues.data[index], p_vm->stack.array.data));

      break;
    }
    case OP_SET_UPVALUE: {
      uint32_t index = READ_BYTE();
      *object_upvalue_get_value(frame->closure->upvalues.data[index], p_vm->stack.array.data) =
          stack_top(&p_vm->stack, 0);
      break;
    }
    case OP_SET_UPVALUE_LONG: {
      uint32_t index = decode_int(frame->ip, OP_GET_UPVALUE_LONG_OPERANDS_WIDTH);
      frame->ip += OP_GET_UPVALUE_LONG_OPERANDS_WIDTH;
      *object_upvalue_get_value(frame->closure->upvalues.data[index], p_vm->stack.array.data) =
          stack_top(&p_vm->stack, 0);
      break;
    }
    case OP_CLOSE_UPVALUE: {
      close_upvalues(p_vm, p_vm->stack.top - 1);
      stack_pop(&p_vm->stack);
      break;
    }
    case OP_CLOSURE: {
      object_function* function = VALUE_AS_FUNCTION(
          frame->closure->function->chunk.constants.data[decode_int(frame->ip, OP_CONSTANT_LONG_OPERANDS_WIDTH)]);
      frame->ip += 3;
      object_specs s = {p_vm->alloc, p_vm->objects_store};
      object_closure* closure = create_object_closure(function, &s);
      if (closure == NULL) {
        runtime_error(p_vm, "out of memory: failed to allocate closure object.");
        interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
        return result;
      }
      gc_guard_up(p_vm->alloc->gc, OBJECT_AS_VALUE(closure));
      stack_push(&p_vm->stack, OBJECT_AS_VALUE(closure));
      gc_guard_down(p_vm->alloc->gc);
      for (uint32_t i = 0; i < closure->function->upvalues; ++i) {
        bool is_local = (bool)READ_BYTE();
        uint32_t index = decode_int(frame->ip, UINT24_BYTE_COUNT);
        frame->ip += UINT24_BYTE_COUNT;
        bool status = true;
        if (is_local) {
          status = upvalue_array_append(&closure->upvalues, capture_upvalue(p_vm, frame->slots + index));
        } else {
          status = upvalue_array_append(&closure->upvalues, frame->closure->upvalues.data[index]);
        }
        if (!status) {
          runtime_error(p_vm, "out of memory: failed to add upvalue.");
          interpret_result result = {.status = RUNTIME_ERROR, .top_level_return = NULL_AS_VALUE()};
          return result;
        }
      }
      break;
    }
    case OP_CALL: {
      const uint8_t argc = READ_BYTE();
      if (!call_value(p_vm, stack_top(&p_vm->stack, argc), argc)) {
        interpret_result result;
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      frame = &p_vm->call_stack.data[p_vm->call_stack.count - 1];
      break;
    }
    case OP_NOT: {
      value* top = stack_top_ptr(&p_vm->stack, 0);
      *top = BOOL_AS_VALUE(is_falsy(*top));
      break;
    }
    case OP_NEGATE: {
      value* top = stack_top_ptr(&p_vm->stack, 0);
      if (!IS_VALUE_NUMBER(*top)) {
        interpret_result result;
        runtime_error(p_vm, "negate operand must be a number.");
        result.status = RUNTIME_ERROR;
        result.top_level_return = NULL_AS_VALUE();
        return result;
      }
      *top = NUMBER_AS_VALUE(-VALUE_AS_NUMBER(*top));
      break;
    }
    case OP_ADD: {
      BIN_OP(NUMBER_AS_VALUE, +);
      break;
    }
    case OP_SUBTRACT: {
      BIN_OP(NUMBER_AS_VALUE, -);
      break;
    }
    case OP_MULTIPLY: {
      BIN_OP(NUMBER_AS_VALUE, *);
      break;
    }
    case OP_DIVIDE: {
      BIN_OP(NUMBER_AS_VALUE, /);
      break;
    }
    case OP_EQUAL: {
      value rhs = stack_popr(&p_vm->stack);
      value* lhs = stack_top_ptr(&p_vm->stack, 0);
      *lhs = BOOL_AS_VALUE(values_equal(*lhs, rhs));
      break;
    }
    case OP_NOT_EQUAL: {
      value rhs = stack_popr(&p_vm->stack);
      value* lhs = stack_top_ptr(&p_vm->stack, 0);
      *lhs = BOOL_AS_VALUE(!values_equal(*lhs, rhs));
      break;
    }
    case OP_LESS: {
      BIN_OP(BOOL_AS_VALUE, <)
      break;
    }
    case OP_GREATER: {
      BIN_OP(BOOL_AS_VALUE, >)
      break;
    }
    case OP_LESS_EQUAL: {
      BIN_OP(BOOL_AS_VALUE, <=)
      break;
    }
    case OP_GREATER_EQUAL: {
      BIN_OP(BOOL_AS_VALUE, >=)
      break;
    }
    case OP_PRINT: {
      value_debug_print(stack_popr(&p_vm->stack));
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
  call_frame* frame = &p_vm->call_stack.data[p_vm->call_stack.count - 1];
  size_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;
  ok_line_info_repeated* info = source_info_find(&frame->closure->function->chunk.source_info, instruction);
  if (info != NULL) {
    fprintf(stderr, "\n%s:%d:%d ", p_vm->source->path, info->line_info.line, info->line_info.offset);
  }

  va_list ap;
  va_start(ap, p_fmt);
  vfprintf(stderr, p_fmt, ap);
  va_end(ap);
  fputs("\nstack trace (most recent call last):\n", stderr);
  for (uint32_t i = 0; i < p_vm->call_stack.count; ++i) {
    call_frame* frame = &p_vm->call_stack.data[i];
    uint32_t instruction = frame->ip - frame->closure->function->chunk.code.data - 1;
    ok_line_info_repeated* info = source_info_find(&frame->closure->function->chunk.source_info, instruction);
    if (info != NULL) {
      fprintf(stderr,
              "%s:%d:%d in %s.\n",
              p_vm->source->path,
              info->line_info.line,
              info->line_info.offset,
              frame->closure->function->name->string.chars);
    } else {
      fprintf(stderr, "%s in %s.\n", p_vm->source->path, frame->closure->function->name->string.chars);
    }
  }
  stack_resize(&p_vm->stack, 0);
  p_vm->call_stack.count = 0;
}

value* read_local(vm* p_vm, call_frame* p_frame) {
  return &p_vm->stack.array.data[p_frame->slots + *p_frame->ip++];
}

value* read_local_long(vm* p_vm, call_frame* p_frame) {
  value* ret = &p_vm->stack.array.data[p_frame->slots + decode_int(p_frame->ip, OP_XX_LOCAL_LONG_OPERANDS_WIDTH)];
  p_frame->ip += OP_XX_LOCAL_LONG_OPERANDS_WIDTH;
  return ret;
}

bool call_value(vm* p_vm, value p_callee, uint32_t p_argc) {
  if (IS_VALUE_OBJECT(p_callee)) {
    object* obj = VALUE_AS_OBJECT(p_callee);
    switch (object_get_type(obj)) {
    case OBJ_CLOSURE:
      return call(p_vm, VALUE_AS_CLOSURE(p_callee), p_argc);
    case OBJ_NATIVE_FUNCTION:
      return call_native(p_vm, VALUE_AS_NATIVE_FUNCTION(p_callee), p_argc);
    default:;
    }
  }
  runtime_error(p_vm, "invalid call: value is not callable.");
  return false;
}

bool call(vm* p_vm, object_closure* p_closure, uint32_t p_argc) {
  if (p_argc != p_closure->function->arity) {
    runtime_error(p_vm, "invalid call: expected %d arguments, got: %d.", p_closure->function->arity, p_argc);
    return false;
  }
  call_frame frame;
  frame.closure = p_closure;
  frame.ip = p_closure->function->chunk.code.data;
  frame.slots = p_vm->stack.top - p_argc - 1;
  frame.top = frame.slots;
  return call_stack_append(&p_vm->call_stack, frame);
}

static bool call_native(vm* p_vm, object_native_function* p_native, uint32_t p_argc) {
  if (p_argc != p_native->arity) {
    runtime_error(p_vm,
                  "invalid call: expected %d arguments, got: %d.\nnote: when calling a native function.",
                  p_native->arity,
                  p_argc);
    return false;
  }
  value argv[p_argc];
  memcpy(argv, p_vm->stack.array.data + p_vm->stack.top - p_argc, sizeof(value) * p_argc);
  p_vm->in_native_call = true;
  value result = p_native->function(p_vm->ok, p_argc, argv);
  p_vm->in_native_call = false;
  const bool res = !p_vm->native_has_error;
  if (!res) {
    runtime_error(p_vm, "%s", p_vm->native_error_message.chars);
    p_vm->native_has_error = false;
    string_deinit(&p_vm->native_error_message, p_vm->alloc);
    return false;
  }
  gc_guard_up(p_vm->alloc->gc, result);
  p_vm->native_has_error = false;
  string_deinit(&p_vm->native_error_message, p_vm->alloc);
  stack_resize(&p_vm->stack, p_vm->stack.top - p_argc - 1);
  stack_push(&p_vm->stack, result);
  gc_guard_down(p_vm->alloc->gc);
  return res;
}

object_upvalue* capture_upvalue(vm* p_vm, uint32_t p_local_index) {
  object_upvalue* prev = NULL;
  object_upvalue* upvalue = p_vm->open_upvalues;
  while (upvalue != NULL && object_upvalue_get_location(upvalue) > p_local_index) {
    prev = upvalue;
    upvalue = upvalue->next;
  }
  if (upvalue != NULL && object_upvalue_get_location(upvalue) == p_local_index) {
    return upvalue;
  }
  object_specs s = {p_vm->alloc, p_vm->objects_store};
  object_upvalue* captured = create_object_upvalue(p_local_index, &s);
  if (captured == NULL) {
    runtime_error(p_vm, "out of memory: failed to allocate memory for upvalue.");
    return NULL;
  }
  captured->next = upvalue;
  if (prev == NULL) {
    p_vm->open_upvalues = captured;
  } else {
    prev->next = captured;
  }
  return captured;
}

static void close_upvalues(vm* p_vm, uint32_t p_last) {
  while (p_vm->open_upvalues != NULL && object_upvalue_get_location(p_vm->open_upvalues) >= p_last) {
    object_upvalue* upvalue = p_vm->open_upvalues;
    upvalue->closed = p_vm->stack.array.data[object_upvalue_get_location(upvalue)];
    object_upvalue_set_closed(upvalue);
    p_vm->open_upvalues = upvalue->next;
  }
}

bool is_falsy(value p_value) {
  return (IS_VALUE_BOOL(p_value) && !VALUE_AS_BOOL(p_value)) || IS_VALUE_NULL(p_value);
}

bool values_equal(value p_lhs, value p_rhs) {
  if (IS_VALUE_NUMBER(p_lhs) && IS_VALUE_NUMBER(p_rhs))
    return VALUE_AS_NUMBER(p_lhs) == VALUE_AS_NUMBER(p_rhs);
  else if (IS_VALUE_STRING(p_lhs) && IS_VALUE_STRING(p_rhs)) {
    return strncmp(VALUE_AS_STRING(p_lhs)->string.chars,
                   VALUE_AS_STRING(p_rhs)->string.chars,
                   string_get_length(&VALUE_AS_STRING(p_lhs)->string)) == 0;
  }
  return p_lhs == p_rhs;
}

ARRAY_DEFINE_DEFAULT(stack_array, value, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(call_stack, call_frame, ARRAY_DEFAULT_TYPE_DEINIT);

void stack_init(stack* p_stack, allocators* p_alloc) {
  stack_array_init(&p_stack->array, p_alloc);
  p_stack->top = 0;
  p_stack->alloc = p_alloc;
}

bool stack_init_warm(stack* p_stack, uint32_t p_initial_capacity, allocators* p_alloc) {
  stack_init(p_stack, p_alloc);
  if (p_initial_capacity > 0) { // so we dont trigger free in the reallocate function.
    return stack_array_grow(&p_stack->array, p_initial_capacity);
  }
  return false;
}

void stack_resize(stack* p_stack, uint32_t p_new_size) {
  p_stack->top = p_new_size;
}

bool stack_shrink(stack* p_stack, uint32_t p_new_size) {
  return stack_array_grow(&p_stack->array, p_new_size);
}

bool stack_sshrink(stack* p_stack, uint32_t p_new_size) {
  if (p_stack->top > p_new_size) {
    return false;
  }
  return stack_shrink(p_stack, p_new_size);
}

void stack_push(stack* p_stack, value p_value) {
  if (p_stack->top == p_stack->array.count) {
    stack_array_append(&p_stack->array, p_value);
    p_stack->top++;
  } else {
    p_stack->array.data[p_stack->top] = p_value;
    p_stack->top++;
  }
}

value stack_top(stack* p_stack, uint32_t p_index) {
  if (p_index >= p_stack->array.count) {
    return NULL_AS_VALUE();
  }
  return p_stack->array.data[p_stack->top - 1 - p_index];
}

value* stack_top_ptr(stack* p_stack, uint32_t p_index) {
  return &p_stack->array.data[p_stack->top - 1 - p_index];
}

void stack_pop(stack* p_stack) {
  if (p_stack->top != 0) {
    p_stack->top--;
  }
}

value stack_popr(stack* p_stack) {
  if (p_stack->top != 0) {
    return p_stack->array.data[--p_stack->top];
  }
  return NULL_AS_VALUE();
}

void stack_deinit(stack* p_stack) {
  stack_array_deinit(&p_stack->array, p_stack->alloc);
  stack_init(p_stack, p_stack->alloc);
}
