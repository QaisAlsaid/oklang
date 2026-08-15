#ifndef OK_LEXER_H
#define OK_LEXER_H

#include "source.h"
#include "stdint.h"
#include "token.h"

typedef struct {
  const char* start;
  const char* current;
  line_info line_info;
} lexer;

void lexer_init(lexer* p_lexer, const char* p_src);
void lexer_free(lexer* p_lexer);
token lexer_lex(lexer* p_lexer);

#endif // OK_LEXER_H
