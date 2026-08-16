#include "parser.h"
#include "utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static precedence get_precedence(token_type p_type);

static ast_root* parse_root(parser* p_parser);
static ast_expression* parse_expression(parser* p_parser, precedence p_precedence);
static ast_statement* parse_statement(parser* p_parser);
static ast_statement* try_parse_declaration(parser* p_parser);
static ast_empty_statement* parse_empty_statement(parser* p_parser);
static ast_expression_statement* parse_expression_statement(parser* p_parser);
static ast_eof_statement* parse_eof_statement(parser* p_parser);

static bool advance(parser* p_parser);
static bool expect(parser* p_parser, token_type p_type, string p_message);
static token current(parser* p_parser);
static token peek(parser* p_parser);
static void sync_state(parser* p_parser);

static ast_expression* parse_unary_prefix(parser* p_parser, token p_trigger);
static ast_expression* parse_grouping(parser* p_parser, token p_trigger);
// static ast_expression* parse_map(parser* p_parser, token p_trigger);
// static ast_expression* parse_array(parser* p_parser, token p_trigger);
static ast_expression* parse_identifier(parser* p_parser, token p_trigger);
static ast_expression* parse_string(parser* p_parser, token p_trigger);
static ast_expression* parse_number(parser* p_parser, token p_trigger);
static ast_expression* parse_boolean(parser* p_parser, token p_trigger);
static ast_expression* parse_null(parser* p_parser, token p_trigger);
// static ast_expression* parse_this(parser* p_parser, token p_trigger);
// static ast_expression* parse_super(parser* p_parser, token p_trigger);

// static ast_expression* parse_assign(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
// is_right_associative, token p_trigger);
static ast_expression* parse_binary_infix(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
// static ast_expression* parse_conditional(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
// is_right_associative, token p_trigger); static ast_expression* parse_as(parser* p_parser, ast_expression* p_left,
// precedence p_precedence, bool is_right_associative, token p_trigger);

// static ast_expression* parse_unary_postfix(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
// is_right_associative, token p_trigger); static ast_expression* parse_call(parser* p_parser, ast_expression* p_left,
// precedence p_precedence, bool is_right_associative, token p_trigger); static ast_expression* parse_subscript(parser*
// p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);

typedef ast_expression* (*prefix_parse_function)(parser* p_parser, token p_trigger);
typedef ast_expression* (*infix_parse_function)(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);

typedef struct {
  prefix_parse_function prefix;
  infix_parse_function infix;
  precedence precedence;
} parse_rule;

static const parse_rule parse_rules[] = {
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_ILLEGAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    //[TOKEN_ASSIGN] = { NULL, parse_assign, PREC_ASSIGNMENT },
    [TOKEN_PLUS] = {parse_unary_prefix, parse_binary_infix, PREC_SUM},
    [TOKEN_MINUS] = {parse_unary_prefix, parse_binary_infix, PREC_SUM},
    [TOKEN_ASTERISK] = {NULL, parse_binary_infix, PREC_PRODUCT},
    [TOKEN_SLASH] = {NULL, parse_binary_infix, PREC_PRODUCT},
    [TOKEN_MODULO] = {NULL, parse_binary_infix, PREC_PRODUCT},
    [TOKEN_CARET] = {NULL, parse_binary_infix, PREC_BITWISE_XOR},
    [TOKEN_AMPERSAND] = {NULL, parse_binary_infix, PREC_AND},
    [TOKEN_BAR] = {NULL, parse_binary_infix, PREC_OR},
    //[TOKEN_PLUS_PLUS] = { parse_unary_prefix, parse_unary_postfix, PREC_POSTFIX },
    //[TOKEN_MINUS_MINUS] = { parse_unary_prefix, parse_unary_postfix, PREC_POSTFIX },
    [TOKEN_PLUS_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_MINUS_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_ASTERISK_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_SLASH_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_MODULO_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_CARET_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_AMPERSAND_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_BAR_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_SHIFT_LEFT_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_SHIFT_RIGHT_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    //[TOKEN_QUESTION] = { NULL, parse_conditional, PREC_NONE },
    [TOKEN_BANG] = {parse_unary_prefix, NULL, PREC_PREFIX},
    [TOKEN_BANG_EQUAL] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_LESS_EQUAL] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_GREATER_EQUAL] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_LESS] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_GREATER] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_LEFT_PAREN] = {parse_grouping, /*parse_call*/ NULL, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    //[TOKEN_LEFT_BRACE] = { parse_map, NULL, PREC_NONE },
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    //[TOKEN_LEFT_BRACKET] = { parse_array, NULL, PREC_NONE },
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_ARROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {parse_identifier, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {parse_number, NULL, PREC_NONE},
    [TOKEN_STRING] = {parse_string, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_AS] = {NULL, NULL, PREC_NONE},
    [TOKEN_FU] = {NULL, NULL, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_INHERITS] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_NULL] = {parse_null, NULL, PREC_NONE},
    [TOKEN_TRUE] = {parse_boolean, NULL, PREC_NONE},
    [TOKEN_FALSE] = {parse_boolean, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_NOT] = {parse_unary_prefix, NULL, PREC_PREFIX},
    [TOKEN_OK] = {NULL, NULL, PREC_NONE},
    [TOKEN_OPERATOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_GLOB] = {NULL, NULL, PREC_NONE},
    [TOKEN_EXPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MUT] = {NULL, NULL, PREC_NONE},
    [TOKEN_STATIC] = {NULL, NULL, PREC_NONE},
    [TOKEN_ASYNC] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRY] = {NULL, NULL, PREC_NONE},
    [TOKEN_CATCH] = {NULL, NULL, PREC_NONE},
    [TOKEN_THROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_FINALIZE] = {NULL, NULL, PREC_NONE},
};

