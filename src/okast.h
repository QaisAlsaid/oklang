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
  AST_RANGE_ABSTRACT_BEGIN = 0,
  AST_NODE = AST_RANGE_ABSTRACT_BEGIN,
  AST_EXPRESSION,
  AST_STATEMENT,
  AST_DECLARATION, // it is a statement, but i guess it is cleaner this way (doesn't matter regardless).
  AST_RANGE_ABSTRACT_END = AST_DECLARATION,

  AST_RANGE_ROOT_BEGIN,
  AST_ROOT = AST_RANGE_ROOT_BEGIN,
  AST_RANGE_ROOT_END = AST_ROOT,

  AST_RANGE_BINDING_BEGIN,
  AST_BINDING = AST_RANGE_BINDING_BEGIN,
  AST_RANGE_BINDING_END = AST_BINDING,

  AST_RANGE_EXPRESSION_BEGIN,
  AST_IDENTIFIER_EXPRESSION = AST_RANGE_EXPRESSION_BEGIN,
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
  AST_SPREAD_EXPRESSION,
  AST_YIELD_EXPRESSION,
  AST_TO_STRING_EXPRESSION, // the P3 interpolation wrapper: one child, compiled to
                            // OP_TOSTRING. only parse_fstring builds it.
  AST_RANGE_EXPRESSION_END = AST_TO_STRING_EXPRESSION,

  AST_RANGE_STATEMENT_BEGIN,
  AST_RANGE_PURE_STATEMENT_BEGIN,
  AST_EMPTY_STATEMENT = AST_RANGE_PURE_STATEMENT_BEGIN,
  AST_EXPRESSION_STATEMENT,
  AST_PRINT_STATEMENT,
  AST_COMPOUND_STATEMENT,
  AST_IF_STATEMENT,
  AST_FOR_STATEMENT,
  AST_FOR_IN_STATEMENT,
  AST_FOR_NUMERIC_STATEMENT,
  AST_WHILE_STATEMENT,
  AST_CONTROL_FLOW_STATEMENT,
  AST_RETURN_STATEMENT,
  AST_THROW_STATEMENT,
  AST_TRY_STATEMENT,
  AST_DEFER_STATEMENT,
  AST_IMPORT_STATEMENT,
  AST_EOF_STATEMENT,
  AST_RANGE_PURE_STATEMENT_END = AST_EOF_STATEMENT,

  AST_RANGE_DECLARATION_BEGIN,
  AST_LET_DECLARATION = AST_RANGE_DECLARATION_BEGIN,
  AST_FUNCTION_DECLARATION,
  AST_CLASS_DECLARATION,
  AST_RANGE_DECLARATION_END = AST_CLASS_DECLARATION,
  AST_RANGE_STATEMENT_END = AST_RANGE_DECLARATION_END,
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
  ast_expression* default_value; // parameter default (NULL = required) (functions only)
  ast_binding_modifiers modifiers;
  bool is_varargs; // `fu f(a, rest...)`: the last parameter, packs the trailing
                   // arguments into a fresh array. set by the parser.
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
  DECLARATION_EXPORT = 1 << 2,
  DECLARATION_PUB = 1 << 3,
  DECLARATION_STATIC = 1 << 4,
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
  // the P2 string rework: when the literal carries escapes (or \u{}
  // expansions), the translated bytes live here, arena-allocated, and
  // get_value serves this buffer; a plain literal stays a source slice.
  char* unescaped;
  uint32_t unescaped_length;
} ast_string_expression;

void ast_string_expression_init(ast_string_expression* p_string, const token p_token, ast_specs* p_specs);
void ast_string_expression_deinit(ast_string_expression* p_string);
ok_string_view ast_string_expression_get_value(const ast_string_expression* p_string);
bool ast_string_expression_print(const ast_string_expression* p_string);
string ast_string_expression_asprint(const ast_string_expression* p_string);

typedef struct {
  ast_expression expression;
  ast_expression* right;
} ast_to_string_expression;

void ast_to_string_expression_init(ast_to_string_expression* p_to_string,
                                   const token p_token,
                                   ast_expression* p_right,
                                   ast_specs* p_specs);
