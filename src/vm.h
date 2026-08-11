#ifndef OK_VM_H
#define OK_VM_H

#include "chunk.h"

typedef struct {
  uint32_t count;
  uint32_t capacity;
  uint32_t top; // for keeping track of the top since we are using dynamic array, thus a pointer will be annoying to keep updating and kinda defeats the purpose. points one past the top most element so indexing becomes like values[top_index - 1] so you get the top most element.
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
  OK,
  COMPILE_ERROR, // evantually the vm won't be coupled to the compiler, but we eat that for now
  RUNTIME_ERROR,
} interpret_result; 

void vm_init(vm* p_vm);
void vm_free(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, chunk* p_chunk);
interpret_result vm_run(vm* p_vm);

#endif // OK_VM_H
