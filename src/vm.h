#ifndef OK_VM_H
#define OK_VM_H

#include "chunk.h"

typedef struct {
  uint32_t count;
  uint32_t capacity;
  uint32_t top; // for keeping track of the top since we are using dynamic array, thus a pointer will be annoying to
                // keep updating and kinda defeats the purpose. points one past the top most element so indexing becomes
                // like values[top_index - 1] so you get the top most element.
  value* values;
} stack;

void stack_init(stack* p_stack);
void stack_init_warm(stack* p_stack, uint32_t p_initial_capacity);
void stack_resize(stack* p_stack, uint32_t p_new_size);
void stack_push(stack* p_stack, value p_value);
void stack_pop(stack* p_stack);
value stack_popr(stack* p_stack);
void stack_free(stack* p_stack);

typedef struct {
  stack stack;
  chunk* chunk;
} vm;

typedef enum {
  RUNTIME_OK,
  RUNTIME_ERROR,
} runtime_status;

typedef struct {
  runtime_status status;
  value top_level_return; // TODO when gc, instead of returning raw value return a gc guarded proxy.
} interpret_result;

void interpret_result_deinit(interpret_result* p_interpret_result);

typedef struct {
  source* source;
  chunk* chunk;
} interpret_specs;

void vm_init(vm* p_vm);
void vm_deinit(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, interpret_specs);
interpret_result vm_run(vm* p_vm);

#endif // OK_VM_H
