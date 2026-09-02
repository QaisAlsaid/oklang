#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "okast.h"
#include "okchunk.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okobject_store.h"
#include "oksource.h"

#if defined(OK_PARANOID)
#define OK_DEBUG_DUMP_CODE
#endif // defined(OK_PARANOID)

#define UNDEFINED_LOCAL UINT32_MAX
#define REDEFINED_LOCAL (UNDEFINED_LOCAL - 1)
#define LOCAL_OVERFLOW (REDEFINED_LOCAL - 1)
#define LOCAL_ILL_MUTATION (LOCAL_OVERFLOW - 1)
#define LOCAL_ERROR (LOCAL_ILL_MUTATION - 1)
#define IS_LOCAL_INDEX_VALID(index) (((index) & 0xf0000000) == 0)

#define UNDEFINED_UPVALUE UINT32_MAX
#define UPVALUE_OVERFLOW (UNDEFINED_UPVALUE - 1)
#define UPVALUE_ERROR (UPVALUE_OVERFLOW - 1)
#define IS_UPVALUE_INDEX_VALID(index) (((index) & 0xf0000000) == 0)

#define LOCALS_MAX UINT24_MAX

#define VARIABLES_MAX UINT24_MAX
#define IS_VARIABLE_DECLARATION_VALID(decl) (decl <= VARIABLES_MAX)

typedef enum {
  LOCAL_FLAG_UNINITIALIZED = 0,
  LOCAL_FLAG_MUTABLE = 1,
  LOCAL_FLAG_CAPTURED = 2,
} local_flags;

typedef struct {
  uint32_t
      info; // 24 bit depth, 1 bits for variable declaration flags, 1 bit for
            // uninitialized 1 bit for captured + 4 higher bits for error propagation (errors discard the lower bits).
} local;

typedef struct {
  uint32_t info;
} upvalue;

ARRAY_DECLARE_DEFAULT(uint32_array, uint32_t)
TABLE_DECLARE_DEFAULT(scope_locals, hashed_string, uint32_t /* index to the locals array */, hash_t)
ARRAY_DECLARE_DEFAULT(locals, local)
ARRAY_DECLARE_DEFAULT(upvalues, upvalue)
typedef struct {
  scope_locals locals_table;
  uint32_array locals_array;
} scope;

ARRAY_DECLARE_DEFAULT(scopes, scope)

typedef struct {
  uint32_t scope_depth;
  uint32_t continue_target;
  uint32_t break_target;
  uint32_array continues;
  uint32_array breaks;
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
  scopes scopes;
  locals locals;
  upvalues upvalues;
  loop_stack loop_stack;
  compile_function function;
} function;

ARRAY_DECLARE_DEFAULT(functions, function)

typedef struct {
  source* source;
  object_store* objects_store;
  globals_store* globals_store;
  functions functions;
  bool had_error;
  bool panic;
} compiler;

typedef enum {
  COMPILE_OK,
  COMPILE_ERROR,
} compile_status;

typedef struct {
  compile_status status;
  object_function* function;
} compile_result;

typedef struct {
  source* source;
  ast_root* root;
  object_store* objects_store;
  globals_store* globals_store;
} compiler_specs;

void compiler_init(compiler* compiler);
void compiler_deinit(compiler* p_compiler);
compile_result compiler_compile(compiler* p_compiler, compiler_specs p_specs);

#endif // OK_COMPILER_H
