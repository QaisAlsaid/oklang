#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okast.h"
#include "okutils.h"

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
  X(AST_CALL_EXPRESSION, ast_call_expression)                                                                          \
  X(AST_ASSIGN_EXPRESSION, ast_assign_expression)                                                                      \
  X(AST_COMPOUND_ASSIGN_EXPRESSION, ast_compound_assign_expression)                                                    \
  X(AST_CONDITIONAL_EXPRESSION, ast_conditional_expression)                                                            \
  X(AST_BOOLEAN_EXPRESSION, ast_boolean_expression)                                                                    \
  X(AST_NULL_EXPRESSION, ast_null_expression)                                                                          \
  X(AST_ACCESS_EXPRESSION, ast_access_expression)                                                                        \
  X(AST_THIS_EXPRESSION, ast_this_expression)                                                                            \
  X(AST_SUPER_EXPRESSION, ast_super_expression)                                                                    \
  X(AST_ARRAY_EXPRESSION, ast_array_expression)                                                                        \
  X(AST_MAP_EXPRESSION, ast_map_expression)                                                                            \
  X(AST_SUBSCRIPT_EXPRESSION, ast_subscript_expression)                                                                \
  X(AST_FUNCTION_EXPRESSION, ast_function_expression)                                                                  \
  X(AST_SPREAD_EXPRESSION, ast_spread_expression)                                                                      \
  X(AST_YIELD_EXPRESSION, ast_yield_expression)                                                                      \
  X(AST_EMPTY_STATEMENT, ast_empty_statement)                                                                          \
  X(AST_EXPRESSION_STATEMENT, ast_expression_statement)                                                                \
  X(AST_PRINT_STATEMENT, ast_print_statement)                                                                          \
  X(AST_COMPOUND_STATEMENT, ast_compound_statement)                                                                    \
  X(AST_IF_STATEMENT, ast_if_statement)                                                                                \
  X(AST_WHILE_STATEMENT, ast_while_statement)                                                                          \
  X(AST_FOR_STATEMENT, ast_for_statement)                                                                              \
  X(AST_FOR_IN_STATEMENT, ast_for_in_statement)                                                                        \
  X(AST_FOR_NUMERIC_STATEMENT, ast_for_numeric_statement)                                                              \
  X(AST_CONTROL_FLOW_STATEMENT, ast_control_flow_statement)                                                            \
  X(AST_RETURN_STATEMENT, ast_return_statement)                                                                        \
  X(AST_THROW_STATEMENT, ast_throw_statement)                                                                          \
  X(AST_TRY_STATEMENT, ast_try_statement)                                                                              \
  X(AST_DEFER_STATEMENT, ast_defer_statement)                                                                          \
  X(AST_IMPORT_STATEMENT, ast_import_statement)                                                                        \
  X(AST_EOF_STATEMENT, ast_eof_statement)                                                                              \
  X(AST_LET_DECLARATION, ast_let_declaration)                                                                          \
  X(AST_FUNCTION_DECLARATION, ast_function_declaration)                                                                \
  X(AST_CLASS_DECLARATION, ast_class_declaration)

void ast_node_init(ast_node* p_node, ast_node_type p_type, token p_token, ast_specs* p_specs) {
  p_node->node_type = p_type;
  p_node->token = p_token;
  p_node->alloc = p_specs->p_alloc;
}

void ast_node_deinit(ast_node* p_node) {
  (void)p_node;
}

bool ast_node_print(const ast_node* p_node) {
  (void)p_node;
  return false;
}

string ast_node_asprint(const ast_node* p_node) {
  (void)p_node;
  return create_static_string(NULL, 0);
}

void ast_expression_init(ast_expression* p_expression,
                         const ast_node_type p_type,
                         const token p_token,
                         ast_specs* p_specs) {
  ast_node_init(&p_expression->node, p_type, p_token, p_specs);
}

void ast_expression_deinit(ast_expression* p_expression) {
  (void)p_expression;
}

bool ast_expression_print(const ast_expression* p_expression) {
  (void)p_expression;
  return false;
}

string ast_expression_asprint(const ast_expression* p_expression) {
  (void)p_expression;
  return create_static_string(NULL, 0);
}

void ast_statement_init(ast_statement* p_statement,
                        const ast_node_type p_type,
                        const token p_token,
                        ast_specs* p_specs) {
  ast_node_init(&p_statement->node, p_type, p_token, p_specs);
}

void ast_statement_deinit(ast_statement* p_statement) {
  (void)p_statement;
}

bool ast_statement_print(const ast_statement* p_statement) {
  (void)p_statement;
  return false;
}

string ast_statement_asprint(const ast_statement* p_statement) {
  (void)p_statement;
  return create_static_string(NULL, 0);
}

string ast_binding_modifiers_asprint(ast_binding_modifiers_t p_modifiers) {
  if (p_modifiers == BINDING_NONE) {
    return create_static_string("", 0);
  }
  if ((p_modifiers & BINDING_MUT) != 0) {
    return create_static_string("mut", STRING_CALCULATE_LENGTH);
  }
  return create_static_string("unknown-binding-modifier", STRING_CALCULATE_LENGTH);
}

void ast_binding_init(ast_binding* p_binding,
                      ast_expression* p_lvalue,
                      ast_binding_modifiers_t p_modifiers,
                      const token p_token,
                      ast_specs* p_specs) {
  ast_node_init(&p_binding->node, AST_BINDING, p_token, p_specs);
  p_binding->lvalue = p_lvalue;
  p_binding->default_value = NULL; // set by the parser when a parameter has "= <expr>"
  p_binding->modifiers = p_modifiers;
  p_binding->is_varargs = false; // set by the parser on `rest...` parameters
}

void ast_binding_deinit(ast_binding* p_binding) {
  allocators* alloc = p_binding->node.alloc;
  if (p_binding->default_value != NULL) {
    ast_dispatch_deinit((ast_node*)p_binding->default_value);
    alloc->release(alloc, p_binding->default_value);
    p_binding->default_value = NULL;
  }
  ast_dispatch_deinit((ast_node*)p_binding->lvalue);
  alloc->release(alloc, p_binding->lvalue);
  p_binding->lvalue = NULL;
  p_binding->modifiers = BINDING_NONE;
  ast_node_deinit(&p_binding->node);
}

bool ast_binding_print(const ast_binding* p_binding) {
  bool res = printf("%s%s",
                    ast_binding_modifiers_asprint(p_binding->modifiers).chars,
                    p_binding->modifiers != BINDING_NONE ? " " : "") > 0;
  return res & ast_dispatch_print((ast_node*)p_binding->lvalue);
}

string ast_binding_asprint(const ast_binding* p_binding) {
  allocators* alloc = p_binding->node.alloc;
  string lvalue_str = ast_dispatch_asprint((ast_node*)p_binding->lvalue);
  if (lvalue_str.chars != NULL) {
    string result = asprint(alloc,
                            "%s%s%s",
                            lvalue_str,
                            p_binding->modifiers != BINDING_NONE ? " " : "",
                            ast_binding_modifiers_asprint(p_binding->modifiers).chars);
    string_deinit(&lvalue_str, alloc);
    return result;
  }
  return create_static_string(NULL, 0);
}

// doesn't preserve order.
string ast_declaration_modifiers_asprint(ast_declaration_modifiers_t p_modifiers, allocators* p_alloc) {
  if ((p_modifiers & DECLARATION_PUB) != 0) {
    return create_static_string("pub ", 4);
  }
  if ((p_modifiers & DECLARATION_STATIC) != 0) {
    return create_static_string("static ", 7);
  }
  return (p_modifiers & DECLARATION_NONE) != 0 ? create_static_string("", 0)
                                               : asprint(p_alloc,
                                                         "%s%s",
                                                         (p_modifiers & DECLARATION_GLOB) != 0 ? "glob " : "",
                                                         (p_modifiers & DECLARATION_EXPORT) != 0 ? "export" : "");
}

void ast_declaration_init(ast_declaration* p_declaration,

                          const ast_node_type p_type,
                          const token p_token,
                          ast_declaration_modifiers_t p_modifiers,
                          ast_specs* p_specs) {
  ast_statement_init(&p_declaration->statement, p_type, p_token, p_specs);
  p_declaration->modifiers = p_modifiers;
}

void ast_declaration_deinit(ast_declaration* p_declaration) {
  ast_statement_deinit(&p_declaration->statement);
  p_declaration->modifiers = DECLARATION_NONE;
}

bool ast_declaration_print(const ast_declaration* p_declaration) {
  string mods_str = ast_declaration_modifiers_asprint(p_declaration->modifiers, p_declaration->statement.node.alloc);
  if (mods_str.chars != NULL) {
    const bool status = printf("%s", mods_str.chars) > 0;
    string_deinit(&mods_str, p_declaration->statement.node.alloc);
    return status;
  }
  return false;
}

string ast_declaration_asprint(const ast_declaration* p_declaration) {
  return ast_declaration_modifiers_asprint(p_declaration->modifiers, p_declaration->statement.node.alloc);
}

void statements_list_deinit_element(ast_statement** p_elem, allocators* p_alloc) {
  ast_dispatch_deinit((ast_node*)*p_elem);
  p_alloc->release(p_alloc, *p_elem);
  *p_elem = NULL;
}

ARRAY_DEFINE_DEFAULT(ast_statements_list, ast_statement*, statements_list_deinit_element)

void ast_root_init(ast_root* p_root, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_root->statement, AST_ROOT, p_token, p_specs);
  ast_statements_list_init(&p_root->statements, p_specs->p_alloc);
}

void ast_root_deinit(ast_root* p_root) {
  ast_statements_list_deinit(&p_root->statements, p_root->statement.node.alloc);
  ast_statement_deinit(&p_root->statement);
}

