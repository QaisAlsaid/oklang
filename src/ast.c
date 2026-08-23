#include "ast.h"
#include "utils.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AST_LIST_X(X)                                                                                                  \
  X(AST_NODE, ast_node)                                                                                                \
  X(AST_EXPRESSION, ast_expression)                                                                                    \
  X(AST_STATEMENT, ast_statement)                                                                                      \
  X(AST_DECLARATION, ast_declaration)                                                                                  \
  X(AST_ROOT, ast_root)                                                                                                \
  X(AST_BINDING, ast_binding)                                                                                          \
  X(AST_IDENTIFIER_EXPRESSION, ast_identifier_expression)                                                              \
  X(AST_NUMBER_EXPRESSION, ast_number_expression)                                                                      \
  X(AST_STRING_EXPRESSION, ast_string_expression)                                                                      \
  X(AST_PREFIX_UNARY_EXPRESSION, ast_prefix_unary_expression)                                                          \
  X(AST_INFIX_BINARY_EXPRESSION, ast_infix_binary_expression)                                                          \
  X(AST_POSTFIX_UNARY_EXPRESSION, ast_postfix_unary_expression)                                                        \
  /*X(AST_CALL_EXPRESSION, ast_call_expression)*/                                                                      \
  X(AST_ASSIGN_EXPRESSION, ast_assign_expression)                                                                      \
  /*X(AST_COMPOUND_ASSIGN_EXPRESSION, ast_compound_assign_expression)*/                                                \
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
  X(AST_PRINT_STATEMENT, ast_print_statement)                                                                          \
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
  X(AST_EOF_STATEMENT, ast_eof_statement)                                                                              \
  X(AST_LET_DECLARATION, ast_let_declaration)                                                                          \
  /*X(AST_FUNCTION_DECLARATION, ast_function_declaration)*/                                                            \
  /*X(AST_CLASS_DECLARATION, ast_class_declaration)*/

void ast_node_init(ast_node* p_node, ast_node_type p_type, token p_token) {
  p_node->node_type = p_type;
  p_node->token = p_token;
}

void ast_node_deinit(ast_node* p_node) {
  (void)p_node;
}

bool ast_node_print(const ast_node* p_node) {
  return false;
}

string ast_node_asprint(const ast_node* p_node) {
  return create_string(NULL, 0, false);
}

void ast_expression_init(ast_expression* p_expression, const ast_node_type p_type, const token p_token) {
  ast_node_init(&p_expression->node, p_type, p_token);
}

void ast_expression_deinit(ast_expression* p_expression) {
  (void)p_expression;
}

bool ast_expression_print(const ast_expression* p_expression) {
  return false;
}

string ast_expression_asprint(const ast_expression* p_expression) {
  return create_string(NULL, 0, false);
}

void ast_statement_init(ast_statement* p_statement, const ast_node_type p_type, const token p_token) {
  ast_node_init(&p_statement->node, p_type, p_token);
}

void ast_statement_deinit(ast_statement* p_statement) {
  (void)p_statement;
}

bool ast_statement_print(const ast_statement* p_statement) {
  return false;
}

string ast_statement_asprint(const ast_statement* p_statement) {
  return create_string(NULL, 0, false);
}

string ast_binding_modifiers_asprint(ast_binding_modifiers_t p_modifiers) {
  if ((p_modifiers & BINDING_NONE) != 0) {
    return create_string("", 0, false);
  }
  if ((p_modifiers & BINDING_MUT) != 0) {
    return create_string("mut", STRING_CALCULATE_LENGTH, false);
  }
  return create_string("unknown-binding-modifier", STRING_CALCULATE_LENGTH, false);
}

void ast_binding_init(ast_binding* p_binding,
                      ast_expression* p_lvalue,
                      ast_binding_modifiers_t p_modifiers,
                      const token p_token) {
  ast_node_init(&p_binding->node, AST_BINDING, p_token);
  p_binding->lvalue = p_lvalue;
  p_binding->modifiers = p_modifiers;
}

