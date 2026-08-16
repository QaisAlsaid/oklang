#ifndef OK_AST_H
#define OK_AST_H

#include "operator.h"
#include "token.h"
#include "utils.h"
#include "value.h" // for QNAN
#include <stdbool.h>

typedef enum {
  AST_NODE = 0,
  AST_EXPRESSION,
  AST_STATEMENT,
  AST_DECLARATION, // it is a statement, but i guess it is cleaner this way (doesn't matter regardless).

  AST_ROOT,

  AST_BINDING,

  AST_IDENTIFIER_EXPRESSION,
  AST_NUMBER_EXPRESSION,
  AST_STRING_EXPRESSION,
  AST_PREFIX_UNARY_EXPRESSION,
  AST_INFIX_BINARY_EXPRESSION,
  AST_POSTFIX_UNARY_EXPRESSION,
  AST_CALL_EXPRESSION,
  AST_ASSIGN_EXPRESSION,
  AST_COMPOUND_ASSIGN_EXPRESSION,
  AST_OPERATOR_EXPRESSION,
  AST_CONDITIONAL_EXPRESSION,
  AST_BOOLEAN_EXPRESSION,
  AST_NULL_EXPRESSION,
  AST_ACCESS_EXPRESSION,
  AST_THIS_EXPRESSION,
  AST_SUPER_EXPRESSION,
  AST_ARRAY_EXPRESSION,
  AST_MAP_EXPRESSION,
  AST_SUBSCRIPT_EXPRESSION,

  AST_EMPTY_STATEMENT,
  AST_EXPRESSION_STATEMENT,
  AST_PRINT_STATEMENT,
  AST_BLOCK_STATEMENT,
  AST_IF_STATEMENT,
  AST_FOR_STATEMENT,
  AST_WHILE_STATEMENT,
  AST_CONTROL_FLOW_STATEMENT,
  AST_RETURN_STATEMENT,
  AST_THROW_STATEMENT,
  AST_TRY_STATEMENT,
  AST_CATCH_STATEMENT,
  AST_FINALIZE_STATEMENT,
  AST_TRY_CATCH_STATEMENT,
  AST_EOF_STATEMENT,

  AST_LET_DECLARATION,
  AST_FUNCTION_DECLARATION,
  AST_CLASS_DECLARATION,
} ast_node_type;

typedef struct {
  ast_node_type node_type;
  token token;
} ast_node;

void ast_node_init(ast_node* p_node, const ast_node_type p_type, const token p_token);
void ast_node_deinit(ast_node* p_node);
bool ast_node_print(const ast_node* p_node);
string ast_node_asprint(const ast_node* p_node);

typedef struct {
  ast_node node;
} ast_expression;

void ast_expression_init(ast_expression* p_expression, const ast_node_type p_type, const token p_token);
void ast_expression_deinit(ast_expression* p_expression);
bool ast_expression_print(const ast_expression* p_expression);
string ast_expression_asprint(const ast_expression* p_expression);

typedef struct {
  ast_node node;
} ast_statement;

void ast_statement_init(ast_statement* p_statement, const ast_node_type p_type, const token p_token);
void ast_statement_deinit(ast_statement* p_statement);
bool ast_statement_print(const ast_statement* p_statement);
string ast_statement_asprint(const ast_statement* p_statement);

typedef struct {
  ast_statement statement;
} ast_declaration;

void ast_declaration_init(ast_declaration* p_declaration, const ast_node_type p_type, const token p_token);
void ast_declaration_deinit(ast_declaration* p_declaration);
bool ast_declaration_print(const ast_declaration* p_declaration);
string ast_declaration_asprint(const ast_declaration* p_declaration);

typedef struct ast_statements_list_node ast_statements_list_node;

struct ast_statements_list_node {
  ast_statement* statement;
  ast_statements_list_node* next;
};

typedef struct {
  ast_statements_list_node* head;
  ast_statements_list_node* tail;
  uint32_t count;
} ast_statements_list;

void ast_statements_list_init(ast_statements_list* p_list);
bool ast_statements_list_append(ast_statements_list* p_list, ast_statement* p_statement);
void ast_statement_list_deinit(ast_statements_list* p_list);

typedef struct {
  ast_statement statement;
  ast_statements_list statements;
} ast_root;

void ast_root_init(ast_root* p_root, token p_token);
void ast_root_deinit(ast_root* p_root);
bool ast_root_print(const ast_root* p_root);
string ast_root_asprint(const ast_root* p_root);

typedef struct {
  ast_expression expression;
} ast_identifier_expression;

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, const token p_token);
void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier);
string ast_identifier_expression_get_value(const ast_identifier_expression* p_identifier);
bool ast_identifier_expression_print(const ast_identifier_expression* p_expression);
string ast_identifier_expression_asprint(const ast_identifier_expression* p_expression);

typedef struct {
  ast_expression expression;
} ast_number_expression;

