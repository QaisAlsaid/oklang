#ifndef OK_AST_H
#define OK_AST_H

#include <stdbool.h>

#include "okoperator.h"
#include "oktoken.h"
#include "okvalue.h" // for QNAN

#if defined(OK_PARANOID)
#define OK_TRACE_AST
#endif // defined(OK_PARANOID)

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
  AST_FUNCTION_EXPRESSION,

  AST_EMPTY_STATEMENT,
  AST_EXPRESSION_STATEMENT,
  AST_PRINT_STATEMENT,
  AST_COMPOUND_STATEMENT,
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

// per ast tree.
typedef struct {
  allocators* p_alloc;
} ast_specs;

typedef struct {
  ast_node_type node_type;
  token token;
  allocators* alloc;
} ast_node;

void ast_node_init(ast_node* p_node, const ast_node_type p_type, const token p_token, ast_specs* p_specs);
void ast_node_deinit(ast_node* p_node);
bool ast_node_print(const ast_node* p_node);
string ast_node_asprint(const ast_node* p_node);

typedef struct {
  ast_node node;
} ast_expression;

void ast_expression_init(ast_expression* p_expression,
                         const ast_node_type p_type,
                         const token p_token,
                         ast_specs* p_specs);
void ast_expression_deinit(ast_expression* p_expression);
bool ast_expression_print(const ast_expression* p_expression);
string ast_expression_asprint(const ast_expression* p_expression);

typedef struct {
  ast_node node;
} ast_statement;

void ast_statement_init(ast_statement* p_statement,
                        const ast_node_type p_type,
                        const token p_token,
                        ast_specs* p_specs);
void ast_statement_deinit(ast_statement* p_statement);
bool ast_statement_print(const ast_statement* p_statement);
string ast_statement_asprint(const ast_statement* p_statement);

typedef enum {
  BINDING_NONE = 0,
  BINDING_MUT = 1 << 0,
  BINDING_ERROR = 1 << 2,
} ast_binding_modifiers;

typedef uint8_t ast_binding_modifiers_t;
string ast_binding_modifiers_asprint(ast_binding_modifiers_t p_modifiers);

typedef struct {
  ast_node node;
  ast_expression* lvalue;
  ast_binding_modifiers modifiers;
} ast_binding;

void ast_binding_init(ast_binding* p_binding,
                      ast_expression* p_lvalue,
                      ast_binding_modifiers_t p_modifiers,
                      const token p_token,
                      ast_specs* p_specs);
void ast_binding_deinit(ast_binding* p_binding);
bool ast_binding_print(const ast_binding* p_binding);
string ast_binding_asprint(const ast_binding* p_binding);

typedef enum {
  DECLARATION_NONE = 0,
  DECLARATION_ERROR = 1,
  DECLARATION_GLOB = 1 << 1,
  DECLARATION_STATIC = 1 << 2,
  DECLARATION_ASYNC = 1 << 3,
  DECLARATION_EXPORT = 1 << 4,
} ast_declaration_modifiers;
#define DECLARATION_COUNT 5

typedef uint8_t ast_declaration_modifiers_t;
string ast_declaration_modifiers_asprint(ast_declaration_modifiers_t p_modifiers, allocators* p_alloc);

typedef struct {
  ast_statement statement;
  ast_declaration_modifiers_t modifiers;
} ast_declaration;

void ast_declaration_init(ast_declaration* p_declaration,
                          const ast_node_type p_type,
                          const token p_token,
                          ast_declaration_modifiers_t p_modifiers,
                          ast_specs* p_specs);
void ast_declaration_deinit(ast_declaration* p_declaration);
bool ast_declaration_print(const ast_declaration* p_declaration);
string ast_declaration_asprint(const ast_declaration* p_declaration);

ARRAY_DECLARE_DEFAULT(ast_statements_list, ast_statement*)

typedef struct {
  ast_statement statement;
  ast_statements_list statements;
} ast_root;

void ast_root_init(ast_root* p_root, token p_token, ast_specs* p_specs);
void ast_root_deinit(ast_root* p_root);
bool ast_root_print(const ast_root* p_root);
string ast_root_asprint(const ast_root* p_root);

typedef struct {
  ast_expression expression;
} ast_identifier_expression;

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, const token p_token, ast_specs* p_specs);
void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier);
ok_string_view ast_identifier_expression_get_value(const ast_identifier_expression* p_identifier);
bool ast_identifier_expression_print(const ast_identifier_expression* p_expression);
string ast_identifier_expression_asprint(const ast_identifier_expression* p_expression);

typedef struct {
  ast_expression expression;
} ast_number_expression;

#define AST_NUMBER_EXPRESSION_PARSE_ERROR (double)((uint64_t)(QNAN | 1))