void ast_binding_deinit(ast_binding* p_binding) {
  ast_dispatch_deinit((ast_node*)p_binding->lvalue);
  free(p_binding->lvalue);
  p_binding->lvalue = NULL;
  p_binding->modifiers = BINDING_NONE;
  ast_node_deinit(&p_binding->node);
}

bool ast_binding_print(const ast_binding* p_binding) {
  bool res = printf("%s%s",
                    ast_binding_modifiers_asprint(p_binding->modifiers).chars,
                    p_binding->modifiers != BINDING_NONE ? " " : "") > 0;
  return res && ast_dispatch_print((ast_node*)p_binding->lvalue);
}

string ast_binding_asprint(const ast_binding* p_binding) {
  string lvalue_str = ast_dispatch_asprint((ast_node*)p_binding->lvalue);
  if (lvalue_str.chars != NULL) {
    string result = asprint("%s%s%s",
                            lvalue_str,
                            p_binding->modifiers != BINDING_NONE ? " " : "",
                            ast_binding_modifiers_asprint(p_binding->modifiers).chars);
    string_deinit(&lvalue_str);
    return result;
  }
  return create_string(NULL, 0, false);
}

// doesn't preserve order.
string ast_declaration_modifiers_asprint(ast_declaration_modifiers_t p_modifiers) {
  return (p_modifiers & DECLARATION_NONE) != 0 ? create_string("", 0, false)
                                               : asprint("%s%s%s%s",
                                                         (p_modifiers & DECLARATION_GLOB) != 0 ? "glob " : "",
                                                         (p_modifiers & DECLARATION_STATIC) != 0 ? "static " : "",
                                                         (p_modifiers & DECLARATION_EXPORT) != 0 ? "export " : "",
                                                         (p_modifiers & DECLARATION_ASYNC) != 0 ? "async" : "");
}

void ast_declaration_init(ast_declaration* p_declaration,

                          const ast_node_type p_type,
                          const token p_token,
                          ast_declaration_modifiers_t p_modifiers) {
  ast_statement_init(&p_declaration->statement, p_type, p_token);
  p_declaration->modifiers = p_modifiers;
}

void ast_declaration_deinit(ast_declaration* p_declaration) {
  ast_statement_deinit(&p_declaration->statement);
  p_declaration->modifiers = DECLARATION_NONE;
}

bool ast_declaration_print(const ast_declaration* p_declaration) {
  string mods_str = ast_declaration_modifiers_asprint(p_declaration->modifiers);
  if (mods_str.chars != NULL) {
    const bool status = printf("%s", mods_str.chars) > 0;
    string_deinit(&mods_str);
    return status;
  }
  return false;
}

string ast_declaration_asprint(const ast_declaration* p_declaration) {
  return ast_declaration_modifiers_asprint(p_declaration->modifiers);
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

  p_list->tail->next = node;
  p_list->tail = node;
  p_list->count++;
  return true;
}

void ast_statement_list_deinit(ast_statements_list* p_list) {
  ast_statements_list_node* node = p_list->head;
  while (node != NULL) {
    ast_dispatch_deinit((ast_node*)node->statement);
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
    status &= ast_dispatch_print((ast_node*)node->statement);
  }
  return status;
}

string ast_root_asprint(const ast_root* p_root) {
  char* chars = NULL;
  size_t len = 0;
  for (ast_statements_list_node* node = p_root->statements.head; node != NULL; node = node->next) {
    string res = ast_dispatch_asprint((ast_node*)node->statement);
    if (res.chars != NULL) {
      len += string_get_length(&res);
      char* temp = malloc(len);
      free(chars);
      if (temp == NULL) {
        string_deinit(&res);
        return create_string(NULL, 0, false);
      }
      chars = temp;
    }
  }
  return create_string(chars, len, true);
}

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, token p_token) {
  ast_expression_init(&p_identifier->expression, AST_IDENTIFIER_EXPRESSION, p_token);
}

void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier) {
  ast_expression_deinit(&p_identifier->expression);
}

