#include "ast.h"
#include "utils.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AST_LIST_X(node_type)                                                                                          \
  X(AST_NODE, ast_node)                                                                                                \
  X(AST_EXPRESSION, ast_expression)                                                                                    \
  X(AST_STATEMENT, ast_statement)                                                                                      \
  X(AST_DECLARATION, ast_declaration)                                                                                  \
  X(AST_ROOT, ast_root)                                                                                                \
  /*X(AST_BINDING, ast_binding)*/                                                                                      \
  X(AST_IDENTIFIER_EXPRESSION, ast_identifier_expression)                                                              \
  X(AST_NUMBER_EXPRESSION, ast_number_expression)                                                                      \
  X(AST_STRING_EXPRESSION, ast_string_expression)                                                                      \
  X(AST_PREFIX_UNARY_EXPRESSION, ast_prefix_unary_expression)                                                          \
  X(AST_INFIX_BINARY_EXPRESSION, ast_infix_binary_expression)                                                          \
  X(AST_POSTFIX_UNARY_EXPRESSION, ast_postfix_unary_expression)                                                        \
  /*X(AST_CALL_EXPRESSION, ast_call_expression)*/                                                                      \
  /*X(AST_ASSIGN_EXPRESSION, ast_assign_expression)*/                                                                  \
  /*X(AST_COMPOUND_ASSIGN_EXPRESSION, ast_compound_assign_expression)*/                                                \
  /*X(AST_OPERATOR_EXPRESSION, ast_operator_expression)*/                                                              \
  /*X(AST_CONDITIONAL_EXPRESSION, ast_conditional_expression)*/                                                        \
  X(AST_BOOLEAN_EXPRESSION, ast_boolean_expression)                                                                    \
  X(AST_NULL_EXPRESSION, ast_null_expression)                                                                          \
  /*X(AST_ACCESS_EXPRESSION, ast_access_expression)*/                                                                  \
  /*X(AST_THIS_EXPRESSION, ast_this_expression)*/                                                                      \
  /*X(AST_SUPER_EXPRESSION, ast_super_expression)*/                                                                    \
  /*X(AST_NODE, ast_array_expression)*/                                                                                \
  /*X(AST_MAP_EXPRESSION, ast_map_expression)*/                                                                        \
  /*X(AST_SUBSCRIPT_EXPRESSION, ast_subscript_expression)*/                                                            \
  X(AST_EMPTY_STATEMENT, ast_empty_statement)                                                                          \
  X(AST_EXPRESSION_STATEMENT, ast_expression_statement)                                                                \
  /*X(AST_PRINT_STATEMENT, ast_print_statement)*/                                                                      \
  /*X(AST_BLOCK_STATEMENT, ast_block_statement)*/                                                                      \
  /*X(AST_IF_STATEMENT, ast_if_statement)*/                                                                            \
  /*X(AST_FOR_STATEMENT, ast_for_statement)*/                                                                          \
  /*X(AST_WHILE_STATEMENT, ast_while_statement)*/                                                                      \
  /*X(AST_CONTROL_FLOW_STATEMENT, ast_control_flow_statement)*/                                                        \
  /*X(AST_RETURN_STATEMENT, ast_return_statement)*/                                                                    \
  /*X(AST_THROW_STATEMENT, ast_throw_statement)*/                                                                      \
  /*X(AST_TRY_STATEMENT, ast_try_statement)*/                                                                          \
  /*X(AST_CATCH_STATEMENT, ast_catch_statement)*/                                                                      \
  /*X(AST_FINALIZE_STATEMENT, ast_finalize_statement)*/                                                                \
  /*X(AST_TRY_CATCH_STATEMENT, ast_try_catch_statement)*/                                                              \
  /*X(AST_EOF_STATEMENT, ast_eof_statement)*/                                                                          \
  /*X(AST_LET_DECLARATION, ast_let_declaration)*/                                                                      \
  /*X(AST_FUNCTION_DECLARATION, ast_function_declaration)*/                                                            \
  /*X(AST_CLASS_DECLARATION, ast_class_declaration)*/

void ast_node_init(ast_node* p_node, ast_node_type p_type, token p_token) {
  p_node->node_type = p_type;
  p_node->token = p_token;
}