void ast_number_expression_init(ast_number_expression* p_number, const token p_token, ast_specs* p_specs);
void ast_number_expression_deinit(ast_number_expression* p_number);
double ast_number_expression_get_value(const ast_number_expression* p_number);
bool ast_number_expression_print(const ast_number_expression* p_number);
string ast_number_expression_asprint(const ast_number_expression* p_number);

typedef struct {
  ast_expression expression;
} ast_string_expression;

void ast_string_expression_init(ast_string_expression* p_string, const token p_token, ast_specs* p_specs);
void ast_string_expression_deinit(ast_string_expression* p_string);
ok_string_view ast_string_expression_get_value(const ast_string_expression* p_string);
bool ast_string_expression_print(const ast_string_expression* p_string);
string ast_string_expression_asprint(const ast_string_expression* p_string);

typedef struct {
  ast_expression expression;
} ast_boolean_expression;

void ast_boolean_expression_init(ast_boolean_expression* p_boolean, const token p_token, ast_specs* p_specs);
void ast_boolean_expression_deinit(ast_boolean_expression* p_boolean);
bool ast_boolean_expression_get_value(const ast_boolean_expression* p_boolean);
bool ast_boolean_expression_print(const ast_boolean_expression* p_boolean);
string ast_boolean_expression_asprint(const ast_boolean_expression* p_boolean);

typedef struct {
  ast_expression expression;
} ast_null_expression;

void ast_null_expression_init(ast_null_expression* p_null, const token p_token, ast_specs* p_specs);
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
                                      ast_expression* p_right,
                                      ast_specs* p_specs);
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
                                       ast_expression* p_left,
                                       ast_specs* p_specs);
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
                                      ast_expression* p_right,
                                      ast_specs* p_specs);
void ast_infix_binary_expression_deinit(ast_infix_binary_expression* p_infix);
bool ast_infix_binary_expression_print(const ast_infix_binary_expression* p_infix);
string ast_infix_binary_expression_asprint(const ast_infix_binary_expression* p_infix);

typedef struct {
  ast_expression expression;
  ast_expression* left;
  ast_expression* right;
} ast_assign_expression;

void ast_assign_expression_init(ast_assign_expression* p_assign,
                                const token p_token,
                                ast_expression* p_left,
                                ast_expression* p_right,
                                ast_specs* p_specs);
void ast_assign_expression_deinit(ast_assign_expression* p_assign);
bool ast_assign_expression_print(const ast_assign_expression* p_assign);
string ast_assign_expression_asprint(const ast_assign_expression* p_assign);

ARRAY_DECLARE_DEFAULT(ast_bindings_list, ast_binding*)

typedef struct {
  ast_expression expression;
  ast_bindings_list parameters;
  ast_statement* body;
} ast_function_expression;

void ast_function_expression_init(ast_function_expression* p_function,
                                  const token p_token,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  ast_specs* p_specs);
void ast_function_expression_deinit(ast_function_expression* p_function);
bool ast_function_expression_print(const ast_function_expression* p_function);
string ast_function_expression_asprint(const ast_function_expression* p_function);

ARRAY_DECLARE_DEFAULT(ast_expressions_list, ast_expression*)

typedef struct {
  ast_expression expression;
  ast_expression* callable;
  ast_expressions_list arguments;
} ast_call_expression;

void ast_call_expression_init(ast_call_expression* p_call,
                              const token p_token,
                              ast_expression* p_callable,
                              ast_expressions_list p_args,
                              ast_specs* p_specs);
void ast_call_expression_deinit(ast_call_expression* p_call);
bool ast_call_expression_print(const ast_call_expression* p_call);
string ast_call_expression_asprint(const ast_call_expression* p_call);

typedef struct {
  ast_statement statement;
} ast_empty_statement;

void ast_empty_statement_init(ast_empty_statement* p_empty, const token p_token, ast_specs* p_specs);
void ast_empty_statement_deinit(ast_empty_statement* p_empty);
bool ast_empty_statement_print(const ast_empty_statement* p_empty);
string ast_empty_statement_asprint(const ast_empty_statement* p_empty);

typedef struct {
  ast_statement statement;
  ast_expression* expression;
} ast_expression_statement;

void ast_expression_statement_init(ast_expression_statement* p_expression_statement,
                                   const token p_token,
                                   ast_expression* p_expression,
                                   ast_specs* p_specs);
void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement);
bool ast_expression_statement_print(const ast_expression_statement* p_expression_statement);
string ast_expression_statement_asprint(const ast_expression_statement* p_expression_statement);

typedef struct {
  ast_statement statement;
} ast_eof_statement;

void ast_eof_statement_init(ast_eof_statement* p_eof, const token p_token, ast_specs* p_specs);
void ast_eof_statement_deinit(ast_eof_statement* p_eof);
bool ast_eof_statement_print(const ast_eof_statement* p_eof);
string ast_eof_statement_asprint(const ast_eof_statement* p_eof);

