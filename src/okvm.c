#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "okvm.h"
#define OK_TRACE_EXECUTION
#if defined(OK_TRACE_EXECUTION)
#include "okdebug.h"
#endif // defined(OK_TRACE_EXECUTION)

#include "okarray.h"
#include "okobject.h"

#define STACK_SIZE 256

static void runtime_error(vm* p_vm, const char* p_fmt, ...);
static bool is_falsy(value p_value);
static bool values_equal(value p_lhs, value p_rhs);
static value* read_local(vm* p_vm, call_frame* p_frame);
static value* read_local_long(vm* p_vm, call_frame* p_frame);

void interpret_result_deinit(interpret_result* p_interpret_result) {
}

void vm_init(vm* p_vm) {
  stack_init_warm(&p_vm->stack, STACK_SIZE);
  call_stack_init(&p_vm->call_stack);
  p_vm->objects_store = NULL;
  p_vm->globals_store = NULL;
  p_vm->source = NULL;
}

void vm_deinit(vm* p_vm) {
  p_vm->source = NULL;
  p_vm->globals_store = NULL;
  p_vm->objects_store = NULL;
  call_stack_deinit(&p_vm->call_stack);
  stack_free(&p_vm->stack);
}

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  p_vm->source = p_specs.source; // having it this way means only one source per vm. but it's ok will fix soon.
  p_vm->objects_store = p_specs.objects_store;
  p_vm->globals_store = p_specs.globals_store;
  call_frame frame = {.function = p_specs.function, .ip = p_specs.function->chunk.code.data, .slots = 0, .top = 0};
  stack_push(&p_vm->stack, OBJECT_AS_VALUE(frame.function));
  call_stack_append(&p_vm->call_stack, frame);
  interpret_result interpret_result = vm_run(p_vm);
  return interpret_result;
}

interpret_result vm_run(vm* p_vm) {
  call_frame* frame = &p_vm->call_stack.data[p_vm->call_stack.count - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() frame->function->chunk.constants.data[READ_BYTE()]
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

#if defined(OK_TRACE_EXECUTION)
  disassembler disassembler;
  disassembler_specs specs = {.chunk = &frame->function->chunk, .globals_store = p_vm->globals_store};
  disassembler_init(&disassembler, specs);
#endif // defined(OK_TRACE_EXECUTION)

  for (;;) {
#if defined(OK_TRACE_EXECUTION)
    for (value* slot = p_vm->stack.array.data; slot < p_vm->stack.array.data + p_vm->stack.top; ++slot) {
      printf("[ ");
      value_debug_print(*slot);
      printf(" ]");
    }
    printf("\n");
    debug_disassemble_instruction(&disassembler, (uint32_t)(frame->ip - frame->function->chunk.code.data));
#endif // defined(OK_TRACE_EXECUTION)

    byte instruction = READ_BYTE();
    switch (instruction) {
    case OP_RETURN: {
      value returned = stack_popr(&p_vm->stack);
      interpret_result result;
      result.top_level_return = returned;
      result.status = RUNTIME_OK;
      return result;
    }
    case OP_CONSTANT: {
      value constant = READ_CONSTANT();
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
      p_vm->globals_store->global_values.data[index] = stack_popr(&p_vm->stack);
      break;
    }
    case OP_SET_GLOBAL_LONG: {
      uint32_t index = decode_int(frame->ip, OP_SET_GLOBAL_LONG_OPERANDS_WIDTH);
      p_vm->globals_store->global_values.data[index] = stack_popr(&p_vm->stack);
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
      *top = -*top;
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
  size_t instruction = frame->ip - frame->function->chunk.code.data - 1;
  line_info_repeated* info = source_info_find(&frame->function->chunk.source_info, instruction);
  if (info != NULL) {
    fprintf(stderr, "%s:%d:%d", p_vm->source->path, info->line_info.line, info->line_info.offset);
  }
  va_list ap;
  va_start(ap, p_fmt);
  vfprintf(stderr, p_fmt, ap);
  va_end(ap);
  fputs("", stderr);
  stack_resize(&p_vm->stack, 0);
}

static value* read_local(vm* p_vm, call_frame* p_frame) {
  return &p_vm->stack.array.data[p_frame->slots + *p_frame->ip++];
}

static value* read_local_long(vm* p_vm, call_frame* p_frame) {
  value* ret = &p_vm->stack.array.data[p_frame->slots + decode_int(p_frame->ip, OP_XX_LOCAL_LONG_OPERANDS_WIDTH)];
  p_frame->ip += OP_XX_LOCAL_LONG_OPERANDS_WIDTH;
  return ret;
}

bool is_falsy(value p_value) {
  return (IS_VALUE_BOOL(p_value) && !VALUE_AS_BOOL(p_value)) || IS_VALUE_NULL(p_value);
}

static bool values_equal(value p_lhs, value p_rhs) {
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

void stack_init(stack* p_stack) {
  stack_array_init(&p_stack->array);
  p_stack->top = 0;
}

bool stack_init_warm(stack* p_stack, uint32_t p_initial_capacity) {
  stack_init(p_stack);
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

void stack_free(stack* p_stack) {
  stack_array_deinit(&p_stack->array);
  stack_init(p_stack);
}