void ast_node_deinit(ast_node* p_node) {
  switch (p_node->node_type) {
  case AST_NODE:
    break;
  case AST_EXPRESSION:
    ast_expression_deinit((ast_expression*)p_node);
    break;
  case AST_STATEMENT:
    ast_statement_deinit((ast_statement*)p_node);
    break;
  case AST_DECLARATION:
    ast_declaration_deinit((ast_declaration*)p_node);
    break;
  case AST_ROOT:
    ast_root_deinit((ast_root*)p_node);
    break;
  case AST_BINDING:
    break;
  case AST_IDENTIFIER_EXPRESSION:
    ast_identifier_expression_deinit((ast_identifier_expression*)p_node);
    break;
  case AST_NUMBER_EXPRESSION:
    ast_number_expression_deinit((ast_number_expression*)p_node);
    break;
  case AST_STRING_EXPRESSION:
    ast_string_expression_deinit((ast_string_expression*)p_node);
    break;
  case AST_PREFIX_UNARY_EXPRESSION:
    ast_prefix_unary_expression_deinit((ast_prefix_unary_expression*)p_node);
    break;
  case AST_INFIX_BINARY_EXPRESSION:
    ast_infix_binary_expression_deinit((ast_infix_binary_expression*)p_node);
    break;
  case AST_POSTFIX_UNARY_EXPRESSION:
    ast_postfix_unary_expression_deinit((ast_postfix_unary_expression*)p_node);
    break;
  case AST_CALL_EXPRESSION:
  case AST_ASSIGN_EXPRESSION:
  case AST_COMPOUND_ASSIGN_EXPRESSION:
  case AST_OPERATOR_EXPRESSION:
  case AST_CONDITIONAL_EXPRESSION:
    assert(0);
  case AST_BOOLEAN_EXPRESSION:
    ast_boolean_expression_deinit((ast_boolean_expression*)p_node);
    break;
  case AST_NULL_EXPRESSION:
    ast_null_expression_deinit((ast_null_expression*)p_node);
    break;
  case AST_ACCESS_EXPRESSION:
  case AST_THIS_EXPRESSION:
  case AST_SUPER_EXPRESSION:
  case AST_ARRAY_EXPRESSION:
  case AST_MAP_EXPRESSION:
  case AST_SUBSCRIPT_EXPRESSION:
    assert(0);

  case AST_EMPTY_STATEMENT:
    ast_empty_statement_deinit((ast_empty_statement*)p_node);
    break;
  case AST_EXPRESSION_STATEMENT:
    ast_expression_statement_deinit((ast_expression_statement*)p_node);
    break;
  case AST_PRINT_STATEMENT:
  case AST_BLOCK_STATEMENT:
  case AST_IF_STATEMENT:
  case AST_FOR_STATEMENT:
  case AST_WHILE_STATEMENT:
  case AST_CONTROL_FLOW_STATEMENT:
  case AST_RETURN_STATEMENT:
  case AST_THROW_STATEMENT:
  case AST_TRY_STATEMENT:
  case AST_CATCH_STATEMENT:
  case AST_FINALIZE_STATEMENT:
  case AST_TRY_CATCH_STATEMENT:

  case AST_LET_DECLARATION:
  case AST_FUNCTION_DECLARATION:
  case AST_CLASS_DECLARATION:
    assert(0);
  case AST_EOF_STATEMENT:
    ast_eof_statement_deinit((ast_eof_statement*)p_node);
    break;
  }
}

#define PRINT_FMT "%.*s", p_node->token.length, p_node->token.start

bool ast_node_print(const ast_node* p_node) {
  return printf(PRINT_FMT) > 0;
}

string ast_node_asprint(const ast_node* p_node) {
  return asprint(PRINT_FMT);
}

#undef PRINT_FMT

void ast_expression_init(ast_expression* p_expression, const ast_node_type p_type, const token p_token) {
  ast_node_init(&p_expression->node, p_type, p_token);
}

void ast_expression_deinit(ast_expression* p_expression) {
}

bool ast_expression_print(const ast_expression* p_expression) {
  return ast_node_print(&p_expression->node);
}

string ast_expression_asprint(const ast_expression* p_expression) {
  return ast_node_asprint(&p_expression->node);
}

void ast_statement_init(ast_statement* p_statement, const ast_node_type p_type, const token p_token) {
  ast_node_init(&p_statement->node, p_type, p_token);
}

