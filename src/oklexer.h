#ifndef OK_LEXER_H
#define OK_LEXER_H

#include <stdint.h>

#include "ok/ok_source.h"
#include "oktoken.h"

typedef struct {
  const char* src; // the source begin: token offsets are (start - src), see create_token
  const char* start;
  const char* current;
  const char* line_start; // first byte of the current line: columns are (start - line_start + 1)
  ok_line_info line_info;
} lexer;

void lexer_init(lexer* p_lexer, const char* p_src);
void lexer_deinit(lexer* p_lexer);
token lexer_lex(lexer* p_lexer);

#endif // OK_LEXER_H
