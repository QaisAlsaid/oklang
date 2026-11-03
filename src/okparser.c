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
static ast_let_declaration* parse_let_declaration(parser* p_parser,
                                                ast_declaration_modifiers_t p_modifiers,
                                                bool p_in_class_body);
static ast_let_declaration* parse_let_declaration_span(parser* p_parser,
                                                       ast_declaration_modifiers_t p_modifiers,
                                                       ast_binding_modifiers_t p_binding_mods);
static ast_function_declaration* parse_function_declaration(parser* p_parser,
                                                             ast_declaration_modifiers_t p_modifiers,
                                                             bool p_in_class_body);
static ast_class_declaration* parse_class_declaration(parser* p_parser, ast_declaration_modifiers_t p_modifiers);
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
static ast_throw_statement* parse_throw_statement(parser* p_parser);
static ast_try_statement* parse_try_statement(parser* p_parser);
static ast_defer_statement* parse_defer_statement(parser* p_parser, bool p_is_err);

static ast_bindings_list parse_bindings_list(parser* p_parser,
                                             ast_binding_modifiers_t p_allowed_binds,
                                             token_type p_delim,
                                             token_type p_end,
                                             bool p_allow_varargs);
static ast_expressions_list parse_expressions_list(parser* p_parser, token_type p_delim, token_type p_end);

static void error_at(parser* p_parser, token p_token, const ok_string_view p_message);
static void
error_at_noted(parser* p_parser, token p_token, const ok_string_view p_message, const ok_string_view p_note);

static bool advance(parser* p_parser);
static bool expect(parser* p_parser, token_type p_type, const ok_string_view p_message);
static token current(parser* p_parser);
static token peek(parser* p_parser);
static void sync_state(parser* p_parser);

static ast_expression* parse_unary_prefix(parser* p_parser, token p_trigger);
static ast_expression* parse_grouping(parser* p_parser, token p_trigger);
// static ast_expression* parse_map(parser* p_parser, token p_trigger);
static ast_expression* parse_map_literal(parser* p_parser, token p_trigger);
// static ast_expression* parse_array(parser* p_parser, token p_trigger);
static ast_expression* parse_array_literal(parser* p_parser, token p_trigger);
static ast_expression* parse_subscript(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
static ast_expression* parse_compound_assign_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
static ast_expression* parse_identifier(parser* p_parser, token p_trigger);
static ast_expression* parse_access(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
static ast_expression* parse_this(parser* p_parser, token p_trigger);
static ast_expression* parse_super(parser* p_parser, token p_trigger);
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
static ast_expression* parse_conditional_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);

// static ast_expression* parse_unary_postfix(parser* p_parser, ast_expression* p_left, precedence p_precedence, bool
//  is_right_associative, token p_trigger);
static ast_expression* parse_call(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);
// static ast_expression* parse_subscript(
//     parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger);

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
    [TOKEN_PLUS_EQUAL] = {NULL, parse_compound_assign_expression, PREC_ASSIGNMENT},
    [TOKEN_MINUS_EQUAL] = {NULL, parse_compound_assign_expression, PREC_ASSIGNMENT},
    [TOKEN_ASTERISK_EQUAL] = {NULL, parse_compound_assign_expression, PREC_ASSIGNMENT},
    [TOKEN_SLASH_EQUAL] = {NULL, parse_compound_assign_expression, PREC_ASSIGNMENT},
    [TOKEN_MODULO_EQUAL] = {NULL, parse_compound_assign_expression, PREC_ASSIGNMENT},
    [TOKEN_CARET_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_AMPERSAND_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_BAR_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_SHIFT_LEFT_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_SHIFT_RIGHT_EQUAL] = {NULL, parse_binary_infix, PREC_ASSIGNMENT},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_BANG] = {parse_unary_prefix, NULL, PREC_PREFIX},
    [TOKEN_BANG_EQUAL] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_TRIPLE_EQUAL] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_IS] = {NULL, parse_binary_infix, PREC_EQUALITY},
    [TOKEN_LESS_EQUAL] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_GREATER_EQUAL] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_LESS] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_GREATER] = {NULL, parse_binary_infix, PREC_COMPARISION},
    [TOKEN_LEFT_PAREN] = {parse_grouping, parse_call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {parse_map_literal, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {parse_array_literal, parse_subscript, PREC_SUBSCRIPT},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {parse_identifier, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {parse_number, NULL, PREC_NONE},
    [TOKEN_STRING] = {parse_string, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_AS] = {NULL, parse_binary_infix, PREC_AS},
    [TOKEN_FU] = {parse_function, NULL, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_THEN] = {NULL, parse_conditional_expression, PREC_CONDITIONAL},
    [TOKEN_OTHERWISE] = {NULL, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, parse_binary_infix, PREC_AND},
    [TOKEN_OR] = {NULL, parse_binary_infix, PREC_OR},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_STATIC] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {parse_super, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, parse_access, PREC_CALL},
    [TOKEN_THIS] = {parse_this, NULL, PREC_NONE},
    [TOKEN_NULL] = {parse_null, NULL, PREC_NONE},
    [TOKEN_TRUE] = {parse_boolean, NULL, PREC_NONE},
    [TOKEN_FALSE] = {parse_boolean, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_NOT] = {parse_unary_prefix, NULL, PREC_PREFIX},
    [TOKEN_GLOB] = {NULL, NULL, PREC_NONE},
    [TOKEN_EXPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MUT] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRY] = {NULL, NULL, PREC_NONE},
    [TOKEN_CATCH] = {NULL, NULL, PREC_NONE},
    [TOKEN_THROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_DEFER] = {NULL, NULL, PREC_NONE},
    [TOKEN_DEFERR] = {NULL, NULL, PREC_NONE},
};

