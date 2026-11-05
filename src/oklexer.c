#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "oklexer.h"
#include "oktoken.h"

void lexer_init(lexer* p_lexer, const char* p_src) {
  p_lexer->src = p_src;
  p_lexer->start = p_src;
  p_lexer->current = p_src;
  p_lexer->line_start = p_src;
  p_lexer->end = NULL; // whole-source mode: the NUL is the end
  p_lexer->line_info.line = 1;
  p_lexer->line_info.offset = 0;
  p_lexer->line_info.column = 1;
}

void lexer_init_region(lexer* p_lexer,
                       const char* p_src_base,
                       const char* p_region_start,
                       const char* p_region_end,
                       const char* p_line_start,
                       uint32_t p_line) {
  p_lexer->src = p_src_base;
  p_lexer->start = p_region_start;
  p_lexer->current = p_region_start;
  p_lexer->line_start = p_line_start;
  p_lexer->end = p_region_end;
  p_lexer->line_info.line = p_line;
  p_lexer->line_info.offset = (uint32_t)(p_region_start - p_src_base);
  p_lexer->line_info.column = (uint32_t)(p_region_start - p_line_start) + 1;
}

void lexer_deinit(lexer* p_lexer) {
  p_lexer->src = NULL;
  p_lexer->start = NULL;
  p_lexer->current = NULL;
  p_lexer->line_start = NULL;
  p_lexer->end = NULL;
  p_lexer->line_info.line = 0;
  p_lexer->line_info.offset = 0;
  p_lexer->line_info.column = 0;
}

static bool at_end(lexer* p_lexer) {
  if (p_lexer->end != NULL && p_lexer->current >= p_lexer->end) {
    return true; // region mode: the closing brace is the wall
  }
  return *p_lexer->current == '\0';
}

static token create_token(lexer* p_lexer, token_type p_type) {
  token token;
  token.type = p_type;
  token.line_info = p_lexer->line_info;
  token.line_info.offset = (uint32_t)(p_lexer->start - p_lexer->src);
  token.line_info.column = (uint32_t)(p_lexer->start - p_lexer->line_start) + 1;
  token.length = (uint32_t)(p_lexer->current - p_lexer->start);
  token.start = p_lexer->start;
  return token;
}

static token create_error_token(lexer* p_lexer, const char* p_message) {
  token token;
  token.type = TOKEN_ERROR;
  token.line_info = p_lexer->line_info;
  token.line_info.offset = (uint32_t)(p_lexer->start - p_lexer->src);
  token.line_info.column = (uint32_t)(p_lexer->start - p_lexer->line_start) + 1;
  token.length = strlen(p_message);
  token.start = p_message;
  return token;
}

static char advance(lexer* p_lexer) {
  return (++p_lexer->current)[-1];
}

static bool match(lexer* p_lexer, char p_expect) {
  if (at_end(p_lexer) || *p_lexer->current != p_expect) {
    return false;
  }
  return ++p_lexer->current; // always true
}

static char peek(lexer* p_lexer) {
  return *p_lexer->current;
}

static char peek1(lexer* p_lexer) {
  if (at_end(p_lexer)) {
    return '\0';
  }
  return p_lexer->current[1];
}

static bool is_allowed_identifier(char c);

// ---------------------------------------------------------------------------
// the P3 byte scanners: one extent algorithm, two riders.
//
// an f-string is ONE token, so the lexer must know where its interpolations
// end (a region's strings may quote the body's own quote char), and the
// parser must later re-derive the same extents when it re-opens the token.
// both ride these scanners - one source of truth, no drift. they are pure
// byte walks: extents only, no line counting (the lexer catches its own
// position up afterwards; the parser only ever wanted the span).
//
// p_wall semantics: NULL means the source's NUL is the wall (whole-source
// lexing); otherwise a hard stop (region lexing stops at the interpolation's
// closing brace, the parser's re-scan at the token's closing quote).
static bool at_wall(const char* p, const char* p_wall) {
  if (p_wall != NULL) {
    return p >= p_wall;
  }
  return *p == '\0';
}

// P5: the byte scanners recurse one level per nested quoted string inside
// an interpolation (a quoted body may ride an f-prefix whose regions may
// quote again, ...). the cap turns a hostile `f"${f"${f"${...` chain into
// the clean unterminated-string error instead of a stack overflow. it
// matches the parser's own OK_PARSER_MAX_DEPTH (the token can never be
// deeper than the tree it would build).
#define OK_SCAN_MAX_DEPTH 200u