void ast_statement_deinit(ast_statement* p_statement) {
}

bool ast_statement_print(const ast_statement* p_statement) {
  return ast_node_print(&p_statement->node);
}

string ast_statement_asprint(const ast_statement* p_statement) {
  return ast_node_asprint(&p_statement->node);
}

void ast_declaration_init(ast_declaration* p_declaration, const ast_node_type p_type, const token p_token) {
  ast_statement_init(&p_declaration->statement, p_type, p_token);
}

void ast_declaration_deinit(ast_declaration* p_declaration) {
  ast_statement_deinit(&p_declaration->statement);
}

bool ast_declaration_print(const ast_declaration* p_declaration) {
  return ast_node_print(&p_declaration->statement.node);
}

string ast_declaration_asprint(const ast_declaration* p_declaration) {
  return ast_node_asprint(&p_declaration->statement.node);
}

void ast_statements_list_init(ast_statements_list* p_list) {
  p_list->count = 0;
  p_list->head = NULL;
  p_list->tail = NULL;
}

bool ast_statements_list_append(ast_statements_list* p_list, ast_statement* p_statement) {
  ast_statements_list_node* node = (ast_statements_list_node*)malloc(sizeof(ast_statements_list_node));
  if (node == NULL) {
    return false;
  }
  node->next = NULL;
  node->statement = p_statement;

  if (p_list->head == NULL) {
    p_list->head = node;
    p_list->tail = node;
    return true;
  }

  ast_statements_list_node* tmp = p_list->tail;
  p_list->tail = node;
  tmp->next = node;
  p_list->count++;
  return true;
}

void ast_statement_list_deinit(ast_statements_list* p_list) {
  ast_statements_list_node* node = p_list->head;
  while (node != NULL) {
    ast_node_deinit((ast_node*)node->statement);
    free(node->statement);
    node->statement = NULL;
    ast_statements_list_node* temp = node->next;
    free(node);
    node = temp;
    p_list->count--;
  }
  p_list->head = NULL;
  p_list->tail = NULL;
  p_list->count = 0;
}

void ast_root_init(ast_root* p_root, token p_token) {
  ast_statement_init(&p_root->statement, AST_ROOT, p_token);
  ast_statements_list_init(&p_root->statements);
}

void ast_root_deinit(ast_root* p_root) {
  ast_statement_list_deinit(&p_root->statements);
  ast_statement_deinit(&p_root->statement);
}

bool ast_root_print(const ast_root* p_root) {
  bool status = true;
  for (ast_statements_list_node* node = p_root->statements.head; node != NULL; node = node->next) {
    status &= ast_print((ast_node*)node->statement);
  }
  return status;
}

string ast_root_asprint(const ast_root* p_root) {
  char* chars = NULL;
  size_t len = 0;
  for (ast_statements_list_node* node = p_root->statements.head; node != NULL; node = node->next) {
    string res = ast_asprint((ast_node*)node->statement);
    if (res.chars != NULL) {
      len += string_get_length(&res);
      char* temp = malloc(len);
      free(chars);
      if (temp == NULL) {
        string_deinit(&res);
        return string_create(NULL, 0, false);
      }
      chars = temp;
    }
  }
  return string_create(chars, len, true);
}

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, token p_token) {
  ast_expression_init(&p_identifier->expression, AST_IDENTIFIER_EXPRESSION, p_token);
}

void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier) {
  ast_expression_deinit(&p_identifier->expression);
}

string ast_identifier_expression_get_value(const ast_identifier_expression* p_identifier) {
  const ast_node* node = &p_identifier->expression.node;
  size_t len = node->token.length;
  char* chars = (char*)malloc(len + 1);
  if (chars == NULL) {
    return string_create(NULL, 0, false);
  }
  memcpy(chars, node->token.start, node->token.length);
  chars[node->token.length] = '\0';
  return string_create(chars, len, true);
}

bool ast_identifier_expression_print(const ast_identifier_expression* p_identifier) {
  string ident = ast_identifier_expression_get_value(p_identifier);
  if (ident.chars != NULL) {
    bool printf_status = printf("%s", ident.chars) > 0;
    string_deinit(&ident);
    return printf_status;
  }
  return false;
}

