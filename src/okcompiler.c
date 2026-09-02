#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "okcompiler.h"
#include "okdebug.h"
#include "okobject.h"
#include "okutils.h"
#include "okvalue.h"

typedef enum {
  VARIABLE_DECLRATION_FLAGS_NONE = 0,
  VARIABLE_DECLARATION_FLAGS_MUTABLE = 1 << 0,
  VARIABLE_DECLARATION_FLAGS_GLOBAL = 1 << 1,
} variable_declaration_flags;
typedef uint8_t variable_declaration_flags_t;

typedef struct {
  string_view identifier;
  variable_declaration_flags_t flags;
} variable_declaration;

static inline uint32_t local_get_raw_index(uint32_t p_packed) {
  return p_packed & 0x00ffffff;
}

static bool local_test_flag(uint32_t p_packed, uint8_t p_flag) {
  return (p_packed & (UINT32_C(1) << (24 + p_flag))) != 0;
}

static void local_set_flag(uint32_t* p_packed, uint8_t p_flag) {
  *p_packed |= UINT32_C(1) << (24 + p_flag);
}

static void local_remove_flag(uint32_t* p_packed, uint8_t p_flag) {
  *p_packed &= ~(UINT32_C(1) << (24 + p_flag));
}

#define LOCAL_FLAGS_MASK 0x00ffffff
#define ARE_KEYS_EQUAL(lhs, rhs) (strncmp(lhs.string.chars, rhs.string.chars, string_get_length(&lhs.string)) == 0)
#define GET_HASH(key) (key.hash)

TABLE_DEFINE_DEFAULT(scope_locals,
                     hashed_string,
                     uint32_t,
                     hash_t,
                     ARE_KEYS_EQUAL,
                     hashed_string_deinit,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     GET_HASH)

static void scope_init(scope* p_scope);
static void scope_deinit(scope* p_scope);
static void function_init(function* p_function, object_function* p_obj_function, function_type p_type);
static void function_deinit(function* p_function);
static void loop_context_init(loop_context* p_context);
static void loop_context_deinit(loop_context* p_context);
static upvalue create_upvalue(uint32_t p_index, bool p_is_local);
static uint32_t upvalue_get_index(upvalue* p_upvalue);
static bool upvalue_is_local(upvalue* p_upvalue);
ARRAY_DEFINE_DEFAULT(locals, local, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(upvalues, upvalue, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(scopes, scope, scope_deinit)
ARRAY_DEFINE_DEFAULT(functions, function, function_deinit)
ARRAY_DEFINE_DEFAULT(uint32_array, uint32_t, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(loop_stack, loop_context, loop_context_deinit)

static void error_at(compiler* p_compiler, const ast_node* p_node, const string_view p_message);
static void
error_at_noted(compiler* p_compiler, const ast_node* p_node, const string_view p_message, const string_view p_note);
static function* current_function(compiler* p_compiler);
static scope* current_scope(compiler* p_compiler);
static chunk* current_chunk(compiler* p_compiler);
static bool emit_byte(compiler* p_compiler, byte p_byte, ast_node* p_node);
static bool emit_2bytes(compiler* p_compiler, byte p_1st_byte, byte p_2nd_byte, ast_node* p_node);
static bool emit_bytes(compiler* p_compiler, byte* p_bytes, size_t p_bytes_count, ast_node* p_node);
static bool emit_constant(compiler* p_compiler, value p_value, ast_node* p_node);
static bool emit_default_return(compiler* p_compiler, ast_node* p_node);

static uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node);
static void begin_scope(compiler* p_compiler);
static void end_scope(compiler* p_compiler, ast_node* p_node, bool p_needs_pop);
static bool
push_function(compiler* p_compiler, string_view p_name, function_type p_type, uint8_t p_arity, ast_node* p_node);
static uint32_t add_local(compiler* p_compiler, const variable_declaration p_declration, ast_node* p_node);

static bool compile_node(compiler* p_compiler, ast_node* p_node);

static bool compile_root(compiler* p_compiler, ast_root* p_root);

static bool compile_let_declration(compiler* p_compiler, ast_let_declaration* p_let);
static bool compile_function_declaration(compiler* p_compiler, ast_function_declaration* p_fu);

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement);
static bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof);
static bool compile_print_statement(compiler* p_compiler, ast_print_statement* p_print);
static bool compile_compound_statement(compiler* p_compiler, ast_compound_statement* p_compound);
static bool compile_if_statement(compiler* p_compiler, ast_if_statement* p_if);
static bool compile_while_statement(compiler* p_compiler, ast_while_statement* p_while);
static bool compile_for_statement(compiler* p_compiler, ast_for_statement* p_for);
static bool compile_control_flow_statement(compiler* p_compiler, ast_control_flow_statement* p_control);
static bool compile_return_statement(compiler* p_compiler, ast_return_statement* p_return);

static bool compile_expression(compiler* p_compiler, ast_expression* p_expression);
static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier);
static bool compile_number_expression(compiler* p_compiler, ast_number_expression* p_number);
static bool compile_string_expression(compiler* p_compiler, ast_string_expression* p_string);
static bool compile_prefix_unary_expression(compiler* p_compiler, ast_prefix_unary_expression* p_expression);
static bool compile_infix_binary_expression(compiler* p_compiler, ast_infix_binary_expression* p_expression);
static bool compile_assign_expression(compiler* p_compiler, ast_assign_expression* p_assign);
static bool compile_call_expression(compiler* p_compiler, ast_call_expression* p_call);
static bool compile_logical_operator(compiler* p_compiler, ast_infix_binary_expression* p_logical);
static bool compile_postfix_unary_expression(compiler* p_compiler, ast_postfix_unary_expression* p_expression);
static bool compile_boolean_expression(compiler* p_compiler, ast_boolean_expression* p_boolean);
static bool compile_null_expression(compiler* p_compiler, ast_null_expression* p_null);
static bool compile_function_expression(compiler* p_compiler, ast_function_expression* p_fu);

static bool compile_function_impl(compiler* p_compiler,
                                  string_view p_name,
                                  function_type p_type,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  ast_node* p_node);

void compiler_init(compiler* p_compiler) {
  p_compiler->objects_store = NULL;
  p_compiler->source = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
  functions_init(&p_compiler->functions);
}

void compiler_deinit(compiler* p_compiler) {
  p_compiler->source = NULL;
  p_compiler->objects_store = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
  functions_deinit(&p_compiler->functions);
}

