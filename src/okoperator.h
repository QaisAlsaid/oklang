#ifndef OK_OPERATOR_H
#define OK_OPERATOR_H

#include "oktoken.h"
#include "okutils.h"

#define OPERATOR_LIST_X(X)                                                                                             \
  X(OPERATOR_PRE_PLUS, "+")                                                                                            \
  X(OPERATOR_PRE_MINUS, "-")                                                                                           \
  X(OPERATOR_BIN_PLUS, "+")                                                                                            \
  X(OPERATOR_BIN_MINUS, "-")                                                                                           \
  X(OPERATOR_BIN_ASTERISK, "*")                                                                                        \
  X(OPERATOR_BIN_SLASH, "/")                                                                                           \
  X(OPERATOR_BIN_MODULO, "%")                                                                                          \
  X(OPERATOR_BIN_CARET, "^")                                                                                           \
  X(OPERATOR_PRE_BANG, "!")                                                                                            \
  X(OPERATOR_BIN_EQUALS, "==")                                                                                         \
  X(OPERATOR_BIN_BANG_EQUAL, "!=")                                                                                     \
  X(OPERATOR_BIN_GREATER, ">")                                                                                         \
  X(OPERATOR_BIN_GREATER_EQUAL, ">=")                                                                                  \
  X(OPERATOR_BIN_LESS, "<")                                                                                            \
  X(OPERATOR_BIN_LESS_EQUAL, "<=")                                                                                     \
  X(OPERATOR_POS_CALL, "()")                                                                                           \
  X(OPERATOR_POS_SUBSCRIPT, "[]")                                                                                      \
  X(OPERATOR_BIN_AMPERSAND, "&")                                                                                       \
  X(OPERATOR_BIN_BAR, "|")                                                                                             \
  X(OPERATOR_BIN_TILED, "~")                                                                                           \
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
  X(OPERATOR_PRE_NOT, "not")                                                                                           \
  X(OPERATOR_LOG_OR, "or")                                                                                             \
  X(OPERATOR_LOG_AND, "and")

typedef enum {
  OPERATOR_UNKNOWN = 0,

  // binary operators

  OPERATOR_RANGE_BIN_BEGIN,

  // binary that can operate on any value

  OPERATOR_RANGE_BIN_VALUE_BEGIN = OPERATOR_RANGE_BIN_BEGIN,
  OPERATOR_BIN_PLUS,
  OPERATOR_BIN_MINUS,
  OPERATOR_BIN_ASTERISK,
  OPERATOR_BIN_SLASH,
  OPERATOR_BIN_MODULO,
  OPERATOR_BIN_CARET,
  OPERATOR_BIN_AMPERSAND,
  OPERATOR_BIN_BAR,
  OPERATOR_BIN_TILED,
  OPERATOR_BIN_EQUALS,
  OPERATOR_BIN_BANG_EQUAL,
  OPERATOR_BIN_GREATER,
  OPERATOR_BIN_GREATER_EQUAL,
  OPERATOR_BIN_LESS,
  OPERATOR_BIN_LESS_EQUAL,
  OPERATOR_BIN_AS,
  OPERATOR_RANGE_BIN_VALUE_END = OPERATOR_BIN_AS,

  // binary that can only operate on lvalues
  OPERATOR_RANGE_BIN_LVALUE_BEGIN,
  OPERATOR_PLUS_EQUAL = OPERATOR_RANGE_BIN_LVALUE_BEGIN,
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
  OPERATOR_RANGE_BIN_LVALUE_END = OPERATOR_SHIFT_RIGHT_EQUAL,
  OPERATOR_RANGE_BIN_END = OPERATOR_RANGE_BIN_LVALUE_END,

  // prefix
  OPERATOR_RANGE_PRE_BEGIN,

  // prefix that can operate on any value
  OPERATOR_RANGE_PRE_VALUE_BEGIN = OPERATOR_RANGE_PRE_BEGIN,
  OPERATOR_PRE_PLUS = OPERATOR_RANGE_PRE_VALUE_BEGIN,
  OPERATOR_PRE_MINUS,
  OPERATOR_PRE_NOT,
  OPERATOR_PRE_BANG,
  OPERATOR_RANGE_PRE_VALUE_END = OPERATOR_PRE_BANG,
  OPERATOR_RANGE_PRE_END = OPERATOR_RANGE_PRE_VALUE_END,

  OPERATOR_RANGE_POS_BEGIN,
  OPERATOR_RANGE_POS_VALUE_BEGIN = OPERATOR_RANGE_POS_BEGIN,
  OPERATOR_POS_CALL = OPERATOR_RANGE_POS_VALUE_BEGIN,
  OPERATOR_POS_SUBSCRIPT,
  OPERATOR_RANGE_POS_VALUE_END = OPERATOR_POS_SUBSCRIPT,
  OPERATOR_RANGE_POS_END = OPERATOR_RANGE_POS_VALUE_END,

  OPERATOR_RANGE_LOGICAL_BEGIN,
  OPERATOR_LOG_OR = OPERATOR_RANGE_LOGICAL_BEGIN,
  OPERATOR_LOG_AND,
  OPERATOR_RANGE_LOGICAL_END = OPERATOR_LOG_AND,
} operator_type;

operator_type operator_type_from_token_type(const token_type p_token_type, bool is_prefix);
ok_string_view operator_type_to_string(const operator_type p_operator_type);

#endif // OK_OPERATOR_H