bool ast_root_print(const ast_root* p_root) {
  bool status = true;
  for (uint32_t i = 0; i < p_root->statements.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_root->statements.data[i]);
  }
  return status;
}

string ast_root_asprint(const ast_root* p_root) {
  char* chars = NULL;
  size_t len = 0;
  char* delem = "\n";
  allocators* alloc = p_root->statement.node.alloc;
  for (uint32_t i = 0; i < p_root->statements.count; ++i) {
    string res = ast_dispatch_asprint((ast_node*)p_root->statements.data[i]);
    uint32_t reslen = string_get_length(&res);
    if (res.chars != NULL) {
      char* temp = alloc->allocate(alloc, len + reslen + 1);
      if (temp == NULL) {
        alloc->release(alloc, chars);
        string_deinit(&res, alloc);
        return create_static_string(NULL, 0);
      }
      memcpy(temp, chars, len);
      memcpy(temp + len, delem, 1);
      memcpy(temp + len + 1, res.chars, reslen);
      len += reslen + 1;
      alloc->release(alloc, chars);
      string_deinit(&res, alloc);
      chars = temp;
    }
  }
  return create_dynamic_string(chars, len, alloc);
}

void ast_identifier_expression_init(ast_identifier_expression* p_identifier, token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_identifier->expression, AST_IDENTIFIER_EXPRESSION, p_token, p_specs);
}

void ast_identifier_expression_deinit(ast_identifier_expression* p_identifier) {
  ast_expression_deinit(&p_identifier->expression);
}

ok_string_view ast_identifier_expression_get_value(const ast_identifier_expression* p_identifier) {
  const ast_node* node = &p_identifier->expression.node;
  return ok_create_string_view(node->token.start, node->token.length, false);
}

bool ast_identifier_expression_print(const ast_identifier_expression* p_identifier) {
  string ident = create_string_from_string_view(ast_identifier_expression_get_value(p_identifier),
                                                p_identifier->expression.node.alloc);
  if (ident.chars != NULL) {
    bool printf_status = printf("%s", ident.chars) > 0;
    string_deinit(&ident, p_identifier->expression.node.alloc);
    return printf_status;
  }
  return false;
}

string ast_identifier_expression_asprint(const ast_identifier_expression* p_identifier) {
  allocators* alloc = p_identifier->expression.node.alloc;
  string ident = create_string_from_string_view(ast_identifier_expression_get_value(p_identifier), alloc);
  if (ident.chars != NULL) {
    string str = asprint(alloc, "%s", ident.chars);
    string_deinit(&ident, alloc);
    return str;
  }
  return create_static_string(NULL, 0);
}

void ast_number_expression_init(ast_number_expression* p_number, token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_number->expression, AST_NUMBER_EXPRESSION, p_token, p_specs);
}

void ast_number_expression_deinit(ast_number_expression* p_number) {
  ast_expression_deinit(&p_number->expression);
}

double ast_number_expression_get_value(const ast_number_expression* p_number) {
  const ast_node* node = &p_number->expression.node;
  char buff[64];
  if (node->token.length >= sizeof(buff)) {
    return AST_NUMBER_EXPRESSION_PARSE_ERROR;
  }
  memcpy(buff, node->token.start, node->token.length);
  buff[node->token.length] = '\0';

  errno = 0;
  char* endptr;
  const double value = strtod(buff, &endptr);

  if (endptr == buff) {
    return AST_NUMBER_EXPRESSION_PARSE_ERROR;
  }

  if (*endptr != '\0') {
    return AST_NUMBER_EXPRESSION_PARSE_ERROR;
  }

  if (errno == ERANGE) {
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
    return create_static_string(NULL, 0);
  }
  return asprint(p_number->expression.node.alloc, "%f", value);
}

void ast_string_expression_init(ast_string_expression* p_string, token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_string->expression, AST_STRING_EXPRESSION, p_token, p_specs);
  p_string->unescaped = NULL;
  p_string->unescaped_length = 0;
}

void ast_string_expression_deinit(ast_string_expression* p_string) {
  if (p_string->unescaped != NULL) {
    p_string->expression.node.alloc->release(p_string->expression.node.alloc, p_string->unescaped);
    p_string->unescaped = NULL;
  }
  ast_expression_deinit(&p_string->expression);
}

ok_string_view ast_string_expression_get_value(const ast_string_expression* p_string) {
  const ast_node* node = &p_string->expression.node;
  if (p_string->unescaped != NULL) {
    return ok_create_string_view(p_string->unescaped, p_string->unescaped_length, false);
  }
  return ok_create_string_view(node->token.start + 1, node->token.length - 2, false);
}

bool ast_string_expression_print(const ast_string_expression* p_string) {
  string value =
      create_string_from_string_view(ast_string_expression_get_value(p_string), p_string->expression.node.alloc);
  if (value.chars == NULL) {
    return false;
  }
  const bool ret = printf("%s", value.chars) > 0;
  string_deinit(&value, p_string->expression.node.alloc);
  return ret;
}

string ast_string_expression_asprint(const ast_string_expression* p_string) {
  return create_string_from_string_view(ast_string_expression_get_value(p_string), p_string->expression.node.alloc);
}

void ast_boolean_expression_init(ast_boolean_expression* p_boolean, token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_boolean->expression, AST_BOOLEAN_EXPRESSION, p_token, p_specs);
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
  return asprint(p_boolean->expression.node.alloc, "%s", value ? "true" : "false");
}

void ast_null_expression_init(ast_null_expression* p_null, token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_null->expression, AST_NULL_EXPRESSION, p_token, p_specs);
}

void ast_null_expression_deinit(ast_null_expression* p_null) {
  ast_expression_deinit(&p_null->expression);
}

bool ast_null_expression_print(const ast_null_expression* p_null) {
  (void)p_null;
  return printf("null") > 0;
}

string ast_null_expression_asprint(const ast_null_expression* p_null) {
  return asprint(p_null->expression.node.alloc, "null");
}

void ast_this_expression_init(ast_this_expression* p_this, const token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_this->expression, AST_THIS_EXPRESSION, p_token, p_specs);
}

void ast_this_expression_deinit(ast_this_expression* p_this) {
  ast_expression_deinit(&p_this->expression);
}

bool ast_this_expression_print(const ast_this_expression* p_this) {
  (void)p_this;
  return printf("this") > 0;
}

string ast_this_expression_asprint(const ast_this_expression* p_this) {
  return asprint(p_this->expression.node.alloc, "this");
}

void ast_super_expression_init(ast_super_expression* p_super, const token p_token, ast_specs* p_specs) {
  ast_expression_init(&p_super->expression, AST_SUPER_EXPRESSION, p_token, p_specs);
}

void ast_super_expression_deinit(ast_super_expression* p_super) {
  ast_expression_deinit(&p_super->expression);
}

bool ast_super_expression_print(const ast_super_expression* p_super) {
  (void)p_super;
  return printf("super") > 0;
}

string ast_super_expression_asprint(const ast_super_expression* p_super) {
  return asprint(p_super->expression.node.alloc, "super");
}

void ast_access_expression_init(ast_access_expression* p_access,
                                const token p_token,
                                ast_expression* p_left,
                                const token p_name,
                                ast_specs* p_specs) {
  ast_expression_init(&p_access->expression, AST_ACCESS_EXPRESSION, p_token, p_specs);
  p_access->left = p_left;
  p_access->name = p_name;
  p_access->owned_name = NULL;
}

void ast_access_expression_set_owned_name(ast_access_expression* p_access, char* p_owned_name) {
  allocators* alloc = p_access->expression.node.alloc;
  if (p_access->owned_name != NULL) {
    alloc->release(alloc, p_access->owned_name);
  }
  p_access->owned_name = p_owned_name;
}

ok_string_view ast_access_expression_name(const ast_access_expression* p_access) {
  if (p_access->owned_name != NULL) {
    const uint32_t length = (uint32_t)strlen(p_access->owned_name);
    return ok_create_string_view(p_access->owned_name, length, false);
  }
  return ok_create_string_view(p_access->name.start, p_access->name.length, false);
}

void ast_access_expression_deinit(ast_access_expression* p_access) {
  allocators* alloc = p_access->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_access->left);
  alloc->release(alloc, p_access->left);
  p_access->left = NULL;
  if (p_access->owned_name != NULL) {
    alloc->release(alloc, p_access->owned_name);
    p_access->owned_name = NULL;
  }
  ast_expression_deinit(&p_access->expression);
}

bool ast_access_expression_print(const ast_access_expression* p_access) {
  bool status = ast_dispatch_print((ast_node*)p_access->left);
  status &= printf(".%.*s", p_access->name.length, p_access->name.start) > 0;
  return status;
}

string ast_access_expression_asprint(const ast_access_expression* p_access) {
  allocators* alloc = p_access->expression.node.alloc;
  string left_str = ast_dispatch_asprint((ast_node*)p_access->left);
  string ret = asprint(alloc, "%s.%.*s", left_str.chars, p_access->name.length, p_access->name.start);
  string_deinit(&left_str, alloc);
  return ret;
}

void ast_prefix_unary_expression_init(ast_prefix_unary_expression* p_prefix,
                                      token p_token,
                                      operator_type p_operator,
                                      ast_expression* p_right,
                                      ast_specs* p_specs) {
  ast_expression_init(&p_prefix->expression, AST_PREFIX_UNARY_EXPRESSION, p_token, p_specs);
  p_prefix->_operator = p_operator;
  p_prefix->right = p_right;
}

void ast_prefix_unary_expression_deinit(ast_prefix_unary_expression* p_prefix) {
  allocators* alloc = p_prefix->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_prefix->right);
  alloc->release(alloc, p_prefix->right);
  p_prefix->right = NULL;
  ast_expression_deinit(&p_prefix->expression);
}