string_view ast_identifier_expression_get_value(const ast_identifier_expression* p_identifier) {
  const ast_node* node = &p_identifier->expression.node;
  return create_string_view(node->token.start, node->token.length, false);
}

bool ast_identifier_expression_print(const ast_identifier_expression* p_identifier) {
  string ident = create_string_from_string_view(ast_identifier_expression_get_value(p_identifier));
  if (ident.chars != NULL) {
    bool printf_status = printf("%s", ident.chars) > 0;
    string_deinit(&ident);
    return printf_status;
  }
  return false;
}

string ast_identifier_expression_asprint(const ast_identifier_expression* p_identifier) {
  string ident = create_string_from_string_view(ast_identifier_expression_get_value(p_identifier));
  if (ident.chars != NULL) {
    string str = asprint("%s", ident.chars);
    string_deinit(&ident);
    return str;
  }
  return create_string(NULL, 0, false);
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
    return create_string(NULL, 0, false);
  }
  return asprint("%f", value);
}

void ast_string_expression_init(ast_string_expression* p_string, token p_token) {
  ast_expression_init(&p_string->expression, AST_STRING_EXPRESSION, p_token);
}

void ast_string_expression_deinit(ast_string_expression* p_string) {
  ast_expression_deinit(&p_string->expression);
}

string_view ast_string_expression_get_value(const ast_string_expression* p_string) {
  const ast_node* node = &p_string->expression.node;
  return create_string_view(node->token.start + 1, node->token.length - 2, false);
}

bool ast_string_expression_print(const ast_string_expression* p_string) {
  string value = create_string_from_string_view(ast_string_expression_get_value(p_string));
  if (value.chars == NULL) {
    return false;
  }
  const bool ret = printf("%s", value.chars) > 0;
  string_deinit(&value);
  return ret;
}

string ast_string_expression_asprint(const ast_string_expression* p_string) {
  return create_string_from_string_view(ast_string_expression_get_value(p_string));
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
                                      operator_type p_operator,
                                      ast_expression* p_right) {
  ast_expression_init(&p_prefix->expression, AST_PREFIX_UNARY_EXPRESSION, p_token);
  p_prefix->_operator = p_operator;
  p_prefix->right = p_right;
}

void ast_prefix_unary_expression_deinit(ast_prefix_unary_expression* p_prefix) {
  ast_dispatch_deinit((ast_node*)p_prefix->right);
  free(p_prefix->right);
  p_prefix->right = NULL;
  ast_expression_deinit(&p_prefix->expression);
}

bool ast_prefix_unary_expression_print(const ast_prefix_unary_expression* p_prefix) {
  return printf("%s", operator_type_to_string(p_prefix->_operator).chars) > 0 &&
         ast_dispatch_print((ast_node*)p_prefix->right);
}

string ast_prefix_unary_expression_asprint(const ast_prefix_unary_expression* p_prefix) {
  string ret = create_string(NULL, 0, false);
  string right = ast_dispatch_asprint((ast_node*)p_prefix->right);
  if (right.chars == NULL) {
    return create_string(NULL, 0, false);
  }
  ret = asprint("%s%s", operator_type_to_string(p_prefix->_operator).chars, right.chars);
  string_deinit(&right);
  return ret;
}

#undef OPERATOR_STRING

void ast_postfix_unary_expression_init(ast_postfix_unary_expression* p_postfix,
                                       token p_token,
                                       operator_type p_operator,
                                       ast_expression* p_left) {
  ast_expression_init(&p_postfix->expression, AST_POSTFIX_UNARY_EXPRESSION, p_token);
  p_postfix->_operator = p_operator;
  p_postfix->left = p_left;
}

void ast_postfix_unary_expression_deinit(ast_postfix_unary_expression* p_postfix) {
  ast_dispatch_deinit((ast_node*)p_postfix->left);
  free(p_postfix->left);
  p_postfix->left = NULL;
  ast_expression_deinit(&p_postfix->expression);
}

bool ast_postfix_unary_expression_print(const ast_postfix_unary_expression* p_postfix) {
  return ast_dispatch_print((ast_node*)p_postfix->left) &&
         printf("%s", operator_type_to_string(p_postfix->_operator).chars);
}

