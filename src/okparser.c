#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "okparser.h"
#include "okutils.h"

static precedence get_precedence(token_type p_type);

static ast_root* parse_root(parser* p_parser);
static ast_expression* parse_expression(parser* p_parser, precedence p_precedence);
static ast_statement* parse_statement(parser* p_parser);
static ast_statement* try_parse_declaration(parser* p_parser);
static ast_let_declaration* parse_let_declaration(parser* p_parser, ast_declaration_modifiers_t p_modifiers);
static ast_function_declaration* parse_function_declaration(parser* p_parser, ast_declaration_modifiers_t p_modifiers);
static ast_empty_statement* parse_empty_statement(parser* p_parser);
static ast_expression_statement* parse_expression_statement(parser* p_parser);
static ast_eof_statement* parse_eof_statement(parser* p_parser);
static ast_print_statement* parse_print_statement(parser* p_parser);
static ast_compound_statement* parse_compound_statement(parser* p_parser);
static ast_if_statement* parse_if_statement(parser* p_parser);
static ast_while_statement* parse_while_statement(parser* p_parser);
static ast_for_statement* parse_for_statement(parser* p_parser);
static ast_control_flow_statement* parse_control_flow_statement(parser* p_parser);
static ast_return_statement* parse_return_statement(parser* p_parser);

static ast_bindings_list
parse_bindings_list(parser* p_parser, ast_binding_modifiers_t p_allowed_binds, token_type p_delim, token_type p_end);
static ast_expressions_list parse_expressions_list(parser* p_parser, token_type p_delim, token_type p_end);

static void error_at(parser* p_parser, token p_token, const string_view p_message);
static void error_at_noted(parser* p_parser, token p_token, const string_view p_message, const string_view p_note);

static bool advance(parser* p_parser);
static bool expect(parser* p_parser, token_type p_type, const string_view p_message);
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
static ast_expression* parse_function(parser* p_parser, token p_trigger);
// static ast_expression* parse_this(parser* p_parser, token p_trigger);
// static ast_expression* parse_super(parser* p_parser, token p_trigger);

