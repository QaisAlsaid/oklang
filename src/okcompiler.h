#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "okchunk.h"
#include "okglobals_store.h"
#include "okobject_store.h"
#include "okparser.h"
#include "oksource.h"

typedef struct {
  chunk* current_chunk;
  source* source;
  object_store* objects_store;
  globals_store* globals_store;
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