typedef struct {
  ast_statement statement;
  ast_expression* expression;
} ast_print_statement;

void ast_print_statement_init(ast_print_statement* p_print,
                              const token p_token,
                              ast_expression* p_expression,
                              ast_specs* p_specs);
void ast_print_statement_deinit(ast_print_statement* p_print);
bool ast_print_statement_print(const ast_print_statement* p_print);
string ast_print_statement_asprint(const ast_print_statement* p_print);

typedef struct {
  ast_statement statement;
  ast_statements_list statements;
} ast_compound_statement;

void ast_compound_statement_init(ast_compound_statement* p_compound, token p_token, ast_specs* p_specs);
void ast_compound_statement_deinit(ast_compound_statement* p_compound);
bool ast_compound_statement_print(const ast_compound_statement* p_compound);
string ast_compound_statement_asprint(const ast_compound_statement* p_compound);

typedef struct {
  ast_statement statement;
  ast_expression* condition;
  ast_statement* consequence;
  ast_statement* alternative;
} ast_if_statement;

void ast_if_statement_init(ast_if_statement* p_if, token p_token, ast_specs* p_specs);
void ast_if_statement_deinit(ast_if_statement* p_if);
bool ast_if_statement_print(const ast_if_statement* p_if);
string ast_if_statement_asprint(const ast_if_statement* p_if);

typedef struct {
  ast_statement statement;
  ast_expression* condition;
  ast_statement* body;
} ast_while_statement;

void ast_while_statement_init(ast_while_statement* p_while, token p_token, ast_specs* p_specs);
void ast_while_statement_deinit(ast_while_statement* p_while);
bool ast_while_statement_print(const ast_while_statement* p_while);
string ast_while_statement_asprint(const ast_while_statement* p_while);

typedef struct {
  ast_statement statement;
  ast_statement* initializer;
  ast_expression* condition;
  ast_expression* update;
  ast_statement* body;
} ast_for_statement;

void ast_for_statement_init(ast_for_statement* p_for, token p_token, ast_specs* p_specs);
void ast_for_statement_deinit(ast_for_statement* p_for);
bool ast_for_statement_print(const ast_for_statement* p_for);
string ast_for_statement_asprint(const ast_for_statement* p_for);

typedef enum {
  CONTROL_FLOW_NONE,
  CONTROL_FLOW_BREAK,
  CONTROL_FLOW_CONTINUE,
} control_flow_type;

string control_flow_type_asprint(control_flow_type p_type);

typedef struct {
  ast_statement statement;
  control_flow_type type;
} ast_control_flow_statement;

void ast_control_flow_statement_init(ast_control_flow_statement* p_control_flow, token p_token, ast_specs* p_specs);
void ast_control_flow_statement_deinit(ast_control_flow_statement* p_control_flow);
bool ast_control_flow_statement_print(const ast_control_flow_statement* p_control_flow);
string ast_control_flow_statement_asprint(const ast_control_flow_statement* p_control_flow);

typedef struct {
  ast_statement statement;
  ast_expression* returned;
} ast_return_statement;

void ast_return_statement_init(ast_return_statement* p_return,
                               ast_expression* p_returned,
                               token p_token,
                               ast_specs* p_specs);
void ast_return_statement_deinit(ast_return_statement* p_return);
bool ast_return_statement_print(const ast_return_statement* p_return);
string ast_return_statement_asprint(const ast_return_statement* p_return);

typedef struct {
  ast_declaration declaration;
  ast_binding* binding;
  ast_expression* value;
} ast_let_declaration;

void ast_let_declaration_init(ast_let_declaration* p_let,
                              ast_binding* p_binding,
                              ast_expression* p_value,
                              ast_declaration_modifiers_t p_modifiers,
                              const token p_token,
                              ast_specs* p_specs);
void ast_let_declaration_deinit(ast_let_declaration* p_let);
bool ast_let_declaration_print(const ast_let_declaration* p_let);
string ast_let_declaration_asprint(const ast_let_declaration* p_let);

typedef struct {
  ast_declaration declaration;
  ast_binding* binding;
  ast_bindings_list parameters;
  ast_statement* body;
} ast_function_declaration;

void ast_function_declaration_init(ast_function_declaration* p_fu,
                                   ast_binding* p_binding,
                                   ast_bindings_list p_params,
                                   ast_statement* p_body,
                                   ast_declaration_modifiers_t p_modifiers,
                                   const token p_token,
                                   ast_specs* p_specs);
void ast_function_declaration_deinit(ast_function_declaration* p_fu);
bool ast_function_declaration_print(const ast_function_declaration* p_fu);
string ast_function_declaration_asprint(const ast_function_declaration* p_fu);

bool ast_dispatch_print(ast_node* p_node);
string ast_dispatch_asprint(ast_node* p_node);
void ast_dispatch_deinit(ast_node* p_node);

#endif // OK_AST_H