compile_result compiler_compile(compiler* p_compiler, compiler_specs p_specs) {
  compile_result result;
  result.function = NULL;
  result.status = COMPILE_ERROR;
  p_compiler->source = p_specs.source;
  p_compiler->objects_store = p_specs.objects_store;
  p_compiler->globals_store = p_specs.globals_store;
  variable_declaration decl = {.identifier = "", .flags = VARIABLE_DECLRATION_FLAGS_NONE};
  if (!push_function(p_compiler,
                     create_string_view("<main>", STRING_VIEW_CALCULATE_LENGTH, true),
                     FUNCTION_SCRIPT,
                     0,
                     (ast_node*)p_specs.root)) {
    goto ret;
  }
  begin_scope(p_compiler);
  if (!IS_LOCAL_INDEX_VALID(add_local(p_compiler, decl, (ast_node*)p_specs.root))) {
    goto ret;
  }
  bool res = compile_node(p_compiler, (ast_node*)p_specs.root);
  result.function = current_function(p_compiler)->function.function;
  emit_default_return(p_compiler, (ast_node*)p_specs.root);
  end_scope(p_compiler, (ast_node*)p_specs.root, false);
  functions_remove(&p_compiler->functions, p_compiler->functions.count - 1, p_compiler->functions.count - 1);
  if (p_compiler->had_error == false && res != false) {
    result.status = COMPILE_OK;
#if defined(OK_DEBUG_DUMP_CODE)
    disassembler disassembler;
    disassembler_specs specs = {.chunk = &result.function->chunk, .globals_store = p_compiler->globals_store};
    disassembler_init(&disassembler, specs);
    debug_disassemble_chunk(&disassembler, "<main>");
    disassembler_deinit(&disassembler);
#endif // defined (OK_DEBUG_DUMP_CODE)
  }
ret:
  compiler_deinit(p_compiler);
  compiler_init(p_compiler);
  return result;
}

void error_at(compiler* p_compiler, const ast_node* p_node, const string_view p_message) {
  error_at_noted(p_compiler, p_node, p_message, create_string_view(NULL, 0, false));
}

void error_at_noted(compiler* p_compiler,
                    const ast_node* p_node,
                    const string_view p_message,
                    const string_view p_note) {
  report_at_noted(&p_compiler->panic,
                  &p_compiler->had_error,
                  p_node->node_type == AST_EOF_STATEMENT,
                  p_compiler->source,
                  &p_node->token,
                  REPORT_SEVERITY_ERROR,
                  p_message,
                  p_note);
}

function* current_function(compiler* p_compiler) {
  return &p_compiler->functions.data[p_compiler->functions.count - 1];
}

chunk* current_chunk(compiler* p_compiler) {
  return &current_function(p_compiler)->function.function->chunk;
}

scope* current_scope(compiler* p_compiler) {
  function* fun = current_function(p_compiler);
  return &fun->scopes.data[fun->scopes.count - 1];
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

void begin_scope(compiler* p_compiler) {
  scope scp;
  scope_init(&scp);
  function* fun = current_function(p_compiler);
  scopes_append(&fun->scopes, scp);
}

void end_scope(compiler* p_compiler, ast_node* p_node, bool p_needs_pop) {
  function* fun = current_function(p_compiler);
  scope* scp = &fun->scopes.data[fun->scopes.count - 1];
  for (uint32_t i = 0; i < scp->locals_array.count; ++i) {
    if (local_test_flag(fun->locals.data[scp->locals_array.data[i]].info, LOCAL_FLAG_CAPTURED)) {
      emit_byte(p_compiler, OP_CLOSE_UPVALUE, p_node);
    } else if (p_needs_pop) {
      emit_byte(p_compiler, OP_POP, p_node);
    }
  }

  scopes_remove(&fun->scopes, fun->scopes.count - 1, fun->scopes.count - 1); // TODO: pop
}

void scope_init(scope* p_scope) {
  scope_locals_init(&p_scope->locals_table);
  uint32_array_init(&p_scope->locals_array);
}

void scope_deinit(scope* p_scope) {
  scope_locals_deinit(&p_scope->locals_table);
  uint32_array_deinit(&p_scope->locals_array);
}

void function_init(function* p_function, object_function* p_obj_function, function_type p_type) {
  locals_init(&p_function->locals);
  upvalues_init(&p_function->upvalues);
  scopes_init(&p_function->scopes);
  loop_stack_init(&p_function->loop_stack);
  p_function->function.function = p_obj_function;
  p_function->function.type = p_type;
}

void function_deinit(function* p_function) {
  loop_stack_deinit(&p_function->loop_stack);
  scopes_deinit(&p_function->scopes);
  upvalues_deinit(&p_function->upvalues);
  locals_deinit(&p_function->locals);
  p_function->function.function = NULL;
  p_function->function.type = FUNCTION_NONE;
}

void loop_context_init(loop_context* p_context) {
  p_context->break_target = 0;
  p_context->continue_target = 0;
  p_context->continue_forward = false;
  uint32_array_init(&p_context->continues);
  uint32_array_init(&p_context->breaks);
}

void loop_context_deinit(loop_context* p_context) {
  uint32_array_deinit(&p_context->continues);
  uint32_array_deinit(&p_context->breaks);
}

upvalue create_upvalue(uint32_t p_index, bool p_is_local) {
  upvalue up;
  up.info = p_index & 0x00ffffff;
  if (p_is_local) {
    up.info |= UINT32_C(1) << 24;
  }
  return up;
}

uint32_t upvalue_get_index(upvalue* p_upvalue) {
  return p_upvalue->info & 0x00ffffff;
}

bool upvalue_is_local(upvalue* p_upvalue) {
  return (p_upvalue->info & (UINT32_C(1) << 24)) != 0;
}

uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node) {
  value_array* constants = &current_function(p_compiler)->function.function->chunk.constants;
  if (constants->count >= CONSTANT_MAX) {
    return CONSTANT_OVERFLOW;
  }
  if (!value_array_append(constants, p_value)) {
    return CONSTANT_OVERFLOW;
  }
  return constants->count - 1;
}