void ast_to_string_expression_deinit(ast_to_string_expression* p_to_string);
bool ast_to_string_expression_print(const ast_to_string_expression* p_to_string);
string ast_to_string_expression_asprint(const ast_to_string_expression* p_to_string);

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

// `p_condition then p_then_arm otherwise p_otherwise_arm` - the conditional
// (ternary) expression. both arms compile into the caller's result register.
typedef struct {
  ast_expression expression;
  ast_expression* condition;
  ast_expression* then_arm;
  ast_expression* otherwise_arm;
} ast_conditional_expression;

void ast_conditional_expression_init(ast_conditional_expression* p_conditional,
                                     const token p_token,
                                     ast_expression* p_condition,
                                     ast_expression* p_then_arm,
                                     ast_expression* p_otherwise_arm,
                                     ast_specs* p_specs);
void ast_conditional_expression_deinit(ast_conditional_expression* p_conditional);
bool ast_conditional_expression_print(const ast_conditional_expression* p_conditional);
string ast_conditional_expression_asprint(const ast_conditional_expression* p_conditional);

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
  ast_expressions_list keys; /* keys[i] pairs with values[i]. storage is
                              * parallel, evaluation is left to right per entry
                              * (key, value, key, value). codegen walks entries */

  ast_expressions_list values;
} ast_map_expression;

void ast_map_expression_init(ast_map_expression* p_map,
                             token p_token,
                             ast_expressions_list p_keys,
                             ast_expressions_list p_values,
                             ast_specs* p_specs);
void ast_map_expression_deinit(ast_map_expression* p_map);
bool ast_map_expression_print(const ast_map_expression* p_map);
string ast_map_expression_asprint(const ast_map_expression* p_map);

typedef struct {
  ast_expression expression;
  ast_expressions_list elements;
} ast_array_expression;

void ast_array_expression_init(ast_array_expression* p_array,
                               token p_token,
                               ast_expressions_list p_elements,
                               ast_specs* p_specs);
void ast_array_expression_deinit(ast_array_expression* p_array);
bool ast_array_expression_print(const ast_array_expression* p_array);
string ast_array_expression_asprint(const ast_array_expression* p_array);

typedef struct {
  ast_expression expression;
} ast_this_expression;

void ast_this_expression_init(ast_this_expression* p_this, const token p_token, ast_specs* p_specs);
void ast_this_expression_deinit(ast_this_expression* p_this);
bool ast_this_expression_print(const ast_this_expression* p_this);
string ast_this_expression_asprint(const ast_this_expression* p_this);

typedef struct {
  ast_expression expression;
} ast_super_expression;

void ast_super_expression_init(ast_super_expression* p_super, const token p_token, ast_specs* p_specs);
void ast_super_expression_deinit(ast_super_expression* p_super);
bool ast_super_expression_print(const ast_super_expression* p_super);
string ast_super_expression_asprint(const ast_super_expression* p_super);

// `left.name` member access. on instances this is a field read (when visible
// from the calling hierarchy) or a bound method; the compiler fuses an access
// in call position into OP_INVOKE. `left` may be an ast_super_expression:
// `super.name(...)` compiles to OP_SUPER_INVOKE.
//
// owned_name: when non-NULL it is a heap copy of a synthesized member name
// (the operator member syntax `a.operator +`), preferred over the token view
// by ast_access_expression_name. freed by the deinit.
typedef struct {
  ast_expression expression;
  ast_expression* left;
  token name;
  char* owned_name;
} ast_access_expression;

void ast_access_expression_init(ast_access_expression* p_access,
                                const token p_token,
                                ast_expression* p_left,
                                const token p_name,
                                ast_specs* p_specs);
// takes ownership of p_owned_name (an alloc->allocate copy); NULL clears it.
void ast_access_expression_set_owned_name(ast_access_expression* p_access, char* p_owned_name);
// the member name: the owned synthesized spelling when present, else the token view.
ok_string_view ast_access_expression_name(const ast_access_expression* p_access);
void ast_access_expression_deinit(ast_access_expression* p_access);
bool ast_access_expression_print(const ast_access_expression* p_access);
string ast_access_expression_asprint(const ast_access_expression* p_access);

