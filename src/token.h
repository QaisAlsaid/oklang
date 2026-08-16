#ifndef OK_TOKEN_HPP
#define OK_TOKEN_HPP

#include "source.h"
#include <stdint.h>

#define TOKEN_LIST_X(X)                                                                                                \
  X(TOKEN_ERROR, "error")                                                                                              \
  X(TOKEN_ILLEGAL, "illegal")                                                                                          \
  X(TOKEN_EOF, "")                                                                                                     \
  X(TOKEN_ASSIGN, "=")                                                                                                 \
  X(TOKEN_PLUS, "+")                                                                                                   \
  X(TOKEN_MINUS, "-")                                                                                                  \
  X(TOKEN_ASTERISK, "*")                                                                                               \
  X(TOKEN_SLASH, "/")                                                                                                  \
  X(TOKEN_MODULO, "%")                                                                                                 \
  X(TOKEN_CARET, "^")                                                                                                  \
  X(TOKEN_AMPERSAND, "&")                                                                                              \
  X(TOKEN_BAR, "|")                                                                                                    \
  X(TOKEN_TILED, "~")                                                                                                  \
  X(TOKEN_PLUS_PLUS, "++")                                                                                             \
  X(TOKEN_MINUS_MINUS, "--")                                                                                           \
  X(TOKEN_PLUS_EQUAL, "+=")                                                                                            \
  X(TOKEN_MINUS_EQUAL, "-=")                                                                                           \
  X(TOKEN_ASTERISK_EQUAL, "*=")                                                                                        \
  X(TOKEN_SLASH_EQUAL, "/=")                                                                                           \
  X(TOKEN_MODULO_EQUAL, "%=")                                                                                          \
  X(TOKEN_CARET_EQUAL, "^=")                                                                                           \
  X(TOKEN_AMPERSAND_EQUAL, "&=")                                                                                       \
  X(TOKEN_BAR_EQUAL, "|=")                                                                                             \
  X(TOKEN_TILED_EQUAL, "~=")                                                                                           \
  X(TOKEN_SHIFT_LEFT_EQUAL, "<<=")                                                                                     \
  X(TOKEN_SHIFT_RIGHT_EQUAL, ">>=")                                                                                    \
  X(TOKEN_SHIFT_LEFT, "<<")                                                                                            \
  X(TOKEN_SHIFT_RIGHT, ">>")                                                                                           \
  X(TOKEN_COMMA, ",")                                                                                                  \
  X(TOKEN_COLON, ":")                                                                                                  \
  X(TOKEN_SEMICOLON, ";")                                                                                              \
  X(TOKEN_DOT, ".")                                                                                                    \
  X(TOKEN_QUESTION, "?")                                                                                               \
  X(TOKEN_BANG, "!")                                                                                                   \
  X(TOKEN_BANG_EQUAL, "!=")                                                                                            \
  X(TOKEN_EQUAL, "==")                                                                                                 \
  X(TOKEN_LESS_EQUAL, "<=")                                                                                            \
  X(TOKEN_GREATER_EQUAL, ">=")                                                                                         \
  X(TOKEN_LESS, "<")                                                                                                   \
  X(TOKEN_GREATER, ">")                                                                                                \
  X(TOKEN_LEFT_PAREN, "(")                                                                                             \
  X(TOKEN_RIGHT_PAREN, ")")                                                                                            \
  X(TOKEN_LEFT_BRACE, "{")                                                                                             \
  X(TOKEN_RIGHT_BRACE, "}")                                                                                            \
  X(TOKEN_LEFT_BRACKET, "[")                                                                                           \
  X(TOKEN_RIGHT_BRACKET, "]")                                                                                          \
  X(TOKEN_ARROW, "->")                                                                                                 \
  X(TOKEN_IDENTIFIER, "identifier")                                                                                    \
  X(TOKEN_NUMBER, "number")                                                                                            \
  X(TOKEN_STRING, "string")                                                                                            \
  X(TOKEN_PRINT, "print")                                                                                              \
  X(TOKEN_IMPORT, "import")                                                                                            \
  X(TOKEN_AS, "as")                                                                                                    \
  X(TOKEN_FU, "fu")                                                                                                    \
  X(TOKEN_LET, "let")                                                                                                  \
  X(TOKEN_WHILE, "while")                                                                                              \
  X(TOKEN_FOR, "for")                                                                                                  \
  X(TOKEN_BREAK, "break")                                                                                              \
  X(TOKEN_CONTINUE, "continue")                                                                                        \
  X(TOKEN_IF, "if")                                                                                                    \
  X(TOKEN_ELSE, "else")                                                                                                \
  X(TOKEN_AND, "and")                                                                                                  \
  X(TOKEN_OR, "or")                                                                                                    \
  X(TOKEN_CLASS, "class")                                                                                              \
  X(TOKEN_SUPER, "super")                                                                                              \
  X(TOKEN_INHERITS, "inherits")                                                                                        \
  X(TOKEN_THIS, "this")                                                                                                \
  X(TOKEN_NULL, "null")                                                                                                \
  X(TOKEN_TRUE, "true")                                                                                                \
  X(TOKEN_FALSE, "false")                                                                                              \
  X(TOKEN_RETURN, "return")                                                                                            \
  X(TOKEN_NOT, "not")                                                                                                  \
  X(TOKEN_OK, "ok")                                                                                                    \
  X(TOKEN_OPERATOR, "operator")                                                                                        \
  X(TOKEN_GLOB, "glob")                                                                                                \
  X(TOKEN_EXPORT, "export")                                                                                            \
  X(TOKEN_MUT, "mut")                                                                                                  \
  X(TOKEN_STATIC, "static")                                                                                            \
  X(TOKEN_ASYNC, "async")                                                                                              \
  X(TOKEN_TRY, "try")                                                                                                  \
  X(TOKEN_CATCH, "catch")                                                                                              \
  X(TOKEN_THROW, "throw")                                                                                              \
  X(TOKEN_FINALIZE, "finalize")