static const char* scan_region(const char* p, const char* p_wall, const char** p_died, uint32_t p_depth);

// scan a string body (from just past its opening quote) to its closing
// quote. an escape never closes anything (the P2 ruling: `\` + newline
// rides multiline only); p_interpolate skips ${...} regions, which may
// quote the body's own quote char. returns the closing quote's position,
// or NULL with *p_died at the byte the wall was found on.
static const char* scan_body(const char* p,
                             const char* p_wall,
                             char p_quote,
                             bool p_multiline,
                             bool p_interpolate,
                             const char** p_died,
                             uint32_t p_depth) {
  for (;;) {
    if (at_wall(p, p_wall)) {
      *p_died = p;
      return NULL;
    }
    const char c = *p;
    if (c == '\\') {
      ++p; // the escape byte never closes anything
      if (at_wall(p, p_wall)) {
        *p_died = p;
        return NULL; // a dangling escape at the wall
      }
      if (*p == '\n' && !p_multiline) {
        *p_died = p;
        return NULL; // a single-line string dies at its newline
      }
      ++p;
      continue;
    }
    if (c == '\n' && !p_multiline) {
      *p_died = p;
      return NULL;
    }
    if (p_interpolate && c == '$' && !at_wall(p + 1, p_wall) && p[1] == '{') {
      if (p_depth >= OK_SCAN_MAX_DEPTH) {
        *p_died = p; // the nesting cap: report it as the wall winning
        return NULL;
      }
      const char* close = scan_region(p + 2, p_wall, p_died, p_depth + 1);
      if (close == NULL) {
        return NULL;
      }
      p = close + 1;
      continue;
    }
    if (c == p_quote) {
      return p;
    }
    ++p;
  }
}