typedef struct {
  ast_expression expression;
  ast_expression* left;
  ast_expression* index;
} ast_subscript_expression;

void ast_subscript_expression_init(ast_subscript_expression* p_subscript,
                                   token p_token,
                                   ast_expression* p_left,
                                   ast_expression* p_index,
                                   ast_specs* p_specs);
void ast_subscript_expression_deinit(ast_subscript_expression* p_subscript);
bool ast_subscript_expression_print(const ast_subscript_expression* p_subscript);
string ast_subscript_expression_asprint(const ast_subscript_expression* p_subscript);

typedef struct {
  ast_expression expression;
  operator_type _operator;
  ast_expression* left;
  ast_expression* right;
} ast_compound_assign_expression;

void ast_compound_assign_expression_init(ast_compound_assign_expression* p_compound,
                                         token p_token,
                                         const operator_type p_operator,
                                         ast_expression* p_left,
                                         ast_expression* p_right,
                                         ast_specs* p_specs);
void ast_compound_assign_expression_deinit(ast_compound_assign_expression* p_compound);
bool ast_compound_assign_expression_print(const ast_compound_assign_expression* p_compound);
string ast_compound_assign_expression_asprint(const ast_compound_assign_expression* p_compound);

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
  ast_expression expression;
  // `args...` in a call's argument list: the inner expression must
  // evaluate to an array; CALL_SPREAD stages its elements into the argument
  // window at runtime. legal only as the last argument of a call.
  // the other positions reject it at compile with an error.
  ast_expression* inner;
} ast_spread_expression;

void ast_spread_expression_init(ast_spread_expression* p_spread,
                                const token p_token,
                                ast_expression* p_inner,
                                ast_specs* p_specs);
void ast_spread_expression_deinit(ast_spread_expression* p_spread);
bool ast_spread_expression_print(const ast_spread_expression* p_spread);
string ast_spread_expression_asprint(const ast_spread_expression* p_spread);

// `yield [value]` - a coroutine's suspend point, an expression both ways:
// the value goes out to the resumer, the next resume's argument is the
// expression's own result. the parser fills a NULL value with a synthesized
// null expression, so the compiler always has an operand (bare `yield` is
// `yield null`).
typedef struct {
  ast_expression expression;
  ast_expression* value;
} ast_yield_expression;

void ast_yield_expression_init(ast_yield_expression* p_yield,
                               const token p_token,
                               ast_expression* p_value,
                               ast_specs* p_specs);
void ast_yield_expression_deinit(ast_yield_expression* p_yield);
bool ast_yield_expression_print(const ast_yield_expression* p_yield);
string ast_yield_expression_asprint(const ast_yield_expression* p_yield);

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

typedef struct {
  ast_statement statement;
  ast_binding* binding;       // the loop variable (name + mutability)
  ast_binding* value_binding; // nullable: the map two-binding form (`for let k, v : m`).
                              // when set, binding binds keys and this binds values.
  ast_expression* iterable;   // evaluated once at loop entry
  ast_statement* body;
} ast_for_in_statement;

void ast_for_in_statement_init(ast_for_in_statement* p_for_in, token p_token, ast_specs* p_specs);
void ast_for_in_statement_deinit(ast_for_in_statement* p_for_in);
bool ast_for_in_statement_print(const ast_for_in_statement* p_for_in);
string ast_for_in_statement_asprint(const ast_for_in_statement* p_for_in);

typedef struct {
  ast_statement statement;
  ast_binding* binding;  // the loop counter (name + mutability)
  ast_expression* start; // evaluated once at loop entry
  ast_expression* limit; // evaluated once at loop entry, frozen for the whole trip
  ast_expression* step;  // nullable, defaults to 1; zero step is a runtime error
  ast_statement* body;
} ast_for_numeric_statement;