void parser_init(parser* p_parser, source* p_source) {
  p_parser->panic = false;
  p_parser->had_error = false;
  p_parser->source = p_source;
  lexer_init(&p_parser->lexer, p_source->code);
  advance(p_parser);
}

void parser_free(parser* p_parser) {
  lexer_free(&p_parser->lexer);
  p_parser->source = NULL;
}

parse_result parser_parse(parser* p_parser) {
  ast_root* root = parse_root(p_parser);
  parse_result result;
  result.status = (root == NULL || p_parser->had_error) ? PARSE_ERROR : PARSE_OK;
  result.root = root;
  return result;
}

void parse_result_deinit(parse_result* parse_result) {
  ast_root_deinit(parse_result->root);
  free(parse_result->root);
  parse_result->root = NULL;
}

bool advance(parser* p_parser) {
  p_parser->previous = p_parser->current;
  for (;;) {
    p_parser->current = lexer_lex(&p_parser->lexer);
    if (p_parser->current.type != TOKEN_ERROR) {
      break;
    }
    string message = asprint("%*.s", p_parser->current.length, p_parser->current.start);
    report_at(&p_parser->panic,
              &p_parser->had_error,
              p_parser->current.type == TOKEN_EOF,
              p_parser->source,
              &p_parser->current,
              REPORT_SEVERITY_ERROR,
              &message);
    string_deinit(&message);
    return false;
  }
  return true;
}

bool expect(parser* p_parser, token_type p_type, string p_message) {
  if (p_parser->current.type == p_type) {
    return advance(p_parser);
  }
  report_at(&p_parser->panic,
            &p_parser->had_error,
            p_parser->current.type == TOKEN_EOF,
            p_parser->source,
            &p_parser->current,
            REPORT_SEVERITY_ERROR,
            &p_message);
  return false;
}

void sync_state(parser* p_parser) {
  p_parser->panic = false;
  while (p_parser->current.type != TOKEN_EOF) {
    switch (p_parser->previous.type) {
    case TOKEN_CLASS:
    case TOKEN_FU:
    case TOKEN_LET:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
    case TOKEN_SEMICOLON:
      return;
    default:;
    }
    advance(p_parser);
  }
}

