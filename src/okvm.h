#ifndef OK_VM_H
#define OK_VM_H

#include "okarray.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okobject.h"

ARRAY_DECLARE_DEFAULT(register_array, value)

typedef struct {
  uint32_t top;
  register_array array;
} register_file;

void register_file_init(register_file* p_registers, allocators* p_alloc);
bool register_file_init_warm(register_file* p_registers, uint32_t p_initial_capacity, allocators* p_alloc);
void register_file_deinit(register_file* p_registers);

enum { RETURN_COUNT_DYNAMIC = UINT32_MAX };
typedef struct {
  object_closure* closure;
  uint32_t* ip;
  uint32_t registers_base;
  uint32_t return_base;
  uint32_t return_count;
  uint16_t argc; // set by call(): the callee's own prologue (defaults ladder) reads it
} call_frame;

ARRAY_DECLARE_DEFAULT(call_stack, call_frame)

typedef struct {
  call_stack call_stack;
  register_file registers;
  object_upvalue* open_upvalues;
  string native_error_message;
  bool native_has_error;

  // set by runtime_error, and checked by call_native (a native returning after an
  // inner error must fail its OP_CALL) and consumed at the outermost C entry
  // point (vm_call/vm_interpret with native_call_depth == 0).
  bool error_pending;
  uint32_t native_call_depth;
} execution_state;

struct vm {
  execution_state* current_execution_state;
  ok_source* source;
  object_store* objects_store;
  globals_store* globals_store;
  allocators* alloc;
  ok* ok;
  // print routing (ok_set_print): NULL keeps the CLI's formatted stdout.
  ok_print_fn print_cb;
  void* print_ctx;
};

typedef enum {
  RUNTIME_OK,
  RUNTIME_ERROR,
} runtime_status;

typedef struct {
  runtime_status status;
  value top_level_return;
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
  value_array top_level_values;
} interpret_specs;

void vm_init(vm* p_vm, vm_specs p_specs);
void vm_deinit(vm* p_vm);
interpret_result vm_interpret(vm* p_vm, interpret_specs p_specs);
interpret_result vm_call(vm* p_vm, value p_callee, uint32_t p_argc);
bool vm_native_error(vm* p_vm, ok_string_view p_message);

typedef enum { TT_STR, TT_NUM, TT_BOOL } temporary_type_tags;

#endif // OK_VM_H