void ast_for_numeric_statement_init(ast_for_numeric_statement* p_for_numeric, token p_token, ast_specs* p_specs);
void ast_for_numeric_statement_deinit(ast_for_numeric_statement* p_for_numeric);
bool ast_for_numeric_statement_print(const ast_for_numeric_statement* p_for_numeric);
string ast_for_numeric_statement_asprint(const ast_for_numeric_statement* p_for_numeric);

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
  // a return yields a span of expressions: 0 = bare `return;` (one null at
  // runtime), 1 = the classic single form (bytecode-identical path), n >= 2 =
  // the multi-result form (SPANS R2: arity by inference, every path must agree).
  ast_expressions_list returned;
} ast_return_statement;

void ast_return_statement_init(ast_return_statement* p_return,
                               ast_expressions_list p_returned,
                               token p_token,
                               ast_specs* p_specs);
void ast_return_statement_deinit(ast_return_statement* p_return);
bool ast_return_statement_print(const ast_return_statement* p_return);
string ast_return_statement_asprint(const ast_return_statement* p_return);

// `throw expr;` the value is the error, any value. a NULL expression
// is the bare rethrow `throw;`, legal only inside a catch block
// (checked at compile: the compiler knows the enclosing catch's parked-error register).
typedef struct {
  ast_statement statement;
  ast_expression* expression; // nullable: bare rethrow
} ast_throw_statement;

void ast_throw_statement_init(ast_throw_statement* p_throw,
                              ast_expression* p_expression,
                              token p_token,
                              ast_specs* p_specs);
void ast_throw_statement_deinit(ast_throw_statement* p_throw);
bool ast_throw_statement_print(const ast_throw_statement* p_throw);
string ast_throw_statement_asprint(const ast_throw_statement* p_throw);

// `try { B } catch e { H }` the catch binder is an immutable local scoped to the
// statement. a NULL catch_binding is the bare `catch { }`. the try block
// must be followed by a catch clause (the parser enforces it).
typedef struct {
  ast_statement statement;
  ast_statement* body;
  ast_binding* catch_binding; // nullable: `catch { }` discards the value
  ast_statement* catch_body;  // nullable (never: the parser requires one)
} ast_try_statement;

void ast_try_statement_init(ast_try_statement* p_try, token p_token, ast_specs* p_specs);
void ast_try_statement_deinit(ast_try_statement* p_try);
bool ast_try_statement_print(const ast_try_statement* p_try);
string ast_try_statement_asprint(const ast_try_statement* p_try);

// `defer f(a, b)` / `deferr f(a, b)` the callee and the arguments are evaluated at the defer site into hidden locals
// (a snapshot: reassigning `a` afterwards does not change what the deferred call sees),
// and what runs at scope exit is `thunk` a lambda over the  hidden names, built by the parser and compiled by the
// ordinary lambda path. `is_err` marks deferr: identical registration, but the scope's normal exits kill the slot
// instead of running it (the unwind scan runs both).
typedef struct {
  ast_statement statement;
  ast_call_expression* call;      // the original call
  ast_function_expression* thunk; // fu() { <hidden>(<hidden>...) } over the names
  char* names;
  ok_string_view slot_name;
  bool is_err;
} ast_defer_statement;

void ast_defer_statement_init(ast_defer_statement* p_defer,
                              token p_token,
                              ast_call_expression* p_call,
                              ast_function_expression* p_thunk,
                              char* p_names,
                              ok_string_view p_slot_name,
                              bool p_is_err,
                              ast_specs* p_specs);
void ast_defer_statement_deinit(ast_defer_statement* p_defer);
bool ast_defer_statement_print(const ast_defer_statement* p_defer);
string ast_defer_statement_asprint(const ast_defer_statement* p_defer);

// one imported symbol of a `from "p" import a, b as c;` - the name as spelled
// in the importing module, plus the local alias when `as` renames it.
typedef struct {
  token name;  // the exported symbol's identifier
  token alias; // alias.type == TOKEN_IDENTIFIER when present, else TOKEN_ERROR
} ast_import_specifier;

ARRAY_DECLARE_DEFAULT(ast_import_specifier_list, ast_import_specifier)