string ast_postfix_unary_expression_asprint(const ast_postfix_unary_expression* p_postfix) {
  string ret = create_string(NULL, 0, false);
  string left = ast_dispatch_asprint((ast_node*)p_postfix->left);
  if (left.chars == NULL) {
    return create_string(NULL, 0, false);
  }
  ret = asprint("%s%s", left.chars, operator_type_to_string(p_postfix->_operator).chars);
  string_deinit(&left);
  return ret;
}

void ast_infix_binary_expression_init(ast_infix_binary_expression* p_infix,
                                      token p_token,
                                      operator_type p_operator,
                                      ast_expression* p_left,
                                      ast_expression* p_right) {
  ast_expression_init(&p_infix->expression, AST_INFIX_BINARY_EXPRESSION, p_token);
  p_infix->_operator = p_operator;
  p_infix->left = p_left;
  p_infix->right = p_right;
}

void ast_infix_binary_expression_deinit(ast_infix_binary_expression* p_infix) {
  ast_dispatch_deinit((ast_node*)p_infix->left);
  ast_dispatch_deinit((ast_node*)p_infix->right);
  free(p_infix->left);
  free(p_infix->right);
  p_infix->left = NULL;
  p_infix->right = NULL;
  ast_expression_deinit(&p_infix->expression);
}

bool ast_infix_binary_expression_print(const ast_infix_binary_expression* p_infix) {
  return ast_dispatch_print((ast_node*)p_infix->left) &&
         printf("%s", operator_type_to_string(p_infix->_operator).chars) &&
         ast_dispatch_print((ast_node*)p_infix->right);
}

string ast_infix_binary_expression_asprint(const ast_infix_binary_expression* p_infix) {
  string ret = create_string(NULL, 0, false);
  string left = ast_dispatch_asprint((ast_node*)p_infix->left);
  string right = ast_dispatch_asprint((ast_node*)p_infix->right);
  if (left.chars != NULL && right.chars != NULL) {
    ret = asprint("%s%s%s", left.chars, operator_type_to_string(p_infix->_operator).chars, right.chars);
  }
  string_deinit(&left);
  string_deinit(&right);
  return ret;
}

void ast_assign_expression_init(ast_assign_expression* p_assign,
                                const token p_token,
                                ast_expression* p_left,
                                ast_expression* p_right) {
  ast_expression_init(&p_assign->expression, AST_ASSIGN_EXPRESSION, p_token);
  p_assign->left = p_left;
  p_assign->right = p_right;
}

void ast_assign_expression_deinit(ast_assign_expression* p_assign) {
  ast_dispatch_deinit((ast_node*)p_assign->left);
  ast_dispatch_deinit((ast_node*)p_assign->right);
  free(p_assign->left);
  free(p_assign->right);
  p_assign->left = NULL;
  p_assign->right = NULL;
  ast_expression_deinit(&p_assign->expression);
}

bool ast_assign_expression_print(const ast_assign_expression* p_assign) {
  bool ret = ast_dispatch_print((ast_node*)p_assign->left);
  ret &= puts(" = ");
  ret &= ast_dispatch_print((ast_node*)p_assign->right);
  return ret;
}

string ast_assign_expression_asprint(const ast_assign_expression* p_assign) {
  string ret = create_string(NULL, 0, false);
  string left_str = ast_dispatch_asprint((ast_node*)p_assign->left);
  string right_str = ast_dispatch_asprint((ast_node*)p_assign->right);
  if (left_str.chars != NULL && right_str.chars != NULL) {
    ret = asprint("%s = %s", left_str.chars, right_str.chars);
  }
  string_deinit(&left_str);
  string_deinit(&right_str);
  return ret;
}

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
  return create_string(NULL, 0, false);
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
  return create_string(NULL, 0, false);
}

void ast_expression_statement_init(ast_expression_statement* p_expression_statement,
                                   token p_token,
                                   ast_expression* p_expression) {
  ast_statement_init(&p_expression_statement->statement, AST_EXPRESSION_STATEMENT, p_token);
  p_expression_statement->expression = p_expression;
}