void parser_init(parser* p_parser, parser_specs p_specs) {
  p_parser->panic = false;
  p_parser->had_error = false;
  p_parser->defer_serial = 0;
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

void error_at(parser* p_parser, token p_token, const ok_string_view p_message) {
  error_at_noted(p_parser, p_token, p_message, ok_create_string_view(NULL, 0, false));
}

void error_at_noted(parser* p_parser, token p_token, const ok_string_view p_message, const ok_string_view p_note) {
  ok_note note;
  note.text = p_note;
  note.has_span = false;
  note.span.line = 0;
  note.span.offset = 0;
  note.span.length = 0;
  report_at_noted(&p_parser->panic,
                  &p_parser->had_error,
                  p_token.type == TOKEN_EOF,
                  p_parser->source,
                  &p_token,
                  REPORT_SEVERITY_ERROR,
                  p_message,
                  p_note.chars != NULL ? &note : NULL);
}

bool advance(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  p_parser->previous = p_parser->current;
  for (;;) {
    p_parser->current = lexer_lex(&p_parser->lexer);
    if (p_parser->current.type != TOKEN_ERROR) {
      break;
    }
    string message = asprint(alloc, "%.*s", p_parser->current.length, p_parser->current.start);
    error_at(p_parser, p_parser->current, ok_create_string_view_from_string(message));
    string_deinit(&message, alloc);
    return false;
  }
  return true;
}

bool expect(parser* p_parser, token_type p_type, const ok_string_view p_message) {
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
             ok_create_string_view("failed to allocate memory for root node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected an expression.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for prefix node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)opperand);
    alloc->release(alloc, opperand);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_prefix_unary_expression_init(
      prefix, p_trigger, operator_type_from_token_type(p_trigger.type, true), opperand, &s);
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
  if (expect(
          p_parser, TOKEN_RIGHT_PAREN, ok_create_string_view("expected ')'.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
    return expr;
  }
  ast_node_deinit((ast_node*)expr);
  alloc->release(alloc, expr);
  return NULL;
}

// static ast_expression* parse_array(parser* p_parser, token p_trigger) {
//   allocators* alloc = p_parser->alloc;
//   token trigger = p_parser->previous;
//   ast_expressions_list args = parse_expressions_list(p_parser, TOKEN_COMMA, TOKEN_RIGHT_BRACKET);
//   if (args.capacity == 1 && args.count == 0) {
//     error_at(p_parser,
//              p_parser->previous,
//              ok_create_string_view("failed to parse array elements.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
//     return NULL;
//   }
//   advance(p_parser);
//   ast_call_expression* call = (ast_call_expression*)alloc->allocate(alloc, sizeof(ast_call_expression));
//   if (call == NULL) {
//     error_at(p_parser,
//              trigger,
//              ok_create_string_view("failed to allocate memory for call node.", OK_STRING_VIEW_CALCULATE_LENGTH,
//              true));
//     ast_dispatch_deinit((ast_node*)p_left);
//     alloc->release(alloc, p_left);
//     ast_expressions_list_deinit(&args, alloc);
//     return NULL;
//   }
//   ast_specs s = {alloc};
//   ast_call_expression_init(call, trigger, p_left, args, &s);
//   return (ast_expression*)call;
// }

ast_expression* parse_identifier(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_identifier_expression* identifier =
      (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
  if (identifier == NULL) {
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for identifier node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for string node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for number node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for boolean node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("failed to allocate memory for null node", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("expected '(' in function.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_bindings_list params = parse_bindings_list(p_parser, BINDING_MUT, TOKEN_COMMA, TOKEN_RIGHT_PAREN, true);
  if (params.capacity == 1 && params.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse function parameters.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  advance(p_parser);
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse function body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_expression* fu = (ast_function_expression*)alloc->allocate(alloc, sizeof(ast_function_expression));
  if (fu == NULL) {
    error_at(
        p_parser,
        p_parser->previous,
        ok_create_string_view("failed to allocate memory for function node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("failed to allocate memory for infix node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_infix_binary_expression_init(
      infix, p_trigger, operator_type_from_token_type(p_trigger.type, false), p_left, right, &s);
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
    error_at(
        p_parser,
        p_trigger,
        ok_create_string_view("failed to allocate memory for assign node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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

// `p_left then <then_arm> otherwise <otherwise_arm>` - the conditional
// expression. the then arm parses at PREC_CONDITIONAL so it stops at
// `otherwise`; the otherwise arm parses one level lower (PREC_CONDITIONAL - 1)
// making the operator right associative: a then b otherwise c then d otherwise e
// is a then b otherwise (c then d otherwise e). assignments inside an arm need
// parentheses, matching how the middle of the C ternary behaves.
ast_expression* parse_conditional_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  (void)p_precedence;
  (void)is_right_associative;
  allocators* alloc = p_parser->alloc;
  advance(p_parser); // past 'then': parse_expression dispatches on previous
  ast_expression* then_arm = parse_expression(p_parser, PREC_CONDITIONAL - 1);
  if (then_arm == NULL) {
    goto fail;
  }
  if (p_parser->current.type != TOKEN_OTHERWISE) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view(
                 "expected 'otherwise' to complete the conditional.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)then_arm);
    alloc->release(alloc, then_arm);
    goto fail;
  }
  advance(p_parser); // consume 'otherwise'
  advance(p_parser); // past it: previous becomes the arm's first token
  ast_expression* otherwise_arm = parse_expression(p_parser, PREC_CONDITIONAL - 1);
  if (otherwise_arm == NULL) {
    ast_dispatch_deinit((ast_node*)then_arm);
    alloc->release(alloc, then_arm);
    goto fail;
  }
  ast_conditional_expression* conditional =
      (ast_conditional_expression*)alloc->allocate(alloc, sizeof(ast_conditional_expression));
  if (conditional == NULL) {
    error_at(p_parser,
             p_trigger,
             ok_create_string_view(
                 "failed to allocate memory for conditional node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)then_arm);
    alloc->release(alloc, then_arm);
    ast_dispatch_deinit((ast_node*)otherwise_arm);
    alloc->release(alloc, otherwise_arm);
    goto fail;
  }
  ast_specs s = {alloc};
  ast_conditional_expression_init(conditional, p_trigger, p_left, then_arm, otherwise_arm, &s);
  return (ast_expression*)conditional;
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
             ok_create_string_view("failed to parse function arguments.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  ast_call_expression* call = (ast_call_expression*)alloc->allocate(alloc, sizeof(ast_call_expression));
  if (call == NULL) {
    error_at(p_parser,
             trigger,
             ok_create_string_view("failed to allocate memory for call node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    ast_expressions_list_deinit(&args, alloc);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_call_expression_init(call, trigger, p_left, args, &s);
  return (ast_expression*)call;
}

ast_expression* parse_array_literal(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous; // '['
  ast_expressions_list elements = parse_expressions_list(p_parser, TOKEN_COMMA, TOKEN_RIGHT_BRACKET);
  if (elements.capacity == 1 && elements.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse array elements.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser); // ']'
  ast_array_expression* array = (ast_array_expression*)alloc->allocate(alloc, sizeof(ast_array_expression));
  if (array == NULL) {
    error_at(p_parser,
             trigger,
             ok_create_string_view("failed to allocate memory for array node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_expressions_list_deinit(&elements, alloc);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_array_expression_init(array, trigger, elements, &s);
  return (ast_expression*)array;
}

// `{k: v, ...}` in expression position. statement-position `{` is a block
// (parse_statement never consults the expression rules for its first token),
// so the disambiguation is positional and needs no lookahead. `{:}` is the
// empty map: a leading ':' is unambiguous because no expression starts with
// one. a trailing comma is allowed, mirroring arrays.
ast_expression* parse_map_literal(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous; // '{'
  ast_expressions_list keys;
  ast_expressions_list values;
  ast_expressions_list_init(&keys, alloc);
  ast_expressions_list_init(&values, alloc);

  if (p_parser->current.type == TOKEN_RIGHT_BRACE) {
    // `{}` in expression position: the ruled surface has exactly one empty
    // map literal, `{:}` - a bare `{}` stays the block statement's shape and
    // is rejected here rather than guessed at.
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("empty '{}' is not an expression - the empty map literal is '{:}'.",
                                   OK_STRING_VIEW_CALCULATE_LENGTH,
                                   true));
    goto fail_lists;
  }

  if (p_parser->current.type == TOKEN_COLON) {
    // `{:}` - the load-bearing empty literal (bindings are immutable, so
    // `let m = null` needs mut for later assignment; `{:}` doesn't).
    advance(p_parser); // ':' consumed, current = '}' (or an error to report)
    if (!expect(
            p_parser,
            TOKEN_RIGHT_BRACE,
            ok_create_string_view("expected '}' to complete the empty map.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
      goto fail_lists;
    }
  } else {
    for (;;) {
      advance(p_parser); // first key token into previous
      ast_expression* key = parse_expression(p_parser, PREC_NONE);
      if (key == NULL) {
        error_at(p_parser,
                 p_parser->previous,
                 ok_create_string_view("failed to parse map key.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto fail_lists;
      }
      if (!expect(p_parser,
                  TOKEN_COLON,
                  ok_create_string_view("expected ':' after map key.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
        ast_dispatch_deinit((ast_node*)key);
        alloc->release(alloc, key);
        goto fail_lists;
      }
      advance(p_parser); // first value token into previous
      ast_expression* value = parse_expression(p_parser, PREC_NONE);
      if (value == NULL) {
        error_at(p_parser,
                 p_parser->previous,
                 ok_create_string_view("failed to parse map value.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        ast_dispatch_deinit((ast_node*)key);
        alloc->release(alloc, key);
        goto fail_lists;
      }
      if (!ast_expressions_list_append(&keys, key)) {
        // the list did not take ownership
        ast_dispatch_deinit((ast_node*)key);
        alloc->release(alloc, key);
        ast_dispatch_deinit((ast_node*)value);
        alloc->release(alloc, value);
        goto fail_lists;
      }
      if (!ast_expressions_list_append(&values, value)) {
        // keys owns its entry, values does not
        ast_dispatch_deinit((ast_node*)value);
        alloc->release(alloc, value);
        goto fail_lists;
      }
      if (p_parser->current.type == TOKEN_COMMA) {
        advance(p_parser); // ',' consumed
        if (p_parser->current.type == TOKEN_RIGHT_BRACE) {
          break; // trailing comma
        }
        continue;
      }
      break;
    }
    if (!expect(p_parser,
                TOKEN_RIGHT_BRACE,
                ok_create_string_view("expected '}' after map literal.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
      goto fail_lists;
    }
  }

  ast_map_expression* map = (ast_map_expression*)alloc->allocate(alloc, sizeof(ast_map_expression));
  if (map == NULL) {
    error_at(p_parser,
             trigger,
             ok_create_string_view("failed to allocate memory for map node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_lists;
  }
  ast_specs s = {alloc};
  ast_map_expression_init(map, trigger, keys, values, &s);
  return (ast_expression*)map;
fail_lists:
  for (uint32_t i = 0; i < keys.count; ++i) {
    ast_dispatch_deinit((ast_node*)keys.data[i]);
    alloc->release(alloc, keys.data[i]);
  }
  for (uint32_t i = 0; i < values.count; ++i) {
    ast_dispatch_deinit((ast_node*)values.data[i]);
    alloc->release(alloc, values.data[i]);
  }
  ast_expressions_list_deinit(&keys, alloc);
  ast_expressions_list_deinit(&values, alloc);
  return NULL;
}

ast_expression* parse_subscript(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  (void)p_precedence;
  (void)is_right_associative;
  allocators* alloc = p_parser->alloc;
  token trigger = p_parser->previous; // '['
  advance(p_parser);                  // move the first index token into previous for parse_expression
  ast_expression* index = parse_expression(p_parser, PREC_NONE);
  if (index == NULL) {
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  if (p_parser->current.type != TOKEN_RIGHT_BRACKET) {
    error_at(
        p_parser, p_parser->current, ok_create_string_view("expected ']'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)index);
    alloc->release(alloc, index);
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  advance(p_parser); // ']'
  ast_subscript_expression* subscript =
      (ast_subscript_expression*)alloc->allocate(alloc, sizeof(ast_subscript_expression));
  if (subscript == NULL) {
    error_at(
        p_parser,
        trigger,
        ok_create_string_view("failed to allocate memory for subscript node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)index);
    alloc->release(alloc, index);
    ast_dispatch_deinit((ast_node*)p_left);
    alloc->release(alloc, p_left);
    return NULL;
  }
  ast_specs s = {alloc};
  ast_subscript_expression_init(subscript, trigger, p_left, index, &s);
  return (ast_expression*)subscript;
}

ast_expression* parse_compound_assign_expression(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  (void)p_precedence;
  (void)is_right_associative;
  allocators* alloc = p_parser->alloc;
  advance(p_parser); // first rhs token into previous
  if (!is_lvalue(p_left)) {
    error_at_noted(p_parser,
                   p_trigger,
                   ok_create_string_view("invalid compound assignment target.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view(
                       "the left side must be a variable, field or subscript.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  {
    ast_expression* right = parse_expression(p_parser, PREC_ASSIGNMENT - 1);
    if (right == NULL) {
      goto fail;
    }
    ast_compound_assign_expression* compound =
        (ast_compound_assign_expression*)alloc->allocate(alloc, sizeof(ast_compound_assign_expression));
    if (compound == NULL) {
      error_at(p_parser,
               p_trigger,
               ok_create_string_view(
                   "failed to allocate memory for compound assign node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)right);
      alloc->release(alloc, right);
      goto fail;
    }
    ast_specs s = {alloc};
    ast_compound_assign_expression_init(
        compound, p_trigger, operator_type_from_token_type(p_trigger.type, false), p_left, right, &s);
    return (ast_expression*)compound;
  }
fail:
  ast_dispatch_deinit((ast_node*)p_left);
  alloc->release(alloc, p_left);
  return NULL;
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
    return (ast_statement*)parse_throw_statement(p_parser);
  case TOKEN_TRY:
    return (ast_statement*)parse_try_statement(p_parser);
  case TOKEN_DEFER:
    return (ast_statement*)parse_defer_statement(p_parser, false);
  case TOKEN_DEFERR:
    return (ast_statement*)parse_defer_statement(p_parser, true);
  case TOKEN_CATCH:
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("'catch' can only follow a 'try' block.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
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
  case TOKEN_EXPORT:
    return DECLARATION_EXPORT;
  case TOKEN_PUB:
    return DECLARATION_PUB;
  case TOKEN_STATIC:
    return DECLARATION_STATIC;
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
      error_at(p_parser, p_parser->current, ok_create_string_view_from_string(message));
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
    statement = (ast_statement*)parse_let_declaration(p_parser, mods, false);
    break;
  }
  case TOKEN_FU: {
    if (mods == DECLARATION_NONE && parse_binding_modifier(p_parser->current) == BINDING_NONE &&
        p_parser->current.type != TOKEN_IDENTIFIER) {
      goto _default;
    }
    statement = (ast_statement*)parse_function_declaration(p_parser, mods, false);
    break;
  }
  case TOKEN_CLASS: {
    statement = (ast_statement*)parse_class_declaration(p_parser, mods);
    break;
  }
  default:
  _default: {
    if (mods != DECLARATION_NONE) {
      error_at_noted(p_parser,
                     p_parser->previous,
                     ok_create_string_view("illegal declaration modifier(s).", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                     ok_create_string_view("declaration modifiers can only appear before declarations.",
                                           OK_STRING_VIEW_CALCULATE_LENGTH,
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
      error_at_noted(p_parser,
                     p_parser->current,
                     ok_create_string_view_from_string(message),
                     ok_create_string_view_from_string(note));
      string_deinit(&mod_str, alloc);
      string_deinit(&message, alloc);
      string_deinit(&allowed_str, alloc);
      string_deinit(&note, alloc);
      return BINDING_ERROR;
    } else if ((mod & modifiers) != 0) { // this lowk should be a warning.
      string mod_str = ast_binding_modifiers_asprint(mod);
      string message = asprint(alloc, "duplicated binding modifier %s.", mod_str.chars);
      error_at(p_parser, p_parser->current, ok_create_string_view_from_string(message));
      string_deinit(&mod_str, alloc);
      string_deinit(&message, alloc);
      return BINDING_ERROR;
    }
    modifiers |= mod;
    advance(p_parser);
  }
  return modifiers;
}

ast_let_declaration* parse_let_declaration(parser* p_parser,
                                          ast_declaration_modifiers_t p_declaration_modifiers,
                                          bool p_in_class_body) {
  allocators* alloc = p_parser->alloc;
  const token let_tok = p_parser->previous;
  // static is a class-member modifier: fields only exist in class bodies.
  // pub is never allowed on a let - fields are not the surface, methods are.
  const ast_declaration_modifiers_t let_allowed_decl_mods =
      DECLARATION_GLOB | DECLARATION_EXPORT | (p_in_class_body ? DECLARATION_STATIC : 0);
  if ((p_declaration_modifiers & ~let_allowed_decl_mods) != 0) {
    string mods_str = ast_declaration_modifiers_asprint(p_declaration_modifiers, alloc);
    string message = asprint(alloc,
                             "illegal declaration modifier(s): [%s] in let declaration.",
                             mods_str.chars); // TODO: extract the offending only.
    string allowed_str = ast_declaration_modifiers_asprint(let_allowed_decl_mods, alloc);
    string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
    error_at_noted(
        p_parser, let_tok, ok_create_string_view_from_string(message), ok_create_string_view_from_string(note));
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
  // the destructuring form: `let (x, y) = f();`
  // the paren after the binding modifiers is unambiguous. `let` is a statement keyword, no
  // expression form starts here. the binding list takes the same per-binder
  // modifiers as any binding (`let (mut x, y) = f();`).
  if (p_parser->current.type == TOKEN_LEFT_PAREN) {
    return parse_let_declaration_span(p_parser, p_declaration_modifiers, binding_mods);
  }
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(
        p_parser,
        let_tok,
        ok_create_string_view("failed to allocate memory for binding node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_let_declaration* let = (ast_let_declaration*)alloc->allocate(alloc, sizeof(ast_let_declaration));
  if (let == NULL) {
    alloc->release(alloc, binding);
    error_at(p_parser,
             let_tok,
             ok_create_string_view("failed to allocate memory for let node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
      error_at(
          p_parser,
          let_tok,
          ok_create_string_view("expected an identifier in let declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)expr);
      goto free;
    }
  } else {
    error_at(
        p_parser,
        let_tok,
        ok_create_string_view("expected an identifier in let declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free;
  }
  advance(p_parser);
  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected ';' after let declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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

// `let (x, y) = f();` the destructuring let. the '(' is already
// current on entry. per-binder modifiers ride the shared binding-list parser.
// defaults are rejected (a let binding never has one), and the value must be
// a call (the compiler enforces that with an error).
ast_let_declaration* parse_let_declaration_span(parser* p_parser,
                                                ast_declaration_modifiers_t p_declaration_modifiers,
                                                ast_binding_modifiers_t p_binding_mods) {
  allocators* alloc = p_parser->alloc;
  const token let_tok = p_parser->previous;
  if (p_declaration_modifiers != DECLARATION_NONE) {
    string mods_str = ast_declaration_modifiers_asprint(p_declaration_modifiers, alloc);
    string message = asprint(alloc, "illegal declaration modifier(s): [%s] on a destructuring let.", mods_str.chars);
    error_at_noted(p_parser,
                   let_tok,
                   ok_create_string_view_from_string(message),
                   ok_create_string_view("the span bindings are locals only.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    string_deinit(&message, alloc);
    string_deinit(&mods_str, alloc);
    return NULL;
  }
  (void)p_binding_mods; // per-binder modifiers are parsed inside the list itself
  advance(p_parser);    // consume '('
  ast_bindings_list bindings = parse_bindings_list(p_parser, BINDING_MUT, TOKEN_COMMA, TOKEN_RIGHT_PAREN, false);
  if (bindings.capacity == 1 && bindings.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view(
                 "failed to parse the destructuring binding list.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (bindings.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view(
                 "a destructuring let needs at least one binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bindings;
  }
  for (uint32_t i = 0; i < bindings.count; ++i) {
    if (bindings.data[i]->default_value != NULL) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view(
                   "a destructuring let binding cannot have a default.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bindings;
    }
  }
  advance(p_parser); // consume ')'
  if (p_parser->current.type != TOKEN_ASSIGN) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view(
                 "expected '=' after the destructuring binding list.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bindings;
  }
  advance(p_parser); // consume '='
  advance(p_parser); // first value token into previous (parse_expression starts there)
  ast_expression* value = parse_expression(p_parser, PREC_NONE);
  if (value == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse the span call.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bindings;
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected ';' after let declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)value);
    alloc->release(alloc, value);
    goto free_bindings;
  }
  advance(p_parser); // consume ';'
  ast_let_declaration* let = (ast_let_declaration*)alloc->allocate(alloc, sizeof(ast_let_declaration));
  if (let == NULL) {
    error_at(p_parser,
             let_tok,
             ok_create_string_view("failed to allocate memory for let node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)value);
    alloc->release(alloc, value);
    goto free_bindings;
  }
  ast_specs s = {alloc};
  ast_let_declaration_init_span(let, bindings, value, p_declaration_modifiers, let_tok, &s);
  return let;
free_bindings:
  ast_bindings_list_deinit(&bindings, alloc);
  return NULL;
}

// the shared operator-spelling parser: the token after `operator` (already
// peeked at p_parser->current) is consumed, validated against the dispatchable
// set, and the CANONICAL name - "operator" + one space + spelling - is built
// into a fresh allocation the caller owns. one spelling for every use site
// (definitions and member access alike), independent of the user's whitespace:
// `operator+` and `operator  +` both canonicalize to "operator +".
// returns NULL after reporting an error.
static char* parse_operator_spelling(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token end = p_parser->current;
  const char* spelling = NULL;
  uint32_t spelling_length = 0;
  switch (end.type) {
  case TOKEN_LEFT_PAREN:
    advance(p_parser);
    if (!expect(p_parser, TOKEN_RIGHT_PAREN, ok_create_string_view("expected ')' after 'operator ('.",
                                                  OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
      return NULL;
    }
    end = p_parser->previous;
    spelling = "()";
    spelling_length = 2;
    break;
  case TOKEN_LEFT_BRACKET: {
    advance(p_parser); // consumed '['
    if (!expect(p_parser, TOKEN_RIGHT_BRACKET, ok_create_string_view("expected ']' after 'operator ['.",
                                                     OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
      return NULL;
    }
    // the store spelling's '=' trails the bracket pair: `operator []=`
    const bool is_store = p_parser->current.type == TOKEN_ASSIGN;
    if (is_store) {
      advance(p_parser);
    }
    end = p_parser->previous;
    spelling = is_store ? "[]=" : "[]";
    spelling_length = is_store ? 3 : 2;
    break;
  }
  case TOKEN_IDENTIFIER: {
    // conversion: one target identifier. builtin targets (str, num, bool)
    // and class names both parse here; validity is the compiler's call.
    token target = p_parser->current;
    if (target.length == 8 && memcmp(target.start, "operator", 8) == 0) {
      error_at(p_parser, target, ok_create_string_view("'operator' cannot follow 'operator'.",
                                                          OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL;
    }
    advance(p_parser);
    end = p_parser->previous;
    spelling = end.start;
    spelling_length = end.length;
    break;
  }
  case TOKEN_PLUS:
  case TOKEN_MINUS:
  case TOKEN_ASTERISK:
  case TOKEN_SLASH:
  case TOKEN_MODULO:
  case TOKEN_EQUAL:
  case TOKEN_LESS:
  case TOKEN_LESS_EQUAL:
  case TOKEN_GREATER:
  case TOKEN_GREATER_EQUAL:
    advance(p_parser);
    end = p_parser->previous;
    spelling = end.start;
    spelling_length = end.length;
    break;
  case TOKEN_BANG:
    error_at(p_parser,
             end,
             ok_create_string_view("'!' (truthiness) is not overloadable - truthiness is the control-flow substrate.",
                                   OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  case TOKEN_BANG_EQUAL:
    error_at(p_parser,
             end,
             ok_create_string_view("'!=' is the negation of '==' and is not separately overloadable - define 'operator =='.",
                                   OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  case TOKEN_CARET:
  case TOKEN_AMPERSAND:
  case TOKEN_BAR:
  case TOKEN_TILED:
  case TOKEN_SHIFT_LEFT:
  case TOKEN_SHIFT_RIGHT:
  case TOKEN_PLUS_EQUAL:
  case TOKEN_MINUS_EQUAL:
  case TOKEN_ASTERISK_EQUAL:
  case TOKEN_SLASH_EQUAL:
  case TOKEN_MODULO_EQUAL:
  case TOKEN_CARET_EQUAL:
  case TOKEN_AMPERSAND_EQUAL:
  case TOKEN_BAR_EQUAL:
  case TOKEN_TILED_EQUAL:
  case TOKEN_SHIFT_LEFT_EQUAL:
  case TOKEN_SHIFT_RIGHT_EQUAL: {
    string message = asprint(alloc,
                             "operator '%.*s' has no dispatchable form in 0.5.0 (compound operators desugar to their "
                             "binary form; bitwise operators are not implemented).",
                             end.length,
                             end.start);
    error_at(p_parser, end, ok_create_string_view_from_string(message));
    string_deinit(&message, alloc);
    return NULL;
  }
  default:
    error_at(p_parser,
             end,
             ok_create_string_view("expected an operator symbol after 'operator'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  // the canonical name: "operator" + ' ' + spelling, always exactly one space.
  char* canonical = (char*)alloc->allocate(alloc, 9 + spelling_length + 1); // 8 + 1 + spelling + NUL
  if (canonical == NULL) {
    error_at(p_parser,
             end,
             ok_create_string_view("failed to allocate memory for operator name.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  memcpy(canonical, "operator ", 9);
  memcpy(canonical + 9, spelling, spelling_length);
  canonical[9 + spelling_length] = '\0';
  return canonical;
}

ast_function_declaration* parse_function_declaration(parser* p_parser,
                                                     ast_declaration_modifiers_t p_declaration_modifiers,
                                                     bool p_in_class_body) {
  allocators* alloc = p_parser->alloc;
  token fu_tok = p_parser->previous;
  const ast_declaration_modifiers_t fu_allowed_decl_mods =
      DECLARATION_GLOB | DECLARATION_EXPORT | (p_in_class_body ? (DECLARATION_PUB | DECLARATION_STATIC) : 0);
  if ((p_declaration_modifiers & ~fu_allowed_decl_mods) != 0) {
    string mods_str = ast_declaration_modifiers_asprint(p_declaration_modifiers, alloc);
    string message = asprint(alloc,
                             "illegal declaration modifier(s): [%s] in fu declaration.",
                             mods_str.chars); // TODO: extract the offending only.
    string allowed_str = ast_declaration_modifiers_asprint(fu_allowed_decl_mods, alloc);
    string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
    error_at_noted(
        p_parser, fu_tok, ok_create_string_view_from_string(message), ok_create_string_view_from_string(note));
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
             ok_create_string_view("expected an identifier as function name.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  token name_tok = p_parser->previous;
  // a method literally named "operator" is an operator declaration: the
  // canonical name ("operator +", "operator ()", "operator []=", "operator
  // Target") is synthesized by the shared spelling parser, so every consumer
  // (the method table, the dispatch arms, the member syntax) sees one string
  // regardless of the user's whitespace. non-dispatchable spellings
  // ('!', '!=', compound and bitwise forms) are refused here, at the
  // definition site, with the reason.
  char* mangled_name = NULL;
  if (p_in_class_body && name_tok.length == 8 && memcmp(name_tok.start, "operator", 8) == 0) {
    mangled_name = parse_operator_spelling(p_parser);
    if (mangled_name == NULL) {
      return NULL;
    }
    p_parser->previous = name_tok; // the binding's token stays the bare keyword
  }
  ast_identifier_expression* ident =
      (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
  if (ident == NULL) {
    error_at(
        p_parser,
        p_parser->previous,
        ok_create_string_view("failed to allocate memory for identifier node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_identifier_expression_init(ident, p_parser->previous, &s); // don't attempt to parse as an expression, because the
                                                                 // parse rule for () call expression will trigger.
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(
        p_parser,
        fu_tok,
        ok_create_string_view("failed to allocate memory for binding node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_ident;
  }
  ast_binding_init(binding, (ast_expression*)ident, binding_mods, p_parser->current, &s);
  if (p_parser->current.type != TOKEN_LEFT_PAREN) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected '(' in function declaration", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind;
  }
  advance(p_parser);
  ast_bindings_list params = parse_bindings_list(p_parser, BINDING_MUT, TOKEN_COMMA, TOKEN_RIGHT_PAREN, true);
  if (params.capacity == 1 && params.count == 0) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse function parameters.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind;
  }

  advance(p_parser);
  advance(p_parser);
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to parse function body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_declaration* fu = (ast_function_declaration*)alloc->allocate(alloc, sizeof(ast_function_declaration));
  if (fu == NULL) {
    error_at(
        p_parser,
        p_parser->previous,
        ok_create_string_view("failed to allocate memory for function node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_bind_list;
  }
  ast_function_declaration_init(fu, binding, params, body, p_declaration_modifiers, fu_tok, &s);
  ast_function_declaration_set_mangled_name(fu, mangled_name); // ownership moves into the node
  return fu;
free_bind_list:
  if (mangled_name != NULL) {
    alloc->release(alloc, mangled_name);
  }
  ast_bindings_list_deinit(&params, alloc);
free_bind:
  if (mangled_name != NULL) {
    alloc->release(alloc, mangled_name);
  }
  ast_binding_deinit(binding);
  alloc->release(alloc, binding);
  return NULL; // if binding successfully initialized it will free ident.
free_ident:
  ast_identifier_expression_deinit(ident);
  alloc->release(alloc, ident);
  return NULL;
}

// `.` member access: left.name. in call position the compiler fuses this
// into OP_INVOKE; standing alone it is a field read (visible) or a bound
// method.
ast_expression* parse_access(
    parser* p_parser, ast_expression* p_left, precedence p_precedence, bool is_right_associative, token p_trigger) {
  (void)p_precedence;
  (void)is_right_associative;
  allocators* alloc = p_parser->alloc;
  advance(p_parser); // the '.'
  if (p_parser->previous.type != TOKEN_IDENTIFIER) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a member name after '.'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  // the operator member syntax: `a.operator +`, `Foo.operator ()` - the same
  // fused spelling as declarations, producing the same canonical name, so an
  // operator method is reachable (and bindable) like any other member. the
  // identifier `operator` is reserved in member position: a member so named
  // must spell an operator.
  char* owned_name = NULL;
  if (p_parser->previous.length == 8 && memcmp(p_parser->previous.start, "operator", 8) == 0) {
    owned_name = parse_operator_spelling(p_parser);
    if (owned_name == NULL) {
      goto fail;
    }
  }
  ast_access_expression* access = (ast_access_expression*)alloc->allocate(alloc, sizeof(ast_access_expression));
  if (access == NULL) {
    if (owned_name != NULL) {
      alloc->release(alloc, owned_name);
    }
    error_at(p_parser,
             p_trigger,
             ok_create_string_view("failed to allocate memory for access node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  ast_specs s = {alloc};
  ast_access_expression_init(access, p_trigger, p_left, p_parser->previous, &s);
  ast_access_expression_set_owned_name(access, owned_name);
  return (ast_expression*)access;
fail:
  ast_dispatch_deinit((ast_node*)p_left);
  alloc->release(alloc, p_left);
  return NULL;
}

ast_expression* parse_this(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_this_expression* this_expr = (ast_this_expression*)alloc->allocate(alloc, sizeof(ast_this_expression));
  if (this_expr == NULL) {
    error_at(p_parser,
             p_trigger,
             ok_create_string_view("failed to allocate memory for this node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_this_expression_init(this_expr, p_trigger, &s);
  return (ast_expression*)this_expr;
}

ast_expression* parse_super(parser* p_parser, token p_trigger) {
  allocators* alloc = p_parser->alloc;
  ast_super_expression* super_expr = (ast_super_expression*)alloc->allocate(alloc, sizeof(ast_super_expression));
  if (super_expr == NULL) {
    error_at(p_parser,
             p_trigger,
             ok_create_string_view("failed to allocate memory for super node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  ast_specs s = {alloc};
  ast_super_expression_init(super_expr, p_trigger, &s);
  return (ast_expression*)super_expr;
}

// `class Name : Parent { members }`. members are let fields and fu methods
// (pub composes on methods); anything else is a parse error.
ast_class_declaration* parse_class_declaration(parser* p_parser, ast_declaration_modifiers_t p_modifiers) {
  allocators* alloc = p_parser->alloc;
  token class_tok = p_parser->previous;
  const ast_declaration_modifiers_t class_allowed_decl_mods = DECLARATION_GLOB | DECLARATION_EXPORT;
  if ((p_modifiers & ~class_allowed_decl_mods) != 0) {
    string mods_str = ast_declaration_modifiers_asprint(p_modifiers, alloc);
    string message = asprint(alloc, "illegal declaration modifier(s): [%s] in class declaration.", mods_str.chars);
    string allowed_str = ast_declaration_modifiers_asprint(class_allowed_decl_mods, alloc);
    string note = asprint(alloc, "allowed are: [%s]", allowed_str.chars);
    error_at_noted(
        p_parser, class_tok, ok_create_string_view_from_string(message), ok_create_string_view_from_string(note));
    string_deinit(&message, alloc);
    string_deinit(&mods_str, alloc);
    string_deinit(&note, alloc);
    string_deinit(&allowed_str, alloc);
    return NULL;
  }
  if (p_parser->current.type != TOKEN_IDENTIFIER) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected a class name.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser);
  token name_tok = p_parser->previous;
  ast_expression* parent = NULL;
  if (p_parser->current.type == TOKEN_COLON) {
    advance(p_parser); // the ':'
    advance(p_parser); // the parent's first token, staged for parse_expression
    parent = parse_expression(p_parser, PREC_NONE);
    if (parent == NULL) {
      return NULL;
    }
  }
  if (!expect(p_parser, TOKEN_LEFT_BRACE,
               ok_create_string_view("expected '{' to open the class body.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
    goto free_parent;
  }
  ast_statements_list members;
  ast_statements_list_init(&members, alloc);
  while (p_parser->current.type != TOKEN_RIGHT_BRACE && p_parser->current.type != TOKEN_EOF) {
    if (!advance(p_parser)) {
      goto free_members;
    }
    ast_declaration_modifiers_t mods = try_parse_declaration_modifiers(p_parser);
    if (mods == DECLARATION_ERROR) {
      goto free_members;
    }
    ast_statement* member = NULL;
    switch (p_parser->previous.type) {
    case TOKEN_LET: {
      member = (ast_statement*)parse_let_declaration(p_parser, mods, true);
      break;
    }
    case TOKEN_FU: {
      member = (ast_statement*)parse_function_declaration(p_parser, mods, true);
      break;
    }
    default:
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view("class bodies take 'let' fields and 'fu' methods.",
                                     OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_members;
    }
    if (member == NULL) {
      goto free_members;
    }
    // the `operator` reservation: a field so named would be unreachable through
    // the member syntax (`.operator` always fuses into an operator member
    // name), so the declaration is refused rather than silently stranded.
    if (member->node.node_type == AST_LET_DECLARATION) {
      ast_let_declaration* let_member = (ast_let_declaration*)member;
      const ok_string_view member_name =
          ast_identifier_expression_get_value((ast_identifier_expression*)let_member->binding->lvalue);
      if (ok_string_view_get_length(&member_name) == 8 && memcmp(member_name.chars, "operator", 8) == 0) {
        const token where = let_member->declaration.statement.node.token; // captured before the free
        ast_dispatch_deinit((ast_node*)member);
        alloc->release(alloc, member);
        error_at(p_parser,
                 where,
                 ok_create_string_view("'operator' is reserved in class bodies: spell an operator method "
                                       "('fu operator +') or choose another field name.",
                                       OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto free_members;
      }
    }
    if (!ast_statements_list_append(&members, member)) {
      ast_dispatch_deinit((ast_node*)member);
      alloc->release(alloc, member);
      goto free_members;
    }
  }
  if (!expect(p_parser, TOKEN_RIGHT_BRACE,
               ok_create_string_view("expected '}' to close the class body.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
    goto free_members;
  }
  {
    ast_class_declaration* klass =
        (ast_class_declaration*)alloc->allocate(alloc, sizeof(ast_class_declaration));
    if (klass == NULL) {
      error_at(p_parser,
               class_tok,
               ok_create_string_view("failed to allocate memory for class node.",
                                     OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_members;
    }
    ast_specs s = {alloc};
    ast_class_declaration_init(klass, name_tok, parent, members, p_modifiers, class_tok, &s);
    return klass;
  }
free_members:
  // the list deinit dispatches on every element: no manual walk (it would
  // free each member twice).
  ast_statements_list_deinit(&members, alloc);
free_parent:
  if (parent != NULL) {
    ast_dispatch_deinit((ast_node*)parent);
    alloc->release(alloc, parent);
  }
  return NULL;
}

ast_empty_statement* parse_empty_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;

  ast_empty_statement* empty = (ast_empty_statement*)alloc->allocate(alloc, sizeof(ast_empty_statement));
  if (empty == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to allocate memory for empty node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("expected ';' after expression statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  advance(p_parser); // ;
  ast_expression_statement* expression_statement = (ast_expression_statement*)malloc(sizeof(ast_expression_statement));
  if (expression_statement == NULL) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view(
                 "failed to allocate memory for expression statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("expected ';' after print statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail;
  }
  advance(p_parser);
  ast_print_statement* print = (ast_print_statement*)alloc->allocate(alloc, sizeof(ast_print_statement));
  if (print == NULL) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("failed to allocate memory for print node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser, p_parser->current, ok_create_string_view("expected '}'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
             ok_create_string_view("expected an expression as if condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (p_parser->current.type != TOKEN_LEFT_BRACE) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected '{' after if condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  advance(p_parser); // to '{'
  ast_statement* cons = parse_statement(p_parser);
  if (cons == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a statement as if consequense.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  ast_statement* alt = NULL;
  if (p_parser->current.type == TOKEN_ELSE) {
    advance(p_parser);
    if (p_parser->current.type == TOKEN_LEFT_BRACE) {
      // advance(p_parser);
    }
    advance(p_parser);
    alt = parse_statement(p_parser);
    if (alt == NULL) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view("expected a statement as if alternative.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_parser->previous,
        ok_create_string_view("expected an expression as while condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (p_parser->current.type != TOKEN_LEFT_BRACE) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected '{' after while condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  advance(p_parser); // to '{'
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a statement as while body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_cond;
  }
  ast_while_statement* _while = (ast_while_statement*)alloc->allocate(alloc, sizeof(ast_while_statement));
  if (_while == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to allocate memory for while node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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

// called with current == TOKEN_COLON and the loop-variable head already consumed:
// `for (let (mut)?) IDENT :` the rest is `<iterable> <compound_statement>`.
// p_has_value: the two-binding map form `for let k, (mut)? v :` - binding binds
// keys, value_binding binds values (arrays reject it at runtime).
static ast_for_in_statement* parse_for_in_rest(parser* p_parser,
                                               token p_for_tok,
                                               token p_name_tok,
                                               ast_binding_modifiers_t p_binds,
                                               token p_value_name_tok,
                                               ast_binding_modifiers_t p_value_binds,
                                               bool p_has_value) {
  allocators* alloc = p_parser->alloc;
  advance(p_parser); // ':' consumed, current = first iterable token
  advance(p_parser); // iterable's first token into previous
  ast_expression* iterable = parse_expression(p_parser, PREC_NONE);
  if (iterable == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected an expression to iterate.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  if (p_parser->current.type != TOKEN_LEFT_BRACE) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected '{' after ranged-for iterable.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)iterable);
    alloc->release(alloc, iterable);
    return NULL;
  }
  advance(p_parser); // to '{'
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a statement as ranged-for body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_dispatch_deinit((ast_node*)iterable);
    alloc->release(alloc, iterable);
    return NULL;
  }
  ast_identifier_expression* ident =
      (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
  if (ident == NULL) {
    error_at(p_parser,
             p_for_tok,
             ok_create_string_view(
                 "failed to allocate memory for ranged-for binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_body;
  }
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(p_parser,
             p_for_tok,
             ok_create_string_view(
                 "failed to allocate memory for ranged-for binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_identifier_expression_deinit(ident);
    alloc->release(alloc, ident);
    goto fail_body;
  }
  ast_identifier_expression* value_ident = NULL;
  ast_binding* value_binding = NULL;
  if (p_has_value) {
    value_ident = (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
    value_binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
    if (value_ident == NULL || value_binding == NULL) {
      error_at(p_parser,
               p_for_tok,
               ok_create_string_view(
                   "failed to allocate memory for ranged-for binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      if (value_ident != NULL) {
        alloc->release(alloc, value_ident);
      }
      if (value_binding != NULL) {
        alloc->release(alloc, value_binding);
      }
      goto fail_binding;
    }
  }
  ast_for_in_statement* fin = (ast_for_in_statement*)alloc->allocate(alloc, sizeof(ast_for_in_statement));
  if (fin == NULL) {
    error_at(
        p_parser,
        p_for_tok,
        ok_create_string_view("failed to allocate memory for ranged-for node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (value_ident != NULL) {
      ast_identifier_expression_deinit(value_ident);
      alloc->release(alloc, value_ident);
    }
    alloc->release(alloc, value_binding);
    goto fail_binding;
  }
  ast_specs s = {alloc};
  ast_identifier_expression_init(ident, p_name_tok, &s);
  ast_binding_init(binding, (ast_expression*)ident, p_binds, p_name_tok, &s);
  if (p_has_value) {
    ast_identifier_expression_init(value_ident, p_value_name_tok, &s);
    ast_binding_init(value_binding, (ast_expression*)value_ident, p_value_binds, p_value_name_tok, &s);
  }
  ast_for_in_statement_init(fin, p_for_tok, &s);
  fin->binding = binding;
  fin->value_binding = p_has_value ? value_binding : NULL;
  fin->iterable = iterable;
  fin->body = body;
  return fin;
fail_binding:
  ast_binding_deinit(binding); // frees ident
  alloc->release(alloc, binding);
fail_body:
  ast_dispatch_deinit((ast_node*)body);
  alloc->release(alloc, body);
  ast_dispatch_deinit((ast_node*)iterable);
  alloc->release(alloc, iterable);
  return NULL;
}

// `for let (mut)? IDENT = <start> : <limit> (: <step>)? <compound_statement>`.
// called with current == TOKEN_COLON and the head already parsed into p_init_expr
// (an `IDENT = <expr>` assign expression, dismantled here the same way
// build_let_init dismantles the C style initializer).
static ast_for_numeric_statement* parse_for_numeric_rest(
    parser* p_parser, token p_for_tok, token p_name_tok, ast_binding_modifiers_t p_binds, ast_expression* p_init_expr) {
  allocators* alloc = p_parser->alloc;
  if (p_init_expr == NULL || p_init_expr->node.node_type != AST_ASSIGN_EXPRESSION ||
      ((ast_assign_expression*)p_init_expr)->left->node.node_type != AST_IDENTIFIER_EXPRESSION) {
    error_at(p_parser,
             p_name_tok,
             ok_create_string_view("expected '=' after the for binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (p_init_expr != NULL) {
      ast_dispatch_deinit((ast_node*)p_init_expr);
      alloc->release(alloc, p_init_expr);
    }
    return NULL;
  }
  ast_assign_expression* assign = (ast_assign_expression*)p_init_expr;

  advance(p_parser); // ':' consumed, current = first limit token
  advance(p_parser); // limit's first token into previous
  ast_expression* limit = parse_expression(p_parser, PREC_NONE);
  if (limit == NULL) {
    error_at(
        p_parser,
        p_parser->previous,
        ok_create_string_view("expected an expression as numeric for limit.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_assign;
  }

  ast_expression* step = NULL;
  if (p_parser->current.type == TOKEN_COLON) {
    advance(p_parser); // second ':' consumed, current = first step token
    advance(p_parser); // step's first token into previous
    step = parse_expression(p_parser, PREC_NONE);
    if (step == NULL) {
      error_at(
          p_parser,
          p_parser->previous,
          ok_create_string_view("expected an expression as numeric for step.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto fail_limit;
    }
  }

  if (p_parser->current.type != TOKEN_LEFT_BRACE) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected '{' after numeric for clauses.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_step;
  }
  advance(p_parser); // to '{'
  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a statement as numeric for body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_step;
  }

  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  if (binding == NULL) {
    error_at(p_parser,
             p_for_tok,
             ok_create_string_view(
                 "failed to allocate memory for numeric for binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_body;
  }
  ast_for_numeric_statement* fnum =
      (ast_for_numeric_statement*)alloc->allocate(alloc, sizeof(ast_for_numeric_statement));
  if (fnum == NULL) {
    error_at(p_parser,
             p_for_tok,
             ok_create_string_view(
                 "failed to allocate memory for numeric for node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    alloc->release(alloc, binding);
    goto fail_body;
  }
  ast_specs s = {alloc};
  // steal the assign's sides like build_let_init does, then free the hollow shell
  ast_binding_init(binding, assign->left, p_binds, p_name_tok, &s);
  ast_for_numeric_statement_init(fnum, p_for_tok, &s);
  fnum->binding = binding;
  fnum->start = assign->right;
  fnum->limit = limit;
  fnum->step = step;
  fnum->body = body;
  assign->left = NULL;
  assign->right = NULL;
  alloc->release(alloc, assign);
  return fnum;
fail_body:
  ast_dispatch_deinit((ast_node*)body);
  alloc->release(alloc, body);
fail_step:
  if (step != NULL) {
    ast_dispatch_deinit((ast_node*)step);
    alloc->release(alloc, step);
  }
fail_limit:
  ast_dispatch_deinit((ast_node*)limit);
  alloc->release(alloc, limit);
fail_assign:
  ast_dispatch_deinit((ast_node*)assign);
  alloc->release(alloc, assign);
  return NULL;
}

// rebuilds `let (mut)? IDENT (= expr)? ;` from the expression the for head
// discrimination already parsed, mirroring parse_let_declaration's extraction.
// leaves the parser exactly where the original C style for flow expects it:
// previous = the init's ';'.
static ast_let_declaration* build_let_init_from_expression(
    parser* p_parser, token p_let_tok, token p_name_tok, ast_binding_modifiers_t p_binds, ast_expression* expr) {
  allocators* alloc = p_parser->alloc;
  ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
  ast_let_declaration* let = (ast_let_declaration*)alloc->allocate(alloc, sizeof(ast_let_declaration));
  if (binding == NULL || let == NULL) {
    error_at(p_parser,
             p_let_tok,
             ok_create_string_view("failed to allocate memory for let node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (expr != NULL) {
      ast_dispatch_deinit((ast_node*)expr);
      alloc->release(alloc, expr);
    }
    if (binding != NULL) {
      alloc->release(alloc, binding);
    }
    if (let != NULL) {
      alloc->release(alloc, let);
    }
    return NULL;
  }
  ast_specs s = {alloc};
  ast_null_expression* null = NULL; // set for the bare `let x;` form, initialized by the ';'
  if (expr != NULL && expr->node.node_type == AST_ASSIGN_EXPRESSION) {
    ast_assign_expression* assign = (ast_assign_expression*)expr;
    if (assign->left->node.node_type == AST_IDENTIFIER_EXPRESSION) {
      ast_binding_init(binding, assign->left, p_binds, p_name_tok, &s);
      ast_let_declaration_init(let, binding, assign->right, DECLARATION_NONE, p_let_tok, &s);
      assign->left = NULL;
      assign->right = NULL;
      alloc->release(alloc, expr);
    } else {
      ast_dispatch_deinit((ast_node*)expr);
      goto malformed;
    }
  } else if (expr != NULL && expr->node.node_type == AST_IDENTIFIER_EXPRESSION) {
    // `let x;` style null initializer: the node is authored by the ';' (see below)
    null = (ast_null_expression*)alloc->allocate(alloc, sizeof(ast_null_expression));
    if (null == NULL) {
      ast_dispatch_deinit((ast_node*)expr);
      goto malformed;
    }
    ast_binding_init(binding, expr, p_binds, p_name_tok, &s);
    ast_let_declaration_init(let, binding, (ast_expression*)null, DECLARATION_NONE, p_let_tok, &s);
  } else {
    if (expr != NULL) {
      ast_dispatch_deinit((ast_node*)expr);
    }
    goto malformed;
  }
  advance(p_parser);
  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected ';' after for initializer.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    ast_let_declaration_deinit(let);
    alloc->release(alloc, let);
    return NULL;
  }
  if (null != NULL) {
    ast_null_expression_init(null, p_parser->previous, &s); // the ';' authored the null
  }
  return let;
malformed:
  error_at(p_parser,
           p_let_tok,
           ok_create_string_view("expected an identifier in let declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  alloc->release(alloc, binding);
  alloc->release(alloc, let);
  return NULL;
}

ast_for_statement* parse_for_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token for_tok = p_parser->previous;
  advance(p_parser);
  ast_statement* init = NULL;
  ast_expression* cond = NULL;
  ast_expression* up = NULL;

  if (p_parser->previous.type == TOKEN_LET) {
    // `for let ...` is either the for-in form (`for let v : arr {`) or a C style
    // let initializer (`for let mut i = 0; ...`). one token lookahead: consume the
    // binding head, then discriminate on ':'.
    const ast_binding_modifiers_t binds = parse_binding_modifiers(p_parser, BINDING_MUT);
    if (binds == BINDING_ERROR) {
      return NULL;
    }
    if (p_parser->current.type != TOKEN_IDENTIFIER) {
      error_at(
          p_parser,
          p_parser->current,
          ok_create_string_view("expected an identifier in for declaration.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL;
    }
    const token name_tok = p_parser->current;
    advance(p_parser); // previous = name, current = ':' | ',' | '=' | ';' | ...
    if (p_parser->current.type == TOKEN_COLON) {
      // the cast is safe: both statements share the ast_statement prefix.
      // the caller dispatches on node_type.
      return (ast_for_statement*)parse_for_in_rest(p_parser, for_tok, name_tok, binds, name_tok, BINDING_NONE, false);
    }
    if (p_parser->current.type == TOKEN_COMMA) {
      // `for let k, (mut)? v : <iterable>` - the two-binding map form: keys and
      // values. one `let` governs the pair, each binder takes its own `mut`.
      advance(p_parser); // ',' consumed, current = 'mut' | identifier
      const ast_binding_modifiers_t value_binds = parse_binding_modifiers(p_parser, BINDING_MUT);
      if (value_binds == BINDING_ERROR) {
        return NULL;
      }
      if (p_parser->current.type != TOKEN_IDENTIFIER) {
        error_at(p_parser,
                 p_parser->current,
                 ok_create_string_view(
                     "expected an identifier for the map for value binding.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL;
      }
      const token value_tok = p_parser->current;
      advance(p_parser); // previous = value name, current = ':'
      if (p_parser->current.type != TOKEN_COLON) {
        error_at(
            p_parser,
            p_parser->current,
            ok_create_string_view("expected ':' after the map for bindings.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL;
      }
      return (ast_for_statement*)parse_for_in_rest(p_parser, for_tok, name_tok, binds, value_tok, value_binds, true);
    }
    // `for let (mut)? IDENT = <expr> ...` or the bare `for let x; ...` C init:
    // parse the head expression once (previous = name, so `x = 0` parses as an
    // assign expression exactly like build_let_init's own parse did), then
    // discriminate on its terminator: ':' -> numeric, ';' -> C style.
    ast_expression* init_expr = parse_expression(p_parser, PREC_NONE);
    if (p_parser->current.type == TOKEN_COLON) {
      // the cast is safe: both statements share the ast_statement prefix.
      // the caller dispatches on node_type.
      return (ast_for_statement*)parse_for_numeric_rest(p_parser, for_tok, name_tok, binds, init_expr);
    }
    init = (ast_statement*)build_let_init_from_expression(p_parser, for_tok, name_tok, binds, init_expr);
    if (init == NULL) {
      return NULL;
    }
  } else if (p_parser->previous.type != TOKEN_SEMICOLON) {
    init = try_parse_declaration(p_parser);
    if (init == NULL) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view("invalid for initializer.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL;
    }
  }

  advance(p_parser);
  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    cond = parse_expression(p_parser, PREC_NONE);
    if (cond == NULL) {
      error_at(
          p_parser,
          p_parser->previous,
          ok_create_string_view("expected an expression as for condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_init;
    }
    advance(p_parser);
  }

  if (p_parser->previous.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected ';' after if condition.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_cond;
  }
  advance(p_parser);

  if (p_parser->previous.type != TOKEN_LEFT_BRACE) {
    up = parse_expression(p_parser, PREC_NONE);
    if (up == NULL) {
      error_at(
          p_parser,
          p_parser->previous,
          ok_create_string_view("expected an expression as for increment.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_cond;
    }
    advance(p_parser);
  }

  if (p_parser->previous.type != TOKEN_LEFT_BRACE) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("excpected '{' after for clauses", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_up;
  }
  // advance(p_parser);

  ast_statement* body = parse_statement(p_parser);
  if (body == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("expected a statement as for body.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto free_up;
  }

  ast_for_statement* _for = (ast_for_statement*)alloc->allocate(alloc, sizeof(ast_for_statement));
  if (_for == NULL) {
    error_at(p_parser,
             p_parser->previous,
             ok_create_string_view("failed to allocate memory for for node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
    error_at(
        p_parser,
        p_parser->current,
        ok_create_string_view("expected ';' after control flow statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
  ast_expressions_list returned;
  ast_expressions_list_init(&returned, alloc);
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    // the span form: `return a, b, c;` a comma list of expressions under one
    // return (the count is the path's result arity, inferred).
    // convention: parse_expression starts at previous, so each element costs
    // one advance into its first token, after a comma that is two (comma, then
    // the token). the same shape parse_expressions_list rides.
    advance(p_parser); // first element's first token into previous
    while (true) {
      ast_expression* expr = parse_expression(p_parser, PREC_NONE);
      if (expr == NULL) {
        goto free;
      }
      if (!ast_expressions_list_append(&returned, expr)) {
        ast_dispatch_deinit((ast_node*)expr);
        alloc->release(alloc, expr);
        goto free;
      }
      if (p_parser->current.type == TOKEN_COMMA) {
        advance(p_parser); // consume ','
        advance(p_parser); // next element's first token into previous
        continue;
      }
      break;
    }
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected ';' after return statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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
  ast_expressions_list_deinit(&returned, alloc);
  return NULL;
}
// `throw expr;` the value is the error. `throw;` (bare) is the rethrow:
// legal only inside a catch block, checked at compile time
// (the compiler knows the enclosing catch's parked-error register.
// the parser does not track catch context).
static ast_throw_statement* parse_throw_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token throw_tok = p_parser->previous;
  ast_expression* expression = NULL;

  if (p_parser->current.type != TOKEN_SEMICOLON) {
    advance(p_parser); // first expression token into previous
    expression = parse_expression(p_parser, PREC_NONE);
    if (expression == NULL) {
      return NULL;
    }
  }
  if (p_parser->current.type != TOKEN_SEMICOLON) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected ';' after throw statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (expression != NULL) {
      ast_dispatch_deinit((ast_node*)expression);
      alloc->release(alloc, expression);
    }
    return NULL;
  }
  advance(p_parser);
  ast_throw_statement* throw_stmt = (ast_throw_statement*)alloc->allocate(alloc, sizeof(ast_throw_statement));
  if (throw_stmt == NULL) {
    error_at(p_parser,
             throw_tok,
             ok_create_string_view("failed to allocate memory for throw node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (expression != NULL) {
      ast_dispatch_deinit((ast_node*)expression);
      alloc->release(alloc, expression);
    }
    return NULL;
  }
  ast_specs s = {alloc};
  ast_throw_statement_init(throw_stmt, expression, throw_tok, &s);
  return throw_stmt;
}

// `try { B } catch e { H }` / `catch { H }` the binder is optional and
// immutable (no parens anywhere, `catch e { }` reads like every other
// binding in the language). the try block must be followed by a catch
// clause. deferred cleanup is not a clause of try, it is the `defer` statement, scoped to whatever block owns the
// resource.
static ast_try_statement* parse_try_statement(parser* p_parser) {
  allocators* alloc = p_parser->alloc;
  token try_tok = p_parser->previous;
  ast_specs s = {alloc};

  if (!expect(p_parser,
              TOKEN_LEFT_BRACE,
              ok_create_string_view("expected '{' after 'try'.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
    return NULL;
  }
  ast_compound_statement* body = parse_compound_statement(p_parser);
  if (body == NULL) {
    return NULL;
  }

  ast_binding* catch_binding = NULL;
  ast_statement* catch_body = NULL;

  if (p_parser->current.type == TOKEN_CATCH) {
    advance(p_parser); // 'catch' into previous; the binder (if any) is current
    if (p_parser->current.type == TOKEN_MUT) {
      error_at(p_parser,
               p_parser->current,
               ok_create_string_view("the catch binder is immutable.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto fail_body;
    }
    if (p_parser->current.type == TOKEN_IDENTIFIER) {
      const token name_tok = p_parser->current;
      advance(p_parser); // binder into previous
      // the binder: an immutable local scoped to the catch block. built the
      // same way for-in builds its loop binding.
      ast_identifier_expression* ident =
          (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
      if (ident == NULL) {
        error_at(p_parser,
                 try_tok,
                 ok_create_string_view(
                     "failed to allocate memory for catch binder.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto fail_body;
      }
      ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
      if (binding == NULL) {
        alloc->release(alloc, ident);
        error_at(p_parser,
                 try_tok,
                 ok_create_string_view(
                     "failed to allocate memory for catch binder.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto fail_body;
      }
      ast_identifier_expression_init(ident, name_tok, &s);
      ast_binding_init(binding, (ast_expression*)ident, BINDING_NONE, name_tok, &s);
      catch_binding = binding;
    }
    if (!expect(p_parser,
                TOKEN_LEFT_BRACE,
                ok_create_string_view("expected '{' after catch binder.", OK_STRING_VIEW_CALCULATE_LENGTH, true))) {
      goto fail_binding;
    }
    ast_compound_statement* handler = parse_compound_statement(p_parser);
    if (handler == NULL) {
      goto fail_binding;
    }
    catch_body = (ast_statement*)handler;
  }

  if (catch_body == NULL) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view("expected 'catch' after try block.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_body;
  }

  {
    ast_try_statement* try_stmt = (ast_try_statement*)alloc->allocate(alloc, sizeof(ast_try_statement));
    if (try_stmt == NULL) {
      error_at(p_parser,
               try_tok,
               ok_create_string_view("failed to allocate memory for try node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto fail_all;
    }
    ast_try_statement_init(try_stmt, try_tok, &s);
    try_stmt->body = (ast_statement*)body;
    try_stmt->catch_binding = catch_binding;
    try_stmt->catch_body = catch_body;
    return try_stmt;
  }

fail_all:
  if (catch_body != NULL) {
    ast_dispatch_deinit((ast_node*)catch_body);
    alloc->release(alloc, catch_body);
  }
fail_binding:
  if (catch_binding != NULL) {
    ast_binding_deinit(catch_binding);
    alloc->release(alloc, catch_binding);
  }
fail_body:
  ast_dispatch_deinit((ast_node*)body);
  alloc->release(alloc, body);
  return NULL;
}

// a synthetic TOKEN_IDENTIFIER over an owned hidden name: the position rides
// the defer statement's (diagnostics inside the thunk point at the defer
// site, which is the honest author), the bytes come from the names block.
static token hidden_name_token(token p_source, const char* p_start, uint32_t p_length) {
  token t = p_source;
  t.type = TOKEN_IDENTIFIER;
  t.start = p_start;
  t.length = p_length;
  return t;
}

// `defer f(a, b)` / `deferr f(a, b)`: schedule the call for the enclosing
// scope's exit. the parsed call is kept whole, it is the snapshot source
// (callee and arguments compile into hidden locals at the defer site, so
// later reassignments of `a` are not what the deferred call sees). and the
// thing that runs is `thunk`: a parser-built lambda over the hidden names,
// `fu() { " d3c"(" d3a0", " d3a1") }`, compiled by the ordinary lambda path.
// one mechanism for both spellings: the only difference is that the scope's
// normal exits kill a deferr slot instead of running it, the unwind scan
// runs both. and covers errdefer.
static ast_defer_statement* parse_defer_statement(parser* p_parser, bool p_is_err) {
  allocators* alloc = p_parser->alloc;
  token defer_tok = p_parser->previous; // 'defer' / 'deferr' already in previous
  ast_specs s = {alloc};

  ast_expression* expression = NULL;
  if (p_parser->current.type == TOKEN_SEMICOLON || p_parser->current.type == TOKEN_EOF) {
    error_at(p_parser,
             p_parser->current,
             ok_create_string_view(
                 "'defer' schedules a call, like: defer close(file).", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL;
  }
  advance(p_parser); // first expression token into previous
  expression = parse_expression(p_parser, PREC_NONE);
  if (expression == NULL) {
    return NULL;
  }
  if (expression->node.node_type != AST_CALL_EXPRESSION) {
    error_at(p_parser,
             defer_tok,
             ok_create_string_view(
                 "'defer' schedules a call, like: defer close(file).", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_expression;
  }
  ast_call_expression* call = (ast_call_expression*)expression;
  const uint32_t argc = call->arguments.count;

  // the hidden names, one owned NUL-separated block the thunk's identifier
  // tokens view into (the defer node outlives compilation, so the views stay
  // valid as long as the AST does. the compiler's scope maps copy their keys).
  const uint32_t serial = p_parser->defer_serial++;
  uint32_t total = 0;
  char piece[48];
  int n = snprintf(piece, sizeof(piece), " d%uc", serial);
  total += (uint32_t)n + 1;
  for (uint32_t i = 0; i < argc; ++i) {
    n = snprintf(piece, sizeof(piece), " d%ua%u", serial, i);
    total += (uint32_t)n + 1;
  }
  n = snprintf(piece, sizeof(piece), " d%us", serial);
  total += (uint32_t)n + 1;
  char* names = (char*)alloc->allocate(alloc, total);
  if (names == NULL) {
    error_at(
        p_parser,
        defer_tok,
        ok_create_string_view("failed to allocate memory for defer names.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    goto fail_expression;
  }

  // the thunk: identifiers over the block, one call, one expression statement,
  // one zero-arity function. built bottom-up. on failure everything not yet
  // handed to a parent is released here, everything that has been is the
  // parent's (single-owner discipline).
  ast_expressions_list thunk_args;
  ast_expressions_list_init(&thunk_args, alloc);
  ok_string_view slot_name = ok_create_string_view(NULL, 0, false);
  ast_identifier_expression* callee_ident = NULL;
  char* cursor = names;
  {
    n = snprintf(cursor, (size_t)(total - (uint32_t)(cursor - names)), " d%uc", serial);
    if (n < 0) {
      goto fail_thunk_oom;
    }
    callee_ident = (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
    if (callee_ident == NULL) {
      goto fail_thunk_oom;
    }
    ast_identifier_expression_init(callee_ident, hidden_name_token(defer_tok, cursor, (uint32_t)n), &s);
    cursor += n + 1;
    for (uint32_t i = 0; i < argc; ++i) {
      n = snprintf(cursor, (size_t)(total - (uint32_t)(cursor - names)), " d%ua%u", serial, i);
      if (n < 0 || ast_expressions_list_append(&thunk_args, NULL) == false) {
        goto fail_thunk_oom;
      }
      ast_identifier_expression* arg_ident =
          (ast_identifier_expression*)alloc->allocate(alloc, sizeof(ast_identifier_expression));
      if (arg_ident == NULL) {
        thunk_args.count--; // the NULL placeholder never owned anything
        goto fail_thunk_oom;
      }
      thunk_args.data[thunk_args.count - 1] = (ast_expression*)arg_ident;
      ast_identifier_expression_init(arg_ident, hidden_name_token(defer_tok, cursor, (uint32_t)n), &s);
      cursor += n + 1;
    }
    // the slot's own name: written last, never referenced by the thunk.
    // the enclosing scope declares it (the compiler reads the view off the node).
    n = snprintf(cursor, (size_t)(total - (uint32_t)(cursor - names)), " d%us", serial);
    slot_name = ok_create_string_view(cursor, (uint32_t)n, false);
  }
  {
    ast_call_expression* thunk_call = (ast_call_expression*)alloc->allocate(alloc, sizeof(ast_call_expression));
    if (thunk_call == NULL) {
      goto fail_thunk_oom;
    }
    ast_call_expression_init(thunk_call, defer_tok, (ast_expression*)callee_ident, thunk_args, &s);
    thunk_args.count = 0; // ownership moved into the call node
    callee_ident = NULL;
    ast_expression_statement* thunk_body =
        (ast_expression_statement*)alloc->allocate(alloc, sizeof(ast_expression_statement));
    if (thunk_body == NULL) {
      ast_dispatch_deinit((ast_node*)thunk_call);
      alloc->release(alloc, thunk_call);
      goto fail_thunk_list;
    }
    ast_expression_statement_init(thunk_body, defer_tok, (ast_expression*)thunk_call, &s);
    ast_bindings_list thunk_params;
    ast_bindings_list_init(&thunk_params, alloc);
    ast_function_expression* thunk = (ast_function_expression*)alloc->allocate(alloc, sizeof(ast_function_expression));
    if (thunk == NULL) {
      ast_expression_statement_deinit(thunk_body);
      alloc->release(alloc, thunk_body);
      goto fail_thunk_list;
    }
    ast_function_expression_init(thunk, defer_tok, thunk_params, (ast_statement*)thunk_body, &s);

    if (p_parser->current.type != TOKEN_SEMICOLON) {
      error_at(p_parser,
               p_parser->current,
               ok_create_string_view("expected ';' after defer statement.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_function_expression_deinit(thunk);
      alloc->release(alloc, thunk);
      goto fail_thunk_list;
    }
    advance(p_parser); // ';'

    ast_defer_statement* defer = (ast_defer_statement*)alloc->allocate(alloc, sizeof(ast_defer_statement));
    if (defer == NULL) {
      error_at(
          p_parser,
          defer_tok,
          ok_create_string_view("failed to allocate memory for defer node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_function_expression_deinit(thunk);
      alloc->release(alloc, thunk);
      goto fail_thunk_list;
    }
    ast_defer_statement_init(defer, defer_tok, call, thunk, names, slot_name, p_is_err, &s);
    return defer;
  }

fail_thunk_oom:
  error_at(p_parser,
           defer_tok,
           ok_create_string_view("failed to allocate memory for defer thunk.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
fail_thunk_list:
  if (callee_ident != NULL) {
    ast_identifier_expression_deinit(callee_ident);
    alloc->release(alloc, callee_ident);
  }
  for (uint32_t i = 0; i < thunk_args.count; ++i) {
    if (thunk_args.data[i] != NULL) {
      ast_dispatch_deinit((ast_node*)thunk_args.data[i]);
      alloc->release(alloc, thunk_args.data[i]);
    }
  }
  ast_expressions_list_deinit(&thunk_args, alloc);
  alloc->release(alloc, names);
fail_expression:
  ast_dispatch_deinit((ast_node*)expression);
  alloc->release(alloc, expression);
  return NULL;
}

ast_bindings_list parse_bindings_list(parser* p_parser,
                                      ast_binding_modifiers_t p_allowed_binds,
                                      token_type p_delim,
                                      token_type p_end,
                                      bool p_allow_varargs) {
  allocators* alloc = p_parser->alloc;
  ast_bindings_list list;
  ast_bindings_list_init(&list, alloc);
  bool seen_default = false; // defaults must be trailing: there are no keyword arguments
  bool seen_varargs = false; // at most one, and it must be the last
  while (p_parser->current.type != p_end && p_parser->current.type != TOKEN_EOF) {
    ast_binding_modifiers_t binds = parse_binding_modifiers(p_parser, p_allowed_binds);
    token tok = p_parser->previous;
    if (binds == BINDING_ERROR) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view("failed to parse binding modifiers.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    if (p_parser->current.type != TOKEN_IDENTIFIER) {
      error_at(
          p_parser,
          p_parser->previous,
          ok_create_string_view("expected an identifier as parameter name.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    advance(p_parser);
    // the name is parsed with the assign infix disabled (PREC_ASSIGNMENT blocks
    // TOKEN_ASSIGN's rule): a bare identifier is the only valid binding lvalue here.
    // parsing at PREC_NONE used to swallow `b = null` into an assign node that the
    // compiler later cast to an identifier. silent garbage, was never covered by a test.
    ast_expression* ident = parse_expression(p_parser, PREC_ASSIGNMENT);
    if (ident == NULL) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view("failed to parse parameter.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_bind_list;
    }
    if (ident->node.node_type != AST_IDENTIFIER_EXPRESSION) {
      error_at(
          p_parser,
          p_parser->previous,
          ok_create_string_view("expected an identifier as parameter name.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)ident);
      alloc->release(alloc, ident);
      goto free_bind_list;
    }
    // `rest...`: the varargs marker, parsed after the name so the
    // identifier's parse anchor is untouched. only function parameters allow
    // it (p_allow_varargs). anywhere else it is a syntax error.
    bool is_varargs = false;
    if (p_parser->current.type == TOKEN_ELLIPSIS) {
      if (!p_allow_varargs) {
        error_at(
            p_parser,
            p_parser->current,
            ok_create_string_view("'...' is not allowed in this binding list.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto free_bind_list;
      }
      advance(p_parser); // consume '...'
      is_varargs = true;
      if (seen_varargs) {
        error_at(
            p_parser,
            p_parser->previous,
            ok_create_string_view("only one varargs parameter is allowed.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto free_bind_list;
      }
      seen_varargs = true;
    }
    ast_expression* default_value = NULL;
    if (p_parser->current.type == TOKEN_ASSIGN) {
      advance(p_parser); // consume '='
      advance(p_parser); // first default token into previous (parse_expression starts there)
      default_value = parse_expression(p_parser, PREC_NONE);
      if (default_value == NULL) {
        error_at(p_parser,
                 p_parser->previous,
                 ok_create_string_view("failed to parse parameter default.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        ast_dispatch_deinit((ast_node*)ident);
        alloc->release(alloc, ident);
        goto free_bind_list;
      }
    }
    // the defaults-trailing rule has one exempt: the varargs parameter. it is
    // last by construction and never requires an argument, so it may follow
    // defaulted params (`fu f(a, b = 2, c...)`).
    if (default_value == NULL && seen_default && !is_varargs) {
      error_at_noted(
          p_parser,
          p_parser->previous,
          ok_create_string_view("required parameter after a defaulted one.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
          ok_create_string_view(
              "defaults must be trailing. there are no keyword arguments.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)ident);
      alloc->release(alloc, ident);
      goto free_bind_list;
    }
    if (default_value != NULL) {
      seen_default = true;
    }
    if (is_varargs && default_value != NULL) {
      error_at_noted(
          p_parser,
          p_parser->previous,
          ok_create_string_view("a varargs parameter cannot have a default.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
          ok_create_string_view("it always defaults to an empty array. every argument past the fixed ones is its.",
                                OK_STRING_VIEW_CALCULATE_LENGTH,
                                true));
      ast_dispatch_deinit((ast_node*)ident);
      alloc->release(alloc, ident);
      goto free_bind_list;
    }
    if (is_varargs && p_parser->current.type != p_end) {
      error_at(
          p_parser,
          p_parser->current,
          ok_create_string_view("a varargs parameter must be the last one.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      ast_dispatch_deinit((ast_node*)ident);
      alloc->release(alloc, ident);
      goto free_bind_list;
    }
    ast_binding* binding = (ast_binding*)alloc->allocate(alloc, sizeof(ast_binding));
    if (binding == NULL) {
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view(
                   "failed to allocate memory for parameter binding node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_ident;
    }
    ast_specs s = {alloc};
    ast_binding_init(binding, ident, binds, tok, &s);
    binding->default_value = default_value;
    binding->is_varargs = is_varargs;
    if (!ast_bindings_list_append(&list, binding)) {
      goto free_bind_list;
    }
    if (p_parser->current.type == p_delim) {
      advance(p_parser);
    } else if (p_parser->current.type != p_end) {
      goto expect_end;
    }
    continue;
  free_ident:
    if (default_value != NULL) {
      ast_dispatch_deinit((ast_node*)default_value);
      alloc->release(alloc, default_value);
    }
    ast_dispatch_deinit((ast_node*)ident);
    alloc->release(alloc, ident);
    goto free_bind_list;
  }
  if (p_parser->current.type != p_end) {
  expect_end: { // label followed by decl is c23 ext lol.
    string message = asprint(alloc, "expected '%s'.", token_type_to_string(p_end));
    error_at(p_parser, p_parser->current, ok_create_string_view_from_string(message));
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
      error_at(p_parser,
               p_parser->previous,
               ok_create_string_view(
                   "failed to parse expression in expressions list.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      goto free_expr_list;
    }
    // `args...`: the spread marker wraps the element. parsed in any
    // expressions list, rejected at compile anywhere but a call's last
    // argument. the parser stays permissive, and the errors stay precise.
    if (p_parser->current.type == TOKEN_ELLIPSIS) {
      advance(p_parser); // consume '...'
      ast_spread_expression* spread = (ast_spread_expression*)alloc->allocate(alloc, sizeof(ast_spread_expression));
      if (spread == NULL) {
        ast_dispatch_deinit((ast_node*)expr);
        alloc->release(alloc, expr);
        error_at(
            p_parser,
            p_parser->previous,
            ok_create_string_view("failed to allocate memory for spread node.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        goto free_expr_list;
      }
      ast_specs s = {alloc};
      ast_spread_expression_init(spread, p_parser->previous, expr, &s);
      expr = (ast_expression*)spread;
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
    error_at(p_parser, p_parser->current, ok_create_string_view_from_string(message));
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
