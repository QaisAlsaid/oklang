#ifndef OK_OPERATOR_H
#define OK_OPERATOR_H

#include "token.h"
#include "utils.h"

#define OPERATOR_LIST_X(X)                                                                                             \
  X(OPERATOR_PLUS, "+")                                                                                                \
  X(OPERATOR_MINUS, "-")                                                                                               \
  X(OPERATOR_ASTERISK, "*")                                                                                            \
  X(OPERATOR_SLASH, "/")                                                                                               \
  X(OPERATOR_MODULO, "%")                                                                                              \
  X(OPERATOR_CARET, "^")                                                                                               \
  X(OPERATOR_BANG, "!")                                                                                                \
  X(OPERATOR_EQUAL, "==")                                                                                              \
  X(OPERATOR_BANG_EQUAL, "!=")                                                                                         \
  X(OPERATOR_GREATER, ">")                                                                                             \
  X(OPERATOR_GREATER_EQUAL, ">=")                                                                                      \
  X(OPERATOR_LESS, "<")                                                                                                \
  X(OPERATOR_LESS_EQUAL, "<=")                                                                                         \
  X(OPERATOR_CALL, "()")                                                                                               \
  X(OPERATOR_SUBSCRIPT, "[]")                                                                                          \
  X(OPERATOR_AMPERSAND, "&")                                                                                           \
  X(OPERATOR_BAR, "|")                                                                                                 \
  X(OPERATOR_TILED, "~")                                                                                               \
  X(OPERATOR_PLUS_PLUS, "++")                                                                                          \
  X(OPERATOR_MINUS_MINUS, "--")                                                                                        \
  X(OPERATOR_PLUS_EQUAL, "+=")                                                                                         \
  X(OPERATOR_MINUS_EQUAL, "-=")                                                                                        \
  X(OPERATOR_ASTERISK_EQUAL, "*=")                                                                                     \
  X(OPERATOR_SLASH_EQUAL, "/=")                                                                                        \
  X(OPERATOR_MODULO_EQUAL, "%=")                                                                                       \
  X(OPERATOR_CARET_EQUAL, "^=")                                                                                        \
  X(OPERATOR_AMPERSAND_EQUAL, "&=")                                                                                    \
  X(OPERATOR_BAR_EQUAL, "|=")                                                                                          \
  X(OPERATOR_TILED_EQUAL, "~=")                                                                                        \
  X(OPERATOR_SHIFT_LEFT, "<<")                                                                                         \
  X(OPERATOR_SHIFT_RIGHT, ">>")                                                                                        \
  X(OPERATOR_SHIFT_LEFT_EQUAL, "<<=")                                                                                  \
  X(OPERATOR_SHIFT_RIGHT_EQUAL, ">>=")                                                                                 \
  X(OPERATOR_NOT, "not")                                                                                               \
  X(OPERATOR_OR, "or")                                                                                                 \
  X(OPERATOR_AND, "and")

typedef enum {
  OPERATOR_UNKNOWN = 0,
  OPERATOR_PLUS,
  OPERATOR_MINUS,
  OPERATOR_ASTERISK,
  OPERATOR_SLASH,
  OPERATOR_MODULO,
  OPERATOR_CARET,
  OPERATOR_BANG,
  OPERATOR_EQUAL,
  OPERATOR_BANG_EQUAL,
  OPERATOR_GREATER,
  OPERATOR_GREATER_EQUAL,
  OPERATOR_LESS,
  OPERATOR_LESS_EQUAL,
  OPERATOR_CALL,
  OPERATOR_SUBSCRIPT,
  OPERATOR_AMPERSAND,
  OPERATOR_BAR,
  OPERATOR_TILED,
  OPERATOR_PLUS_PLUS,
  OPERATOR_MINUS_MINUS,
  OPERATOR_PLUS_EQUAL,
  OPERATOR_MINUS_EQUAL,
  OPERATOR_ASTERISK_EQUAL,
  OPERATOR_SLASH_EQUAL,
  OPERATOR_MODULO_EQUAL,
  OPERATOR_CARET_EQUAL,
  OPERATOR_AMPERSAND_EQUAL,
  OPERATOR_BAR_EQUAL,
  OPERATOR_TILED_EQUAL,
  OPERATOR_SHIFT_LEFT,
  OPERATOR_SHIFT_RIGHT,
  OPERATOR_SHIFT_LEFT_EQUAL,
  OPERATOR_SHIFT_RIGHT_EQUAL,
  OPERATOR_NOT,
  OPERATOR_OR,
  OPERATOR_AND,
} operator_type;

operator_type operator_type_from_token_type(const token_type p_token_type);
string_view operator_type_to_string(const operator_type p_operator_type);

#endif // OK_OPERATOR_H