// scan a ${...} region (from just past its '${') to its matching brace.
// braces nest (map literals); line comments hide theirs to the newline; a
// quoted string hides its own - and one riding an f/fm prefix is an f-string
// itself, whose regions may in turn quote this region's quote char, so its
// body takes the same scan one level down. (the prefix test mirrors the
// lexer's own token start: an f before the quote only counts when the byte
// before IT cannot continue an identifier - `xf"` is the identifier `xf`
// then a plain string. `2f"` disagrees only in garbage source, where a
// number glued to an f-string is not a program either side accepts.)
static const char* scan_region(const char* p, const char* p_wall, const char** p_died, uint32_t p_depth) {
  uint32_t depth = 1;
  for (;;) {
    if (at_wall(p, p_wall)) {
      *p_died = p;
      return NULL;
    }
    const char c = *p;
    if (c == '/' && !at_wall(p + 1, p_wall) && p[1] == '/') {
      p += 2; // a line comment hides braces to its newline
      while (!at_wall(p, p_wall) && *p != '\n') {
        ++p;
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      const bool f_prefix = p[-1] == 'f' && !is_allowed_identifier(p[-2]);
      const bool m_prefix = p[-1] == 'm' && !is_allowed_identifier(p[-2]);
      const bool fm_prefix = p[-1] == 'm' && p[-2] == 'f' && !is_allowed_identifier(p[-3]);
      ++p; // the quote
      const char* close =
          scan_body(p, p_wall, c, m_prefix || fm_prefix, f_prefix || fm_prefix, p_died, p_depth);
      if (close == NULL) {
        return NULL;
      }
      p = close + 1;
      continue;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (--depth == 0) {
        return p;
      }
    }
    ++p;
  }
}

// the P3 prefixed strings ride the same scan: f"..." interpolates, m"..."
// spans lines, fm"..." stacks both (the prefix lives in the token bytes; the
// parser's slice arithmetic knows each type's prefix length). a successful
// token reports its OPENING line and column - a multiline body crosses
// newlines, and the unterminated error especially must name the opening line
// (the m"" ruling), not whatever line the wall was found on.
static token string(lexer* p_lexer, char p_start, bool p_multiline, bool p_interpolate) {
  const uint32_t opening_line = p_lexer->line_info.line;
  const char* opening_line_start = p_lexer->line_start;
  // the body scan is a pure byte walk; the wall is the region's hard end
  // when lexing inside an interpolation, else the source's NUL.
  const char* const wall = p_lexer->end;
  const char* const body = p_lexer->current; // just past the opening quote
  const char* died = body;
  const char* close = scan_body(body, wall, p_start, p_multiline, p_interpolate, &died, 0);
  if (close == NULL) {
    // catch up to where the scan died, counting lines honestly (the error
    // span and every token after it keep real positions)
    while (p_lexer->current < died) {
      if (*p_lexer->current == '\n') {
        p_lexer->line_info.line++;
        p_lexer->line_start = p_lexer->current + 1;
      }
      advance(p_lexer);
    }
    token error = create_error_token(p_lexer, "unterminated string.");
    error.line_info.line = opening_line;
    error.line_info.column = (uint32_t)(p_lexer->start - opening_line_start) + 1;
    return error;
  }
  // catch up to the closing quote, counting lines honestly (a multiline
  // body crosses newlines; every later token anchors to its real line)
  while (p_lexer->current < close) {
    if (*p_lexer->current == '\n') {
      p_lexer->line_info.line++;
      p_lexer->line_start = p_lexer->current + 1;
    }
    advance(p_lexer);
  }
  advance(p_lexer); // the closing quote
  const token_type type = p_multiline ? (p_interpolate ? TOKEN_FMSTRING : TOKEN_MSTRING)
                                      : (p_interpolate ? TOKEN_FSTRING : TOKEN_STRING);
  token out = create_token(p_lexer, type);
  out.line_info.line = opening_line;
  out.line_info.column = (uint32_t)(p_lexer->start - opening_line_start) + 1;
  return out;
}

// the parser's f-string re-scan: where does the ${...} region starting at
// p_start end? the same scanner the lexer rode to close the token - one
// source of truth for the interpolation extents, so they can never drift
// from the token's own boundaries. p_wall is the token body's end (its
// closing quote). returns the matching '}', or NULL when the wall wins.
const char* ok_fstring_region_end(const char* p_start, const char* p_wall) {
  const char* died = p_start;
  return scan_region(p_start, p_wall, &died, 0);
}

static token number(lexer* p_lexer) {
  while (isdigit(peek(p_lexer))) {
    advance(p_lexer);
  }
  if (peek(p_lexer) == '.' && isdigit(peek1(p_lexer))) {
    advance(p_lexer);
    while (isdigit(peek(p_lexer))) {
      advance(p_lexer);
    }
  }
  return create_token(p_lexer, TOKEN_NUMBER);
}

static bool is_allowed_identifier(char c) {
  switch (c) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ';':
  case ':':
  case ',':
  case '.':
  case '+':
  case '-':
  case '*':
  case '/':
  case '!':
  case '=':
  case '<':
  case '>':
  case '\'':
  case '"':
  case '\\':
  case '#':
  case '@':
  case '$':
  case '%':
  case '^':
  case '&':
  case '|':
  case '~':
  case '?':
  case '`':
  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case '\0':
    return false;
  }
  return true; // maybe lol! (no utf8 validation)
}

static token_type
check_keyword(lexer* p_lexer, uint32_t p_start, uint32_t p_length, const char* p_rest, token_type p_type) {
  if (p_lexer->current - p_lexer->start == p_start + p_length &&
      memcmp(p_lexer->start + p_start, p_rest, p_length) == 0) {
    return p_type;
  }
  return TOKEN_IDENTIFIER;
}