// `import "a/b";` / `import "a/b" as c;` / `from "a/b" import x, y as z;`
// (OR-1, M1/M2/M5). the path is the raw STRING token - the view strips the
// quotes exactly like a string literal. the statement is only created at
// module top level (parse_root); anywhere else the parser refuses it. the
// compiler never resolves it: the module system resolved the whole graph
// before this TU compiled, and hands the bindings over via compile_specs.
typedef struct {
  ast_statement statement;
  token path;                          // the quoted path (quotes included, like string tokens)
  bool has_alias;                     // `import "p" as name;`
  token alias;                        // the alias identifier when has_alias
  ast_import_specifier_list symbols;  // `from` form only; empty for path imports
} ast_import_statement;

void ast_import_statement_init(ast_import_statement* p_import,
                               token p_token,
                               token p_path,
                               ast_import_specifier_list p_symbols,
                               bool p_has_alias,
                               token p_alias,
                               ast_specs* p_specs);
void ast_import_statement_deinit(ast_import_statement* p_import);
bool ast_import_statement_print(const ast_import_statement* p_import);
string ast_import_statement_asprint(const ast_import_statement* p_import);
// the path literal's contents (no quotes), same convention as string expressions.
ok_string_view ast_import_statement_path(const ast_import_statement* p_import);

typedef struct {
  ast_declaration declaration;
  ast_binding* binding; // the single-binder form (`let x = v;`), NULL in the span form
  ast_expression* value;
  // the destructuring form `let (x, y) = f();`
  // empty list = the single form above. the bindings take the call's result window slot by slot.
  ast_bindings_list span_bindings;
} ast_let_declaration;

void ast_let_declaration_init(ast_let_declaration* p_let,
                              ast_binding* p_binding,
                              ast_expression* p_value,
                              ast_declaration_modifiers_t p_modifiers,
                              const token p_token,
                              ast_specs* p_specs);
// the destructuring form: `let (x, y) = f();`
void ast_let_declaration_init_span(ast_let_declaration* p_let,
                                   ast_bindings_list p_bindings,
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
  // non-NULL for operator declarations: the canonical synthesized name
  // ("operator +", "operator ()", "operator []=", "operator num"), heap-copied
  // and freed by the deinit. ast_function_declaration_name prefers it over the
  // binding's token view so every consumer sees one spelling regardless of the
  // user's whitespace.
  char* mangled_name;
} ast_function_declaration;

void ast_function_declaration_init(ast_function_declaration* p_fu,
                                   ast_binding* p_binding,
                                   ast_bindings_list p_params,
                                   ast_statement* p_body,
                                   ast_declaration_modifiers_t p_modifiers,
                                   const token p_token,
                                   ast_specs* p_specs);
// takes ownership of p_mangled_name (an alloc->allocate copy); NULL clears it.
void ast_function_declaration_set_mangled_name(ast_function_declaration* p_fu, char* p_mangled_name);
// the declaration's name: the canonical operator spelling when present, else the binding's identifier.
ok_string_view ast_function_declaration_name(const ast_function_declaration* p_fu);
void ast_function_declaration_deinit(ast_function_declaration* p_fu);
bool ast_function_declaration_print(const ast_function_declaration* p_fu);
string ast_function_declaration_asprint(const ast_function_declaration* p_fu);

// `class Name : Parent { members }`. members are let declarations (fields,
// the binding's value is the per-instance initializer) and function
// declarations (methods; DECLARATION_PUB marks `pub fu`). the parent is an
// arbitrary expression evaluated at definition time (null = object).
typedef struct {
  ast_declaration declaration;
  token name;
  ast_expression* parent; // nullable: no `: Parent` clause
  ast_statements_list members;
} ast_class_declaration;

void ast_class_declaration_init(ast_class_declaration* p_class,
                                const token p_name,
                                ast_expression* p_parent,
                                ast_statements_list p_members,
                                ast_declaration_modifiers_t p_modifiers,
                                const token p_token,
                                ast_specs* p_specs);
void ast_class_declaration_deinit(ast_class_declaration* p_class);
bool ast_class_declaration_print(const ast_class_declaration* p_class);
string ast_class_declaration_asprint(const ast_class_declaration* p_class);

bool ast_dispatch_print(ast_node* p_node);
string ast_dispatch_asprint(ast_node* p_node);
void ast_dispatch_deinit(ast_node* p_node);

#endif // OK_AST_H