bool ast_prefix_unary_expression_print(const ast_prefix_unary_expression* p_prefix) {
  return printf("%s", operator_type_to_string(p_prefix->_operator).chars) > 0 &&
         ast_dispatch_print((ast_node*)p_prefix->right);
}

string ast_prefix_unary_expression_asprint(const ast_prefix_unary_expression* p_prefix) {
  string right = ast_dispatch_asprint((ast_node*)p_prefix->right);
  if (right.chars == NULL) {
    return create_static_string(NULL, 0);
  }
  string ret =
      asprint(p_prefix->expression.node.alloc, "%s%s", operator_type_to_string(p_prefix->_operator).chars, right.chars);
  string_deinit(&right, p_prefix->expression.node.alloc);
  return ret;
}

void ast_postfix_unary_expression_init(ast_postfix_unary_expression* p_postfix,
                                       token p_token,
                                       operator_type p_operator,
                                       ast_expression* p_left,
                                       ast_specs* p_specs) {
  ast_expression_init(&p_postfix->expression, AST_POSTFIX_UNARY_EXPRESSION, p_token, p_specs);
  p_postfix->_operator = p_operator;
  p_postfix->left = p_left;
}

void ast_postfix_unary_expression_deinit(ast_postfix_unary_expression* p_postfix) {
  allocators* alloc = p_postfix->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_postfix->left);
  alloc->release(alloc, p_postfix->left);
  p_postfix->left = NULL;
  ast_expression_deinit(&p_postfix->expression);
}

bool ast_postfix_unary_expression_print(const ast_postfix_unary_expression* p_postfix) {
  return ast_dispatch_print((ast_node*)p_postfix->left) &&
         printf("%s", operator_type_to_string(p_postfix->_operator).chars);
}

string ast_postfix_unary_expression_asprint(const ast_postfix_unary_expression* p_postfix) {
  string left = ast_dispatch_asprint((ast_node*)p_postfix->left);
  if (left.chars == NULL) {
    return create_static_string(NULL, 0);
  }
  string ret = asprint(
      p_postfix->expression.node.alloc, "%s%s", left.chars, operator_type_to_string(p_postfix->_operator).chars);
  string_deinit(&left, p_postfix->expression.node.alloc);
  return ret;
}

void ast_infix_binary_expression_init(ast_infix_binary_expression* p_infix,
                                      token p_token,
                                      operator_type p_operator,
                                      ast_expression* p_left,
                                      ast_expression* p_right,
                                      ast_specs* p_specs) {
  ast_expression_init(&p_infix->expression, AST_INFIX_BINARY_EXPRESSION, p_token, p_specs);
  p_infix->_operator = p_operator;
  p_infix->left = p_left;
  p_infix->right = p_right;
}

void ast_infix_binary_expression_deinit(ast_infix_binary_expression* p_infix) {
  allocators* alloc = p_infix->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_infix->left);
  ast_dispatch_deinit((ast_node*)p_infix->right);
  alloc->release(alloc, p_infix->left);
  alloc->release(alloc, p_infix->right);
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
  allocators* alloc = p_infix->expression.node.alloc;
  string ret = create_static_string(NULL, 0);
  string left = ast_dispatch_asprint((ast_node*)p_infix->left);
  string right = ast_dispatch_asprint((ast_node*)p_infix->right);
  if (left.chars != NULL && right.chars != NULL) {
    ret = asprint(p_infix->expression.node.alloc,
                  "%s%s%s",
                  left.chars,
                  operator_type_to_string(p_infix->_operator).chars,
                  right.chars);
  }
  string_deinit(&left, alloc);
  string_deinit(&right, alloc);
  return ret;
}

void ast_assign_expression_init(ast_assign_expression* p_assign,
                                const token p_token,
                                ast_expression* p_left,
                                ast_expression* p_right,
                                ast_specs* p_specs) {
  ast_expression_init(&p_assign->expression, AST_ASSIGN_EXPRESSION, p_token, p_specs);
  p_assign->left = p_left;
  p_assign->right = p_right;
}

void ast_assign_expression_deinit(ast_assign_expression* p_assign) {
  allocators* alloc = p_assign->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_assign->left);
  ast_dispatch_deinit((ast_node*)p_assign->right);
  alloc->release(alloc, p_assign->left);
  alloc->release(alloc, p_assign->right);
  p_assign->left = NULL;
  p_assign->right = NULL;
  ast_expression_deinit(&p_assign->expression);
}

bool ast_assign_expression_print(const ast_assign_expression* p_assign) {
  bool ret = ast_dispatch_print((ast_node*)p_assign->left);
  ret &= printf(" = ") > 0;
  ret &= ast_dispatch_print((ast_node*)p_assign->right);
  return ret;
}

string ast_assign_expression_asprint(const ast_assign_expression* p_assign) {
  allocators* alloc = p_assign->expression.node.alloc;
  string ret = create_static_string(NULL, 0);
  string left_str = ast_dispatch_asprint((ast_node*)p_assign->left);
  string right_str = ast_dispatch_asprint((ast_node*)p_assign->right);
  if (left_str.chars != NULL && right_str.chars != NULL) {
    ret = asprint(p_assign->expression.node.alloc, "%s = %s", left_str.chars, right_str.chars);
  }
  string_deinit(&left_str, alloc);
  string_deinit(&right_str, alloc);
  return ret;
}

void ast_conditional_expression_init(ast_conditional_expression* p_conditional,
                                     const token p_token,
                                     ast_expression* p_condition,
                                     ast_expression* p_then_arm,
                                     ast_expression* p_otherwise_arm,
                                     ast_specs* p_specs) {
  ast_expression_init(&p_conditional->expression, AST_CONDITIONAL_EXPRESSION, p_token, p_specs);
  p_conditional->condition = p_condition;
  p_conditional->then_arm = p_then_arm;
  p_conditional->otherwise_arm = p_otherwise_arm;
}

void ast_conditional_expression_deinit(ast_conditional_expression* p_conditional) {
  allocators* alloc = p_conditional->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_conditional->condition);
  ast_dispatch_deinit((ast_node*)p_conditional->then_arm);
  ast_dispatch_deinit((ast_node*)p_conditional->otherwise_arm);
  alloc->release(alloc, p_conditional->condition);
  alloc->release(alloc, p_conditional->then_arm);
  alloc->release(alloc, p_conditional->otherwise_arm);
  p_conditional->condition = NULL;
  p_conditional->then_arm = NULL;
  p_conditional->otherwise_arm = NULL;
  ast_expression_deinit(&p_conditional->expression);
}

bool ast_conditional_expression_print(const ast_conditional_expression* p_conditional) {
  bool ret = ast_dispatch_print((ast_node*)p_conditional->condition);
  ret &= printf(" then ") > 0;
  ret &= ast_dispatch_print((ast_node*)p_conditional->then_arm);
  ret &= printf(" otherwise ") > 0;
  ret &= ast_dispatch_print((ast_node*)p_conditional->otherwise_arm);
  return ret;
}

string ast_conditional_expression_asprint(const ast_conditional_expression* p_conditional) {
  allocators* alloc = p_conditional->expression.node.alloc;
  string ret = create_static_string(NULL, 0);
  string condition_str = ast_dispatch_asprint((ast_node*)p_conditional->condition);
  string then_str = ast_dispatch_asprint((ast_node*)p_conditional->then_arm);
  string otherwise_str = ast_dispatch_asprint((ast_node*)p_conditional->otherwise_arm);
  if (condition_str.chars != NULL && then_str.chars != NULL && otherwise_str.chars != NULL) {
    ret = asprint(p_conditional->expression.node.alloc,
                  "%s then %s otherwise %s",
                  condition_str.chars,
                  then_str.chars,
                  otherwise_str.chars);
  }
  string_deinit(&condition_str, alloc);
  string_deinit(&then_str, alloc);
  string_deinit(&otherwise_str, alloc);
  return ret;
}

static void bindings_list_deinit_element(ast_binding** p_elem, allocators* p_alloc) {
  ast_binding_deinit(*p_elem);
  p_alloc->release(p_alloc, *p_elem);
  *p_elem = NULL;
}

ARRAY_DEFINE_DEFAULT(ast_bindings_list, ast_binding*, bindings_list_deinit_element)
#undef BINDINGS_LIST_DEINIT

static string ast_bindings_list_asprint(const ast_bindings_list* p_list, const ok_string_view p_delem) {
  char* chars = NULL;
  size_t len = 0;
  uint32_t delemlen = ok_string_view_get_length(&p_delem);
  allocators* alloc = p_list->alloc;
  for (uint32_t i = 0; i < p_list->count; ++i) {
    string res = ast_dispatch_asprint((ast_node*)p_list->data[i]);
    uint32_t reslen = string_get_length(&res);
    if (res.chars != NULL) {
      char* temp = p_list->alloc->allocate(p_list->alloc, len + reslen + delemlen);
      if (temp == NULL) {
        alloc->release(alloc, chars);
        string_deinit(&res, alloc);
        return create_static_string(NULL, 0);
      }
      memcpy(temp, chars, len);
      memcpy(temp + len, p_delem.chars, delemlen);
      memcpy(temp + len + delemlen, res.chars, reslen);
      len += reslen + delemlen;
      alloc->release(alloc, chars);
      string_deinit(&res, alloc);
      chars = temp;
    }
  }
  return create_dynamic_string(chars, len, p_list->alloc);
}

void ast_function_expression_init(ast_function_expression* p_function,
                                  const token p_token,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  ast_specs* p_specs) {
  ast_expression_init(&p_function->expression, AST_FUNCTION_EXPRESSION, p_token, p_specs);
  p_function->parameters = p_params;
  p_function->body = p_body;
}

