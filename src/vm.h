#ifndef OK_VM_H
#define OK_VM_H

#include "array.h"
#include "chunk.h"
#include "object_store.h"

ARRAY_DECLARE(stack_array, value, uint32_t);
typedef struct {
  uint32_t top; // for keeping track of the top since we are using dynamic array, thus a pointer will be annoying to
                // keep updating and kinda defeats the purpose. points one past the top most element so indexing becomes
                // like values[top_index - 1] so you get the top most element.
  stack_array array;
} stack;

void stack_init(stack* p_stack);
bool stack_init_warm(stack* p_stack, uint32_t p_initial_capacity);
// keeps allocated memory, only moves the top pointer
void stack_resize(stack* p_stack, uint32_t p_new_size);
// resizes the underlying array (fails if new size < top) or reallocation failed
bool stack_sshrink(stack* p_stack, uint32_t p_new_size);
// fails only when reallocation failed doesn't respect top pointer
bool stack_shrink(stack* p_stack, uint32_t p_new_size);
void stack_push(stack* p_stack, value p_value);
value stack_top(stack* p_stack, uint32_t p_index);
value* stack_top_ptr(stack* p_stack, uint32_t p_index);
void stack_pop(stack* p_stack);
value stack_popr(stack* p_stack);
void stack_free(stack* p_stack);

typedef struct {
  stack stack;
  chunk* chunk;
  source* source;
  byte* ip;
  object_store* objects_store;
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
  object_store* objects_store;
} interpret_specs;

void vm_init(vm* p_vm);
void vm_deinit(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, interpret_specs);
interpret_result vm_run(vm* p_vm);

#endif // OK_VM_H
