#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "okast.h"
#include "okchunk.h"
#include "okglobals_store.h"
#include "okobject_store.h"
#include "oksource.h"

typedef struct {
  string_view name;
  uint32_t depth; // 24 bit, 1 bit for invalid depth, 1 bit reserved for when adding upvalues, 2 bits reserved for
                  // variable declaration flags
} local;

ARRAY_DECLARE(locals, local, uint32_t)

typedef struct {
  chunk* current_chunk;
  source* source;
  object_store* objects_store;
  globals_store* globals_store;
  locals locals;        // will be moved when adding functions
  uint32_t scope_depth; // 24bit
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