static token_type lookup_identifier(lexer* p_lexer) {
  switch (p_lexer->start[0]) {
  case 'p': {
    if (p_lexer->current - p_lexer->start > 1 && p_lexer->start[1] == 'u') {
      return check_keyword(p_lexer, 2, 1, "b", TOKEN_PUB);
    }
    return check_keyword(p_lexer, 1, 4, "rint", TOKEN_PRINT);
  }
  case 'l':
    return check_keyword(p_lexer, 1, 2, "et", TOKEN_LET);
  case 'b':
    return check_keyword(p_lexer, 1, 4, "reak", TOKEN_BREAK);
  case 'm':
    return check_keyword(p_lexer, 1, 2, "ut", TOKEN_MUT);
  case 'g':
    return check_keyword(p_lexer, 1, 3, "lob", TOKEN_GLOB);
  case 'w':
    return check_keyword(p_lexer, 1, 4, "hile", TOKEN_WHILE);
  case 'r':
    return check_keyword(p_lexer, 1, 5, "eturn", TOKEN_RETURN);
  case 'd': {
    if (p_lexer->current - p_lexer->start > 1 && p_lexer->start[1] == 'e') {
      token_type t = check_keyword(p_lexer, 2, 4, "ferr", TOKEN_DEFERR);
      if (t != TOKEN_IDENTIFIER) {
        return t;
      }
      return check_keyword(p_lexer, 2, 3, "fer", TOKEN_DEFER);
    }
    break;
  }
  case 'f': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'u':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_FU);
      case 'a':
        return check_keyword(p_lexer, 2, 3, "lse", TOKEN_FALSE);
      case 'o':
        return check_keyword(p_lexer, 2, 1, "r", TOKEN_FOR);
      case 'r':
        return check_keyword(p_lexer, 2, 2, "om", TOKEN_FROM);
      }
    }
    break;
  }
  case 'a': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 's':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_AS);
      case 'n':
        return check_keyword(p_lexer, 2, 1, "d", TOKEN_AND);
      }
    }
    break;
  }
  case 'i': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'm':
        return check_keyword(p_lexer, 2, 4, "port", TOKEN_IMPORT);
      case 'f':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_IF);
      case 's':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_IS);
      }
    }
    break;
  }
  case 'c': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'l':
        return check_keyword(p_lexer, 2, 3, "ass", TOKEN_CLASS); // lol
      case 'o':
        return check_keyword(p_lexer, 2, 6, "ntinue", TOKEN_CONTINUE);
      case 'a':
        return check_keyword(p_lexer, 2, 3, "tch", TOKEN_CATCH);
      }
    }
    break;
  }
  case 'n': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'u':
        return check_keyword(p_lexer, 2, 2, "ll", TOKEN_NULL);
      case 'o':
        return check_keyword(p_lexer, 2, 1, "t", TOKEN_NOT);
      }
    }
    break;
  }
  case 't': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'h': {
        if (p_lexer->current - p_lexer->start > 2) {
          switch (p_lexer->start[2]) {
          case 'i':
            return check_keyword(p_lexer, 3, 1, "s", TOKEN_THIS);
          case 'r':
            return check_keyword(p_lexer, 3, 2, "ow", TOKEN_THROW);
          case 'e':
            return check_keyword(p_lexer, 3, 1, "n", TOKEN_THEN);
          }
        }
        break;
      }
      case 'r': {
        if (p_lexer->current - p_lexer->start > 2) {
          switch (p_lexer->start[2]) {
          case 'u':
            return check_keyword(p_lexer, 3, 1, "e", TOKEN_TRUE);
          case 'y':
            return check_keyword(p_lexer, 3, 0, "", TOKEN_TRY);
          }
        }
        break;
      }
      }
    }
    break;
  }
  case 's': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'u':
        return check_keyword(p_lexer, 2, 3, "per", TOKEN_SUPER);
      case 't':
        return check_keyword(p_lexer, 2, 4, "atic", TOKEN_STATIC);
      }
    }
    break;
  }
  case 'y':
    return check_keyword(p_lexer, 1, 4, "ield", TOKEN_YIELD);
  case 'o': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 't':
        return check_keyword(p_lexer, 2, 7, "herwise", TOKEN_OTHERWISE);
      case 'r':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_OR);
      }
    }
    break;
  }
  case 'e': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'x':
        return check_keyword(p_lexer, 2, 4, "port", TOKEN_EXPORT);
      case 'l':
        return check_keyword(p_lexer, 2, 2, "se", TOKEN_ELSE);
      }
    }
    break;
  }
  }
  return TOKEN_IDENTIFIER;
}

static token identifier(lexer* p_lexer) {
  while (is_allowed_identifier(peek(p_lexer))) {
    advance(p_lexer);
  }
  const token_type type = lookup_identifier(p_lexer);
  return create_token(p_lexer, type);
}

