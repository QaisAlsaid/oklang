#include "okoperator.h"

operator_type operator_type_from_token_type(const token_type p_token_type, bool p_is_prefix) {
  switch (p_token_type) {
  case TOKEN_PLUS:
    return p_is_prefix ? OPERATOR_PRE_PLUS : OPERATOR_BIN_PLUS;
  case TOKEN_MINUS:
    return p_is_prefix ? OPERATOR_PRE_MINUS : OPERATOR_BIN_MINUS;
  case TOKEN_ASTERISK:
    return OPERATOR_BIN_ASTERISK;
  case TOKEN_SLASH:
    return OPERATOR_BIN_SLASH;
  case TOKEN_MODULO:
    return OPERATOR_BIN_MODULO;
  case TOKEN_CARET:
    return OPERATOR_BIN_CARET;
  case TOKEN_BANG:
    return OPERATOR_PRE_BANG;
  case TOKEN_EQUAL:
    return OPERATOR_BIN_EQUALS;
  case TOKEN_BANG_EQUAL:
    return OPERATOR_BIN_BANG_EQUAL;
  case TOKEN_GREATER:
    return OPERATOR_BIN_GREATER;
  case TOKEN_GREATER_EQUAL:
    return OPERATOR_BIN_GREATER_EQUAL;
  case TOKEN_LESS:
    return OPERATOR_BIN_LESS;
  case TOKEN_LESS_EQUAL:
    return OPERATOR_BIN_LESS_EQUAL;
  case TOKEN_AS:
    return OPERATOR_BIN_AS;
  case TOKEN_LEFT_PAREN:
    return OPERATOR_POS_CALL;
  case TOKEN_LEFT_BRACKET:
    return OPERATOR_POS_SUBSCRIPT;
  case TOKEN_AMPERSAND:
    return OPERATOR_BIN_AMPERSAND;
  case TOKEN_BAR:
    return OPERATOR_BIN_BAR;
  case TOKEN_TILED:
    return OPERATOR_BIN_TILED;
  case TOKEN_PLUS_EQUAL:
    return OPERATOR_PLUS_EQUAL;
  case TOKEN_MINUS_EQUAL:
    return OPERATOR_MINUS_EQUAL;
  case TOKEN_ASTERISK_EQUAL:
    return OPERATOR_ASTERISK_EQUAL;
  case TOKEN_SLASH_EQUAL:
    return OPERATOR_SLASH_EQUAL;
  case TOKEN_CARET_EQUAL:
    return OPERATOR_CARET_EQUAL;
  case TOKEN_MODULO_EQUAL:
    return OPERATOR_MODULO_EQUAL;
  case TOKEN_AMPERSAND_EQUAL:
    return OPERATOR_AMPERSAND_EQUAL;
  case TOKEN_BAR_EQUAL:
    return OPERATOR_BAR_EQUAL;
  case TOKEN_TILED_EQUAL:
    return OPERATOR_TILED_EQUAL;
  case TOKEN_SHIFT_LEFT:
    return OPERATOR_SHIFT_LEFT;
  case TOKEN_SHIFT_RIGHT:
    return OPERATOR_SHIFT_RIGHT;
  case TOKEN_SHIFT_LEFT_EQUAL:
    return OPERATOR_SHIFT_LEFT_EQUAL;
  case TOKEN_SHIFT_RIGHT_EQUAL:
    return OPERATOR_SHIFT_RIGHT_EQUAL;
  case TOKEN_NOT:
    return OPERATOR_PRE_NOT;
  case TOKEN_AND:
    return OPERATOR_LOG_AND;
  case TOKEN_OR:
    return OPERATOR_LOG_OR;
  default:
    return OPERATOR_UNKNOWN;
  }
}

ok_string_view operator_type_to_string(const operator_type p_operator_type) {
  switch (p_operator_type) {
#define X(type, str)                                                                                                   \
  case type:                                                                                                           \
    return ok_create_string_view(str, OK_STRING_VIEW_CALCULATE_LENGTH, true);
    OPERATOR_LIST_X(X)
#undef X
  default:
    return ok_create_string_view("UNKNOWN_OPERATOR", OK_STRING_VIEW_CALCULATE_LENGTH, true);
  }
}