static ast_expression* parse_binary_infix(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
static ast_expression* parse_assign_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
// static ast_expression* parse_conditional(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
// is_right_associative, token p_trigger); static ast_expression* parse_as(parser* p_parser, ast_expression* p_left,
// precedence p_precedence, bool is_right_associative, token p_trigger);

// static ast_expression* parse_unary_postfix(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
//  is_right_associative, token p_trigger);
static ast_expression* parse_call(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
// static ast_expression* parse_subscript(parser*
//  p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);

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
    [TOKEN_ASSIGN] = {NULL, parse_assign_expression, PREC_ASSIGNMENT},
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
    [TOKEN_LEFT_PAREN] = {parse_grouping, parse_call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {NULL, NULL, PREC_SUBSCRIPT},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_ARROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {parse_identifier, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {parse_number, NULL, PREC_NONE},
    [TOKEN_STRING] = {parse_string, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_AS] = {NULL, NULL, PREC_NONE},
    [TOKEN_FU] = {parse_function, NULL, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, parse_binary_infix, PREC_AND},
    [TOKEN_OR] = {NULL, parse_binary_infix, PREC_OR},
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

void parser_init(parser* p_parser, parser_specs p_specs) {
  p_parser->panic = false;
  p_parser->had_error = false;
  p_parser->source = p_specs.source;
  p_parser->alloc = p_specs.alloc;
  lexer_init(&p_parser->lexer, p_parser->source->code);
  advance(p_parser);
}

void parser_deinit(parser* p_parser) {
  lexer_deinit(&p_parser->lexer);
  p_parser->source = NULL;
}

parse_result parser_parse(parser* p_parser) {
  ast_root* root = parse_root(p_parser);
  parse_result result;
  result.alloc = p_parser->alloc;
  result.status = (root == NULL || p_parser->had_error) ? PARSE_ERROR : PARSE_OK;
  result.root = root;
  return result;
}

void parse_result_deinit(parse_result* parse_result) {
  if (parse_result->root != NULL) {
    ast_root_deinit(parse_result->root);
    parse_result->alloc->release(parse_result->alloc, parse_result->root);
    parse_result->root = NULL;
  }
}

void error_at(parser* p_parser, token p_token, const string_view p_message) {
  error_at_noted(p_parser, p_token, p_message, create_string_view("", 0, true));
}

void error_at_noted(parser* p_parser, token p_token, const string_view p_message, const string_view p_note) {
  report_at_noted(&p_parser->panic,
                  &p_parser->had_error,
                  p_token.type == TOKEN_EOF,
                  p_parser->source,
                  &p_token,
                  REPORT_SEVERITY_ERROR,
                  p_message,
                  p_note);
}

bool advance(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  p_parser->previous = p_parser->current;
  for (;;) {
    p_parser->current = lexer_lex(&p_parser->lexer);
    if (p_parser->current.type != TOKEN_ERROR) {
      break;
    }
    string message = asprint(alloc, "%*.s", p_parser->current.length, p_parser->current.start);
    error_at(p_parser, p_parser->current, create_string_view_from_string(message));
    string_deinit(&message, alloc);
    return false;
  }
  return true;
}

bool expect(parser* p_parser, token_type p_type, const string_view p_message) {
  if (p_parser->current.type == p_type) {
    return advance(p_parser);
  }
  error_at(p_parser, p_parser->current, p_message);
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
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->current;
  ast_root* root = (ast_root*)alloc->allocate(alloc, sizeof(ast_root));
  if (root == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for root node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_root_init(root, trigger, &s);
  while (advance(p_parser) && p_parser->previous.type != TOKEN_EOF) {
    ast_statement* statement = try_parse_declaration(p_parser);
    if (statement != NULL && statement->node.node_type != AST_EMPTY_STATEMENT) {
      ast_statements_list_append(&root->statements, statement);
    } else if (statement != NULL && statement->node.node_type == AST_EMPTY_STATEMENT) {
      ast_empty_statement_deinit((ast_empty_statement*)statement);
      alloc->release(alloc, statement);
    } else {
      p_parser->had_error = true;
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
    error_at(
        p_parser, p_parser->current, create_string_view("expected an expression.", STRING_VIEW_CALCULATE_LENGTH, true));
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
    left = infix_rule.infix(p_parser, left, infix_rule.precedence, false, tok);
    lookahead = p_parser->current;
  }
  return left;
}

ast_expression* parse_unary_prefix(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  if (!advance(p_parser)) {
    return NULL;
  }
  ast_expression* opperand = parse_expression(p_parser, PREC_PREFIX);
  if (opperand == NULL) {
    return NULL;
  }
  ast_prefix_unary_expression* prefix =
      (ast_prefix_unary_expression*)alloc->allocate(alloc, sizeof(ast_prefix_unary_expression));
  if (prefix == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for prefix node.", STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)opperand);
    alloc->release(alloc, opperand);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_prefix_unary_expression_init(prefix, p_trigger, operator_type_from_token_type(p_trigger.type), opperand, &s);
  return (ast_expression*)prefix;
}

ast_expression* parse_grouping(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  if (!advance(p_parser)) {
    return NULL;
  }
  ast_expression* expr = parse_expression(p_parser, PREC_NONE);
  if (expr == NULL) {
    return NULL;
  }
  if (expect(p_parser, TOKEN_RIGHT_PAREN, create_string_view("expected ')'.", STRING_VIEW_CALCULATE_LENGTH, true))) {
    return expr;
  }
  ast_node_deinit((ast_node*)expr);
  alloc->release(alloc, expr);
  return NULL;
}

ast_expression* parse_identifier(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_identifier_expression* identifier =
      (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
  if (identifier == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for identifier node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_identifier_expression_init(identifier, p_trigger, &s);
  return (ast_expression*)identifier;
}

ast_expression* parse_string(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_string_expression* string = (ast_string_expression*)alloc->allocate(alloc, sizeof(ast_string_expression));
  if (string == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for string node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_string_expression_init(string, p_trigger, &s);
  return (ast_expression*)string;
}

ast_expression* parse_number(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_number_expression* number = (ast_number_expression*)alloc->allocate(alloc, sizeof(ast_number_expression));
  if (number == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for number node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_number_expression_init(number, p_trigger, &s);
  return (ast_expression*)number;
}

ast_expression* parse_boolean(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_boolean_expression* boolean = (ast_boolean_expression*)alloc->allocate(alloc, sizeof(ast_boolean_expression));
  if (boolean == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for boolean node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_boolean_expression_init(boolean, p_trigger, &s);
  return (ast_expression*)boolean;
}

ast_expression* parse_null(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_null_expression* null = (ast_null_expression*)alloc->allocate(alloc, sizeof(ast_null_expression));
  if (null == NULL) {
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for null node", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_null_expression_init(null, p_trigger, &s);
  return (ast_expression*)null;
}

static ast_expression* parse_function(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  token fu_tok = p_parser->previous;
  if (p_parser->current.type != TOKEN_LEFT_PAREN) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected '(' in function.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_bindings_list params = parse_bindings_list(p_parser, BINDING_MUT, TOKEN_COMMA, TOKEN_RIGHT_PAREN);
  if (params.capacity == 1 && params.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to parse function parameters.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  advance(p_parser);
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to parse function body.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_expression* fu = (ast_function_expression*)alloc->allocate(alloc, sizeof(ast_function_expression));
  if (fu == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for function node.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_specs s = {alloc};
  ast_function_expression_init(fu, fu_tok, params, body, &s);
  return (ast_expression*)fu;
free_bind_list:
  ast_bindings_list_deinit(&params, alloc);
  return NULL;
}

ast_expression* parse_binary_infix(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  advance(p_parser);
  ast_expression* right = parse_expression(p_parser, p_precedence - (is_right_associative ? 1 : 0));
  if (right == NULL) {
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  ast_infix_binary_expression* infix =
      (ast_infix_binary_expression*)alloc->allocate(alloc, sizeof(ast_infix_binary_expression));
  if (infix == NULL) {
    ast_dispatch_deinit((ast_node*)right);
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for infix node.", STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_infix_binary_expression_init(infix, p_trigger, operator_type_from_token_type(p_trigger.type), p_left, right, &s);
  return (ast_expression*)infix;
}

static bool is_lvalue(ast_expression* p_expression) {
  switch (p_expression->node.node_type) {
  case AST_IDENTIFIER_EXPRESSION:
  case AST_ACCESS_EXPRESSION:
  case AST_SUBSCRIPT_EXPRESSION:
    return true;
  default:
    return false;
  }
}

ast_expression* parse_assign_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  advance(p_parser);
  if (!is_lvalue(p_left)) {
    goto fail;
  }
  ast_expression* right = parse_expression(p_parser, PREC_ASSIGNMENT - 1); // a = b = c => a = (b = c)
  if (right == NULL) {
    goto fail;
  }
  ast_assign_expression* assign = (ast_assign_expression*)alloc->allocate(alloc, sizeof(ast_assign_expression));
  if (assign == NULL) {
    ast_dispatch_deinit((ast_node*)right);
    error_at(p_parser,
             p_trigger,
             create_string_view("failed to allocate memory for assign node.", STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)right);
    alloc->release(alloc, right);
    goto fail;
  }
  ast_specs s = {alloc};
  ast_assign_expression_init(assign, p_trigger, p_left, right, &s);
  return (ast_expression*)assign;
fail:
  ast_dispatch_deinit((ast_node*)p_left);
  alloc->release(alloc, p_left);
  return NULL;
}

ast_expression* parse_call(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous;
  ast_expressions_list args = parse_expressions_list(p_parser, TOKEN_COMMA, TOKEN_RIGHT_PAREN);
  if (args.capacity == 1 && args.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to parse function arguments.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_call_expression* call = (ast_call_expression*)alloc->allocate(alloc, sizeof(ast_call_expression));
  if (call == NULL) {
    error_at(p_parser,
             trigger,
             create_string_view("failed to allocate memory for call node.", STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    ast_expressions_list_deinit(&args, alloc);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_call_expression_init(call, trigger, p_left, args, &s);
  return (ast_expression*)call;
}

ast_statement* parse_statement(parser* p_parser) {
  switch (p_parser->previous.type) {
  case TOKEN_PRINT:
    return (ast_statement*)parse_print_statement(p_parser);
  case TOKEN_LEFT_BRACE:
    return (ast_statement*)parse_compound_statement(p_parser);
  case TOKEN_IF:
    return (ast_statement*)parse_if_statement(p_parser);
  case TOKEN_WHILE:
    return (ast_statement*)parse_while_statement(p_parser);
  case TOKEN_FOR:
    return (ast_statement*)parse_for_statement(p_parser);
  case TOKEN_BREAK:
  case TOKEN_CONTINUE:
    return (ast_statement*)parse_control_flow_statement(p_parser);
  case TOKEN_RETURN:
    return (ast_statement*)parse_return_statement(p_parser);
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

static ast_declaration_modifiers_t parse_declaration_modifier(token p_token) {
  switch (p_token.type) {
  case TOKEN_GLOB:
    return DECLARATION_GLOB;
  case TOKEN_STATIC:
    return DECLARATION_STATIC;
  case TOKEN_ASYNC:
    return DECLARATION_ASYNC;
  case TOKEN_EXPORT:
    return DECLARATION_EXPORT;
  default:
    return DECLARATION_NONE;
  }
}

static ast_declaration_modifiers_t try_parse_declaration_modifiers(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  ast_binding_modifiers_t modifiers = DECLARATION_NONE;
  while (p_parser->previous.type != TOKEN_EOF) {
    ast_declaration_modifiers_t mod = parse_declaration_modifier(p_parser->previous);
    if (mod == DECLARATION_NONE) {
      break;
    }
    if ((mod & modifiers) != 0) { // this lowk should be a warning X2.
      string mod_str = ast_declaration_modifiers_asprint(mod, alloc);
      string message = asprint(alloc, "duplicated declaration modifier '%s'.", mod_str.chars);
      error_at(p_parser, p_parser->current, create_string_view_from_string(message));
      string_deinit(&mod_str, alloc);
      string_deinit(&message, alloc);
      return DECLARATION_ERROR;
    }
    modifiers |= mod;
    advance(p_parser);
  }
  return modifiers;
}

static ast_binding_modifiers_t parse_binding_modifier(token p_token) {
  switch (p_token.type) {
  case TOKEN_MUT:
    return BINDING_MUT;
  default:
    return BINDING_NONE;
  }
}

ast_statement* try_parse_declaration(parser* p_parser) {
  ast_statement* statement;
  ast_declaration_modifiers_t mods = try_parse_declaration_modifiers(p_parser);
  if (mods == DECLARATION_ERROR) {
    return NULL;
  }
  switch (p_parser->previous.type) {
  case TOKEN_LET: {
    statement = (ast_statement*)parse_let_declaration(p_parser, mods);
    break;
  }
  case TOKEN_FU: {
    if (mods == DECLARATION_NONE && parse_binding_modifier(p_parser->current) == BINDING_NONE &&
        p_parser->current.type != TOKEN_IDENTIFIER) {
      goto _default;
    }
    statement = (ast_statement*)parse_function_declaration(p_parser, mods);
    break;
  }
  case TOKEN_CLASS: {
    assert(0);
  }
  default:
  _default: {
    if (mods != DECLARATION_NONE) {
      error_at_noted(p_parser,
                     p_parser->previous,
                     create_string_view("illegal declaration modifier(s).", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("declaration modifiers can only appear before declarations.",
                                        STRING_VIEW_CALCULATE_LENGTH,
                                        true));
      return NULL;
    }
    statement = parse_statement(p_parser);
    break;
  }
  }
  if (p_parser->panic) {
    sync_state(p_parser);
  }
  return statement;
}

static ast_binding_modifiers_t parse_binding_modifiers(parser* p_parser, ast_binding_modifiers_t p_allowed) {
  allocators* alloc = p_parser->alloc;
  ast_binding_modifiers_t modifiers = BINDING_NONE;
  while (p_parser->current.type != TOKEN_EOF) {
    ast_binding_modifiers_t mod = parse_binding_modifier(p_parser->current);
    if (mod == BINDING_NONE) {
      break;
    }
    if ((mod & p_allowed) == 0) {
      string mod_str = ast_binding_modifiers_asprint(mod);
      string message = asprint(alloc, "illegal binding modifier '%s', in %s binding.", mod_str.chars, "");
      string allowed_str = ast_binding_modifiers_asprint(p_allowed);
      string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
      error_at_noted(
          p_parser, p_parser->current, create_string_view_from_string(message), create_string_view_from_string(note));
      string_deinit(&mod_str, alloc);
      string_deinit(&message, alloc);
      string_deinit(&allowed_str, alloc);
      string_deinit(&note, alloc);
      return BINDING_ERROR;
    } else if ((mod & modifiers) != 0) { // this lowk should be a warning.
      string mod_str = ast_binding_modifiers_asprint(mod);
      string message = asprint(alloc, "duplicated binding modifier %s.", mod_str.chars);
      error_at(p_parser, p_parser->current, create_string_view_from_string(message));
      string_deinit(&mod_str, alloc);
      string_deinit(&message, alloc);
      return BINDING_ERROR;
    }
    modifiers |= mod;
    advance(p_parser);
  }
  return modifiers;
}

ast_let_declaration* parse_let_declaration(parser* p_parser, ast_declaration_modifiers_t p_declaration_modifiers) {
  allocators* alloc = p_parser->alloc;
  const token let_tok = p_parser->previous;
  const ast_declaration_modifiers_t let_allowed_decl_mods = DECLARATION_GLOB | DECLARATION_EXPORT;
  if ((p_declaration_modifiers & ~let_allowed_decl_mods) != 0) {
    string mods_str = ast_declaration_modifiers_asprint(p_declaration_modifiers, alloc);
    string message = asprint(alloc,
                             "illegal declaration modifier(s): [%s] in let declaration.",
                             mods_str.chars); // TODO: extract the offending only.
    string allowed_str = ast_declaration_modifiers_asprint(let_allowed_decl_mods, alloc);
    string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
    error_at_noted(p_parser, let_tok, create_string_view_from_string(message), create_string_view_from_string(note));
    string_deinit(&message, alloc);
    string_deinit(&mods_str, alloc);
    string_deinit(&note, alloc);
    string_deinit(&allowed_str, alloc);
    return NULL;
  }
  const ast_binding_modifiers_t binding_mods = parse_binding_modifiers(p_parser, BINDING_MUT);
  if (binding_mods == BINDING_ERROR) {
    return NULL;
  }
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(p_parser,
             let_tok,
             create_string_view("failed to allocate memory for binding node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_let_declaration* let = (ast_let_declaration*)alloc->allocate(alloc, sizeof(ast_let_declaration));
  if (let == NULL) {
    alloc->release(alloc, binding);
    error_at(p_parser,
             let_tok,
             create_string_view("failed to allocate memory for let node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_null_expression* null = NULL;
  ast_expression* expr = parse_expression(p_parser, PREC_NONE);
  ast_specs s = {alloc};
  if (expr != NULL) {
    if (expr->node.node_type == AST_ASSIGN_EXPRESSION) {
      ast_assign_expression* assign = (ast_assign_expression*)expr;
      if (assign->left->node.node_type == AST_IDENTIFIER_EXPRESSION) {
        ast_binding_init(binding, assign->left, binding_mods, assign->left->node.token, &s);
        ast_let_declaration_init(let, binding, assign->right, p_declaration_modifiers, let_tok, &s);
        assign->left = NULL;
        assign->right = NULL;
        alloc->release(alloc, expr);
      } else {
        ast_dispatch_deinit((ast_node*)expr);
        goto free;
      }
    } else if (expr->node.node_type == AST_IDENTIFIER_EXPRESSION) {
      null = (ast_null_expression*)alloc->allocate(alloc, sizeof(ast_null_expression));
      if (null == NULL) {
        ast_dispatch_deinit((ast_node*)expr);
        goto free;
      }
      ast_expression* ident = (ast_expression*)expr;
      ast_binding_init(binding, ident, binding_mods, ident->node.token, &s);
      ast_let_declaration_init(let, binding, (ast_expression*)null, p_declaration_modifiers, let_tok, &s);
    } else {
      error_at(p_parser,
               let_tok,
               create_string_view("expected an identifier in let declaration.", STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)expr);
      goto free;
    }
  } else {
    error_at(p_parser,
             let_tok,
             create_string_view("expected an identifier in let declaration.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free;
  }
  advance(p_parser);
  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected ';' after let declaration.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_let;
  }
  if (null != NULL) {
    ast_null_expression_init(
        null, p_parser->previous, &s); // generated because there was an end to the declaration without a value "let x;"
                                       // thus the semicolon token is the one that authored the null node.
  }
  return let;
free_let:
  ast_let_declaration_deinit(let);
  alloc->release(alloc, let);
  return NULL;
free:
  alloc->release(alloc, binding);
  alloc->release(alloc, let);
  alloc->release(alloc, expr);
  return NULL;
}

ast_function_declaration* parse_function_declaration(parser* p_parser,
                                                     ast_declaration_modifiers_t p_declaration_modifiers) {
  allocators* alloc = p_parser->alloc;
  token fu_tok = p_parser->previous;
  const ast_declaration_modifiers_t fu_allowed_decl_mods = DECLARATION_GLOB | DECLARATION_EXPORT | DECLARATION_ASYNC;
  if ((p_declaration_modifiers & ~fu_allowed_decl_mods) != 0) {
    string mods_str = ast_declaration_modifiers_asprint(p_declaration_modifiers, alloc);
    string message = asprint(alloc,
                             "illegal declaration modifier(s): [%s] in fu declaration.",
                             mods_str.chars); // TODO: extract the offending only.
    string allowed_str = ast_declaration_modifiers_asprint(fu_allowed_decl_mods, alloc);
    string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
    error_at_noted(p_parser, fu_tok, create_string_view_from_string(message), create_string_view_from_string(note));
    string_deinit(&message, alloc);
    string_deinit(&mods_str, alloc);
    string_deinit(&note, alloc);
    string_deinit(&allowed_str, alloc);
    return NULL;
  }
  const ast_binding_modifiers_t binding_mods = parse_binding_modifiers(p_parser, BINDING_MUT);
  if (binding_mods == BINDING_ERROR) {
    return NULL;
  }
  advance(p_parser);
  if (p_parser->previous.type != TOKEN_IDENTIFIER) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected an identifier as function name.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_identifier_expression* ident =
      (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
  if (ident == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for identifier node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_identifier_expression_init(ident, p_parser->previous, &s); // don't attempt to parse as an expression, because the
                                                                 // parse rule for () call expression will trigger.
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(p_parser,
             fu_tok,
             create_string_view("failed to allocate memory for binding node.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_ident;
  }
  ast_binding_init(binding, (ast_expression*)ident, binding_mods, p_parser->current, &s);
  if (p_parser->current.type != TOKEN_LEFT_PAREN) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected '(' in function declaration", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind;
  }
  advance(p_parser);
  ast_bindings_list params = parse_bindings_list(p_parser, BINDING_MUT, TOKEN_COMMA, TOKEN_RIGHT_PAREN);
  if (params.capacity == 1 && params.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to parse function parameters.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind;
  }
  advance(p_parser);
  advance(p_parser);
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to parse function body.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_declaration* fu = (ast_function_declaration*)alloc->allocate(alloc, sizeof(ast_function_declaration));
  if (fu == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for function node.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_declaration_init(fu, binding, params, body, p_declaration_modifiers, fu_tok, &s);
  return fu;
free_bind_list:
  ast_bindings_list_deinit(&params, alloc);
free_bind:
  ast_binding_deinit(binding);
  alloc->release(alloc, binding);
  return NULL; // if binding successfully initialized it will free ident.
free_ident:
  ast_identifier_expression_deinit(ident);
  alloc->release(alloc, ident);
  return NULL;
}

ast_empty_statement* parse_empty_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;

  ast_empty_statement* empty = (ast_empty_statement*)alloc->allocate(alloc, sizeof(ast_empty_statement));
  if (empty == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for empty node.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_empty_statement_init(empty, p_parser->previous, &s);
  return empty;
}

ast_expression_statement* parse_expression_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous;
  ast_expression* expression = parse_expression(p_parser, PREC_NONE);
  if (expression == NULL) {
    return NULL;
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected ';' after expression statement.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  advance(p_parser); // ;
  ast_expression_statement* expression_statement = (ast_expression_statement*)malloc(sizeof(ast_expression_statement));
  if (expression_statement == NULL) {
    error_at(
        p_parser,
        p_parser->current,
        create_string_view("failed to allocate memory for expression statement.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  ast_specs s = {alloc};
  ast_expression_statement_init(expression_statement, trigger, expression, &s);
  return expression_statement;
fail:
  ast_dispatch_deinit((ast_node*)expression);
  alloc->release(alloc, expression);
  return NULL;
}

ast_eof_statement* parse_eof_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  ast_eof_statement* eof = (ast_eof_statement*)alloc->allocate(alloc, sizeof(ast_eof_statement));
  ast_specs s = {alloc};
  ast_eof_statement_init(eof, p_parser->previous, &s);
  return eof;
}

ast_print_statement* parse_print_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous;
  if (!advance(p_parser)) {
    return NULL;
  }
  ast_expression* expression = parse_expression(p_parser, PREC_NONE);
  if (expression == NULL) {
    return NULL;
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected ';' after print statement.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  advance(p_parser);
  ast_print_statement* print = (ast_print_statement*)alloc->allocate(alloc, sizeof(ast_print_statement));
  if (print == NULL) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("failed to allocate memory for print node.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  ast_specs s = {alloc};
  ast_print_statement_init(print, trigger, expression, &s);
  return print;
fail:
  ast_dispatch_deinit((ast_node*)expression);
  alloc->release(alloc, expression);
  return NULL;
}

ast_compound_statement* parse_compound_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous;
  ast_statements_list list;
  ast_statements_list_init(&list, alloc);
  while (p_parser->current.type != TOKEN_RIGHT_BRACE && p_parser->current.type != TOKEN_EOF) {
    if (!advance(p_parser)) {
      goto fail;
    }
    ast_statement* stmt = try_parse_declaration(p_parser);
    if (stmt == NULL) {
      goto fail;
    }
    if (stmt->node.node_type == AST_EMPTY_STATEMENT) {
      ast_dispatch_deinit((ast_node*)stmt);
      alloc->release(alloc, stmt);
      continue;
    }
    if (!ast_statements_list_append(&list, stmt)) {
      goto fail;
    }
  }
  if (p_parser->current.type != TOKEN_RIGHT_BRACE) {
    error_at(p_parser, p_parser->current, create_string_view("expected '}'.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  advance(p_parser);
  ast_compound_statement* compound = (ast_compound_statement*)alloc->allocate(alloc, sizeof(ast_compound_statement));
  if (compound == NULL) {
    goto fail;
  }
  ast_specs s = {alloc};
  ast_compound_statement_init(compound, trigger, &s);
  compound->statements = list;
  return compound;
fail:
  ast_statements_list_deinit(&list, alloc);
  return NULL;
}

ast_if_statement* parse_if_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token if_tok = p_parser->previous;
  advance(p_parser);
  ast_expression* cond = parse_expression(p_parser, PREC_NONE);
  if (cond == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected an expression as if condition.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (p_parser->current.type != TOKEN_QUESTION) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected '?' after if condition.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  advance(p_parser);
  advance(p_parser);
  ast_statement* cons = parse_statement(p_parser);
  if (cons == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected a statement as if consequense.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  ast_statement* alt = NULL;
  if (p_parser->current.type == TOKEN_ELSE) {
    advance(p_parser);
    if (p_parser->current.type == TOKEN_QUESTION) {
      advance(p_parser);
    }
    advance(p_parser);
    alt = parse_statement(p_parser);
    if (alt == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("expected a statement as if alternative.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto fail;
    }
  }
  ast_if_statement* if_stmt = (ast_if_statement*)alloc->allocate(alloc, sizeof(ast_if_statement));
  if (if_stmt == NULL) {
  fail:
    ast_dispatch_deinit((ast_node*)cons);
    alloc->release(alloc, cons);
  fail_cond:
    ast_dispatch_deinit((ast_node*)cond);
    alloc->release(alloc, cond);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_if_statement_init(if_stmt, if_tok, &s);
  if_stmt->condition = cond;
  if_stmt->consequence = cons;
  if_stmt->alternative = alt;
  return if_stmt;
}

ast_while_statement* parse_while_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token while_tok = p_parser->previous;
  advance(p_parser);
  ast_expression* cond = parse_expression(p_parser, PREC_NONE);
  if (cond == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected an expression as while condition.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (p_parser->current.type != TOKEN_QUESTION) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected '?' after while condition.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  advance(p_parser);
  advance(p_parser);
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected a statement as while body.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  ast_while_statement* _while = (ast_while_statement*)alloc->allocate(alloc, sizeof(ast_while_statement));
  if (_while == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for while node.", STRING_VIEW_CALCULATE_LENGTH, true));
  fail:
    ast_dispatch_deinit((ast_node*)body);
    alloc->release(alloc, body);
  fail_cond:
    ast_dispatch_deinit((ast_node*)cond);
    alloc->release(alloc, cond);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_while_statement_init(_while, while_tok, &s);
  _while->condition = cond;
  _while->body = body;
  return _while;
}

ast_for_statement* parse_for_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token for_tok = p_parser->previous;
  advance(p_parser);
  ast_statement* init = NULL;
  ast_expression* cond = NULL;
  ast_expression* up = NULL;

  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    init = try_parse_declaration(p_parser);
    if (init == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("invalid for initializer.", STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL;
    }
  }

  advance(p_parser);
  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    cond = parse_expression(p_parser, PREC_NONE);
    if (cond == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("expected an expression as for condition.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_init;
    }
    advance(p_parser);
  }

  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected ';' after if condition.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_cond;
  }
  advance(p_parser);

  if (p_parser->previous.type != TOKEN_QUESTION) {
    up = parse_expression(p_parser, PREC_NONE);
    if (up == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("expected an expression as for increment.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_cond;
    }
    advance(p_parser);
  }

  if (p_parser->previous.type != TOKEN_QUESTION) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("excpected '?' after for clauses", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_up;
  }
  advance(p_parser);

  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("expected a statement as for body.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_up;
  }

  ast_for_statement* _for = (ast_for_statement*)alloc->allocate(alloc, sizeof(ast_for_statement));
  if (_for == NULL) {
    error_at(p_parser,
             p_parser->previous,
             create_string_view("failed to allocate memory for for node.", STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)body);
    alloc->release(alloc, body);
  free_up:
    ast_dispatch_deinit((ast_node*)up);
    alloc->release(alloc, up);
  free_cond:
    ast_dispatch_deinit((ast_node*)cond);
    alloc->release(alloc, cond);
  free_init:
    ast_dispatch_deinit((ast_node*)init);
    alloc->release(alloc, init);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_for_statement_init(_for, for_tok, &s);
  _for->initializer = init;
  _for->condition = cond;
  _for->update = up;
  _for->body = body;
  return _for;
}

ast_control_flow_statement* parse_control_flow_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  control_flow_type type = CONTROL_FLOW_NONE;
  if (p_parser->previous.type == TOKEN_BREAK) {
    type = CONTROL_FLOW_BREAK;
  } else if (p_parser->previous.type == TOKEN_CONTINUE) {
    type = CONTROL_FLOW_CONTINUE;
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected ';' after control flow statement.", STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_control_flow_statement* control =
      (ast_control_flow_statement*)alloc->allocate(alloc, sizeof(ast_control_flow_statement));
  ast_specs s = {alloc};
  ast_control_flow_statement_init(control, p_parser->previous, &s);
  control->type = type;

  return control;
}

static ast_return_statement* parse_return_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;

  token return_tok = p_parser->previous;
  ast_expression* returned = NULL;
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    advance(p_parser);
    returned = parse_expression(p_parser, PREC_NONE);
    if (returned == NULL) {
      return NULL;
    }
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             create_string_view("expected ';' after return statement.", STRING_VIEW_CALCULATE_LENGTH, true));
    goto free;
  }

  advance(p_parser);
  ast_return_statement* ret = (ast_return_statement*)alloc->allocate(alloc, sizeof(ast_return_statement));
  if (ret == NULL) {
    goto free;
  }
  ast_specs s = {alloc};
  ast_return_statement_init(ret, returned, return_tok, &s);
  return ret;
free:
  if (returned != NULL) {
    ast_dispatch_deinit((ast_node*)returned);
    alloc->release(alloc, returned);
  }
  return NULL;
}

ast_bindings_list
parse_bindings_list(parser* p_parser, ast_binding_modifiers_t p_allowed_binds, token_type p_delim, token_type p_end) {
  allocators* alloc = p_parser->alloc;
  ast_bindings_list list;
  ast_bindings_list_init(&list, alloc);
  while (p_parser->current.type != p_end && p_parser->current.type != TOKEN_EOF) {
    ast_binding_modifiers_t binds = parse_binding_modifiers(p_parser, p_allowed_binds);
    token tok = p_parser->previous;
    if (binds == BINDING_ERROR) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("failed to parse binding modifiers.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    if (p_parser->current.type != TOKEN_IDENTIFIER) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("expected an identifier as parameter name.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    advance(p_parser);
    ast_expression* ident = parse_expression(p_parser, PREC_NONE);
    if (ident == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view("failed to parse parameter.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
    if (binding == NULL) {
      error_at(p_parser,
               p_parser->previous,
               create_string_view(
                   "failed to allocate memory for parameter binding node.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    ast_specs s = {alloc};
    ast_binding_init(binding, ident, binds, tok, &s);
    if (!ast_bindings_list_append(&list, binding)) {
      goto free_bind_list;
    }
    if (p_parser->current.type == p_delim) {
      advance(p_parser);
    } else if (p_parser->current.type != p_end) {
      goto expect_end;
    }
  }
  if (p_parser->current.type != p_end) {
  expect_end: { // label followed by decl is c23 ext lol.
    string message = asprint(alloc, "expected '%s'.", token_type_to_string(p_end));
    error_at(p_parser, p_parser->current, create_string_view_from_string(message));
    string_deinit(&message, alloc);
    goto free_bind_list;
  }
  }
  return list;
free_bind_list:
  ast_bindings_list_deinit(&list, alloc);
  ast_bindings_list_init(&list, alloc);
  list.capacity = 1; // dirty way to signal failure.
  return list;
}

ast_expressions_list parse_expressions_list(parser* p_parser, token_type p_delim, token_type p_end) {
  allocators* alloc = p_parser->alloc;
  ast_expressions_list list;
  ast_expressions_list_init(&list, alloc);
  while (p_parser->current.type != p_end && p_parser->current.type != TOKEN_EOF) {
    advance(p_parser);
    ast_expression* expr = parse_expression(p_parser, PREC_NONE);
    if (expr == NULL) {
      error_at(
          p_parser,
          p_parser->previous,
          create_string_view("failed to parse expression in expressions list.", STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_expr_list;
    }
    if (!ast_expressions_list_append(&list, expr)) {
      goto free_expr_list;
    }
    if (p_parser->current.type == p_delim) {
      advance(p_parser);
    } else if (p_parser->current.type != p_end) {
      goto expect_end;
    }
  }
  if (p_parser->current.type != p_end) {
  expect_end: {
    string message = asprint(alloc, "expected '%s'.", token_type_to_string(p_end));
    error_at(p_parser, p_parser->current, create_string_view_from_string(message));
    string_deinit(&message, alloc);
    goto free_expr_list;
  }
  }
  return list;
free_expr_list:
  ast_expressions_list_deinit(&list, alloc);
  ast_expressions_list_init(&list, alloc);
  list.capacity = 1;
  return list;
}