bool emit_constant(compiler* p_compiler, value p_value, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  const uint32_t index = chunk_write_constant_with_line_info(chunk, p_value, p_node->token.line_info);
  if (!IS_CONSTANT_VALID(index)) {
    if (index == CONSTANT_OVERFLOW) {
      string note = asprint("limit is: %d", CONSTANT_MAX);
      error_at_noted(p_compiler,
                     p_node,
                     create_string_view("too many constants in single function.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view_from_string(note));
      string_deinit(&note);
    } else if (index == CONSTANT_ALLOCATION_FAILED) {
      error_at(p_compiler,
               p_node,
               create_string_view("failed to allocate memory for constant.", STRING_VIEW_CALCULATE_LENGTH, true));
    }
  }
  return IS_CONSTANT_VALID(index);
}

bool emit_default_return(compiler* p_compiler, ast_node* p_node) {
  return emit_2bytes(p_compiler, OP_NULL, OP_RETURN, p_node);
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
    return compile_call_expression(p_compiler, (ast_call_expression*)p_node);
  case AST_ASSIGN_EXPRESSION:
    return compile_assign_expression(p_compiler, (ast_assign_expression*)p_node);
  case AST_COMPOUND_ASSIGN_EXPRESSION:
  case AST_OPERATOR_EXPRESSION:
  case AST_CONDITIONAL_EXPRESSION:
    assert(0);
  case AST_BOOLEAN_EXPRESSION:
    return compile_boolean_expression(p_compiler, (ast_boolean_expression*)p_node);
  case AST_NULL_EXPRESSION:
    return compile_null_expression(p_compiler, (ast_null_expression*)p_node);
  case AST_FUNCTION_EXPRESSION:
    return compile_function_expression(p_compiler, (ast_function_expression*)p_node);
  case AST_ACCESS_EXPRESSION:
  case AST_THIS_EXPRESSION:
  case AST_SUPER_EXPRESSION:
  case AST_ARRAY_EXPRESSION:
  case AST_MAP_EXPRESSION:
  case AST_SUBSCRIPT_EXPRESSION:
    assert(0);
  case AST_EMPTY_STATEMENT:
    // return true;
    assert(0); // parser bug
  case AST_EXPRESSION_STATEMENT:
    return compile_expression_statement(p_compiler, (ast_expression_statement*)p_node);
  case AST_PRINT_STATEMENT:
    return compile_print_statement(p_compiler, (ast_print_statement*)p_node);
  case AST_COMPOUND_STATEMENT:
    return compile_compound_statement(p_compiler, (ast_compound_statement*)p_node);
  case AST_IF_STATEMENT:
    return compile_if_statement(p_compiler, (ast_if_statement*)p_node);
  case AST_WHILE_STATEMENT:
    return compile_while_statement(p_compiler, (ast_while_statement*)p_node);
  case AST_FOR_STATEMENT:
    return compile_for_statement(p_compiler, (ast_for_statement*)p_node);
  case AST_CONTROL_FLOW_STATEMENT:
    return compile_control_flow_statement(p_compiler, (ast_control_flow_statement*)p_node);
  case AST_RETURN_STATEMENT:
  case AST_THROW_STATEMENT:
  case AST_TRY_STATEMENT:
  case AST_CATCH_STATEMENT:
  case AST_FINALIZE_STATEMENT:
  case AST_TRY_CATCH_STATEMENT:
    assert(0);

  case AST_LET_DECLARATION:
    return compile_let_declration(p_compiler, (ast_let_declaration*)p_node);
  case AST_FUNCTION_DECLARATION:
    return compile_function_declaration(p_compiler, (ast_function_declaration*)p_node);
  case AST_CLASS_DECLARATION:
    assert(0);
  case AST_EOF_STATEMENT:
    return compile_eof_statement(p_compiler, (ast_eof_statement*)p_node);
  default: {
    string message = asprint("unable to compile node: %d", p_node->node_type);
    error_at(p_compiler, p_node, create_string_view_from_string(message));
    string_deinit(&message);
    return false;
  }
  }
}

static bool compile_root(compiler* p_compiler, ast_root* p_root) {
  bool res = true;
  for (uint32_t i = 0; i < p_root->statements.count; ++i) {
    res &= compile_node(p_compiler, (ast_node*)p_root->statements.data[i]);
    p_compiler->panic = false;
  }
  return res;
}

static uint32_t add_global(compiler* p_compiler, variable_declaration p_declaration, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  uint32_t index = globals_store_add(p_compiler->globals_store,
                                     p_declaration.identifier,
                                     (p_declaration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0);
  if (!IS_GLOBAL_VALID(index)) {
    if (index == GLOBAL_ALLOCATION_FAILED) {
      error_at(
          p_compiler,
          p_node,
          create_string_view("failed to allocate memory for global identifier.", STRING_VIEW_CALCULATE_LENGTH, true));
      return index;
    } else if (index == GLOBAL_OVERFLOW) {
      string note = asprint("limit is: %u", GLOBAL_MAX);
      error_at_noted(p_compiler,
                     p_node,
                     create_string_view("too many identifiers.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view_from_string(note));
      string_deinit(&note);
    }
  }
  return global_get_raw_index(index);
}

static bool define_global(compiler* p_compiler,
                          const uint32_t p_global,
                          const variable_declaration_flags p_flags,
                          ast_node* p_node) {
  if (p_global < OP_SET_GLOBAL_MAX + 1) {
    emit_2bytes(p_compiler, OP_SET_GLOBAL, p_global, p_node);
  } else if (p_global < OP_SET_GLOBAL_LONG_MAX + 1) {
    emit_byte(p_compiler, OP_SET_GLOBAL_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT];
    encode_int(bytes, UINT24_BYTE_COUNT, p_global);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    return false;
  }
  return true;
}

static uint32_t*
resolve_local(compiler* p_compiler, hashed_string* p_identifier, function* p_function, ast_node* p_node) {
  uint32_t* index = NULL;
  uint32_t depth = p_function->scopes.count;
  while (index == NULL && depth != 0) {
    index = scope_locals_get(&p_function->scopes.data[--depth].locals_table, *p_identifier);
    // TODO: report that we skipped if we couldn't resolve other local with same name.
    if (index != NULL &&
        local_test_flag(*index, LOCAL_FLAG_UNINITIALIZED)) { // let x; { let x = x; /* skip the inner */ }
      index = NULL;
    }
  }
  return index;
}

uint32_t add_local(compiler* p_compiler, const variable_declaration p_declration, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_declration.identifier);
  if (hs.string.chars == NULL) {
    error_at(p_compiler,
             p_node,
             create_string_view("failed to allocate memory for hashed string.", STRING_VIEW_CALCULATE_LENGTH, true));
    return LOCAL_ERROR;
  }
  scope* scp = current_scope(p_compiler);
  uint32_t* resolved = scope_locals_get(&scp->locals_table, hs);
  if (resolved == NULL) {
    function* fun = current_function(p_compiler);
    local local;
    uint32_t index = fun->locals.count;
    if (index > LOCALS_MAX) {
      string note = asprint("limit is: %d.", LOCALS_MAX);
      error_at_noted(
          p_compiler,
          p_node,
          create_string_view("too many local variables in the same scope.", STRING_VIEW_CALCULATE_LENGTH, true),
          create_string_view_from_string(note));
      string_deinit(&note);
      hashed_string_deinit(&hs);
      return LOCAL_OVERFLOW;
    }
    local.info = index & LOCAL_FLAGS_MASK;
    local_set_flag(&local.info, LOCAL_FLAG_UNINITIALIZED);
    if ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0) {
      local_set_flag(&local.info, LOCAL_FLAG_MUTABLE);
    }
    if (!locals_append(&fun->locals, local)) {
      error_at_noted(p_compiler,
                     p_node,
                     create_string_view("failed to add local to the locals array.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("you might be out of memory.", STRING_VIEW_CALCULATE_LENGTH, true));
      hashed_string_deinit(&hs);
      return LOCAL_ERROR;
    }
    if (!uint32_array_append(&current_scope(p_compiler)->locals_array, fun->locals.count - 1)) {
      error_at_noted(p_compiler,
                     p_node,
                     create_string_view("failed to add local to the locals array.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("you might be out of memory.", STRING_VIEW_CALCULATE_LENGTH, true));
      locals_remove(&fun->locals, fun->locals.count - 1, fun->locals.count - 1);
      return LOCAL_ERROR;
    }
    if (!scope_locals_set(&current_scope(p_compiler)->locals_table, hs, fun->locals.count - 1)) {
      error_at_noted(p_compiler,
                     p_node,
                     create_string_view("failed to add local to the locals table.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("you might be out of memory.", STRING_VIEW_CALCULATE_LENGTH, true));
      hashed_string_deinit(&hs);
      locals_remove(&fun->locals, fun->locals.count - 1, fun->locals.count - 1);
      return LOCAL_ERROR;
    }
    return index;
  }
  string name = create_string_from_string_view(p_declration.identifier);
  string message = asprint("redefining local: '%s' in the same scope.", name.chars);
  error_at(p_compiler, p_node, create_string_view_from_string(message));
  string_deinit(&message);
  string_deinit(&name);
  hashed_string_deinit(&hs);
  return REDEFINED_LOCAL;
}

bool push_function(compiler* p_compiler, string_view p_name, function_type p_type, uint8_t p_arity, ast_node* p_node) {
  function fun;
  object_string* obj_str = create_object_string(p_name, p_compiler->objects_store);
  if (obj_str == NULL) {
    return false;
  }
  object_function* obj_fu = create_object_function(obj_str, p_arity, p_compiler->objects_store);
  if (obj_fu == NULL) {
    return false;
  }
  function_init(&fun, obj_fu, p_type);
  return functions_append(&p_compiler->functions, fun);
}

static uint32_t declare_variable(compiler* p_compiler, const variable_declaration p_declration, ast_node* p_node) {
  if ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_GLOBAL) == 0) {
    return add_local(p_compiler, p_declration, p_node);
  }
  return add_global(p_compiler, p_declration, p_node);
}

static bool
define_variable(compiler* p_compiler, const variable_declaration p_declration, uint32_t p_index, ast_node* p_node) {
  if ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_GLOBAL) == 0) {
    function* fun = current_function(p_compiler);
    local* local = &fun->locals.data[p_index];
    local_remove_flag(&local->info, LOCAL_FLAG_UNINITIALIZED);
    return true;
  }
  return define_global(p_compiler, p_index, p_declration.flags, p_node);
}

static variable_declaration_flags_t variable_declaration_flags_from_modifiers(ast_declaration_modifiers_t p_declmods,
                                                                              ast_binding_modifiers_t p_bindmods) {
  return ((p_declmods & DECLARATION_GLOB) != 0 ? VARIABLE_DECLARATION_FLAGS_GLOBAL : VARIABLE_DECLRATION_FLAGS_NONE) |
         ((p_bindmods & BINDING_MUT) != 0 ? VARIABLE_DECLARATION_FLAGS_MUTABLE : VARIABLE_DECLRATION_FLAGS_NONE);
}

static bool compile_let_declration(compiler* p_compiler, ast_let_declaration* p_let) {
  string_view name = ast_identifier_expression_get_value((ast_identifier_expression*)p_let->binding->lvalue);
  variable_declaration vardecl = {
      .identifier = name,
      .flags = variable_declaration_flags_from_modifiers(p_let->declaration.modifiers, p_let->binding->modifiers)};
  compile_node(p_compiler, (ast_node*)p_let->value);
  uint32_t variable = declare_variable(p_compiler, vardecl, (ast_node*)p_let);
  if (!IS_VARIABLE_DECLARATION_VALID(variable)) {
    return false;
  }
  bool result = define_variable(p_compiler, vardecl, variable, (ast_node*)p_let);
  return result;
}

static bool compile_function_declaration(compiler* p_compiler, ast_function_declaration* p_fu) {
  string_view name = ast_identifier_expression_get_value((ast_identifier_expression*)p_fu->binding->lvalue);
  variable_declaration vardecl = {
      .identifier = name,
      .flags = variable_declaration_flags_from_modifiers(p_fu->declaration.modifiers, p_fu->binding->modifiers)};
  uint32_t index = declare_variable(p_compiler, vardecl, (ast_node*)p_fu);
  if (IS_VARIABLE_DECLARATION_VALID(index)) {
    bool status =
        compile_function_impl(p_compiler, name, FUNCTION_FUNCTION, p_fu->parameters, p_fu->body, (ast_node*)p_fu);
    return status & define_variable(p_compiler, vardecl, index, (ast_node*)p_fu);
  }
  return false;
}

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement) {
  if (compile_node(p_compiler, (ast_node*)p_expression_statement->expression)) {
    return emit_byte(p_compiler, OP_POP, (ast_node*)p_expression_statement);
  }
  return false;
}

static uint32_t find_global_index(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  uint32_t index = globals_store_get(p_compiler->globals_store, p_identifier);
  if (!IS_GLOBAL_VALID(index)) {
    // TODO: add a small pass for global resoloutin so we preserve the late bound semantics, while keeping it compile
    // time checked. (currentlly globals are not late bound).
    if (index == GLOBAL_NOT_FOUND) { // no other errors emitted from get call, but it's ok.
      string identifier_str = create_string_from_string_view(p_identifier);
      string message = asprint("undefined global: '%s'.", identifier_str.chars);
      error_at(p_compiler, p_node, create_string_view_from_string(message));
      string_deinit(&message);
      string_deinit(&identifier_str);
    }
  }
  return index;
}

// duplicate better than abstract and eat the cost at runtime!
static uint32_t get_global(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  const uint32_t index = find_global_index(p_compiler, p_identifier, p_node);
  if (!IS_GLOBAL_VALID(index)) {
    return index;
  }
  const uint32_t raw = global_get_raw_index(index);
  if (raw > OP_GET_GLOBAL_MAX) {
    emit_byte(p_compiler, OP_GET_GLOBAL_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT];
    encode_int(bytes, UINT24_BYTE_COUNT, raw);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_GET_GLOBAL, (byte)raw, p_node);
  }
  return index;
}

static uint32_t set_global(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  const uint32_t index = find_global_index(p_compiler, p_identifier, p_node);
  if (!IS_GLOBAL_VALID(index)) {
    return index;
  }
  if (!global_test_flag(index, GLOBAL_FLAG_MUTABLE)) {
    error_at(
        p_compiler,
        p_node,
        create_string_view("attempting to mutate an immutable global variable.", STRING_VIEW_CALCULATE_LENGTH, true));
  }
  const uint32_t raw = global_get_raw_index(index);
  if (raw > OP_SET_GLOBAL_MAX) {
    emit_byte(p_compiler, OP_SET_GLOBAL_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, raw);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_SET_GLOBAL, (byte)raw, p_node);
  }
  return index;
}

static uint32_t get_local(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  uint32_t* index = resolve_local(p_compiler, &hs, current_function(p_compiler), p_node);
  hashed_string_deinit(&hs);
  if (index == NULL) {
    return UNDEFINED_LOCAL;
  }
  if (*index > OP_GET_LOCAL_MAX) {
    emit_byte(p_compiler, OP_GET_LOCAL_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, *index);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_GET_LOCAL, (byte)*index, p_node);
  }
  return *index;
}