token lexer_lex(lexer* p_lexer) {
start:
  p_lexer->start = p_lexer->current;
  if (at_end(p_lexer)) {
    return create_token(p_lexer, TOKEN_EOF);
  }

  const char c = advance(p_lexer);
  switch (c) {
  case '(':
    return create_token(p_lexer, TOKEN_LEFT_PAREN);
  case ')':
    return create_token(p_lexer, TOKEN_RIGHT_PAREN);
  case '{':
    return create_token(p_lexer, TOKEN_LEFT_BRACE);
  case '}':
    return create_token(p_lexer, TOKEN_RIGHT_BRACE);
  case '[':
    return create_token(p_lexer, TOKEN_LEFT_BRACKET);
  case ']':
    return create_token(p_lexer, TOKEN_RIGHT_BRACKET);
  case ';':
    return create_token(p_lexer, TOKEN_SEMICOLON);
  case ':':
    return create_token(p_lexer, TOKEN_COLON);
  case ',':
    return create_token(p_lexer, TOKEN_COMMA);
  case '.':
    if (peek(p_lexer) == '.' && peek1(p_lexer) == '.') {
      advance(p_lexer);
      advance(p_lexer);
      return create_token(p_lexer, TOKEN_ELLIPSIS);
    }
    return create_token(p_lexer, TOKEN_DOT);
  case '?':
    return create_token(p_lexer, TOKEN_QUESTION);
  case '!':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    // longest match first: === (identity) before == before =
    return create_token(p_lexer,
                        match(p_lexer, '=') ? match(p_lexer, '=') ? TOKEN_TRIPLE_EQUAL : TOKEN_EQUAL : TOKEN_ASSIGN);
  case '<':
    return create_token(p_lexer,
                        match(p_lexer, '=')   ? TOKEN_LESS_EQUAL
                        : match(p_lexer, '<') ? match(p_lexer, '=') ? TOKEN_SHIFT_LEFT_EQUAL : TOKEN_SHIFT_LEFT
                                              : TOKEN_LESS);
  case '>':
    return create_token(p_lexer,
                        match(p_lexer, '=')   ? TOKEN_GREATER_EQUAL
                        : match(p_lexer, '>') ? match(p_lexer, '=') ? TOKEN_SHIFT_RIGHT_EQUAL : TOKEN_SHIFT_RIGHT
                                              : TOKEN_GREATER);
  case '+':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
  case '-':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
  case '*':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_ASTERISK_EQUAL : TOKEN_ASTERISK);
  case '^':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_CARET_EQUAL : TOKEN_CARET);
  case '|':
    return create_token(p_lexer, match(p_lexer, '|') ? TOKEN_OR : match(p_lexer, '=') ? TOKEN_BAR_EQUAL : TOKEN_BAR);
  case '&':
    return create_token(p_lexer,
                        match(p_lexer, '&')   ? TOKEN_AND
                        : match(p_lexer, '=') ? TOKEN_AMPERSAND_EQUAL
                                              : TOKEN_AMPERSAND);
  case '%':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_MODULO_EQUAL : TOKEN_MODULO);
  case '/': {
    if (match(p_lexer, '/')) {
      while (!at_end(p_lexer) && peek(p_lexer) != '\n') {
        advance(p_lexer);
      }
      goto start;
    }
    if (match(p_lexer, '='))
      return create_token(p_lexer, TOKEN_SLASH_EQUAL);
    return create_token(p_lexer, TOKEN_SLASH);
  }
  case '\n':
    p_lexer->line_info.line++;
    p_lexer->line_start = p_lexer->current; // current already stepped past the newline
    goto start;
  case ' ':
  case '\t':
  case '\r':
    goto start;
  case '\'':
    return string(p_lexer, '\'', false, false);
  case '"':
    return string(p_lexer, '"', false, false);
  default: {
    if (isdigit(c)) {
      return number(p_lexer);
    }
    // the P3 string prefixes: f" m" fm" (either quote char). only immediate
    // adjacency counts - `f "x"` is an identifier then a separate string,
    // and `mf"` stays the identifier `mf` (fm is the ruled stacking order).
    // anything else falls through to the identifier scan unchanged.
    if (c == 'f' || c == 'm') {
      const bool interpolate = (c == 'f');
      if (peek(p_lexer) == '\'' || peek(p_lexer) == '"') {
        const char quote = advance(p_lexer);
        // f" interpolates without spanning; m" spans without interpolating
        return string(p_lexer, quote, !interpolate, interpolate);
      }
      if (interpolate && peek(p_lexer) == 'm' &&
          (peek1(p_lexer) == '\'' || peek1(p_lexer) == '"')) {
        advance(p_lexer); // the m
        const char quote = advance(p_lexer);
        return string(p_lexer, quote, true, true);
      }
    }
    if (is_allowed_identifier(c)) {
      return identifier(p_lexer);
    }
  }
  }
  return create_error_token(p_lexer, "unexpected character.");
}
