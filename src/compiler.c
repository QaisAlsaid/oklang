#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "utils.h"
#include "value.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define OK_DEBUG_DUMP_CODE

static void error_at(compiler* p_compiler, ast_node* p_node, string p_message);
static void error_at_noted(compiler* p_compiler, ast_node* p_node, string p_message, string p_note);
static chunk* current_chunk(compiler* p_compiler);
static bool emit_byte(compiler* p_compiler, byte p_byte, ast_node* p_node);
static bool emit_2bytes(compiler* p_compiler, byte p_1st_byte, byte p_2nd_byte, ast_node* p_node);
static bool emit_bytes(compiler* p_compiler, byte* p_bytes, size_t p_bytes_count, ast_node* p_node);
static bool emit_constant(compiler* p_compiler, value p_value, ast_node* p_node);

static bool end_compile(compiler* p_compiler, ast_node* p_node);
static uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node);

static bool compile_node(compiler* p_compiler, ast_node* p_node);

static bool compile_root(compiler* p_compiler, ast_root* p_root);

static bool compile_statement(compiler* p_compiler, ast_statement* p_statement);
static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement);
static bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof);

static bool compile_expression(compiler* p_compiler, ast_expression* p_expression);
static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier);
static bool compile_number_expression(compiler* p_compiler, ast_number_expression* p_number);
static bool compile_string_expression(compiler* p_compiler, ast_string_expression* p_string);
static bool compile_prefix_unary_expression(compiler* p_compiler, ast_prefix_unary_expression* p_expression);
static bool compile_infix_binary_expression(compiler* p_compiler, ast_infix_binary_expression* p_expression);
static bool compile_postfix_unary_expression(compiler* p_compiler, ast_postfix_unary_expression* p_expression);
static bool compile_boolean_expression(compiler* p_compiler, ast_boolean_expression* p_boolean);
static bool compile_null_expression(compiler* p_compiler, ast_null_expression* p_null);

void compile_result_deinit(compile_result* p_result) {
  chunk_deinit(p_result->chunk);
  free(p_result->chunk);
  p_result->chunk = NULL;
}

void compiler_init(compiler* p_compiler) {
  p_compiler->current_chunk = NULL;
  p_compiler->objects_store = NULL;
  p_compiler->source = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
}

void compiler_deinit(compiler* p_compiler) {
  p_compiler->current_chunk = NULL;
  p_compiler->source = NULL;
  p_compiler->objects_store = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
}

compile_result compiler_compile(compiler* p_compiler, compiler_specs p_specs) {
  chunk* _chunk = (chunk*)malloc(sizeof(chunk));
  chunk_init(_chunk);
  p_compiler->current_chunk = _chunk;
  p_compiler->source = p_specs.source;
  p_compiler->objects_store = p_specs.objects_store;
  bool res = compile_node(p_compiler, (ast_node*)p_specs.root);
  compile_result result;
  if (p_compiler->had_error == false || res == false) {
    result.status = COMPILE_OK;
    result.chunk = _chunk;
#if defined(OK_DEBUG_DUMP_CODE)
    debug_disassemble_chunk(_chunk, "code");
#endif // defined (OK_DEBUG_DUMP_CODE)
  } else {
    free(_chunk);
    result.status = COMPILE_ERROR;
    result.chunk = NULL;
  }
  return result;
}

void error_at(compiler* p_compiler, ast_node* p_node, string p_message) {
  error_at_noted(p_compiler, p_node, p_message, create_string(NULL, 0, false));
}

void error_at_noted(compiler* p_compiler, ast_node* p_node, string p_message, string p_note) {
  error_at_noted(p_compiler, p_node, p_message, p_note);
}

chunk* current_chunk(compiler* p_compiler) {
  return p_compiler->current_chunk;
}

bool emit_byte(compiler* p_compiler, byte p_byte, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  chunk_write_1byte_code_with_line_info(chunk, p_byte, p_node->token.line_info);
  return true;
}

bool emit_2bytes(compiler* p_compiler, byte p_1st_byte, byte p_2nd_byte, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  chunk_write_2bytes_code_with_line_info(chunk, p_1st_byte, p_2nd_byte, p_node->token.line_info);
  return true;
}

bool emit_bytes(compiler* p_compiler, byte* p_bytes, size_t p_bytes_count, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  chunk_write_code_with_line_info(chunk, p_bytes, p_bytes_count, p_node->token.line_info);
  return true;
}

