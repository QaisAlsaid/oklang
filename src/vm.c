#include "vm.h"
#define OK_TRACE_EXECUTION
#if defined(OK_TRACE_EXECUTION)
#include "debug.h"
#endif // defined(OK_TRACE_EXECUTION)

#include "array.h"
#include "object.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STACK_SIZE 256

static void runtime_error(vm* p_vm, const char* p_fmt, ...);
static bool is_falsy(value p_value);
static bool values_equal(value p_lhs, value p_rhs);

void interpret_result_deinit(interpret_result* p_interpret_result) {
}

void vm_init(vm* p_vm) {
  stack_init_warm(&p_vm->stack, STACK_SIZE);
  p_vm->objects_store = NULL;
  p_vm->ip = NULL;
  p_vm->chunk = NULL;
  p_vm->source = NULL;
}

void vm_deinit(vm* p_vm) {
  stack_free(&p_vm->stack);
  p_vm->objects_store = NULL;
  p_vm->ip = NULL;
  p_vm->chunk = NULL;
  p_vm->source = NULL;
}

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  p_vm->chunk = p_specs.chunk;
  p_vm->source = p_specs.source; // having it this way means only one source per vm. but it's ok will fix soon.
  p_vm->objects_store = p_specs.objects_store;
  interpret_result interpret_result = vm_run(p_vm);
  p_vm->chunk = NULL;
  return interpret_result;
}

interpret_result vm_run(vm* p_vm) {
  register byte* ip = p_vm->chunk->code.data;
#define READ_BYTE() (*ip++)
#define READ_CONSTANT() p_vm->chunk->constants.data[READ_BYTE()]
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
    for (value* slot = p_vm->stack.array.data; slot < p_vm->stack.array.data + p_vm->stack.top; ++slot) {
      printf("[ ");
      value_debug_print(*slot);
      printf(" ]");
    }
    printf("\n");
    debug_disassemble_instruction(p_vm->chunk, (uint32_t)(ip - p_vm->chunk->code.data));
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
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BIN_OP
}

void runtime_error(vm* p_vm, const char* p_fmt, ...) {
  size_t instruction = p_vm->ip - p_vm->chunk->code.data - 1;
  line_info_repeated* info = source_info_find(&p_vm->chunk->source_info, instruction);
  if (info != NULL) {
    fprintf(stderr, "%s:%d:%d", p_vm->source->path, info->line_info.line, info->line_info.offset);
  }
  va_list ap;
  va_start(ap, p_fmt);
  vfprintf(stderr, p_fmt, ap);
  va_end(ap);
  fputs("\n", stderr);
  stack_resize(&p_vm->stack, 0);
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

ARRAY_DEFINE(stack_array, value, uint32_t)

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
