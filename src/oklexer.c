#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "oklexer.h"
#include "oktoken.h"

void lexer_init(lexer* p_lexer, const char* p_src) {
  p_lexer->start = p_src;
  p_lexer->current = p_src;
  p_lexer->line_info.line = 1;
  p_lexer->line_info.offset = 1;
}

void lexer_free(lexer* p_lexer) {
  p_lexer->start = NULL;
  p_lexer->current = NULL;
  p_lexer->line_info.line = 0;
  p_lexer->line_info.offset = 0;
}

static bool at_end(lexer* p_lexer) {
  return *p_lexer->current == '\0';
}

static token create_token(lexer* p_lexer, token_type p_type) {
  token token;
  token.type = p_type;
  token.line_info = p_lexer->line_info;
  token.length = (uint32_t)(p_lexer->current - p_lexer->start);
  token.start = p_lexer->start;
  return token;
}

static token create_error_token(lexer* p_lexer, const char* p_message) {
  token token;
  token.type = TOKEN_ERROR;
  token.line_info = p_lexer->line_info;
  token.length = strlen(p_message);
  token.start = p_message;
  return token;
}

static char advance(lexer* p_lexer) {
  p_lexer->line_info.offset++;
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

static token string(lexer* p_lexer, char p_start, bool p_multiline) {
  while (peek(p_lexer) != p_start && !at_end(p_lexer)) {
    if (peek(p_lexer) == '\n' && !p_multiline) {
      token error = create_error_token(p_lexer, "unterminated string.");
      return error;
    } else if (peek(p_lexer) == '\n') {
      p_lexer->line_info.line++;
    }
    advance(p_lexer);
  }
  if (at_end(p_lexer)) {
    return create_error_token(p_lexer, "unterminated string.");
  }
  advance(p_lexer);
  return create_token(p_lexer, TOKEN_STRING);
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
  case 'p':
    return check_keyword(p_lexer, 1, 4, "rint", TOKEN_PRINT);
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
  case 'f': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'u':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_FU);
      case 'a':
        return check_keyword(p_lexer, 2, 3, "lse", TOKEN_FALSE);
      case 'i':
        return check_keyword(p_lexer, 2, 6, "nalize", TOKEN_FINALIZE);
      }
    }
    break;
  }
  case 'a': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 's': {
        if (p_lexer->current - p_lexer->start > 2) {
          switch (p_lexer->start[2]) {
          case 'y':
            return check_keyword(p_lexer, 3, 2, "nc", TOKEN_ASYNC);
          }
        }
        return check_keyword(p_lexer, 2, 0, "", TOKEN_AS);
      }
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
      case 'n':
        return check_keyword(p_lexer, 2, 6, "herits", TOKEN_INHERITS);
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
  case 'o': {
    if (p_lexer->current - p_lexer->start > 1) {
      switch (p_lexer->start[1]) {
      case 'p':
        return check_keyword(p_lexer, 2, 6, "erator", TOKEN_OPERATOR);
      case 'k':
        return check_keyword(p_lexer, 2, 0, "", TOKEN_OK);
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
    return create_token(p_lexer, TOKEN_DOT);
  case '?':
    return create_token(p_lexer, TOKEN_QUESTION);
  case '!':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_EQUAL : TOKEN_ASSIGN);
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
    return create_token(p_lexer,
                        match(p_lexer, '=')   ? TOKEN_PLUS_EQUAL
                        : match(p_lexer, '+') ? TOKEN_PLUS_PLUS
                                              : TOKEN_PLUS);
  case '-':
    return create_token(p_lexer,
                        match(p_lexer, '=')   ? TOKEN_MINUS_EQUAL
                        : match(p_lexer, '-') ? TOKEN_MINUS_MINUS
                        : match(p_lexer, '>') ? TOKEN_ARROW
                                              : TOKEN_MINUS);
  case '*':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_ASTERISK_EQUAL : TOKEN_ASTERISK);
  case '^':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_CARET_EQUAL : TOKEN_CARET);
  case '|':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_BAR_EQUAL : TOKEN_BAR);
  case '&':
    return create_token(p_lexer, match(p_lexer, '=') ? TOKEN_AMPERSAND_EQUAL : TOKEN_AMPERSAND);
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
    goto start;
  case ' ':
  case '\t':
  case '\r':
    goto start;
  case '\'':
    return string(p_lexer, '\'', false);
  case '"':
    return string(p_lexer, '"', false);
  default: {
    if (isdigit(c)) {
      return number(p_lexer);
    }
    if (is_allowed_identifier(c)) {
      return identifier(p_lexer);
    }
  }
  }
  return create_error_token(p_lexer, "unexpected character.");
}