bool end_compile(compiler* p_compiler, ast_node* p_node) {
  return emit_byte(p_compiler, OP_RETURN, p_node);
}

uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  const uint32_t index = chunk_write_constant_with_line_info(chunk, p_value, p_node->token.line_info);
  if (!IS_CONSTANT_VALID(index)) {
    if (index == CONSTANT_OVERFLOW) {
      string note = asprint("maximum number of constants is: %d", CONSTANT_MAX);
      error_at_noted(p_compiler,
                     p_node,
                     create_string("too many constants in single function.", STRING_IGNORE_LENGTH, false),
                     note);
      string_deinit(&note);
    } else if (index == CONSTANT_ALLOCATION_FAILED) {
      error_at(
          p_compiler, p_node, create_string("failed to allocate memory for constant.", STRING_IGNORE_LENGTH, false));
    }
  }
  return index;
}

bool emit_constant(compiler* p_compiler, value p_value, ast_node* p_node) {
  return IS_CONSTANT_VALID(create_constant(p_compiler, p_value, p_node));
}

static bool compile_node(compiler* p_compiler, ast_node* p_node) {
  if (p_node == NULL) {
    return false;
  }
  switch (p_node->node_type) {
  case AST_ROOT:
    return compile_root(p_compiler, (ast_root*)p_node);
  case AST_BINDING:
    assert(false);
  case AST_IDENTIFIER_EXPRESSION:
    return compile_identifier(p_compiler, (ast_identifier_expression*)p_node);
  case AST_NUMBER_EXPRESSION:
    return compile_number_expression(p_compiler, (ast_number_expression*)p_node);
  case AST_STRING_EXPRESSION:
    return compile_string_expression(p_compiler, (ast_string_expression*)p_node);
  case AST_PREFIX_UNARY_EXPRESSION:
    return compile_prefix_unary_expression(p_compiler, (ast_prefix_unary_expression*)p_node);
  case AST_INFIX_BINARY_EXPRESSION:
    return compile_infix_binary_expression(p_compiler, (ast_infix_binary_expression*)p_node);
  case AST_POSTFIX_UNARY_EXPRESSION:
    return compile_postfix_unary_expression(p_compiler, (ast_postfix_unary_expression*)p_node);
  case AST_CALL_EXPRESSION:
  case AST_ASSIGN_EXPRESSION:
  case AST_COMPOUND_ASSIGN_EXPRESSION:
  case AST_OPERATOR_EXPRESSION:
  case AST_CONDITIONAL_EXPRESSION:
    assert(0);
  case AST_BOOLEAN_EXPRESSION:
    return compile_boolean_expression(p_compiler, (ast_boolean_expression*)p_node);
  case AST_NULL_EXPRESSION:
    return compile_null_expression(p_compiler, (ast_null_expression*)p_node);
  case AST_ACCESS_EXPRESSION:
  case AST_THIS_EXPRESSION:
  case AST_SUPER_EXPRESSION:
  case AST_ARRAY_EXPRESSION:
  case AST_MAP_EXPRESSION:
  case AST_SUBSCRIPT_EXPRESSION:

  case AST_EMPTY_STATEMENT:
    assert(0); // parser bug
  case AST_EXPRESSION_STATEMENT:
    return compile_expression_statement(p_compiler, (ast_expression_statement*)p_node);
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
    return compile_eof_statement(p_compiler, (ast_eof_statement*)p_node);
  default: {
    string message = asprint("unable to compile node: %d", p_node->node_type);
    error_at(p_compiler, p_node, message);
    string_deinit(&message);
    return false;
  }
  }
}

static bool compile_root(compiler* p_compiler, ast_root* p_root) {
  bool res = true;
  for (ast_statements_list_node* node = p_root->statements.head; node != NULL; node = node->next) {
    res &= compile_node(p_compiler, (ast_node*)node->statement);
  }
  return res;
}

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement) {
  if (compile_node(p_compiler, (ast_node*)p_expression_statement->expression)) {
    return emit_byte(p_compiler, OP_POP, (ast_node*)p_expression_statement);
  }
  return false;
}

static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier) {
}

bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof) {
  return emit_byte(p_compiler, OP_RETURN, (ast_node*)p_eof);
}