string ast_identifier_expression_asprint(const ast_identifier_expression* p_identifier) {
  string ident = ast_identifier_expression_get_value(p_identifier);
  if (ident.chars != NULL) {
    string str = asprint("%s", ident.chars);
    string_deinit(&ident);
    return str;
  }
  return string_create(NULL, 0, false);
}

void ast_number_expression_init(ast_number_expression* p_number, token p_token) {
  ast_expression_init(&p_number->expression, AST_NUMBER_EXPRESSION, p_token);
}

void ast_number_expression_deinit(ast_number_expression* p_number) {
  ast_expression_deinit(&p_number->expression);
}

double ast_number_expression_get_value(const ast_number_expression* p_number) {
  const ast_node* node = &p_number->expression.node;
  char* endptr = (char*)node->token.start + node->token.length; // strtod won't touch the string
  const double value = strtod(node->token.start, &endptr);
  if (endptr == node->token.start) {
    return AST_NUMBER_EXPRESSION_PARSE_ERROR;
  }
  return value;
}

bool ast_number_expression_print(const ast_number_expression* p_number) {
  const double value = ast_number_expression_get_value(p_number);
  if (value == AST_NUMBER_EXPRESSION_PARSE_ERROR) {
    return false;
  }
  return printf("%f", value) > 0;
}

string ast_number_expression_asprint(const ast_number_expression* p_number) {
  const double value = ast_number_expression_get_value(p_number);
  if (value == AST_NUMBER_EXPRESSION_PARSE_ERROR) {
    return string_create(NULL, 0, false);
  }
  return asprint("%f", value);
}

void ast_string_expression_init(ast_string_expression* p_string, token p_token) {
  ast_expression_init(&p_string->expression, AST_STRING_EXPRESSION, p_token);
}

void ast_string_expression_deinit(ast_string_expression* p_string) {
  ast_expression_deinit(&p_string->expression);
}

string ast_string_expression_get_value(const ast_string_expression* p_string) {
  const ast_node* node = &p_string->expression.node;
  const size_t len = node->token.length;
  char* str = (char*)malloc(len + 1);
  if (str == NULL) {
    return string_create(NULL, 0, false);
  }
  memcpy(str, node->token.start, node->token.length);
  str[node->token.length] = '\0';
  return string_create(str, len, true);
}

bool ast_string_expression_print(const ast_string_expression* p_string) {
  const string value = ast_string_expression_get_value(p_string);
  if (value.chars == NULL) {
    return false;
  }
  return printf("%s", value.chars) > 0;
}

string ast_string_expression_asprint(const ast_string_expression* p_string) {
  return ast_string_expression_get_value(p_string);
}

void ast_boolean_expression_init(ast_boolean_expression* p_boolean, token p_token) {
  ast_expression_init(&p_boolean->expression, AST_BOOLEAN_EXPRESSION, p_token);
}

void ast_boolean_expression_deinit(ast_boolean_expression* p_boolean) {
  ast_expression_deinit(&p_boolean->expression);
}

bool ast_boolean_expression_get_value(const ast_boolean_expression* p_boolean) {
  const ast_node* node = &p_boolean->expression.node;
  return strncmp("true", node->token.start, node->token.length) == 0 ? true : false;
}

bool ast_boolean_expression_print(const ast_boolean_expression* p_boolean) {
  bool value = ast_boolean_expression_get_value(p_boolean);
  return printf("%s", value ? "true" : "false") > 0;
}

string ast_boolean_expression_asprint(const ast_boolean_expression* p_boolean) {
  bool value = ast_boolean_expression_get_value(p_boolean);
  return asprint("%s", value ? "true" : "false");
}

void ast_null_expression_init(ast_null_expression* p_null, token p_token) {
  ast_expression_init(&p_null->expression, AST_NULL_EXPRESSION, p_token);
}

void ast_null_expression_deinit(ast_null_expression* p_null) {
  ast_expression_deinit(&p_null->expression);
}

bool ast_null_expression_print(const ast_null_expression* p_null) {
  return printf("null") > 0;
}

string ast_null_expression_asprint(const ast_null_expression* p_null) {
  return asprint("null");
}

void ast_prefix_unary_expression_init(ast_prefix_unary_expression* p_prefix,
                                      token p_token,
                                      token_type p_operator,
                                      ast_expression* p_right) {
  ast_expression_init(&p_prefix->expression, AST_PREFIX_UNARY_EXPRESSION, p_token);
  p_prefix->_operator = p_operator;
  p_prefix->right = p_right;
}