void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement) {
  ast_dispatch_deinit((ast_node*)p_expression_statement->expression);
  free(p_expression_statement->expression);
  p_expression_statement->expression = NULL;
  ast_statement_deinit(&p_expression_statement->statement);
}

bool ast_expression_statement_print(const ast_expression_statement* p_expression_statement) {
  return ast_dispatch_print((ast_node*)p_expression_statement->expression);
}

string ast_expression_statement_asprint(const ast_expression_statement* p_expression_statement) {
  return ast_dispatch_asprint((ast_node*)p_expression_statement->expression);
}

void ast_print_statement_init(ast_print_statement* p_print, const token p_token, ast_expression* p_expression) {
  ast_statement_init(&p_print->statement, AST_PRINT_STATEMENT, p_token);
  p_print->expression = p_expression;
}

void ast_print_statement_deinit(ast_print_statement* p_print) {
  ast_dispatch_deinit((ast_node*)p_print->expression);
  free(p_print->expression);
  p_print->expression = NULL;
}

bool ast_print_statement_print(const ast_print_statement* p_print) {
  bool status = printf("print") > 0;
  status &= ast_dispatch_print((ast_node*)p_print->expression);
  return status;
}

string ast_print_statement_asprint(const ast_print_statement* p_print) {
  string expr_str = ast_dispatch_asprint((ast_node*)p_print->expression);
  string result = asprint("print %s", expr_str.chars);
  string_deinit(&expr_str);
  return result;
}

void ast_let_declaration_init(ast_let_declaration* p_let,
                              ast_binding* p_binding,
                              ast_expression* p_value,
                              ast_declaration_modifiers_t p_modifiers,
                              const token p_token) {
  ast_declaration_init(&p_let->declaration, AST_LET_DECLARATION, p_token, p_modifiers);
  p_let->binding = p_binding;
  p_let->value = p_value;
}

void ast_let_declaration_deinit(ast_let_declaration* p_let) {
  ast_dispatch_deinit((ast_node*)p_let->binding);
  ast_dispatch_deinit((ast_node*)p_let->value);
  free(p_let->binding);
  free(p_let->value);
  ast_declaration_deinit(&p_let->declaration);
}

bool ast_let_declaration_print(const ast_let_declaration* p_let) {
  bool fail = ast_declaration_print(&p_let->declaration);
  fail &= printf("let ");
  fail &= printf(" ");
  fail &= ast_binding_print(p_let->binding);
  fail &= printf(" ");
  return fail & ast_dispatch_print((ast_node*)p_let->value);
}

string ast_let_declaration_asprint(const ast_let_declaration* p_let) {
  string decl_str = ast_declaration_asprint(&p_let->declaration);
  string binding_str = ast_binding_asprint(p_let->binding);
  string value_str = ast_dispatch_asprint((ast_node*)p_let->value);
  const bool fail = decl_str.chars == NULL || binding_str.chars == NULL || value_str.chars == NULL;
  string ret = create_string(NULL, 0, false);
  if (!fail) {
    ret = asprint("%s %s %s", decl_str.chars, binding_str.chars, value_str.chars);
  }
  string_deinit(&decl_str);
  string_deinit(&binding_str);
  string_deinit(&value_str);
  return ret;
}

void ast_dispatch_deinit(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    klass##_deinit((klass*)p_node);                                                                                    \
    break;
  switch (p_node->node_type) {
    AST_LIST_X(X)
  default:; // for now
  }
#undef X
}

bool ast_dispatch_print(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    return klass##_print((klass*)p_node);
  switch (p_node->node_type) {
    AST_LIST_X(X);
  default:;
  }
  return false;
#undef X
}

string ast_dispatch_asprint(ast_node* p_node) {
#define X(type, klass)                                                                                                 \
  case type:                                                                                                           \
    return klass##_asprint((klass*)p_node);
  switch (p_node->node_type) {
    AST_LIST_X(X);
  default:;
  }
  return create_string(NULL, 0, false);
#undef X
}