static bool compile_number_expression(compiler* p_compiler, ast_number_expression* p_number) {
  double number = ast_number_expression_get_value(p_number);
  if (number == AST_NUMBER_EXPRESSION_PARSE_ERROR) {
    error_at(p_compiler, (ast_node*)p_number, create_string("failed to parse number.", STRING_IGNORE_LENGTH, false));
  }
  return emit_constant(p_compiler, NUMBER_AS_VALUE(number), (ast_node*)p_number);
}

static bool compile_string_expression(compiler* p_compiler, ast_string_expression* p_string) {
  string parsed_str = ast_string_expression_get_value(p_string);
  if (parsed_str.chars == NULL) {
    error_at(p_compiler, (ast_node*)p_string, create_string("failed to parse string.", STRING_IGNORE_LENGTH, false));
    return false;
  }
  string copied_str = copy_string(parsed_str);
  if (copied_str.chars == NULL) {
    error_at(p_compiler,
             (ast_node*)p_string,
             create_string("failed to allocate memory for string.", STRING_IGNORE_LENGTH, false));
    return false;
  }
  object_string* object_str = create_object_string(copied_str, p_compiler->objects_store);
  if (object_str == NULL) {
    error_at(
        p_compiler, (ast_node*)p_string, create_string("failed create string object.", STRING_IGNORE_LENGTH, false));
    string_deinit(&copied_str);
    return false;
  }
  return emit_constant(p_compiler, OBJECT_AS_VALUE(object_str), (ast_node*)p_string);
}

static bool compile_prefix_unary_expression(compiler* p_compiler, ast_prefix_unary_expression* p_expression) {
  if (!compile_node(p_compiler, (ast_node*)p_expression->right)) {
    return false;
  }
  switch (p_expression->_operator) {
  case TOKEN_MINUS:
    return emit_byte(p_compiler, OP_NEGATE, (ast_node*)p_expression);
  case TOKEN_NOT:
  case TOKEN_BANG:
    return emit_byte(p_compiler, OP_NOT, (ast_node*)p_expression);
  default:;
  }
  return false;
}

static bool compile_infix_binary_expression(compiler* p_compiler, ast_infix_binary_expression* p_expression) {
  if (compile_node(p_compiler, (ast_node*)p_expression->left)) {
    if (compile_node(p_compiler, (ast_node*)p_expression->right)) {
      switch (p_expression->_operator) {
      case TOKEN_PLUS:
        return emit_byte(p_compiler, OP_ADD, (ast_node*)p_expression);
      case TOKEN_MINUS:
        return emit_byte(p_compiler, OP_SUBTRACT, (ast_node*)p_expression);
      case TOKEN_ASTERISK:
        return emit_byte(p_compiler, OP_MULTIPLY, (ast_node*)p_expression);
      case TOKEN_SLASH:
        return emit_byte(p_compiler, OP_DIVIDE, (ast_node*)p_expression);
      case TOKEN_EQUAL:
        return emit_byte(p_compiler, OP_EQUAL, (ast_node*)p_expression);
      case TOKEN_BANG_EQUAL:
        return emit_byte(p_compiler, OP_NOT_EQUAL, (ast_node*)p_expression);
      case TOKEN_LESS:
        return emit_byte(p_compiler, OP_LESS, (ast_node*)p_expression);
      case TOKEN_GREATER:
        return emit_byte(p_compiler, OP_GREATER, (ast_node*)p_expression);
      case TOKEN_LESS_EQUAL:
        return emit_byte(p_compiler, OP_LESS_EQUAL, (ast_node*)p_expression);
      case TOKEN_GREATER_EQUAL:
        return emit_byte(p_compiler, OP_GREATER_EQUAL, (ast_node*)p_expression);

      default:;
      }
    }
  }
  return false;
}

static bool compile_postfix_unary_expression(compiler* p_compiler, ast_postfix_unary_expression* p_expression) {
}

static bool compile_boolean_expression(compiler* p_compiler, ast_boolean_expression* p_boolean) {
  return emit_byte(p_compiler, ast_boolean_expression_get_value(p_boolean) ? OP_TRUE : OP_FALSE, (ast_node*)p_boolean);
}

static bool compile_null_expression(compiler* p_compiler, ast_null_expression* p_null) {
  return emit_byte(p_compiler, OP_NULL, (ast_node*)p_null);
}