static uint32_t set_local(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  uint32_t* index = resolve_local(p_compiler, &hs, current_function(p_compiler), p_node);
  hashed_string_deinit(&hs);
  if (index == NULL) {
    return UNDEFINED_LOCAL;
  }
  local local = current_function(p_compiler)->locals.data[*index];
  if (!local_test_flag(local.info, LOCAL_FLAG_MUTABLE)) {
    error_at(
        p_compiler,
        p_node,
        create_string_view("attempting to mutate an immutable local variable.", STRING_VIEW_CALCULATE_LENGTH, true));
    return LOCAL_ILL_MUTATION;
  }
  if (*index > OP_SET_LOCAL_MAX) {
    emit_byte(p_compiler, OP_SET_LOCAL_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, *index);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_SET_LOCAL, (byte)*index, p_node);
  }
  return *index;
}

static uint32_t add_upvalue(compiler* p_compiler, uint32_t p_upvalue, bool p_is_local, ast_node* p_node) {
  function* fcn = current_function(p_compiler);
  upvalue up = create_upvalue(p_upvalue, p_is_local);
  for (uint32_t i = 0; i < fcn->upvalues.count; ++i) {
    if (fcn->upvalues.data[i].info == up.info) {
      return i;
    }
  }
  if (fcn->upvalues.count >= OP_XX_UPVALUE_LONG_MAX) {
    string note = asprint("limit is: %d.", OP_XX_UPVALUE_LONG_MAX);
    error_at_noted(p_compiler,
                   p_node,
                   create_string_view("too many upvalues in a single function.", STRING_VIEW_CALCULATE_LENGTH, true),
                   create_string_view_from_string(note));
    string_deinit(&note);
  }
  upvalues_append(&fcn->upvalues, up);
  fcn->function.function->upvalues = fcn->upvalues.count;
  return fcn->upvalues.count - 1;
}

