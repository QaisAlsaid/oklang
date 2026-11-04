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
  const char* end; // NULL for whole-source lexing (the NUL is the end); otherwise a hard
                   // stop for region lexing (the f-string interpolation sub-parse, P3):
                   // at_end also answers true at the region's closing brace
  ok_line_info line_info;
} lexer;

void lexer_init(lexer* p_lexer, const char* p_src);
// region lexing (P3): lex the bytes [p_region_start, p_region_end) as a token
// stream, with offsets anchored at p_src_base (the WHOLE source's first byte,
// so diagnostics carry absolute positions) and columns anchored at p_line_start
// (the real first byte of the line the region starts on), starting from the
// real line number. used for ${...} interpolations inside f-strings.
void lexer_init_region(lexer* p_lexer,
                       const char* p_src_base,
                       const char* p_region_start,
                       const char* p_region_end,
                       const char* p_line_start,
                       uint32_t p_line);
void lexer_deinit(lexer* p_lexer);
token lexer_lex(lexer* p_lexer);

// the parser's f-string re-scan: where does the ${...} region starting at
// p_start end? the same byte scanner the lexer rode to close the token - one
// source of truth for the interpolation extents. p_wall is the token body's
// end (its closing quote). returns the matching '}', or NULL at the wall.
const char* ok_fstring_region_end(const char* p_start, const char* p_wall);

#endif // OK_LEXER_H
