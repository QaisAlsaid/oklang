#include "ast.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

void ast_expression_init(ast_expression* p_expression, ast_node_type p_type, token p_token) {
  ast_node_init(&p_expression->node, p_type, p_token);
}

void ast_expression_deinit(ast_expression* p_expression) {
}

void ast_statement_init(ast_statement* p_statement, ast_node_type p_type, token p_token) {
  ast_node_init(&p_statement->node, p_type, p_token);
}

void ast_statement_deinit(ast_statement* p_statement) {
}

void ast_declaration_init(ast_declaration* p_declaration, ast_node_type p_type, token p_token) {
  ast_statement_init(&p_declaration->statement, p_type, p_token);
}

void ast_declaration_deinit(ast_declaration* p_declaration) {
  ast_statement_deinit(&p_declaration->statement);
}

void ast_root_statements_list_init(ast_root_statements_list* p_list) {
  p_list->count = 0;
  p_list->head = NULL;
  p_list->tail = NULL;
}

void ast_root_statements_list_append(ast_root_statements_list* p_list, ast_statement* p_statement) {
  ast_root_statements_list_node* node = (ast_root_statements_list_node*)malloc(sizeof(ast_root_statements_list_node));
  node->next = NULL;
  node->statement = p_statement;

  if (p_list->head == NULL) {
    p_list->head = node;
    p_list->tail = node;
    return;
  }
 
  ast_root_statements_list_node* tmp = p_list->tail;
  p_list->tail = node;
  tmp->next = node;
  p_list->count++;
}

void ast_root_statement_list_deinit(ast_root_statements_list* p_list) {
  ast_root_statements_list_node* node = p_list->head;
  while (node != NULL) {
    ast_node_deinit((ast_node*)node->statement);
    free(node->statement);
    node->statement = NULL;
    ast_root_statements_list_node* temp = node->next;
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
  ast_root_statements_list_init(&p_root->statements);
}

void ast_root_deinit(ast_root* p_root) {
  ast_root_statement_list_deinit(&p_root->statements);
  ast_statement_deinit(&p_root->statement);
}

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, token p_token) {
  ast_expression_init(&p_identifier->expression, AST_IDENTIFIER_EXPRESSION, p_token);
}

void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier) {
  ast_expression_deinit(&p_identifier->expression);
}

char* ast_identifier_expression_get_value(ast_identifier_expression* p_identifier) {
  const ast_node* node = &p_identifier->expression.node;
  char* str = (char*)malloc(sizeof(char) * node->token.length + 1);
  memcpy(str, node->token.start, node->token.length);
  str[node->token.length] = '\0';
  return str;
}

void ast_number_expression_init(ast_number_expression* p_number, token p_token) {
  ast_expression_init(&p_number->expression, AST_NUMBER_EXPRESSION, p_token);
}

void ast_number_expression_deinit(ast_number_expression* p_number) {
  ast_expression_deinit(&p_number->expression);
}

// TODO error reporting?!
double ast_number_expression_get_value(ast_number_expression* p_number) {
  const ast_node* node = &p_number->expression.node;
  char* endptr = node->token.start + node->token.length; 
  const double value = strtod(node->token.start, &endptr);
  if (endptr == node->token.start) {
    return 0;
  }
  return value;
}

void ast_string_expression_init(ast_string_expression* p_string, token p_token) {
  ast_expression_init(&p_string->expression, AST_STRING_EXPRESSION, p_token);
}

void ast_string_expression_deinit(ast_string_expression* p_string) {
  ast_expression_deinit(&p_string->expression);
}

char* ast_string_expression_get_value(ast_string_expression* p_string) {
  const ast_node* node = &p_string->expression.node;
  char* str = (char*)malloc(sizeof(char) * node->token.length + 1);
  memcpy(str, node->token.start, node->token.length);
  str[node->token.length] = '\0';
  return str;
}

void ast_boolean_expression_init(ast_boolean_expression* p_boolean, token p_token) {
  ast_expression_init(&p_boolean->expression, AST_BOOLEAN_EXPRESSION, p_token);
}

void ast_boolean_expression_deinit(ast_boolean_expression* p_boolean) {
  ast_expression_deinit(&p_boolean->expression);
}

bool ast_boolean_expression_get_value(ast_boolean_expression* p_boolean) {
  const ast_node* node = &p_boolean->expression.node;
  return strncmp("true", node->token.start, node->token.length) ? true : false;
}

void ast_null_expression_init(ast_null_expression* p_null, token p_token) {
  ast_expression_init(&p_null->expression, AST_NULL_EXPRESSION, p_token);
}

void ast_null_expression_deinit(ast_null_expression* p_null) {
  ast_expression_deinit(&p_null->expression);
}

void ast_prefix_unary_expression_init(ast_prefix_unary_expression* p_prefix, token p_token, token_type p_operator, ast_expression* p_right) {
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

void ast_postfix_unary_expression_init(ast_postfix_unary_expression* p_postfix, token p_token, token_type p_operator, ast_expression* p_left) {
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

void ast_infix_binary_expression_init(ast_infix_binary_expression* p_infix, token p_token, token_type p_operator, ast_expression* p_left, ast_expression* p_right) {
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

void ast_eof_statement_init(ast_eof_statement* p_eof, token p_token) {
  ast_statement_init(&p_eof->statement, AST_EOF_STATEMENT, p_token);
}

void ast_eof_statement_deinit(ast_eof_statement* p_eof) {
  ast_statement_deinit(&p_eof->statement);
}

void ast_empty_statement_init(ast_empty_statement* p_empty, token p_token) {
  ast_statement_init(&p_empty->statement, AST_EMPTY_STATEMENT, p_token);
}

void ast_empty_statement_deinit(ast_empty_statement* p_empty) {
  ast_statement_deinit(&p_empty->statement);
}

void ast_expression_statement_init(ast_expression_statement* p_expression_statement, token p_token, ast_expression* p_expression) {
  ast_statement_init(&p_expression_statement->statement, AST_EXPRESSION_STATEMENT, p_token);
}

void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement) {
  ast_expression_deinit(p_expression_statement->expression);
  free(p_expression_statement->expression);
  p_expression_statement->expression = NULL;
  ast_statement_deinit(&p_expression_statement->statement);
}
