#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "ok/ok_source.h"
#include "okast.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okglobals_store.h"
#include "okindex.h"
#include "okobject.h"
#include "okobject_store.h"

#if defined(OK_PARANOID)
#define OK_DEBUG_DUMP_CODE
#endif // defined(OK_PARANOID)

#define IS_REGISTER_VALID(r) ((r.errors) == REGISTER_OK)
#define REGISTERS_MAX INDEX_MAX

typedef index_wrapper _register;
typedef enum {
  REGISTER_OK = 0,
  REGISTER_OVERFLOW,
  REGISTER_ILL_MUTATION,
  REGISTER_ALLOCATION_FAILED,
  REGISTER_UNDEFINED_LOCAL,
  REGISTER_REDEFINED_LOCAL,
} register_error;

typedef enum {
  REGISTER_FLAG_NONE = 0,               // this register has a temp
  REGISTER_FLAG_LOCAL = 1 << 0,         // this register has a local
  REGISTER_FLAG_UNINITIALIZED = 1 << 1, // this register has an uninitialized *thing*
  REGISTER_FLAG_MUTABLE = 1 << 2,       // this register has a mutable *thing*
  REGISTER_FLAG_CAPTURED = 1 << 3,      // this register has captured *thing*
  REGISTER_FLAG_DEVNULL = 1 << 4,       // this register is discarded
} register_flags;

#define IS_UPVALUE_VALID(u) ((u.errors) == UPVALUE_OK)
#define UPVALUES_MAX INDEX_MAX

typedef index_wrapper upvalue;

typedef enum {
  UPVALUE_FLAG_UP = 0,
  UPVALUE_FLAG_LOCAL = 1 << 0,
  UPVALUE_FLAG_MUTABLE = 1 << 1,
} upvalue_flags;

typedef enum {
  UPVALUE_OK = 0,
  UPVALUE_UNDEFINED,
  UPVALUE_OVERFLOW,
  UPVALUE_ALLOCATION_FAILED,
} upvalue_error;

typedef enum {
  EXPRESSION_VALUE = 0,
  EXPRESSION_LVALUE = 1 << 0,
  EXPRESSION_ACCESS = 1 << 1,
  EXPRESSION_SUBSCRIPT = 1 << 2,
} expression_type;

typedef index_wrapper location;

typedef enum {
  LOCATION_OK = 0,
  LOCATION_INVALID,
} location_error;

typedef enum {
  LOCATION_KIND_INVALID = 0,
  LOCATION_KIND_REGISTER,
  LOCATION_KIND_CONSTANT,
  LOCATION_KIND_GLOBAL,
  LOCATION_KIND_UPVALUE,
} location_kind;

typedef enum {
  LVALUE_INVALID = 0,
  LVALUE_VARIABLE,
  LVALUE_SUBSCRIPT,
  LVALUE_FIELD,
} lvalue_kind;

typedef struct {
  lvalue_kind kind;
  union {
    location variable;
  };
} lvalue;

typedef struct {
  expression_type type;
  location value;
  lvalue lvalue;
  uint32_t count;
} expression_result;

ARRAY_DECLARE_DEFAULT(uint16_array, uint16_t)
TABLE_DECLARE_DEFAULT(scope_registers, hashed_string, uint16_t /* index to the scope's registers array */, hash_t)
ARRAY_DECLARE_DEFAULT(registers, _register)
ARRAY_DECLARE_DEFAULT(upvalues, upvalue)

typedef struct {
  scope_registers registers_table; // indices into the scope's registers
  uint16_array registers;          // indices into the function's registers
  uint32_t registers_checkpoint;
  bool captures_any;
} scope;

ARRAY_DECLARE_DEFAULT(scopes, scope)

typedef struct {
  uint32_t scope_depth;
  uint32_t continue_target;
  uint32_t break_target;
  uint16_array continues;
  uint16_array breaks;
  bool continue_forward;
} loop_context;

ARRAY_DECLARE_DEFAULT(loop_stack, loop_context)

typedef enum {
  FUNCTION_NONE = 0,
  FUNCTION_SCRIPT,
  FUNCTION_FUNCTION,
} function_type;

typedef struct {
  object_function* function;
  function_type type;
} compile_function;

typedef struct {
  registers registers;
  allocators* alloc;
  uint32_t local_top;
  uint32_t temp_top;
  uint32_t registers_count;
} register_allocator;

typedef struct {
  scopes scopes;
  upvalues upvalues;
  loop_stack loop_stack;
  compile_function function;
  register_allocator register_allocator;
} compile_context;

ARRAY_DECLARE_DEFAULT(compile_contexts, compile_context)
struct compiler {
  ok_source* source;
  allocators* alloc;
  object_store* objects_store;
  globals_store* globals_store;
  compile_contexts contexts;
  bool had_error;
  bool panic;
};

typedef enum {
  COMPILE_OK,
  COMPILE_ERROR,
} compile_status;

typedef struct {
  compile_status status;
  object_function* function;
} compile_result;

typedef struct {
  allocators* alloc;
  object_store* objects_store;
  globals_store* globals_store;
} compiler_specs;

typedef struct {
  ok_source* source;
  ast_root* root;
} compile_specs;

void compiler_init(compiler* compiler, compiler_specs p_specs);
void compiler_deinit(compiler* p_compiler);
compile_result compiler_compile(compiler* p_compiler, compile_specs p_specs);

#endif // OK_COMPILER_H