void ast_prefix_unary_expression_deinit(ast_prefix_unary_expression* p_prefix) {
  ast_node_deinit((ast_node*)p_prefix->right);
  free(p_prefix->right);
  p_prefix->right = NULL;
  ast_expression_deinit(&p_prefix->expression);
}

#define OPERATOR_STRING                                                                                                \
  switch (p_prefix->_operator) {                                                                                       \
  case TOKEN_PLUS:                                                                                                     \
    op = "+";                                                                                                          \
    break;                                                                                                             \
  case TOKEN_MINUS:                                                                                                    \
    op = "-";                                                                                                          \
    break;                                                                                                             \
  case TOKEN_PLUS_PLUS:                                                                                                \
    op = "++";                                                                                                         \
    break;                                                                                                             \
  case TOKEN_MINUS_MINUS:                                                                                              \
    op = "--";                                                                                                         \
    break;                                                                                                             \
  default:; /* i trust the parser. do you?*/                                                                           \
  }

bool ast_prefix_unary_expression_print(const ast_prefix_unary_expression* p_prefix) {
  // TODO proper operator enum
  const char* op;
  OPERATOR_STRING;
  return printf("%s", op) > 0 && ast_print((ast_node*)p_prefix->right);
}

string ast_prefix_unary_expression_asprint(const ast_prefix_unary_expression* p_prefix) {
  const char* op;
  OPERATOR_STRING;
  string right = ast_asprint((ast_node*)p_prefix->right);
  if (right.chars == NULL) {
    string_deinit(&right);
    return string_create(NULL, 0, false);
  }
  return asprint("%s%s", op, right);
}

#undef OPERATOR_STRING

void ast_postfix_unary_expression_init(ast_postfix_unary_expression* p_postfix,
                                       token p_token,
                                       token_type p_operator,
                                       ast_expression* p_left) {
  ast_expression_init(&p_postfix->expression, AST_POSTFIX_UNARY_EXPRESSION, p_token);
  p_postfix->_operator = p_operator;
  p_postfix->left = p_left;
}

void ast_postfix_unary_expression_deinit(ast_postfix_unary_expression* p_postfix) {
  ast_node_deinit((ast_node*)p_postfix->left);
  free(p_postfix->left);
  p_postfix->left = NULL;
  ast_expression_deinit(&p_postfix->expression);
}

#define OPERATOR_STRING                                                                                                \
  switch (p_postfix->_operator) {                                                                                      \
  case TOKEN_PLUS_PLUS:                                                                                                \
    op = "++";                                                                                                         \
    break;                                                                                                             \
  case TOKEN_MINUS_MINUS:                                                                                              \
    op = "--";                                                                                                         \
    break;                                                                                                             \
  default:; /* i trust the parser. do you?*/                                                                           \
  }

bool ast_postfix_unary_expression_print(const ast_postfix_unary_expression* p_postfix) {
  // TODO proper operator enum
  const char* op;
  OPERATOR_STRING;
  return ast_print((ast_node*)p_postfix->left) && printf("%s", op);
}

string ast_postfix_unary_expression_asprint(const ast_postfix_unary_expression* p_postfix) {
  const char* op;
  OPERATOR_STRING;
  string left = ast_asprint((ast_node*)p_postfix->left);
  if (left.chars == NULL) {
    string_deinit(&left);
    return string_create(NULL, 0, false);
  }
  return asprint("%s%s", left, op);
}

#undef OPERATOR_STRING

void ast_infix_binary_expression_init(ast_infix_binary_expression* p_infix,
                                      token p_token,
                                      token_type p_operator,
                                      ast_expression* p_left,
                                      ast_expression* p_right) {
  ast_expression_init(&p_infix->expression, AST_INFIX_BINARY_EXPRESSION, p_token);
  p_infix->_operator = p_operator;
  p_infix->left = p_left;
  p_infix->right = p_right;
}

void ast_infix_binary_expression_deinit(ast_infix_binary_expression* p_infix) {
  ast_node_deinit((ast_node*)p_infix->left);
  ast_node_deinit((ast_node*)p_infix->right);
  free(p_infix->left);
  free(p_infix->right);
  p_infix->left = NULL;
  p_infix->right = NULL;
  ast_expression_deinit(&p_infix->expression);
}