void ast_function_expression_deinit(ast_function_expression* p_function) {
  ast_dispatch_deinit((ast_node*)p_function->body);
  ast_bindings_list_deinit(&p_function->parameters, p_function->expression.node.alloc);
  allocators* alloc = p_function->expression.node.alloc;
  alloc->release(alloc, p_function->body);
  p_function->body = NULL;
  ast_expression_deinit(&p_function->expression);
}

bool ast_function_expression_print(const ast_function_expression* p_function) {
  bool status = printf("fu ") > 0;
  status &= printf("(") > 0;
  for (uint32_t i = 0; i < p_function->parameters.count; ++i) {
    status &= ast_binding_print(p_function->parameters.data[i]);
  }
  status &= printf(")") > 0;
  status &= ast_dispatch_print((ast_node*)p_function->body);
  return status;
}

string ast_function_expression_asprint(const ast_function_expression* p_function) {
  allocators* alloc = p_function->expression.node.alloc;
  string params_str = ast_bindings_list_asprint(&p_function->parameters,
                                                ok_create_string_view(", ", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  string body_str = ast_dispatch_asprint((ast_node*)p_function->body);
  string ret = asprint(p_function->expression.node.alloc, "fu (%s) %s", params_str.chars, body_str.chars);
  string_deinit(&params_str, alloc);
  string_deinit(&body_str, alloc);
  return ret;
}

void expressions_list_deinit_element(ast_expression** p_elem, allocators* p_aloc) {
  ast_dispatch_deinit((ast_node*)*p_elem);
  p_aloc->release(p_aloc, *p_elem);
  *p_elem = NULL;
}

ARRAY_DEFINE_DEFAULT(ast_expressions_list, ast_expression*, expressions_list_deinit_element)
#undef EXPRESSIONS_LIST_DEINIT

string ast_expressions_list_asprint(const ast_expressions_list* p_list, const ok_string_view p_delem) {
  char* chars = NULL;
  size_t len = 0;
  uint32_t delemlen = ok_string_view_get_length(&p_delem);
  allocators* alloc = p_list->alloc;
  for (uint32_t i = 0; i < p_list->count; ++i) {
    string res = ast_dispatch_asprint((ast_node*)p_list->data[i]);
    uint32_t reslen = string_get_length(&res);
    if (res.chars != NULL) {
      char* temp = alloc->allocate(alloc, len + reslen + delemlen);
      if (temp == NULL) {
        alloc->release(alloc, chars);
        string_deinit(&res, alloc);
        return create_static_string(NULL, 0);
      }
      memcpy(temp, chars, len);
      memcpy(temp + len, p_delem.chars, delemlen);
      memcpy(temp + len + delemlen, res.chars, reslen);
      len += reslen + delemlen;
      alloc->release(alloc, chars);
      string_deinit(&res, alloc);
      chars = temp;
    }
  }
  return create_dynamic_string(chars, len, alloc);
}

void ast_call_expression_init(ast_call_expression* p_call,
                              const token p_token,
                              ast_expression* p_callable,
                              ast_expressions_list p_args,
                              ast_specs* p_specs) {
  ast_expression_init(&p_call->expression, AST_CALL_EXPRESSION, p_token, p_specs);
  p_call->callable = p_callable;
  p_call->arguments = p_args;
}

void ast_spread_expression_init(ast_spread_expression* p_spread,
                                const token p_token,
                                ast_expression* p_inner,
                                ast_specs* p_specs) {
  ast_expression_init(&p_spread->expression, AST_SPREAD_EXPRESSION, p_token, p_specs);
  p_spread->inner = p_inner;
}

void ast_spread_expression_deinit(ast_spread_expression* p_spread) {
  allocators* alloc = p_spread->expression.node.alloc;
  if (p_spread->inner != NULL) {
    ast_dispatch_deinit((ast_node*)p_spread->inner);
    alloc->release(alloc, p_spread->inner);
    p_spread->inner = NULL;
  }
  ast_expression_deinit(&p_spread->expression);
}

bool ast_spread_expression_print(const ast_spread_expression* p_spread) {
  bool res = ast_dispatch_print((ast_node*)p_spread->inner);
  res &= printf("...") > 0;
  return res;
}

void ast_yield_expression_init(ast_yield_expression* p_yield,
                               const token p_token,
                               ast_expression* p_value,
                               ast_specs* p_specs) {
  ast_expression_init(&p_yield->expression, AST_YIELD_EXPRESSION, p_token, p_specs);
  p_yield->value = p_value;
}

void ast_yield_expression_deinit(ast_yield_expression* p_yield) {
  allocators* alloc = p_yield->expression.node.alloc;
  if (p_yield->value != NULL) {
    ast_dispatch_deinit((ast_node*)p_yield->value);
    alloc->release(alloc, p_yield->value);
    p_yield->value = NULL;
  }
  ast_expression_deinit(&p_yield->expression);
}

bool ast_yield_expression_print(const ast_yield_expression* p_yield) {
  bool res = printf("yield ") > 0;
  res &= ast_dispatch_print((ast_node*)p_yield->value);
  return res;
}

string ast_yield_expression_asprint(const ast_yield_expression* p_yield) {
  allocators* alloc = p_yield->expression.node.alloc;
  string value_str = ast_dispatch_asprint((ast_node*)p_yield->value);
  string res = asprint(alloc, "yield %s", value_str.chars);
  string_deinit(&value_str, alloc);
  return res;
}

string ast_spread_expression_asprint(const ast_spread_expression* p_spread) {
  allocators* alloc = p_spread->expression.node.alloc;
  string inner = ast_dispatch_asprint((ast_node*)p_spread->inner);
  string res = asprint(alloc, "%s...", inner.chars);
  string_deinit(&inner, alloc);
  return res;
}

void ast_call_expression_deinit(ast_call_expression* p_call) {
  ast_expressions_list_deinit(&p_call->arguments, p_call->expression.node.alloc);
  ast_dispatch_deinit((ast_node*)p_call->callable);
  allocators* alloc = p_call->expression.node.alloc;
  alloc->release(alloc, p_call->callable);
  p_call->callable = NULL;
  ast_expression_deinit(&p_call->expression);
}

bool ast_call_expression_print(const ast_call_expression* p_call) {
  bool status = ast_dispatch_print((ast_node*)p_call->callable);
  status &= printf("(") > 0;
  for (uint32_t i = 0; i < p_call->arguments.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_call->arguments.data[i]);
  }
  status &= printf(")") > 0;
  return status;
}

string ast_call_expression_asprint(const ast_call_expression* p_call) {
  allocators* alloc = p_call->expression.node.alloc;
  string callable_str = ast_dispatch_asprint((ast_node*)p_call->callable);
  string args_str = ast_expressions_list_asprint(&p_call->arguments,
                                                 ok_create_string_view(", ", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  string ret = asprint(p_call->expression.node.alloc, "%s(%s)", callable_str.chars, args_str.chars);
  string_deinit(&callable_str, alloc);
  string_deinit(&args_str, alloc);
  return ret;
}

void ast_array_expression_init(ast_array_expression* p_array,
                               token p_token,
                               ast_expressions_list p_elements,
                               ast_specs* p_specs) {
  ast_expression_init(&p_array->expression, AST_ARRAY_EXPRESSION, p_token, p_specs);
  p_array->elements = p_elements;
}

void ast_array_expression_deinit(ast_array_expression* p_array) {
  ast_expressions_list_deinit(&p_array->elements, p_array->expression.node.alloc);
  ast_expression_deinit(&p_array->expression);
}

bool ast_array_expression_print(const ast_array_expression* p_array) {
  bool status = printf("[") > 0;
  for (uint32_t i = 0; i < p_array->elements.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_array->elements.data[i]);
    status &= printf(", ") > 0;
  }
  status &= printf("]") > 0;
  return status;
}

string ast_array_expression_asprint(const ast_array_expression* p_array) {
  allocators* alloc = p_array->expression.node.alloc;
  string elements_str = ast_expressions_list_asprint(
      &p_array->elements, ok_create_string_view(", ", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  string ret = asprint(alloc, "[%s]", elements_str.chars);
  string_deinit(&elements_str, alloc);
  return ret;
}

void ast_map_expression_init(ast_map_expression* p_map,
                             token p_token,
                             ast_expressions_list p_keys,
                             ast_expressions_list p_values,
                             ast_specs* p_specs) {
  ast_expression_init(&p_map->expression, AST_MAP_EXPRESSION, p_token, p_specs);
  p_map->keys = p_keys;
  p_map->values = p_values;
}

void ast_map_expression_deinit(ast_map_expression* p_map) {
  ast_expressions_list_deinit(&p_map->keys, p_map->expression.node.alloc);
  ast_expressions_list_deinit(&p_map->values, p_map->expression.node.alloc);
  ast_expression_deinit(&p_map->expression);
}

bool ast_map_expression_print(const ast_map_expression* p_map) {
  bool status = printf("{") > 0;
  for (uint32_t i = 0; i < p_map->keys.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_map->keys.data[i]);
    status &= printf(": ") > 0;
    status &= ast_dispatch_print((ast_node*)p_map->values.data[i]);
    if (i + 1 < p_map->keys.count) {
      status &= printf(", ") > 0;
    }
  }
  status &= printf("}") > 0;
  return status;
}

string ast_map_expression_asprint(const ast_map_expression* p_map) {
  allocators* alloc = p_map->expression.node.alloc;
  string ret = create_static_string("{:}", 3); // empty until entries are folded in
  if (p_map->keys.count == 0) {
    return ret;
  }
  string_deinit(&ret, alloc);
  ret = create_static_string("{", 1);
  for (uint32_t i = 0; i < p_map->keys.count; ++i) {
    string k = ast_dispatch_asprint((ast_node*)p_map->keys.data[i]);
    string v = ast_dispatch_asprint((ast_node*)p_map->values.data[i]);
    string joined = asprint(alloc, "%s%s%s: %s%s", ret.chars, i == 0 ? "" : ", ", k.chars, v.chars, "");
    string_deinit(&k, alloc);
    string_deinit(&v, alloc);
    string_deinit(&ret, alloc);
    ret = joined;
  }
  string closed = asprint(alloc, "%s}", ret.chars);
  string_deinit(&ret, alloc);
  return closed;
}

void ast_subscript_expression_init(ast_subscript_expression* p_subscript,
                                   token p_token,
                                   ast_expression* p_left,
                                   ast_expression* p_index,
                                   ast_specs* p_specs) {
  ast_expression_init(&p_subscript->expression, AST_SUBSCRIPT_EXPRESSION, p_token, p_specs);
  p_subscript->left = p_left;
  p_subscript->index = p_index;
}

void ast_subscript_expression_deinit(ast_subscript_expression* p_subscript) {
  allocators* alloc = p_subscript->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_subscript->left);
  alloc->release(alloc, p_subscript->left);
  p_subscript->left = NULL;
  ast_dispatch_deinit((ast_node*)p_subscript->index);
  alloc->release(alloc, p_subscript->index);
  p_subscript->index = NULL;
  ast_expression_deinit(&p_subscript->expression);
}

bool ast_subscript_expression_print(const ast_subscript_expression* p_subscript) {
  bool status = ast_dispatch_print((ast_node*)p_subscript->left);
  status &= printf("[") > 0;
  status &= ast_dispatch_print((ast_node*)p_subscript->index);
  status &= printf("]") > 0;
  return status;
}

string ast_subscript_expression_asprint(const ast_subscript_expression* p_subscript) {
  allocators* alloc = p_subscript->expression.node.alloc;
  string left_str = ast_dispatch_asprint((ast_node*)p_subscript->left);
  string index_str = ast_dispatch_asprint((ast_node*)p_subscript->index);
  string ret = asprint(alloc, "%s[%s]", left_str.chars, index_str.chars);
  string_deinit(&left_str, alloc);
  string_deinit(&index_str, alloc);
  return ret;
}

void ast_compound_assign_expression_init(ast_compound_assign_expression* p_compound,
                                         token p_token,
                                         const operator_type p_operator,
                                         ast_expression* p_left,
                                         ast_expression* p_right,
                                         ast_specs* p_specs) {
  ast_expression_init(&p_compound->expression, AST_COMPOUND_ASSIGN_EXPRESSION, p_token, p_specs);
  p_compound->_operator = p_operator;
  p_compound->left = p_left;
  p_compound->right = p_right;
}

void ast_compound_assign_expression_deinit(ast_compound_assign_expression* p_compound) {
  allocators* alloc = p_compound->expression.node.alloc;
  ast_dispatch_deinit((ast_node*)p_compound->left);
  alloc->release(alloc, p_compound->left);
  p_compound->left = NULL;
  ast_dispatch_deinit((ast_node*)p_compound->right);
  alloc->release(alloc, p_compound->right);
  p_compound->right = NULL;
  ast_expression_deinit(&p_compound->expression);
}

bool ast_compound_assign_expression_print(const ast_compound_assign_expression* p_compound) {
  bool status = ast_dispatch_print((ast_node*)p_compound->left);
  status &= printf(" ?= ") > 0;
  status &= ast_dispatch_print((ast_node*)p_compound->right);
  return status;
}

string ast_compound_assign_expression_asprint(const ast_compound_assign_expression* p_compound) {
  allocators* alloc = p_compound->expression.node.alloc;
  string left_str = ast_dispatch_asprint((ast_node*)p_compound->left);
  string right_str = ast_dispatch_asprint((ast_node*)p_compound->right);
  string ret = asprint(alloc, "%s ?= %s", left_str.chars, right_str.chars);
  string_deinit(&left_str, alloc);
  string_deinit(&right_str, alloc);
  return ret;
}

void ast_eof_statement_init(ast_eof_statement* p_eof, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_eof->statement, AST_EOF_STATEMENT, p_token, p_specs);
}

void ast_eof_statement_deinit(ast_eof_statement* p_eof) {
  ast_statement_deinit(&p_eof->statement);
}

bool ast_eof_statement_print(const ast_eof_statement* p_eof) {
  (void)p_eof;
  return true;
}

string ast_eof_statement_asprint(const ast_eof_statement* p_eof) {
  (void)p_eof;
  return create_static_string("", 0);
}

void ast_empty_statement_init(ast_empty_statement* p_empty, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_empty->statement, AST_EMPTY_STATEMENT, p_token, p_specs);
}

