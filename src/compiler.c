#include "compiler.h"
#include "lexer.h"
#include <stdio.h>

void compiler_init(compiler* p_compiler) {
}

void compiler_compile(compiler* p_compiler, const char* p_src) {
  lexer* lx = &p_compiler->lexer;
  lexer_init(lx, p_src);
  long line = -1;
  for (;;) {
    token token = lexer_lex(lx);
    if (token.line != line) {
      printf("%4d", token.line);
      line = token.line;
    } else {
      printf("   | ");
    }
    printf(" %s '%.*s'\n", token_type_to_string(token.type), token.length, token.start);
    if (token.type == TOKEN_EOF) break;
  }
}

void compiler_free(compiler* p_compiler) {
}
