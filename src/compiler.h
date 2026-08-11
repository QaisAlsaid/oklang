#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "lexer.h"

typedef struct {
  lexer lexer;
} compiler;

void compiler_init(compiler* compiler);
void compiler_compile(compiler* p_compiler, const char* p_src);
void compiler_free(compiler* p_compiler);

#endif // OK_COMPILER_H
