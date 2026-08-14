#ifndef OK_PARSER_H
#define OK_PARSER_H

#include "lexer.h"
#include "source.h"
#include "token.h"
#include "ast.h"

#include <stdbool.h>

typedef enum {
  PREC_NONE = (int)0,
  PREC_ASSIGNMENT, // ? :
  PREC_CONDITIONAL,
  PREC_OR,
  PREC_AND,
  PREC_BITWISE_OR,
  PREC_BITWISE_XOR,
  PREC_BITWISE_AND,
  PREC_EQUALITY,
  PREC_COMPARISION,
  PREC_SHIFT,
  PREC_AS,
  PREC_SUM,
  PREC_PRODUCT,
  PREC_EXPONENT,
  PREC_PREFIX,
  PREC_POSTFIX,
  PREC_CALL,
  PREC_MEMBER,
  PREC_SUBSCRIPT,
} precedence;

typedef struct {
  source* source;
  lexer lexer;
  token current;
  token previous;
  bool panic;
  bool had_error;
} parser;

typedef enum {
  PARSE_OK,
  PARSE_ERROR
} parse_status;

typedef struct {
  parse_status status;
  ast_root* root;
} parse_result;

void parse_result_deinit(parse_result* parse_result);

void parser_init(parser* p_parser, source* p_source);
void parser_deinit(parser* p_parser);
parse_result parser_parse(parser* p_parser);

#endif // OK_PARSER_H