void ast_empty_statement_deinit(ast_empty_statement* p_empty) {
  ast_statement_deinit(&p_empty->statement);
}

bool ast_empty_statement_print(const ast_empty_statement* p_empty) {
  (void)p_empty;
  return true;
}

string ast_empty_statement_asprint(const ast_empty_statement* p_empty) {
  (void)p_empty;
  return create_static_string("", 0);
}

void ast_expression_statement_init(ast_expression_statement* p_expression_statement,
                                   token p_token,
                                   ast_expression* p_expression,
                                   ast_specs* p_specs) {
  ast_statement_init(&p_expression_statement->statement, AST_EXPRESSION_STATEMENT, p_token, p_specs);
  p_expression_statement->expression = p_expression;
}

void ast_expression_statement_deinit(ast_expression_statement* p_expression_statement) {
  ast_dispatch_deinit((ast_node*)p_expression_statement->expression);
  allocators* alloc = p_expression_statement->statement.node.alloc;
  alloc->release(alloc, p_expression_statement->expression);
  p_expression_statement->expression = NULL;
  ast_statement_deinit(&p_expression_statement->statement);
}

bool ast_expression_statement_print(const ast_expression_statement* p_expression_statement) {
  return ast_dispatch_print((ast_node*)p_expression_statement->expression);
}

string ast_expression_statement_asprint(const ast_expression_statement* p_expression_statement) {
  return ast_dispatch_asprint((ast_node*)p_expression_statement->expression);
}

void ast_print_statement_init(ast_print_statement* p_print,
                              const token p_token,
                              ast_expression* p_expression,
                              ast_specs* p_specs) {
  ast_statement_init(&p_print->statement, AST_PRINT_STATEMENT, p_token, p_specs);
  p_print->expression = p_expression;
}

void ast_print_statement_deinit(ast_print_statement* p_print) {
  allocators* alloc = p_print->statement.node.alloc;
  ast_dispatch_deinit((ast_node*)p_print->expression);
  alloc->release(alloc, p_print->expression);
  p_print->expression = NULL;
}

bool ast_print_statement_print(const ast_print_statement* p_print) {
  bool status = printf("print") > 0;
  status &= ast_dispatch_print((ast_node*)p_print->expression);
  return status;
}

string ast_print_statement_asprint(const ast_print_statement* p_print) {
  allocators* alloc = p_print->statement.node.alloc;
  string expr_str = ast_dispatch_asprint((ast_node*)p_print->expression);
  string result = asprint(alloc, "print %s", expr_str.chars);
  string_deinit(&expr_str, alloc);
  return result;
}

void ast_compound_statement_init(ast_compound_statement* p_compound, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_compound->statement, AST_COMPOUND_STATEMENT, p_token, p_specs);
  ast_statements_list_init(&p_compound->statements, p_specs->p_alloc);
}

void ast_compound_statement_deinit(ast_compound_statement* p_compound) {
  ast_statements_list_deinit(&p_compound->statements, p_compound->statement.node.alloc);
  ast_statement_deinit(&p_compound->statement);
}

bool ast_compound_statement_print(const ast_compound_statement* p_compound) {
  bool status = true;
  for (uint32_t i = 0; i < p_compound->statements.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_compound->statements.data[i]);
  }
  return status;
}

string ast_compound_statement_asprint(const ast_compound_statement* p_compound) {
  char* chars = NULL;
  size_t len = 0;
  allocators* alloc = p_compound->statement.node.alloc;
  for (uint32_t i = 0; i < p_compound->statements.count; ++i) {
    string res = ast_dispatch_asprint((ast_node*)p_compound->statements.data[i]);
    if (res.chars != NULL) {
      len += string_get_length(&res);
      char* temp = alloc->allocate(alloc, len);
      alloc->release(alloc, chars);
      if (temp == NULL) {
        string_deinit(&res, alloc);
        return create_static_string("", 0);
      }
      chars = temp;
    }
  }
  return create_dynamic_string(chars, len, alloc);
}

void ast_if_statement_init(ast_if_statement* p_if, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_if->statement, AST_IF_STATEMENT, p_token, p_specs);
  p_if->condition = NULL;
  p_if->consequence = NULL;
  p_if->alternative = NULL;
}

void ast_if_statement_deinit(ast_if_statement* p_if) {
  allocators* alloc = p_if->statement.node.alloc;
  if (p_if->alternative) {
    ast_dispatch_deinit((ast_node*)p_if->alternative);
    alloc->release(alloc, p_if->alternative);
  }
  ast_dispatch_deinit((ast_node*)p_if->consequence);
  ast_dispatch_deinit((ast_node*)p_if->condition);
  ast_statement_deinit(&p_if->statement);
  alloc->release(alloc, p_if->consequence);
  alloc->release(alloc, p_if->condition);
  p_if->alternative = NULL;
  p_if->consequence = NULL;
  p_if->condition = NULL;
}

bool ast_if_statement_print(const ast_if_statement* p_if) {
  bool res = printf("if ") > 0;
  res &= ast_dispatch_print((ast_node*)p_if->condition);
  res &= ast_dispatch_print((ast_node*)p_if->consequence);
  if (p_if->alternative != NULL) {
    res &= printf("else ");
    res &= ast_dispatch_print((ast_node*)p_if->alternative);
  }
  return res;
}

