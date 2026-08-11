#ifndef OK_LEXER_H
#define OK_LEXER_H

#include "stdint.h"
#include "token.h"

typedef struct {
  const char* start;
  const char* current;
  uint32_t line;
  uint32_t offset;
} lexer;

void lexer_init(lexer* p_lexer, const char* p_src);
token lexer_lex(lexer* p_lexer);

#endif // OK_LEXER_H
