#ifndef OK_VM_H
#define OK_VM_H

#include "okarray.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okobject.h"

ARRAY_DECLARE(stack_array, value, uint32_t)
typedef struct {
  uint32_t top; // for keeping track of the top since we are using dynamic array, thus a pointer will be annoying to
                // keep updating and kinda defeats the purpose. points one past the top most element so indexing becomes
                // like values[top_index - 1] so you get the top most element.
  stack_array array;
  allocators* alloc;
} stack;

void stack_init(stack* p_stack, allocators* p_alloc);
bool stack_init_warm(stack* p_stack, uint32_t p_initial_capacity, allocators* p_alloc);
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
void stack_deinit(stack* p_stack);

typedef struct {
  object_closure* closure;
  byte* ip;
  uint32_t slots;
  uint32_t top;
} call_frame;

ARRAY_DECLARE_DEFAULT(call_stack, call_frame)

struct vm {
  stack stack;
  call_stack call_stack;
  ok_source* source;
  object_store* objects_store;
  globals_store* globals_store;
  object_upvalue* open_upvalues;
  allocators* alloc;
  ok* ok;
  string native_error_message;
  bool native_has_error;
  uint32_t native_call_depth;
};

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
  object_store* objects_store;
  globals_store* globals_store;
  allocators* alloc;
  ok* ok;
} vm_specs;

typedef struct {
  ok_source* source;
  object_function* function;
} interpret_specs;

void vm_init(vm* p_vm, vm_specs p_specs);
void vm_deinit(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs);
interpret_result vm_call(vm* p_vm, value p_callee, uint32_t p_argc);
bool vm_native_error(vm* p_vm, ok_string_view p_message);

#endif // OK_VM_H