#define AST_NUMBER_EXPRESSION_PARSE_ERROR (double)((uint64_t)(QNAN | 1))

void ast_number_expression_init(ast_number_expression* p_number, const token p_token);
void ast_number_expression_deinit(ast_number_expression* p_number);
double ast_number_expression_get_value(const ast_number_expression* p_number);
bool ast_number_expression_print(const ast_number_expression* p_number);
string ast_number_expression_asprint(const ast_number_expression* p_number);

typedef struct {
  ast_expression expression;
} ast_string_expression;

void ast_string_expression_init(ast_string_expression* p_string, const token p_token);
void ast_string_expression_deinit(ast_string_expression* p_string);
string ast_string_expression_get_value(const ast_string_expression* p_string);
bool ast_string_expression_print(const ast_string_expression* p_string);
string ast_string_expression_asprint(const ast_string_expression* p_string);

typedef struct {
  ast_expression expression;
} ast_boolean_expression;

void ast_boolean_expression_init(ast_boolean_expression* p_boolean, const token p_token);
void ast_boolean_expression_deinit(ast_boolean_expression* p_boolean);
bool ast_boolean_expression_get_value(const ast_boolean_expression* p_boolean);
bool ast_boolean_expression_print(const ast_boolean_expression* p_boolean);
string ast_boolean_expression_asprint(const ast_boolean_expression* p_boolean);

typedef struct {
  ast_expression expression;
} ast_null_expression;

void ast_null_expression_init(ast_null_expression* p_null, const token p_token);
void ast_null_expression_deinit(ast_null_expression* p_null);
bool ast_null_expression_print(const ast_null_expression* p_null);
string ast_null_expression_asprint(const ast_null_expression* p_null);

typedef struct {
  ast_expression expression;
  operator_type _operator;
  ast_expression* right;
} ast_prefix_unary_expression;

void ast_prefix_unary_expression_init(ast_prefix_unary_expression* p_prefix,
                                      const token p_token,
                                      const operator_type p_operator,
                                      ast_expression* p_right);
void ast_prefix_unary_expression_deinit(ast_prefix_unary_expression* p_prefix);
bool ast_prefix_unary_expression_print(const ast_prefix_unary_expression* p_prefix);
string ast_prefix_unary_expression_asprint(const ast_prefix_unary_expression* p_prefix);

typedef struct {
  ast_expression expression;
  operator_type _operator;
  ast_expression* left;
} ast_postfix_unary_expression;

void ast_postfix_unary_expression_init(ast_postfix_unary_expression* p_postfix,
                                       const token p_token,
                                       const operator_type p_operator,
                                       ast_expression* p_left);
void ast_postfix_unary_expression_deinit(ast_postfix_unary_expression* p_postfix);
bool ast_postfix_unary_expression_print(const ast_postfix_unary_expression* p_postfix);
string ast_postfix_unary_expression_asprint(const ast_postfix_unary_expression* p_postfix);

typedef struct {
  ast_expression expression;
  operator_type _operator;
  ast_expression* left;
  ast_expression* right;
} ast_infix_binary_expression;

void ast_infix_binary_expression_init(ast_infix_binary_expression* p_infix,
                                      const token p_token,
                                      const operator_type p_operator,
                                      ast_expression* p_left,
                                      ast_expression* p_right);
void ast_infix_binary_expression_deinit(ast_infix_binary_expression* p_infix);
bool ast_infix_binary_expression_print(const ast_infix_binary_expression* p_infix);
string ast_infix_binary_expression_asprint(const ast_infix_binary_expression* p_infix);

typedef struct {
  ast_statement statement;
} ast_empty_statement;

void ast_empty_statement_init(ast_empty_statement* p_empty, const token p_token);
void ast_empty_statement_deinit(ast_empty_statement* p_empty);
bool ast_empty_statement_print(const ast_empty_statement* p_empty);
string ast_empty_statement_asprint(const ast_empty_statement* p_empty);

typedef struct {
  ast_statement statement;
  ast_expression* expression;
} ast_expression_statement;

void ast_expression_statement_init(ast_expression_statement* p_expression_statement,
                                   const token p_token,
                                   ast_expression* p_expression);
void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement);
bool ast_expression_statement_print(const ast_expression_statement* p_expression_statement);
string ast_expression_statement_asprint(const ast_expression_statement* p_expression_statement);

typedef struct {
  ast_statement statement;
} ast_eof_statement;

void ast_eof_statement_init(ast_eof_statement* p_eof, const token p_token);
void ast_eof_statement_deinit(ast_eof_statement* p_eof);
bool ast_eof_statement_print(const ast_eof_statement* p_eof);
string ast_eof_statement_asprint(const ast_eof_statement* p_eof);

bool ast_print(ast_node* p_node);
string ast_asprint(ast_node* p_node);
void ast_deinit(ast_node* p_node);

#endif // OK_AST_H