static uint32_t resolve_upvalue(compiler* p_compiler, hashed_string p_identifier, uint32_t p_index, ast_node* p_node) {
  const uint32_t functions = p_compiler->functions.count;
  if (functions - p_index - 2 < 0) {
    return UINT32_MAX;
  }

  function* current = &p_compiler->functions.data[functions - p_index - 1];
  function* up = &p_compiler->functions.data[functions - p_index - 2];
  uint32_t* local = resolve_local(p_compiler, &p_identifier, up, p_node);
  if (local != NULL) {
    local_set_flag(local, LOCAL_FLAG_CAPTURED);
    return add_upvalue(p_compiler, *local, true, p_node);
  }
  uint32_t upvalue = resolve_upvalue(p_compiler, p_identifier, p_index + 1, p_node);
  if (upvalue != UINT32_MAX) {
    return add_upvalue(p_compiler, upvalue, false, p_node);
  }
  return UINT32_MAX;
}

static uint32_t get_upvalue(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  uint32_t up = resolve_upvalue(p_compiler, hs, p_compiler->functions.count - 1, p_node);
  if (!IS_UPVALUE_INDEX_VALID(up)) {
    return up;
  }
  hashed_string_deinit(&hs);
  if (up > OP_GET_UPVALUE_MAX) {
    emit_byte(p_compiler, OP_GET_UPVALUE_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, up);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_GET_UPVALUE, (byte)up, p_node);
  }
  return up;
}

static uint32_t set_upvalue(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  uint32_t up = resolve_upvalue(p_compiler, hs, p_compiler->functions.count - 1, p_node);
  if (!IS_UPVALUE_INDEX_VALID(up)) {
    return up;
  }
  hashed_string_deinit(&hs);
  if (up > OP_SET_UPVALUE_MAX) {
    emit_byte(p_compiler, OP_SET_UPVALUE_LONG, p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, up);
    emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  } else {
    emit_2bytes(p_compiler, OP_SET_UPVALUE, (byte)up, p_node);
  }
  return up;
}

static bool get_variable(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  if (!IS_LOCAL_INDEX_VALID(get_local(p_compiler, p_identifier, p_node))) {
    if (!IS_UPVALUE_INDEX_VALID(get_upvalue(p_compiler, p_identifier, p_node))) {
      return IS_GLOBAL_VALID(get_global(p_compiler, p_identifier, p_node));
    }
  }
  return true;
}

static bool set_variable(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  if (!IS_LOCAL_INDEX_VALID(set_local(p_compiler, p_identifier, p_node))) {
    if (!IS_UPVALUE_INDEX_VALID(set_upvalue(p_compiler, p_identifier, p_node))) {
      return IS_GLOBAL_VALID(set_global(p_compiler, p_identifier, p_node));
    }
  }
  return true;
}

static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier) {
  string_view ident_str = ast_identifier_expression_get_value(p_identifier);
  return get_variable(p_compiler, ident_str, (ast_node*)p_identifier);
}

bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof) {
  // bool res = emit_byte(p_compiler, OP_RETURN, (ast_node*)p_eof);
  // end_scope(p_compiler,
  //           (ast_node*)p_eof); // TODO: we need to end scope at compile time but we dont really care about pops
  //           because
  //  vm will automatically handle that, so this is just extra code bloat remove it.
  return true;
}

bool compile_print_statement(compiler* p_compiler, ast_print_statement* p_print) {
  if (compile_node(p_compiler, (ast_node*)p_print->expression)) {
    return emit_byte(p_compiler, OP_PRINT, (ast_node*)p_print);
  }
  return false;
}

bool compile_compound_statement(compiler* p_compiler, ast_compound_statement* p_compound) {
  bool status = true;
  begin_scope(p_compiler);
  for (uint32_t i = 0; i < p_compound->statements.count; ++i) {
    status &= compile_node(p_compiler, (ast_node*)p_compound->statements.data[i]);
  }
  end_scope(p_compiler, (ast_node*)p_compound, true);
  return status;
}

