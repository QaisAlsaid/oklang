#include "vm.h"
#define OK_TRACE_EXECUTION
#if defined(OK_TRACE_EXECUTION)
#include "debug.h"
#endif // defined(OK_TRACE_EXECUTION)

#include "array.h"
#include "mm.h"
#include <stdio.h>

#define STACK_SIZE 256

void interpret_result_deinit(interpret_result* p_interpret_result) {
}
void vm_init(vm* p_vm) {
  stack_init_warm(&p_vm->stack, STACK_SIZE);
}

void vm_deinit(vm* p_vm) {
  stack_free(&p_vm->stack);
}

interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs) {
  p_vm->chunk = p_specs.chunk;
  interpret_result interpret_result = vm_run(p_vm);
  p_vm->chunk = NULL;
  return interpret_result;
}

interpret_result vm_run(vm* p_vm) {
  register byte* ip = p_vm->chunk->code.code_array;
#define READ_BYTE() (*ip++)
#define READ_CONSTANT() p_vm->chunk->constants.value_array[READ_BYTE()]
#define BIN_OP(op)                                                                                                     \
  do {                                                                                                                 \
    value rhs = stack_popr(&p_vm->stack);                                                                              \
    value lhs = stack_popr(&p_vm->stack);                                                                              \
    stack_push(&p_vm->stack, lhs op rhs);                                                                              \
  } while (0);

  for (;;) {
#if defined(OK_TRACE_EXECUTION)
    for (value* slot = p_vm->stack.values; slot < p_vm->stack.values + p_vm->stack.top; ++slot) {
      printf("[ ");
      value_debug_print(*slot);
      printf(" ]");
    }
    printf("\n");
    debug_disassemble_instruction(p_vm->chunk, (uint32_t)(ip - p_vm->chunk->code.code_array));
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
    case OP_NEGATE: {
      value* top = &p_vm->stack.values[p_vm->stack.top];
      *top = -*top;
      break;
    }
    case OP_ADD: {
      BIN_OP(+);
      break;
    }
    case OP_SUBTRACT: {
      BIN_OP(-);
      break;
    }
    case OP_MULTIPLY: {
      BIN_OP(*);
      break;
    }
    case OP_DIVIDE: {
      BIN_OP(/);
      break;
    }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BIN_OP
}

void stack_init(stack* p_stack) {
  OK_ARRAY_INIT(p_stack->count, p_stack->capacity, p_stack->values);
  p_stack->top = 0;
}

void stack_init_warm(stack* p_stack, uint32_t p_initial_capacity) {
  OK_ARRAY_INIT(p_stack->count, p_stack->capacity, p_stack->values);
  OK_ARRAY_GROW(value, p_stack->values, 0, p_initial_capacity);
  p_stack->top = 0;
}

void stack_resize(stack* p_stack, uint32_t p_new_size) {
  // TODO
}

void stack_push(stack* p_stack, value p_value) {
  if (p_stack->top == p_stack->count) {
    OK_ARRAY_APPEND(value, uint32_t, p_stack->count, p_stack->capacity, p_stack->values, p_value);
    p_stack->top++;
  } else {
    p_stack->values[p_stack->top] = p_value;
    p_stack->top++;
  }
}

void stack_pop(stack* p_stack) {
  if (p_stack->top != 0) {
    p_stack->top--;
  }
}

value stack_popr(stack* p_stack) {
  if (p_stack->top != 0) {
    return p_stack->values[--p_stack->top];
  }
  return 0; // TODO: NULL
}

void stack_free(stack* p_stack) {
  OK_ARRAY_FREE(value, p_stack->capacity, p_stack->values);
  stack_init(p_stack);
}