string ast_if_statement_asprint(const ast_if_statement* p_if) {
  allocators* alloc = p_if->statement.node.alloc;
  string cond_str = ast_dispatch_asprint((ast_node*)p_if->condition);
  string cons_str = ast_dispatch_asprint((ast_node*)p_if->consequence);
  string res;
  if (p_if->alternative != NULL) {
    string alt_str = ast_dispatch_asprint((ast_node*)p_if->alternative);
    res = asprint(alloc, "if %s %s else %s", cond_str.chars, cons_str.chars, alt_str.chars);
    string_deinit(&alt_str, alloc);
  } else {
    res = asprint(alloc, "if %s %s", cond_str.chars, cons_str.chars);
  }
  string_deinit(&cond_str, alloc);
  string_deinit(&cons_str, alloc);
  return res;
}

void ast_while_statement_init(ast_while_statement* p_while, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_while->statement, AST_WHILE_STATEMENT, p_token, p_specs);
  p_while->condition = NULL;
  p_while->body = NULL;
}

void ast_while_statement_deinit(ast_while_statement* p_while) {
  allocators* alloc = p_while->statement.node.alloc;
  ast_dispatch_deinit((ast_node*)p_while->condition);
  ast_dispatch_deinit((ast_node*)p_while->body);
  ast_statement_deinit(&p_while->statement);
  alloc->release(alloc, p_while->condition);
  alloc->release(alloc, p_while->body);
  p_while->condition = NULL;
  p_while->body = NULL;
}

bool ast_while_statement_print(const ast_while_statement* p_while) {
  bool res = printf("while ") > 0;
  res &= ast_dispatch_print((ast_node*)p_while->condition);
  res &= ast_dispatch_print((ast_node*)p_while->body);
  return res;
}

string ast_while_statement_asprint(const ast_while_statement* p_while) {
  allocators* alloc = p_while->statement.node.alloc;
  string cond_str = ast_dispatch_asprint((ast_node*)p_while->condition);
  string body_str = ast_dispatch_asprint((ast_node*)p_while->body);
  string res = asprint(alloc, "while %s? %s", cond_str.chars, body_str.chars);
  string_deinit(&cond_str, alloc);
  string_deinit(&body_str, alloc);
  return res;
}

void ast_for_statement_init(ast_for_statement* p_for, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_for->statement, AST_FOR_STATEMENT, p_token, p_specs);
  p_for->initializer = NULL;
  p_for->condition = NULL;
  p_for->update = NULL;
  p_for->body = NULL;
}

void ast_for_in_statement_init(ast_for_in_statement* p_for_in, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_for_in->statement, AST_FOR_IN_STATEMENT, p_token, p_specs);
  p_for_in->binding = NULL;
  p_for_in->value_binding = NULL;
  p_for_in->iterable = NULL;
  p_for_in->body = NULL;
}

void ast_for_in_statement_deinit(ast_for_in_statement* p_for_in) {
  allocators* alloc = p_for_in->statement.node.alloc;
  if (p_for_in->binding != NULL) {
    ast_binding_deinit(p_for_in->binding);
    alloc->release(alloc, p_for_in->binding);
    p_for_in->binding = NULL;
  }
  if (p_for_in->value_binding != NULL) {
    ast_binding_deinit(p_for_in->value_binding);
    alloc->release(alloc, p_for_in->value_binding);
    p_for_in->value_binding = NULL;
  }
  if (p_for_in->iterable != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_in->iterable);
    alloc->release(alloc, p_for_in->iterable);
    p_for_in->iterable = NULL;
  }
  if (p_for_in->body != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_in->body);
    alloc->release(alloc, p_for_in->body);
    p_for_in->body = NULL;
  }
  ast_statement_deinit(&p_for_in->statement);
}

bool ast_for_in_statement_print(const ast_for_in_statement* p_for_in) {
  bool status = ast_dispatch_print((ast_node*)p_for_in->iterable);
  status &= ast_dispatch_print((ast_node*)p_for_in->body);
  return status;
}

string ast_for_in_statement_asprint(const ast_for_in_statement* p_for_in) {
  (void)p_for_in;
  return create_static_string("for in", STRING_CALCULATE_LENGTH);
}

void ast_for_numeric_statement_init(ast_for_numeric_statement* p_for_numeric, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_for_numeric->statement, AST_FOR_NUMERIC_STATEMENT, p_token, p_specs);
  p_for_numeric->binding = NULL;
  p_for_numeric->start = NULL;
  p_for_numeric->limit = NULL;
  p_for_numeric->step = NULL;
  p_for_numeric->body = NULL;
}

void ast_for_numeric_statement_deinit(ast_for_numeric_statement* p_for_numeric) {
  allocators* alloc = p_for_numeric->statement.node.alloc;
  if (p_for_numeric->binding != NULL) {
    ast_binding_deinit(p_for_numeric->binding);
    alloc->release(alloc, p_for_numeric->binding);
    p_for_numeric->binding = NULL;
  }
  if (p_for_numeric->start != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_numeric->start);
    alloc->release(alloc, p_for_numeric->start);
    p_for_numeric->start = NULL;
  }
  if (p_for_numeric->limit != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_numeric->limit);
    alloc->release(alloc, p_for_numeric->limit);
    p_for_numeric->limit = NULL;
  }
  if (p_for_numeric->step != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_numeric->step);
    alloc->release(alloc, p_for_numeric->step);
    p_for_numeric->step = NULL;
  }
  if (p_for_numeric->body != NULL) {
    ast_dispatch_deinit((ast_node*)p_for_numeric->body);
    alloc->release(alloc, p_for_numeric->body);
    p_for_numeric->body = NULL;
  }
  ast_statement_deinit(&p_for_numeric->statement);
}

bool ast_for_numeric_statement_print(const ast_for_numeric_statement* p_for_numeric) {
  bool status = ast_dispatch_print((ast_node*)p_for_numeric->start);
  status &= ast_dispatch_print((ast_node*)p_for_numeric->limit);
  status &= ast_dispatch_print((ast_node*)p_for_numeric->body);
  return status;
}

string ast_for_numeric_statement_asprint(const ast_for_numeric_statement* p_for_numeric) {
  (void)p_for_numeric;
  return create_static_string("for numeric", STRING_CALCULATE_LENGTH);
}

void ast_for_statement_deinit(ast_for_statement* p_for) {
  allocators* alloc = p_for->statement.node.alloc;
  if (p_for->initializer != NULL) {
    ast_dispatch_deinit((ast_node*)p_for->initializer);
    alloc->release(alloc, p_for->initializer);
    p_for->initializer = NULL;
  }
  if (p_for->condition != NULL) {
    ast_dispatch_deinit((ast_node*)p_for->condition);
    alloc->release(alloc, p_for->condition);
    p_for->condition = NULL;
  }
  if (p_for->update != NULL) {
    ast_dispatch_deinit((ast_node*)p_for->update);
    alloc->release(alloc, p_for->update);
    p_for->update = NULL;
  }
  ast_dispatch_deinit((ast_node*)p_for->body);
  alloc->release(alloc, p_for->body);
  p_for->body = NULL;
  ast_statement_deinit(&p_for->statement);
}

bool ast_for_statement_print(const ast_for_statement* p_for) {
  bool res = printf("for ") > 0;
  if (p_for->initializer != NULL) {
    res &= ast_dispatch_print((ast_node*)p_for->initializer);
    printf(";");
  }
  if (p_for->condition != NULL) {
    res &= ast_dispatch_print((ast_node*)p_for->condition);
    printf(";");
  }
  if (p_for->update != NULL) {
    res &= ast_dispatch_print((ast_node*)p_for->update);
    printf(";");
  }
  res &= ast_dispatch_print((ast_node*)p_for->body);
  return res;
}

string ast_for_statement_asprint(const ast_for_statement* p_for) {
  allocators* alloc = p_for->statement.node.alloc;
  string init_str = create_static_string("", 0);
  string cond_str = create_static_string("", 0);
  string inc_str = create_static_string("", 0);
  if (p_for->initializer != NULL) {
    init_str = ast_dispatch_asprint((ast_node*)p_for->initializer);
  }
  if (p_for->condition != NULL) {
    cond_str = ast_dispatch_asprint((ast_node*)p_for->condition);
  }
  if (p_for->update != NULL) {
    inc_str = ast_dispatch_asprint((ast_node*)p_for->update);
  }
  string body_str = ast_dispatch_asprint((ast_node*)p_for->body);
  string res = asprint(alloc, "for %s;%s;%s? %s", init_str.chars, cond_str.chars, inc_str.chars, body_str.chars);
  string_deinit(&init_str, alloc);
  string_deinit(&cond_str, alloc);
  string_deinit(&inc_str, alloc);
  string_deinit(&body_str, alloc);
  return res;
}

string control_flow_type_asprint(control_flow_type p_type) {
  switch (p_type) {
  case CONTROL_FLOW_BREAK:
    return create_static_string("break", STRING_CALCULATE_LENGTH);
  case CONTROL_FLOW_CONTINUE:
    return create_static_string("continue", STRING_CALCULATE_LENGTH);
  default:
    return create_static_string("", 0);
  }
}

void ast_control_flow_statement_init(ast_control_flow_statement* p_control_flow, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_control_flow->statement, AST_CONTROL_FLOW_STATEMENT, p_token, p_specs);
  p_control_flow->type = CONTROL_FLOW_NONE;
}

void ast_control_flow_statement_deinit(ast_control_flow_statement* p_control_flow) {
  ast_statement_deinit(&p_control_flow->statement);
  p_control_flow->type = CONTROL_FLOW_NONE;
}

bool ast_control_flow_statement_print(const ast_control_flow_statement* p_control_flow) {
  return printf("%s", control_flow_type_asprint(p_control_flow->type).chars) > 0;
}