typedef enum {
  TOKEN_ERROR = 0,
  TOKEN_ILLEGAL,
  TOKEN_EOF,
  TOKEN_ASSIGN,
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_ASTERISK,
  TOKEN_SLASH,
  TOKEN_MODULO,
  TOKEN_CARET,
  TOKEN_AMPERSAND,
  TOKEN_BAR,
  TOKEN_TILED,
  TOKEN_PLUS_PLUS,
  TOKEN_MINUS_MINUS,
  TOKEN_PLUS_EQUAL,
  TOKEN_MINUS_EQUAL,
  TOKEN_ASTERISK_EQUAL,
  TOKEN_SLASH_EQUAL,
  TOKEN_MODULO_EQUAL,
  TOKEN_CARET_EQUAL,
  TOKEN_AMPERSAND_EQUAL,
  TOKEN_BAR_EQUAL,
  TOKEN_TILED_EQUAL,
  TOKEN_SHIFT_LEFT_EQUAL,
  TOKEN_SHIFT_RIGHT_EQUAL,
  TOKEN_SHIFT_LEFT,
  TOKEN_SHIFT_RIGHT,
  TOKEN_COMMA,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  TOKEN_DOT,
  TOKEN_QUESTION,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_LESS_EQUAL,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_GREATER,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_ARROW,

  TOKEN_IDENTIFIER,
  TOKEN_NUMBER,
  TOKEN_STRING,

  TOKEN_PRINT,
  TOKEN_IMPORT,
  TOKEN_AS,
  TOKEN_FU,
  TOKEN_LET,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_CLASS,
  TOKEN_SUPER,
  TOKEN_INHERITS,
  TOKEN_THIS,
  TOKEN_NULL,
  TOKEN_TRUE,
  TOKEN_FALSE,
  TOKEN_RETURN,
  TOKEN_NOT,
  TOKEN_OK,
  TOKEN_OPERATOR,
  TOKEN_GLOB,
  TOKEN_EXPORT,
  TOKEN_MUT,
  TOKEN_STATIC,
  TOKEN_ASYNC,
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,
  TOKEN_FINALIZE,
} token_type;

typedef struct {
  token_type type;
  const char* start;
  line_info line_info;
  uint32_t length;
} token;

const char* token_type_to_string(token_type p_type);

#endif // OK_TOKEN_HPP