ast_root* parse_root(parser* p_parser) {
  token trigger = p_parser->previous;
  ast_root* root = (ast_root*)malloc(sizeof(ast_root));
  ast_root_init(root, trigger);
  while (advance(p_parser) && p_parser->previous.type != TOKEN_EOF) {
    ast_statement* statement = try_parse_declaration(p_parser);
    if (statement != NULL && statement->node.node_type != AST_EMPTY_STATEMENT) {
      ast_statements_list_append(&root->statements, statement);
    }
    if (statement == NULL) {
      p_parser->had_error = true;
      p_parser->panic = true;
    }
  }
  if (p_parser->previous.type == TOKEN_EOF) {
    ast_statements_list_append(&root->statements, (ast_statement*)parse_eof_statement(p_parser));
  }
  return root;
}

ast_expression* parse_expression(parser* p_parser, precedence p_precedence) {
  token tok = p_parser->previous;
  const parse_rule prefix_rule = parse_rules[tok.type];
  if (prefix_rule.prefix == NULL) {
    string message = string_create("expected expression.", STRING_CALCULATE_LENGTH, false);
    report_at(&p_parser->panic,
              &p_parser->had_error,
              p_parser->current.type == TOKEN_EOF,
              p_parser->source,
              &p_parser->current,
              REPORT_SEVERITY_ERROR,
              &message);
    return NULL;
  }
  ast_expression* left = prefix_rule.prefix(p_parser, tok);
  if (left == NULL) {
    return NULL;
  }

  token lookahead = p_parser->current;
  while (lookahead.type != TOKEN_SEMICOLON && lookahead.type != TOKEN_EOF &&
         (p_precedence < parse_rules[lookahead.type].precedence)) {
    parse_rule infix_rule = parse_rules[lookahead.type];
    if (infix_rule.infix == NULL) {
      return left;
    }
    advance(p_parser);
    tok = p_parser->previous;
    lookahead = p_parser->current;
    left = infix_rule.infix(p_parser, left, infix_rule.precedence, false, tok); // move ownership
    lookahead = p_parser->current;
  }
  return left;
}

ast_expression* parse_unary_prefix(parser* p_parser, token p_trigger) {
  if (!advance(p_parser)) {
    return NULL;
  }
  ast_expression* opperand = parse_expression(p_parser, PREC_PREFIX);
  if (opperand == NULL) {
    return NULL;
  }
  ast_prefix_unary_expression* prefix = (ast_prefix_unary_expression*)malloc(sizeof(ast_prefix_unary_expression));
  ast_prefix_unary_expression_init(prefix, p_trigger, p_trigger.type, opperand);
  return (ast_expression*)prefix;
}

ast_expression* parse_grouping(parser* p_parser, token p_trigger) {
  if (!advance(p_parser)) {
    return NULL;
  }
  ast_expression* expr = parse_expression(p_parser, PREC_NONE);
  if (expr == NULL) {
    return NULL;
  }
  if (expect(p_parser, TOKEN_RIGHT_PAREN, string_create("expected ')'.", STRING_CALCULATE_LENGTH, false))) {
    return expr;
  }
  ast_node_deinit((ast_node*)expr);
  free(expr);
  return NULL;
}

ast_expression* parse_identifier(parser* p_parser, token p_trigger) {
  ast_identifier_expression* identifier = (ast_identifier_expression*)malloc(sizeof(ast_identifier_expression));
  ast_identifier_expression_init(identifier, p_trigger);
  return (ast_expression*)identifier;
}

ast_expression* parse_string(parser* p_parser, token p_trigger) {
  ast_string_expression* string = (ast_string_expression*)malloc(sizeof(ast_string_expression));
  ast_string_expression_init(string, p_trigger);
  return (ast_expression*)string;
}