string ast_control_flow_statement_asprint(const ast_control_flow_statement* p_control_flow) {
  return control_flow_type_asprint(p_control_flow->type);
}

void ast_return_statement_init(ast_return_statement* p_return,
                               ast_expressions_list p_returned,
                               token p_token,
                               ast_specs* p_specs) {
  ast_statement_init(&p_return->statement, AST_RETURN_STATEMENT, p_token, p_specs);
  p_return->returned = p_returned;
}

void ast_return_statement_deinit(ast_return_statement* p_return) {
  allocators* alloc = p_return->statement.node.alloc;
  ast_expressions_list_deinit(&p_return->returned, alloc);
  ast_statement_deinit(&p_return->statement);
}

bool ast_return_statement_print(const ast_return_statement* p_return) {
  bool res = printf("return ") > 0;
  for (uint32_t i = 0; i < p_return->returned.count; ++i) {
    res &= ast_dispatch_print((ast_node*)p_return->returned.data[i]);
    if (i + 1 < p_return->returned.count) {
      res &= printf(", ") > 0;
    }
  }
  return res;
}

string ast_return_statement_asprint(const ast_return_statement* p_return) {
  allocators* alloc = p_return->statement.node.alloc;
  if (p_return->returned.count == 0) {
    return create_static_string("return", STRING_CALCULATE_LENGTH);
  }
  string res = create_static_string("return ", STRING_CALCULATE_LENGTH);
  for (uint32_t i = 0; i < p_return->returned.count; ++i) {
    string part = ast_dispatch_asprint((ast_node*)p_return->returned.data[i]);
    string joined = asprint(alloc, "%s%s%s", res.chars, part.chars, i + 1 < p_return->returned.count ? ", " : "");
    string_deinit(&part, alloc);
    string_deinit(&res, alloc); // no-op for the leading static string
    res = joined;
  }
  return res;
}

void ast_throw_statement_init(ast_throw_statement* p_throw,
                              ast_expression* p_expression,
                              token p_token,
                              ast_specs* p_specs) {
  ast_statement_init(&p_throw->statement, AST_THROW_STATEMENT, p_token, p_specs);
  p_throw->expression = p_expression;
}

void ast_throw_statement_deinit(ast_throw_statement* p_throw) {
  allocators* alloc = p_throw->statement.node.alloc;
  if (p_throw->expression != NULL) {
    ast_dispatch_deinit((ast_node*)p_throw->expression);
    alloc->release(alloc, p_throw->expression);
    p_throw->expression = NULL;
  }
  ast_statement_deinit(&p_throw->statement);
}

bool ast_throw_statement_print(const ast_throw_statement* p_throw) {
  bool res = printf("throw") > 0;
  if (p_throw->expression != NULL) {
    res &= printf(" ") > 0;
    res &= ast_dispatch_print((ast_node*)p_throw->expression);
  }
  return res;
}

string ast_throw_statement_asprint(const ast_throw_statement* p_throw) {
  if (p_throw->expression == NULL) {
    return create_static_string("throw", STRING_CALCULATE_LENGTH);
  }
  allocators* alloc = p_throw->statement.node.alloc;
  string part = ast_dispatch_asprint((ast_node*)p_throw->expression);
  string res = asprint(alloc, "throw %s", part.chars);
  string_deinit(&part, alloc);
  return res;
}

void ast_try_statement_init(ast_try_statement* p_try, token p_token, ast_specs* p_specs) {
  ast_statement_init(&p_try->statement, AST_TRY_STATEMENT, p_token, p_specs);
  p_try->body = NULL;
  p_try->catch_binding = NULL;
  p_try->catch_body = NULL;
}

void ast_try_statement_deinit(ast_try_statement* p_try) {
  allocators* alloc = p_try->statement.node.alloc;
  if (p_try->catch_body != NULL) {
    ast_dispatch_deinit((ast_node*)p_try->catch_body);
    alloc->release(alloc, p_try->catch_body);
    p_try->catch_body = NULL;
  }
  if (p_try->catch_binding != NULL) {
    ast_binding_deinit(p_try->catch_binding);
    alloc->release(alloc, p_try->catch_binding);
    p_try->catch_binding = NULL;
  }
  ast_dispatch_deinit((ast_node*)p_try->body);
  alloc->release(alloc, p_try->body);
  p_try->body = NULL;
  ast_statement_deinit(&p_try->statement);
}

bool ast_try_statement_print(const ast_try_statement* p_try) {
  bool res = printf("try ") > 0;
  res &= ast_dispatch_print((ast_node*)p_try->body);
  if (p_try->catch_body != NULL) {
    res &= printf(" catch ") > 0;
    res &= ast_dispatch_print((ast_node*)p_try->catch_body);
  }
  return res;
}

string ast_try_statement_asprint(const ast_try_statement* p_try) {
  allocators* alloc = p_try->statement.node.alloc;
  string body = ast_dispatch_asprint((ast_node*)p_try->body);
  string res = asprint(alloc, "try %s", body.chars);
  string_deinit(&body, alloc);
  if (p_try->catch_body != NULL) {
    string part = ast_dispatch_asprint((ast_node*)p_try->catch_body);
    string joined = asprint(alloc, "%s catch %s", res.chars, part.chars);
    string_deinit(&part, alloc);
    string_deinit(&res, alloc);
    res = joined;
  }
  return res;
}

void ast_defer_statement_init(ast_defer_statement* p_defer,
                              token p_token,
                              ast_call_expression* p_call,
                              ast_function_expression* p_thunk,
                              char* p_names,
                              ok_string_view p_slot_name,
                              bool p_is_err,
                              ast_specs* p_specs) {
  ast_statement_init(&p_defer->statement, AST_DEFER_STATEMENT, p_token, p_specs);
  p_defer->call = p_call;
  p_defer->thunk = p_thunk;
  p_defer->names = p_names;
  p_defer->slot_name = p_slot_name;
  p_defer->is_err = p_is_err;
}

void ast_defer_statement_deinit(ast_defer_statement* p_defer) {
  allocators* alloc = p_defer->statement.node.alloc;
  if (p_defer->thunk != NULL) {
    ast_dispatch_deinit((ast_node*)p_defer->thunk);
    alloc->release(alloc, p_defer->thunk);
    p_defer->thunk = NULL;
  }
  if (p_defer->call != NULL) {
    ast_dispatch_deinit((ast_node*)p_defer->call);
    alloc->release(alloc, p_defer->call);
    p_defer->call = NULL;
  }
  if (p_defer->names != NULL) {
    alloc->release(alloc, p_defer->names);
    p_defer->names = NULL;
  }
  ast_statement_deinit(&p_defer->statement);
}

bool ast_defer_statement_print(const ast_defer_statement* p_defer) {
  bool res = printf(p_defer->is_err ? "deferr " : "defer ") > 0;
  res &= ast_dispatch_print((ast_node*)p_defer->call);
  return res;
}

string ast_defer_statement_asprint(const ast_defer_statement* p_defer) {
  allocators* alloc = p_defer->statement.node.alloc;
  string call = ast_dispatch_asprint((ast_node*)p_defer->call);
  string res = asprint(alloc, "%s %s", p_defer->is_err ? "deferr" : "defer", call.chars);
  string_deinit(&call, alloc);
  return res;
}

ARRAY_DEFINE_DEFAULT(ast_import_specifier_list, ast_import_specifier, ARRAY_DEFAULT_TYPE_DEINIT)

void ast_import_statement_init(ast_import_statement* p_import,
                               token p_token,
                               token p_path,
                               ast_import_specifier_list p_symbols,
                               bool p_has_alias,
                               token p_alias,
                               ast_specs* p_specs) {
  ast_statement_init(&p_import->statement, AST_IMPORT_STATEMENT, p_token, p_specs);
  p_import->path = p_path;
  p_import->has_alias = p_has_alias;
  p_import->alias = p_alias;
  p_import->symbols = p_symbols;
}

void ast_import_statement_deinit(ast_import_statement* p_import) {
  allocators* alloc = p_import->statement.node.alloc;
  ast_import_specifier_list_deinit(&p_import->symbols, alloc);
  ast_statement_deinit(&p_import->statement);
}

bool ast_import_statement_print(const ast_import_statement* p_import) {
  (void)p_import;
  return printf("import") > 0;
}

string ast_import_statement_asprint(const ast_import_statement* p_import) {
  allocators* alloc = p_import->statement.node.alloc;
  const ok_string_view path = ast_import_statement_path(p_import);
  string res = asprint(alloc, "import \"%.*s\"", ok_string_view_get_length(&path), path.chars);
  if (res.chars == NULL) {
    return res;
  }
  if (p_import->has_alias) {
    string joined = asprint(alloc, "%s as %.*s", res.chars, p_import->alias.length, p_import->alias.start);
    string_deinit(&res, alloc);
    res = joined;
  }
  for (uint32_t i = 0; i < p_import->symbols.count && res.chars != NULL; ++i) {
    const ast_import_specifier* spec = &p_import->symbols.data[i];
    const bool has_alias = spec->alias.type == TOKEN_IDENTIFIER;
    string part = asprint(alloc,
                          "%s %.*s%s%.*s",
                          res.chars,
                          spec->name.length,
                          spec->name.start,
                          has_alias ? " as " : "",
                          has_alias ? spec->alias.length : 0,
                          has_alias ? spec->alias.start : "");
    string_deinit(&res, alloc);
    res = part;
  }
  return res;
}

ok_string_view ast_import_statement_path(const ast_import_statement* p_import) {
  return ok_create_string_view(p_import->path.start + 1, p_import->path.length - 2, false);
}