static uint32_t emit_jump(compiler* p_compiler, op_code p_instruction, ast_node* p_node) {
  byte bytes[OP_CODE_WIDTH + OP_XX_JUMP_OPERANDS_WIDTH] = {p_instruction, 0x00, 0x00, 0x00};
  const uint32_t size = OP_CODE_WIDTH + OP_XX_JUMP_OPERANDS_WIDTH;
  emit_bytes(p_compiler, bytes, size, p_node);
  return current_chunk(p_compiler)->code.count - OP_XX_JUMP_OPERANDS_WIDTH;
}

static bool patch_jump(compiler* p_compiler, uint32_t p_operands_start, uint32_t p_jump_end, ast_node* p_node) {
  const uint32_t jmp = p_jump_end - p_operands_start;
  byte* code = current_chunk(p_compiler)->code.data;
  encode_int(code + p_operands_start, UINT24_BYTE_COUNT, jmp);
  return true;
}

static bool patch_loop(compiler* p_compiler, uint32_t p_operands_start, uint32_t p_jump_end, ast_node* p_node) {
  const uint32_t jmp = p_operands_start - p_jump_end;
  byte* code = current_chunk(p_compiler)->code.data;
  encode_int(code + p_operands_start, UINT24_BYTE_COUNT, jmp);
  return true;
}

static bool emit_loop(compiler* p_compiler, const uint32_t p_loop_start, ast_node* p_node) {
  bool status = emit_byte(p_compiler, OP_LOOP, p_node);
  byte bytes[OP_LOOP_OPERANDS_WIDTH] = {0x00, 0x00, 0x00};
  chunk* chunk = current_chunk(p_compiler);
  const uint32_t loop_length = chunk->code.count - p_loop_start;
  encode_int(bytes, OP_LOOP_OPERANDS_WIDTH, loop_length);
  status &= emit_bytes(p_compiler, bytes, OP_LOOP_OPERANDS_WIDTH, p_node);
  if (loop_length > OP_LOOP_MAX) {
    string note = asprint("limit is: %d.", OP_LOOP_MAX);
    error_at_noted(p_compiler,
                   p_node,
                   create_string_view("too many instructions to loop over.", STRING_VIEW_CALCULATE_LENGTH, true),
                   create_string_view_from_string(note));
    string_deinit(&note);
    return false;
  }
  return status;
}

bool compile_if_statement(compiler* p_compiler, ast_if_statement* p_if) {
  bool status = compile_node(p_compiler, (ast_node*)p_if->condition);
  uint32_t cons_jmp = emit_jump(p_compiler, OP_FALSY_JUMP, (ast_node*)p_if);
  status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_if);
  status &= compile_node(p_compiler, (ast_node*)p_if->consequence);
  chunk* chunk = current_chunk(p_compiler);
  uint32_t alt_jump = emit_jump(p_compiler, OP_JUMP, (ast_node*)p_if);
  status &= patch_jump(p_compiler, cons_jmp, chunk->code.count, (ast_node*)p_if);
  emit_byte(p_compiler, OP_POP, (ast_node*)p_if);
  if (p_if->alternative != NULL) {
    status &= compile_node(p_compiler, (ast_node*)p_if->alternative);
  }
  status &= patch_jump(p_compiler, alt_jump, chunk->code.count, (ast_node*)p_if);
  return status;
}

static bool patch_loop_context(compiler* p_compiler, loop_context* p_ctx, ast_node* p_node) {
  chunk* chunk = current_chunk(p_compiler);
  for (uint32_t i = 0; i < p_ctx->breaks.count; ++i) {
    patch_jump(p_compiler, p_ctx->breaks.data[i], p_ctx->break_target, p_node);
  }
  for (uint32_t i = 0; i < p_ctx->continues.count; ++i) {
    uint32_t cont = p_ctx->continues.data[i];
    if (p_ctx->continue_forward) {
      patch_jump(p_compiler, cont, p_ctx->continue_target, p_node);
    } else {
      patch_loop(p_compiler, cont, p_ctx->continue_target, p_node);
    }
  }
  return true;
}

bool compile_while_statement(compiler* p_compiler, ast_while_statement* p_while) {
  function* fun = current_function(p_compiler);
  {
    loop_context ctx;
    loop_context_init(&ctx);
    if (!loop_stack_append(&fun->loop_stack, ctx)) {
      error_at_noted(p_compiler,
                     (ast_node*)p_while,
                     create_string_view("failed to push a new loop context.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("you might be out of memory.", STRING_VIEW_CALCULATE_LENGTH, true));
      loop_context_deinit(&ctx);
      return false;
    }
  }
#define CTX fun->loop_stack.data[fun->loop_stack.count - 1]
  CTX.scope_depth = fun->scopes.count - 1;
  chunk* chunk = current_chunk(p_compiler);
  const uint32_t loop_start = chunk->code.count;
  bool status = compile_node(p_compiler, (ast_node*)p_while->condition);
  const uint32_t exit_jmp = emit_jump(p_compiler, OP_FALSY_JUMP, (ast_node*)p_while);
  emit_byte(p_compiler, OP_POP, (ast_node*)p_while);
  status &= compile_node(p_compiler, (ast_node*)p_while->body);
  status &= emit_loop(p_compiler, loop_start, (ast_node*)p_while);
  status &= patch_jump(p_compiler, exit_jmp, current_chunk(p_compiler)->code.count, (ast_node*)p_while);
  CTX.continue_target = loop_start;
  CTX.break_target = chunk->code.count;
  status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_while);
  status &= patch_loop_context(p_compiler, &CTX, (ast_node*)p_while);
  loop_stack_remove(&fun->loop_stack, fun->loop_stack.count - 1, fun->loop_stack.count - 1);
#undef CTX
  return status;
}

bool compile_for_statement(compiler* p_compiler, ast_for_statement* p_for) {
  function* fun = current_function(p_compiler);
  {
    loop_context ctx;
    loop_context_init(&ctx);
    if (!loop_stack_append(&fun->loop_stack, ctx)) {
      error_at_noted(p_compiler,
                     (ast_node*)p_for,
                     create_string_view("failed to push a new loop context.", STRING_VIEW_CALCULATE_LENGTH, true),
                     create_string_view("you might be out of memory.", STRING_VIEW_CALCULATE_LENGTH, true));
      loop_context_deinit(&ctx);
      return false;
    }
  }
#define CTX fun->loop_stack.data[fun->loop_stack.count - 1]
  begin_scope(p_compiler);
  chunk* chunk = current_chunk(p_compiler);
  CTX.scope_depth = fun->scopes.count - 1;
  bool status = true;
  if (p_for->initializer != NULL) {
    status &= compile_node(p_compiler, (ast_node*)p_for->initializer);
  }
  uint32_t exit_jump = UINT32_MAX;
  const uint32_t loop_start = chunk->code.count;
  if (p_for->condition != NULL) {
    status &= compile_node(p_compiler, (ast_node*)p_for->condition);
    exit_jump = emit_jump(p_compiler, OP_FALSY_JUMP, (ast_node*)p_for->condition);
    status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_for);
  }
  if (p_for->update != NULL) {
    CTX.continue_forward = true;
  }
  status &= compile_node(p_compiler, (ast_node*)p_for->body);
  CTX.continue_target = loop_start;
  if (p_for->update != NULL) {
    CTX.continue_target = chunk->code.count;
    status &= compile_node(p_compiler, (ast_node*)p_for->update);
    status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_for);
  }
  emit_loop(p_compiler, loop_start, (ast_node*)p_for);
  CTX.break_target = chunk->code.count;
  if (exit_jump != UINT32_MAX) {
    status &= patch_jump(p_compiler, exit_jump, current_chunk(p_compiler)->code.count, (ast_node*)p_for);
    status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_for);
  }
  patch_loop_context(p_compiler, &CTX, (ast_node*)p_for);
  loop_stack_remove(&fun->loop_stack, fun->loop_stack.count - 1, fun->loop_stack.count - 1);
  end_scope(p_compiler, (ast_node*)p_for, true);
  return status;