#define OPERATOR_STRING                                                                                                \
  switch (p_infix->_operator) {                                                                                        \
  case TOKEN_PLUS:                                                                                                     \
    op = "+";                                                                                                          \
    break;                                                                                                             \
  case TOKEN_MINUS:                                                                                                    \
    op = "-";                                                                                                          \
    break;                                                                                                             \
  case TOKEN_PLUS_PLUS:                                                                                                \
    op = "*";                                                                                                          \
    break;                                                                                                             \
  case TOKEN_MINUS_MINUS:                                                                                              \
    op = "/";                                                                                                          \
    break;                                                                                                             \
  default:; /* i trust the parser. do you?*/                                                                           \
  }

bool ast_infix_binary_expression_print(const ast_infix_binary_expression* p_infix) {
  // TODO proper operator enum
  const char* op;
  OPERATOR_STRING;
  return ast_print((ast_node*)p_infix->left) && printf("%s", op) && ast_print((ast_node*)p_infix->right);
}

string ast_infix_binary_expression_asprint(const ast_infix_binary_expression* p_infix) {
  // i think this is ugly, but what do i know
  const char* op;
  OPERATOR_STRING;
  string left = ast_asprint((ast_node*)p_infix->left);
  if (left.chars == NULL) {
    string_deinit(&left);
    return string_create(NULL, 0, false);
  }
  string right = ast_asprint((ast_node*)p_infix->right);
  if (right.chars == NULL) {
    string_deinit(&left);
    string_deinit(&right);
    return string_create(NULL, 0, false);
  }
  return asprint("%s%s%s", left, op, right);
}

#undef OPERATOR_STRING

void ast_eof_statement_init(ast_eof_statement* p_eof, token p_token) {
  ast_statement_init(&p_eof->statement, AST_EOF_STATEMENT, p_token);
}

void ast_eof_statement_deinit(ast_eof_statement* p_eof) {
  ast_statement_deinit(&p_eof->statement);
}

bool ast_eof_statement_print(const ast_eof_statement* p_eof) {
  return true;
}

string ast_eof_statement_asprint(const ast_eof_statement* p_eof) {
  return string_create(NULL, 0, false);
}

void ast_empty_statement_init(ast_empty_statement* p_empty, token p_token) {
  ast_statement_init(&p_empty->statement, AST_EMPTY_STATEMENT, p_token);
}

void ast_empty_statement_deinit(ast_empty_statement* p_empty) {
  ast_statement_deinit(&p_empty->statement);
}

bool ast_empty_statement_print(const ast_empty_statement* p_empty) {
  return true;
}

string ast_empty_statement_asprint(const ast_empty_statement* p_empty) {
  return string_create(NULL, 0, false);
}

void ast_expression_statement_init(ast_expression_statement* p_expression_statement,
                                   token p_token,
                                   ast_expression* p_expression) {
  ast_statement_init(&p_expression_statement->statement, AST_EXPRESSION_STATEMENT, p_token);
  p_expression_statement->expression = p_expression;
}

void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement) {
  ast_expression_deinit(p_expression_statement->expression);
  free(p_expression_statement->expression);
  p_expression_statement->expression = NULL;
  ast_statement_deinit(&p_expression_statement->statement);
}

bool ast_expression_statement_print(const ast_expression_statement* p_expression_statement) {
  return ast_print((ast_node*)p_expression_statement->expression);
}

string ast_expression_statement_asprint(const ast_expression_statement* p_expression_statement) {
  return ast_asprint((ast_node*)p_expression_statement->expression);
}

void ast_deinit(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    klass##_deinit((klass*)p_node);
  switch (p_node->node_type) {
    AST_LIST_X(p_node->node_type);
  default:; // for now
  }
#undef X
}

bool ast_print(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    return klass##_print((klass*)p_node);
  switch (p_node->node_type) {
    AST_LIST_X(p_node->node_type);
  default:;
  }
#undef X
  return false;
}

string ast_asprint(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    return klass##_asprint((klass*)p_node);
  switch (p_node->node_type) {
    AST_LIST_X(p_node->node_type);
  default:;
  }
  return string_create(NULL, 0, false);
#undef X
}