void ast_let_declaration_init(ast_let_declaration* p_let,
                              ast_binding* p_binding,
                              ast_expression* p_value,
                              ast_declaration_modifiers_t p_modifiers,
                              const token p_token,
                              ast_specs* p_specs) {
  ast_declaration_init(&p_let->declaration, AST_LET_DECLARATION, p_token, p_modifiers, p_specs);
  p_let->binding = p_binding;
  p_let->value = p_value;
  ast_bindings_list_init(&p_let->span_bindings, p_specs->p_alloc);
}

// the destructuring form: bindings come pre-parsed (paren list), the value
// must be a call expression (the compiler checks that, not the parser).
void ast_let_declaration_init_span(ast_let_declaration* p_let,
                                   ast_bindings_list p_bindings,
                                   ast_expression* p_value,
                                   ast_declaration_modifiers_t p_modifiers,
                                   const token p_token,
                                   ast_specs* p_specs) {
  ast_declaration_init(&p_let->declaration, AST_LET_DECLARATION, p_token, p_modifiers, p_specs);
  p_let->binding = NULL;
  p_let->value = p_value;
  p_let->span_bindings = p_bindings;
}

void ast_let_declaration_deinit(ast_let_declaration* p_let) {
  allocators* alloc = p_let->declaration.statement.node.alloc;
  // binding is NULL in the span form (the span_bindings own the binders)
  if (p_let->binding != NULL) {
    ast_dispatch_deinit((ast_node*)p_let->binding);
    alloc->release(alloc, p_let->binding);
    p_let->binding = NULL;
  }
  if (p_let->value != NULL) {
    ast_dispatch_deinit((ast_node*)p_let->value);
    alloc->release(alloc, p_let->value);
    p_let->value = NULL;
  }
  ast_bindings_list_deinit(&p_let->span_bindings, alloc);
  ast_declaration_deinit(&p_let->declaration);
}

bool ast_let_declaration_print(const ast_let_declaration* p_let) {
  bool fail = ast_declaration_print(&p_let->declaration);
  fail &= printf("let ");
  fail &= printf(" ");
  if (p_let->span_bindings.count > 0) {
    fail &= printf("(") > 0;
    for (uint32_t i = 0; i < p_let->span_bindings.count; ++i) {
      fail &= ast_binding_print(p_let->span_bindings.data[i]);
      if (i + 1 < p_let->span_bindings.count) {
        fail &= printf(", ") > 0;
      }
    }
    fail &= printf(")") > 0;
  } else {
    fail &= ast_binding_print(p_let->binding);
  }
  fail &= printf(" ");
  return fail & ast_dispatch_print((ast_node*)p_let->value);
}

string ast_let_declaration_asprint(const ast_let_declaration* p_let) {
  allocators* alloc = p_let->declaration.statement.node.alloc;
  string decl_str = ast_declaration_asprint(&p_let->declaration);
  // the span form renders its binding list parenthesized: `let (x, y) = f()`
  string bindings_str = create_static_string("(", 1);
  if (p_let->span_bindings.count > 0) {
    for (uint32_t i = 0; i < p_let->span_bindings.count; ++i) {
      string part = ast_binding_asprint(p_let->span_bindings.data[i]);
      string joined =
          asprint(alloc, "%s%s%s", bindings_str.chars, part.chars, i + 1 < p_let->span_bindings.count ? ", " : ")");
      string_deinit(&part, alloc);
      string_deinit(&bindings_str, alloc); // no-op for the leading static string
      bindings_str = joined;
    }
  } else {
    string_deinit(&bindings_str, alloc);
    bindings_str = ast_binding_asprint(p_let->binding);
  }
  string value_str = ast_dispatch_asprint((ast_node*)p_let->value);
  const bool fail = decl_str.chars == NULL || bindings_str.chars == NULL || value_str.chars == NULL;
  string ret = create_static_string("", 0);
  if (!fail) {
    ret = asprint(alloc, "%s %s %s", decl_str.chars, bindings_str.chars, value_str.chars);
  }
  string_deinit(&decl_str, alloc);
  string_deinit(&bindings_str, alloc);
  string_deinit(&value_str, alloc);
  return ret;
}

void ast_function_declaration_init(ast_function_declaration* p_fu,
                                   ast_binding* p_binding,
                                   ast_bindings_list p_params,
                                   ast_statement* p_body,
                                   ast_declaration_modifiers_t p_modifiers,
                                   const token p_token,
                                   ast_specs* p_specs) {
  ast_declaration_init(&p_fu->declaration, AST_FUNCTION_DECLARATION, p_token, p_modifiers, p_specs);
  p_fu->binding = p_binding;
  p_fu->parameters = p_params;
  p_fu->body = p_body;
  p_fu->mangled_name = NULL;
}

void ast_function_declaration_set_mangled_name(ast_function_declaration* p_fu, char* p_mangled_name) {
  allocators* alloc = p_fu->declaration.statement.node.alloc;
  if (p_fu->mangled_name != NULL) {
    alloc->release(alloc, p_fu->mangled_name);
  }
  p_fu->mangled_name = p_mangled_name;
}

ok_string_view ast_function_declaration_name(const ast_function_declaration* p_fu) {
  if (p_fu->mangled_name != NULL) {
    const uint32_t length = (uint32_t)strlen(p_fu->mangled_name);
    return ok_create_string_view(p_fu->mangled_name, length, false);
  }
  return ast_identifier_expression_get_value((ast_identifier_expression*)p_fu->binding->lvalue);
}

void ast_function_declaration_deinit(ast_function_declaration* p_fu) {
  allocators* alloc = p_fu->declaration.statement.node.alloc;
  ast_dispatch_deinit((ast_node*)p_fu->body);
  ast_bindings_list_deinit(&p_fu->parameters, alloc);
  ast_dispatch_deinit((ast_node*)p_fu->binding);
  alloc->release(alloc, p_fu->body);
  alloc->release(alloc, p_fu->binding);
  p_fu->binding = NULL;
  p_fu->body = NULL;
  if (p_fu->mangled_name != NULL) {
    alloc->release(alloc, p_fu->mangled_name);
    p_fu->mangled_name = NULL;
  }
  ast_declaration_deinit(&p_fu->declaration);
}

bool ast_function_declaration_print(const ast_function_declaration* p_fu) {
  bool status = ast_declaration_print(&p_fu->declaration);
  status &= printf("fu ") > 0;
  status &= ast_binding_print(p_fu->binding);
  status &= printf("(") > 0;
  for (uint32_t i = 0; i < p_fu->parameters.count; ++i) {
    status &= ast_binding_print(p_fu->parameters.data[i]);
  }
  status &= printf(")") > 0;
  status &= ast_dispatch_print((ast_node*)p_fu->body);
  return status;
}

string ast_function_declaration_asprint(const ast_function_declaration* p_fu) {
  allocators* alloc = p_fu->declaration.statement.node.alloc;
  string decl_str = ast_declaration_asprint(&p_fu->declaration);
  string binding_str = ast_binding_asprint(p_fu->binding);
  string params_str =
      ast_bindings_list_asprint(&p_fu->parameters, ok_create_string_view(", ", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  string body_str = ast_dispatch_asprint((ast_node*)p_fu->body);
  string ret = asprint(alloc, "%s fu %s (%s) %s", decl_str.chars, binding_str.chars, params_str.chars, body_str.chars);
  string_deinit(&decl_str, alloc);
  string_deinit(&binding_str, alloc);
  string_deinit(&params_str, alloc);
  string_deinit(&body_str, alloc);
  return ret;
}

void ast_class_declaration_init(ast_class_declaration* p_class,
                                const token p_name,
                                ast_expression* p_parent,
                                ast_statements_list p_members,
                                ast_declaration_modifiers_t p_modifiers,
                                const token p_token,
                                ast_specs* p_specs) {
  ast_declaration_init(&p_class->declaration, AST_CLASS_DECLARATION, p_token, p_modifiers, p_specs);
  p_class->name = p_name;
  p_class->parent = p_parent;
  p_class->members = p_members;
}

void ast_class_declaration_deinit(ast_class_declaration* p_class) {
  allocators* alloc = p_class->declaration.statement.node.alloc;
  if (p_class->parent != NULL) {
    ast_dispatch_deinit((ast_node*)p_class->parent);
    alloc->release(alloc, p_class->parent);
    p_class->parent = NULL;
  }
  // the members list owns its elements (the array deinit dispatches on each):
  // a manual walk here would free every member twice.
  ast_statements_list_deinit(&p_class->members, alloc);
  ast_declaration_deinit(&p_class->declaration);
}

bool ast_class_declaration_print(const ast_class_declaration* p_class) {
  bool status = ast_declaration_print(&p_class->declaration);
  status &= printf("class %.*s", p_class->name.length, p_class->name.start) > 0;
  if (p_class->parent != NULL) {
    status &= printf(" : ") > 0;
    status &= ast_dispatch_print((ast_node*)p_class->parent);
  }
  status &= printf(" { ") > 0;
  for (uint32_t i = 0; i < p_class->members.count; ++i) {
    status &= ast_dispatch_print((ast_node*)p_class->members.data[i]);
    status &= printf(" ") > 0;
  }
  status &= printf("}") > 0;
  return status;
}

string ast_class_declaration_asprint(const ast_class_declaration* p_class) {
  allocators* alloc = p_class->declaration.statement.node.alloc;
  string ret = asprint(alloc, "class %.*s", p_class->name.length, p_class->name.start);
  if (p_class->parent != NULL) {
    string parent_str = ast_dispatch_asprint((ast_node*)p_class->parent);
    string with_parent = asprint(alloc, "%s : %s", ret.chars, parent_str.chars);
    string_deinit(&ret, alloc);
    string_deinit(&parent_str, alloc);
    ret = with_parent;
  }
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
  return create_static_string("", 0);
#undef X
}