#undef CTX
}

bool compile_control_flow_statement(compiler* p_compiler, ast_control_flow_statement* p_control) {
  function* fun = current_function(p_compiler);
  if (fun->loop_stack.count == 0) {
    error_at(p_compiler,
             (ast_node*)p_control,
             create_string_view("illegal control flow outside of loop.", STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  bool status = true;
  loop_context* ctx = &fun->loop_stack.data[fun->loop_stack.count - 1];
  uint32_t curr_depth = fun->scopes.count - 1;
  if (curr_depth > ctx->scope_depth && fun->locals.count > 0) {
    uint32_t pops = 0;
    for (uint32_t i = fun->locals.count - 1; i-- > 0;) {
      if (local_get_raw_index(fun->locals.data[i].info) > ctx->scope_depth) {
        pops++;
      }
    }
    for (uint32_t i = 0; i < pops; ++i) {
      status &= emit_byte(p_compiler, OP_POP, (ast_node*)p_control);
    }
  }
  if (p_control->type == CONTROL_FLOW_BREAK) {
    uint32_t jmp = emit_jump(p_compiler, OP_JUMP, (ast_node*)p_control);
    status &= uint32_array_append(&ctx->breaks, jmp);
  } else if (p_control->type == CONTROL_FLOW_CONTINUE) {
    uint32_t jmp = emit_jump(p_compiler, ctx->continue_forward ? OP_JUMP : OP_LOOP, (ast_node*)p_control);
    status &= uint32_array_append(&ctx->continues, jmp);
  }
  return status;
}

static bool compile_return_statement(compiler* p_compiler, ast_return_statement* p_return) {
  if (p_return->returned == NULL) {
    return emit_default_return(p_compiler, (ast_node*)p_return);
  }
  bool status = compile_node(p_compiler, (ast_node*)p_return->returned);
  return status & emit_byte(p_compiler, OP_RETURN, (ast_node*)p_return);
}

static bool compile_number_expression(compiler* p_compiler, ast_number_expression* p_number) {
  double number = ast_number_expression_get_value(p_number);
  if (number == AST_NUMBER_EXPRESSION_PARSE_ERROR) {
    error_at(p_compiler,
             (ast_node*)p_number,
             create_string_view("failed to parse number.", STRING_VIEW_CALCULATE_LENGTH, true));
  }
  return emit_constant(p_compiler, NUMBER_AS_VALUE(number), (ast_node*)p_number);
}

static bool compile_string_expression(compiler* p_compiler, ast_string_expression* p_string) {
  string_view parsed_str = ast_string_expression_get_value(p_string);
  if (parsed_str.chars == NULL) {
    error_at(p_compiler,
             (ast_node*)p_string,
             create_string_view("failed to parse string.", STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  object_string* object_str = create_object_string(parsed_str, p_compiler->objects_store);
  if (object_str == NULL) {
    error_at(p_compiler,
             (ast_node*)p_string,
             create_string_view("failed create string object.", STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  return emit_constant(p_compiler, OBJECT_AS_VALUE(object_str), (ast_node*)p_string);
}

static bool compile_prefix_unary_expression(compiler* p_compiler, ast_prefix_unary_expression* p_expression) {
  if (!compile_node(p_compiler, (ast_node*)p_expression->right)) {
    return false;
  }
  switch (p_expression->_operator) {
  case OPERATOR_MINUS:
    return emit_byte(p_compiler, OP_NEGATE, (ast_node*)p_expression);
  case OPERATOR_NOT:
  case OPERATOR_BANG:
    return emit_byte(p_compiler, OP_NOT, (ast_node*)p_expression);
  default:;
  }
  return false;
}

static bool compile_infix_binary_expression(compiler* p_compiler, ast_infix_binary_expression* p_expression) {
  if (p_expression->_operator == OPERATOR_AND || p_expression->_operator == OPERATOR_OR) {
    return compile_logical_operator(p_compiler, p_expression);
  }
  if (compile_node(p_compiler, (ast_node*)p_expression->left)) {
    if (compile_node(p_compiler, (ast_node*)p_expression->right)) {
      switch (p_expression->_operator) {
      case OPERATOR_PLUS:
        return emit_byte(p_compiler, OP_ADD, (ast_node*)p_expression);
      case OPERATOR_MINUS:
        return emit_byte(p_compiler, OP_SUBTRACT, (ast_node*)p_expression);
      case OPERATOR_ASTERISK:
        return emit_byte(p_compiler, OP_MULTIPLY, (ast_node*)p_expression);
      case OPERATOR_SLASH:
        return emit_byte(p_compiler, OP_DIVIDE, (ast_node*)p_expression);
      case OPERATOR_EQUAL:
        return emit_byte(p_compiler, OP_EQUAL, (ast_node*)p_expression);
      case OPERATOR_BANG_EQUAL:
        return emit_byte(p_compiler, OP_NOT_EQUAL, (ast_node*)p_expression);
      case OPERATOR_LESS:
        return emit_byte(p_compiler, OP_LESS, (ast_node*)p_expression);
      case OPERATOR_GREATER:
        return emit_byte(p_compiler, OP_GREATER, (ast_node*)p_expression);
      case OPERATOR_LESS_EQUAL:
        return emit_byte(p_compiler, OP_LESS_EQUAL, (ast_node*)p_expression);
      case OPERATOR_GREATER_EQUAL:
        return emit_byte(p_compiler, OP_GREATER_EQUAL, (ast_node*)p_expression);
      default:
        assert(false);
      }
    }
  }
  return false;
}

static bool compile_assign_expression(compiler* p_compiler, ast_assign_expression* p_assign) {
  if (p_assign->left->node.node_type != AST_IDENTIFIER_EXPRESSION) {
    return false;
  }
  compile_node(p_compiler, (ast_node*)p_assign->right);
  ast_identifier_expression* ident = (ast_identifier_expression*)p_assign->left;
  string_view name = ast_identifier_expression_get_value(ident);
  set_variable(p_compiler, name, (ast_node*)ident);
  return true;
}

static uint32_t
compile_expressions_list(compiler* p_compiler, ast_expressions_list* p_list, uint32_t p_limit, ast_node* p_node) {
  if (p_list->count > p_limit) {
    string note = asprint("limit is: %d.", p_limit);
    error_at_noted(p_compiler,
                   (ast_node*)p_node,
                   create_string_view("too many arguments.", STRING_VIEW_CALCULATE_LENGTH, true),
                   create_string_view_from_string(note));
    string_deinit(&note);
    return UINT32_MAX;
  }
  for (uint32_t i = 0; i < p_list->count; ++i) {
    ast_expression* arg = p_list->data[i];
    compile_node(p_compiler, (ast_node*)arg);
  }
  return p_list->count;
}

bool compile_call_expression(compiler* p_compiler, ast_call_expression* p_call) {
  bool status = compile_node(p_compiler, (ast_node*)p_call->callable);
  uint32_t count = compile_expressions_list(p_compiler, &p_call->arguments, OP_CALL_MAX, (ast_node*)p_call);
  status &= count != UINT32_MAX;
  return status & emit_2bytes(p_compiler, OP_CALL, count, (ast_node*)p_call);
}

bool compile_logical_operator(compiler* p_compiler, ast_infix_binary_expression* p_logical) {
  bool result = compile_node(p_compiler, (ast_node*)p_logical->left);
  op_code instruction = p_logical->_operator == OPERATOR_AND ? OP_FALSY_JUMP : OP_TRUTHY_JUMP;
  uint32_t jmp = emit_jump(p_compiler, instruction, (ast_node*)p_logical);
  result &= emit_byte(p_compiler, OP_POP, (ast_node*)p_logical);
  result &= compile_node(p_compiler, (ast_node*)p_logical->right);
  return result & patch_jump(p_compiler, jmp, current_chunk(p_compiler)->code.count, (ast_node*)p_logical);
}

static bool compile_postfix_unary_expression(compiler* p_compiler, ast_postfix_unary_expression* p_expression) {
}

static bool compile_boolean_expression(compiler* p_compiler, ast_boolean_expression* p_boolean) {
  return emit_byte(p_compiler, ast_boolean_expression_get_value(p_boolean) ? OP_TRUE : OP_FALSE, (ast_node*)p_boolean);
}

static bool compile_null_expression(compiler* p_compiler, ast_null_expression* p_null) {
  return emit_byte(p_compiler, OP_NULL, (ast_node*)p_null);
}

bool compile_function_expression(compiler* p_compiler, ast_function_expression* p_fu) {
  return compile_function_impl(p_compiler,
                               create_string_view("<lambda>", STRING_VIEW_CALCULATE_LENGTH, true),
                               FUNCTION_FUNCTION,
                               p_fu->parameters,
                               p_fu->body,
                               (ast_node*)p_fu);
}

static bool compile_function_impl(compiler* p_compiler,
                                  string_view p_name,
                                  function_type p_type,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  ast_node* p_node) {
  if (p_params.count > UINT8_MAX) {
    string note = asprint("limit is: %d", UINT8_MAX);
    error_at_noted(p_compiler,
                   p_node,
                   create_string_view("too many function parameters.", STRING_VIEW_CALCULATE_LENGTH, true),
                   create_string_view_from_string(note));
    string_deinit(&note);
    return false;
  }
  bool status = push_function(p_compiler, p_name, p_type, p_params.count, p_node);
  begin_scope(p_compiler);
  variable_declaration decl = {.identifier = p_name, .flags = DECLARATION_NONE};
  status &= IS_LOCAL_INDEX_VALID(add_local(p_compiler, decl, p_node));
  for (uint32_t i = 0; i < p_params.count; ++i) {
    ast_binding* binding = p_params.data[i];
    variable_declaration decl = {
        .identifier = ast_identifier_expression_get_value((ast_identifier_expression*)binding->lvalue),
        .flags = variable_declaration_flags_from_modifiers(DECLARATION_NONE, binding->modifiers)};
    uint32_t index = declare_variable(p_compiler, decl, (ast_node*)binding);
    status &= IS_VARIABLE_DECLARATION_VALID(index);
    if (status) {
      status &= define_variable(p_compiler, decl, index, (ast_node*)binding);
    } else {
      error_at(p_compiler,
               (ast_node*)binding,
               create_string_view("failed to bind function parameter.", STRING_VIEW_CALCULATE_LENGTH, true));
      break;
    }
  }
  status &= compile_node(p_compiler, (ast_node*)p_body);
  status &= emit_default_return(p_compiler, p_node);
  end_scope(p_compiler, p_node, false);
  function* current = current_function(p_compiler);
  object_function* funct = current->function.function; // TODO guard in gc.
  upvalues ups = current->upvalues;
  upvalues_init(&current->upvalues); // poor man's move construction
  status &= functions_remove(&p_compiler->functions, p_compiler->functions.count - 1, p_compiler->functions.count - 1);
  status &= emit_byte(p_compiler, OP_CLOSURE, p_node);
  byte bytes[OP_CONSTANT_LONG_OPERANDS_WIDTH] = {0};
  uint32_t cons = create_constant(p_compiler, OBJECT_AS_VALUE(funct), p_node);
  if (!IS_CONSTANT_VALID(cons)) {
    error_at(p_compiler,
             p_node,
             create_string_view("failed to create constant for function.", STRING_VIEW_CALCULATE_LENGTH, true));
    status = false;
  }
  encode_int(bytes, OP_CONSTANT_LONG_OPERANDS_WIDTH, cons);
  status &= emit_bytes(p_compiler, bytes, OP_CONSTANT_LONG_OPERANDS_WIDTH, p_node);
  for (uint32_t i = 0; i < ups.count; ++i) {
    status &= emit_byte(p_compiler, (byte)upvalue_is_local(&ups.data[i]), p_node);
    byte bytes[UINT24_BYTE_COUNT] = {0};
    encode_int(bytes, UINT24_BYTE_COUNT, upvalue_get_index(&ups.data[i]));
    status &= emit_bytes(p_compiler, bytes, UINT24_BYTE_COUNT, p_node);
  }
  upvalues_deinit(&ups);

#if defined(OK_DEBUG_DUMP_CODE) // this is really useful
  disassembler disassembler;
  disassembler_specs specs = {.chunk = &funct->chunk, .globals_store = p_compiler->globals_store};
  disassembler_init(&disassembler, specs);
  debug_disassemble_chunk(&disassembler, funct->name->string.chars);
  disassembler_deinit(&disassembler);
#endif // defined (OK_DEBUG_DUMP_CODE)
  return status;
}