ast_expression* parse_number(parser* p_parser, token p_trigger) {
  ast_number_expression* number = (ast_number_expression*)malloc(sizeof(ast_number_expression));
  ast_number_expression_init(number, p_trigger);
  return (ast_expression*)number;
}

ast_expression* parse_boolean(parser* p_parser, token p_trigger) {
  ast_boolean_expression* boolean = (ast_boolean_expression*)malloc(sizeof(ast_boolean_expression));
  ast_boolean_expression_init(boolean, p_trigger);
  return (ast_expression*)boolean;
}

ast_expression* parse_null(parser* p_parser, token p_trigger) {
  ast_null_expression* null = (ast_null_expression*)malloc(sizeof(ast_null_expression));
  ast_null_expression_init(null, p_trigger);
  return (ast_expression*)null;
}

ast_expression* parse_binary_infix(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  advance(p_parser);
  ast_expression* right = parse_expression(p_parser, p_precedence - (is_right_associative ? 1 : 0));
  if (right == NULL) {
    return NULL;
  }
  ast_infix_binary_expression* infix = (ast_infix_binary_expression*)malloc(sizeof(ast_infix_binary_expression));
  ast_infix_binary_expression_init(infix, p_trigger, p_trigger.type, p_left, right);
  return (ast_expression*)infix;
}

ast_statement* parse_statement(parser* p_parser) {
  switch (p_parser->previous.type) {
  case TOKEN_PRINT:
  case TOKEN_LEFT_BRACE:
  case TOKEN_IF:
  case TOKEN_WHILE:
  case TOKEN_FOR:
  case TOKEN_BREAK:
  case TOKEN_CONTINUE:
  case TOKEN_RETURN:
  case TOKEN_THROW:
  case TOKEN_TRY:
  case TOKEN_CATCH:
  case TOKEN_FINALIZE:
    assert(0);
  case TOKEN_SEMICOLON:
    return (ast_statement*)parse_empty_statement(p_parser);
  case TOKEN_EOF:
    return (ast_statement*)parse_eof_statement(p_parser);
  default:
    return (ast_statement*)parse_expression_statement(p_parser);
  }
}

ast_statement* try_parse_declaration(parser* p_parser) {
  ast_statement* statement;
  switch (p_parser->previous.type) {
  case TOKEN_LET: {
  }
  case TOKEN_FU: {
  }
  case TOKEN_CLASS: {
    assert(0);
  }
  default: {
    statement = parse_statement(p_parser);
    break;
  }
  }
  if (p_parser->panic) {
    sync_state(p_parser);
  }
  return statement;
}

ast_empty_statement* parse_empty_statement(parser* p_parser) {
  ast_empty_statement* empty = (ast_empty_statement*)malloc(sizeof(ast_empty_statement));
  ast_empty_statement_init(empty, p_parser->previous);
  return empty;
}

ast_expression_statement* parse_expression_statement(parser* p_parser) {
  token trigger = p_parser->previous;
  ast_expression* expression = parse_expression(p_parser, PREC_NONE);
  if (expression == NULL) {
    return NULL;
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    string message = string_create("expected ';' after expression statement.", STRING_CALCULATE_LENGTH, false);
    report_at(&p_parser->panic,
              &p_parser->had_error,
              p_parser->current.type == TOKEN_EOF,
              p_parser->source,
              &p_parser->current,
              REPORT_SEVERITY_ERROR,
              &message);
    return NULL;
  }
  advance(p_parser);
  ast_expression_statement* expression_statement = (ast_expression_statement*)malloc(sizeof(ast_expression_statement));
  ast_expression_statement_init(expression_statement, trigger, expression);
  return expression_statement;
}

ast_eof_statement* parse_eof_statement(parser* p_parser) {
  ast_eof_statement* eof = (ast_eof_statement*)malloc(sizeof(ast_eof_statement));
  ast_eof_statement_init(eof, p_parser->previous);
  return eof;
}
