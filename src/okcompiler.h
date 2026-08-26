#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "okast.h"
#include "okchunk.h"
#include "okglobals_store.h"
#include "okobject_store.h"
#include "oksource.h"
#include "okutils.h"

#if defined(OK_PARANOID)
#define OK_DEBUG_DUMP_CODE
#endif // defined(OK_PARANOID)

#define UNDEFINED_LOCAL UINT32_MAX
#define REDEFINED_LOCAL (UNDEFINED_LOCAL - 1)
#define LOCAL_OVERFLOW (REDEFINED_LOCAL - 1)
#define LOCAL_ILL_MUTATION (LOCAL_OVERFLOW - 1)
#define LOCAL_ERROR (LOCAL_ILL_MUTATION - 1)
#define IS_LOCAL_INDEX_VALID(index) (index < LOCAL_ERROR)

#define LOCALS_MAX UINT24_MAX

#define VARIABLES_MAX UINT24_MAX
#define IS_VARIABLE_DECLARATION_VALID(decl) (decl <= VARIABLES_MAX)

typedef enum {
  LOCAL_FLAG_UNINITIALIZED = 1,
  LOCAL_FLAG_MUTABLE = 2,
} local_flags;

typedef struct {
  uint32_t depth; // 24 bit depth, 2 bits for variable declaration flags (currentl only 1 for mutable), 1 bit for
                  // uninitialized + 4 higher bits for error propagation (errors discard the lower bits).
} local;

TABLE_DECLARE_DEFAULT(scope_locals, hashed_string, uint32_t /* index to the locals array */, hash_t)
ARRAY_DECLARE_DEFAULT(locals, local);

typedef struct {
  scope_locals locals;
} scope;

ARRAY_DECLARE_DEFAULT(scopes, scope)

typedef struct {
  scopes scopes;
  locals locals;
} function;

ARRAY_DECLARE_DEFAULT(functions, function)

typedef struct {
  chunk* current_chunk;
  source* source;
  object_store* objects_store;
  globals_store* globals_store;
  functions functions;
  uint32_t local_index; // runtime local slot
  bool had_error;
  bool panic;
} compiler;

typedef enum {
  COMPILE_OK,
  COMPILE_ERROR,
} compile_status;

typedef struct {
  compile_status status;
  chunk* chunk;
} compile_result;

void compile_result_deinit(compile_result* p_result);

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
