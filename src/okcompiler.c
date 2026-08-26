#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "okcompiler.h"
#include "okdebug.h"
#include "okobject.h"
#include "okutils.h"
#include "okvalue.h"

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
static void function_init(function* p_function);
static void function_deinit(function* p_function);
ARRAY_DEFINE_DEFAULT(locals, local, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(scopes, scope, scope_deinit)
ARRAY_DEFINE_DEFAULT(functions, function, function_deinit)

static void error_at(compiler* p_compiler, const ast_node* p_node, const string_view p_message);
static void
error_at_noted(compiler* p_compiler, const ast_node* p_node, const string_view p_message, const string_view p_note);
static chunk* current_chunk(compiler* p_compiler);
static bool emit_byte(compiler* p_compiler, byte p_byte, ast_node* p_node);
static bool emit_2bytes(compiler* p_compiler, byte p_1st_byte, byte p_2nd_byte, ast_node* p_node);
static bool emit_bytes(compiler* p_compiler, byte* p_bytes, size_t p_bytes_count, ast_node* p_node);
static bool emit_constant(compiler* p_compiler, value p_value, ast_node* p_node);

static bool end_compile(compiler* p_compiler, ast_node* p_node);
static uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node);
static void begin_scope(compiler* p_compiler);
static void end_scope(compiler* p_compiler, ast_node* p_node);

static bool compile_node(compiler* p_compiler, ast_node* p_node);

static bool compile_root(compiler* p_compiler, ast_root* p_root);

static bool compile_let_declration(compiler* p_compiler, ast_let_declaration* p_let);

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement);
static bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof);
static bool compile_print_statement(compiler* p_compiler, ast_print_statement* p_print);
static bool compile_compound_statement(compiler* p_compiler, ast_compound_statement* p_compound);

static bool compile_expression(compiler* p_compiler, ast_expression* p_expression);
static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier);
static bool compile_number_expression(compiler* p_compiler, ast_number_expression* p_number);
static bool compile_string_expression(compiler* p_compiler, ast_string_expression* p_string);
static bool compile_prefix_unary_expression(compiler* p_compiler, ast_prefix_unary_expression* p_expression);
static bool compile_infix_binary_expression(compiler* p_compiler, ast_infix_binary_expression* p_expression);
static bool compile_assign_expression(compiler* p_compiler, ast_assign_expression* p_assign);
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
  p_compiler->local_index = 0;
  functions_init(&p_compiler->functions);
}

void compiler_deinit(compiler* p_compiler) {
  p_compiler->current_chunk = NULL;
  p_compiler->source = NULL;
  p_compiler->objects_store = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
  p_compiler->local_index = 0;
  functions_deinit(&p_compiler->functions);
}

compile_result compiler_compile(compiler* p_compiler, compiler_specs p_specs) {
  chunk* _chunk = (chunk*)malloc(sizeof(chunk));
  chunk_init(_chunk);
  p_compiler->current_chunk = _chunk;
  p_compiler->source = p_specs.source;
  p_compiler->objects_store = p_specs.objects_store;
  p_compiler->globals_store = p_specs.globals_store;
  function fun;
  function_init(&fun);
  functions_append(&p_compiler->functions, fun);
  begin_scope(p_compiler);
  bool res = compile_node(p_compiler, (ast_node*)p_specs.root);
  functions_remove(&p_compiler->functions, p_compiler->functions.count - 1, p_compiler->functions.count - 1);
  compile_result result;
  result.chunk = _chunk;
  if (p_compiler->had_error == false && res != false) {
    result.status = COMPILE_OK;
#if defined(OK_DEBUG_DUMP_CODE)
    disassembler disassembler;
    disassembler_specs specs = {.chunk = _chunk, .globals_store = p_compiler->globals_store};
    disassembler_init(&disassembler, specs);
    debug_disassemble_chunk(&disassembler, "code");
    disassembler_deinit(&disassembler);
#endif // defined (OK_DEBUG_DUMP_CODE)
  } else {
    result.status = COMPILE_ERROR;
  }
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

chunk* current_chunk(compiler* p_compiler) {
  return p_compiler->current_chunk;
}

function* current_function(compiler* p_compiler) {
  return &p_compiler->functions.data[p_compiler->functions.count - 1];
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

bool end_compile(compiler* p_compiler, ast_node* p_node) {
  return emit_byte(p_compiler, OP_RETURN, p_node);
}

void begin_scope(compiler* p_compiler) {
  scope scp;
  scope_init(&scp);
  function* fun = current_function(p_compiler);
  scopes_append(&fun->scopes, scp);
}

void end_scope(compiler* p_compiler, ast_node* p_node) {
  function* fun = current_function(p_compiler);
  scope* scp = &fun->scopes.data[fun->scopes.count - 1];
  for (uint32_t i = 0; i < scp->locals.count; ++i) {
    emit_byte(p_compiler, OP_POP, p_node);
  }
  scopes_remove(&fun->scopes, fun->scopes.count - 1, fun->scopes.count - 1); // TODO: pop
}

void scope_init(scope* p_scope) {
  scope_locals_init(&p_scope->locals);
}

void scope_deinit(scope* p_scope) {
  scope_locals_deinit(&p_scope->locals);
}

void function_init(function* p_function) {
  locals_init(&p_function->locals);
  scopes_init(&p_function->scopes);
}

static void function_deinit(function* p_function) {
  scopes_deinit(&p_function->scopes);
  locals_deinit(&p_function->locals);
}

uint32_t create_constant(compiler* p_compiler, value p_value, ast_node* p_node) {
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
    return compile_assign_expression(p_compiler, (ast_assign_expression*)p_node);
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
    return compile_print_statement(p_compiler, (ast_print_statement*)p_node);
  case AST_COMPOUND_STATEMENT:
    return compile_compound_statement(p_compiler, (ast_compound_statement*)p_node);
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
    return compile_let_declration(p_compiler, (ast_let_declaration*)p_node);
  case AST_FUNCTION_DECLARATION:
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

static uint32_t* resolve_local(compiler* p_compiler, hashed_string* p_identifier, ast_node* p_node) {
  uint32_t* index = NULL;
  function* fun = current_function(p_compiler);
  uint32_t depth = fun->scopes.count;
  while (index == NULL && depth != 0) {
    index = scope_locals_get(&fun->scopes.data[--depth].locals, *p_identifier);
    // TODO: report that we skipped if we couldn't resolve other local with same name.
    if (index != NULL &&
        local_test_flag(*index, LOCAL_FLAG_UNINITIALIZED)) { // let x; { let x = x; /* skip the inner */ }
      index = NULL;
    }
  }
  return index;
}

static uint32_t add_local(compiler* p_compiler, const variable_declaration p_declration, ast_node* p_node) {
  hashed_string hs = create_hashed_string_hash(p_declration.identifier);
  if (hs.string.chars == NULL) {
    error_at(p_compiler,
             p_node,
             create_string_view("failed to allocate memory for hashed string.", STRING_VIEW_CALCULATE_LENGTH, true));
    return LOCAL_ERROR;
  }
  scope* scp = current_scope(p_compiler);
  uint32_t* resolved = scope_locals_get(&scp->locals, hs);
  if (resolved == NULL) {
    local local;
    uint32_t index = p_compiler->local_index++;
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
    function* fun = current_function(p_compiler);
    local.depth = index & LOCAL_FLAGS_MASK;
    local_set_flag(&local.depth, LOCAL_FLAG_UNINITIALIZED);
    if ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0) {
      local_set_flag(&local.depth, LOCAL_FLAG_MUTABLE);
    }
    if (!locals_append(&fun->locals, local)) {
      error_at(p_compiler,
               p_node,
               create_string_view("failed to add local to the locals array.", STRING_VIEW_CALCULATE_LENGTH, true));
      hashed_string_deinit(&hs);
      return LOCAL_ERROR;
    }
    if (!scope_locals_set(&current_scope(p_compiler)->locals, hs, fun->locals.count - 1)) {
      error_at(p_compiler,
               p_node,
               create_string_view("failed to add local to the locals table.", STRING_VIEW_CALCULATE_LENGTH, true));
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
    local_remove_flag(&local->depth, LOCAL_FLAG_UNINITIALIZED);
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
  uint32_t* index = resolve_local(p_compiler, &hs, p_node);
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
  uint32_t* index = resolve_local(p_compiler, &hs, p_node);
  hashed_string_deinit(&hs);
  if (index == NULL) {
    return UNDEFINED_LOCAL;
  }
  local local = current_function(p_compiler)->locals.data[*index];
  if (!local_test_flag(local.depth, LOCAL_FLAG_MUTABLE)) {
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

static bool get_variable(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  if (!IS_LOCAL_INDEX_VALID(get_local(p_compiler, p_identifier, p_node))) {
    return IS_GLOBAL_VALID(get_global(p_compiler, p_identifier, p_node));
  }
  return true;
}

static bool set_variable(compiler* p_compiler, const string_view p_identifier, ast_node* p_node) {
  if (!IS_LOCAL_INDEX_VALID(set_local(p_compiler, p_identifier, p_node))) {
    return IS_GLOBAL_VALID(set_global(p_compiler, p_identifier, p_node));
  }
  return true;
}

static bool compile_identifier(compiler* p_compiler, ast_identifier_expression* p_identifier) {
  string_view ident_str = ast_identifier_expression_get_value(p_identifier);
  return get_variable(p_compiler, ident_str, (ast_node*)p_identifier);
}

bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof) {
  bool res = emit_byte(p_compiler, OP_RETURN, (ast_node*)p_eof);
  end_scope(p_compiler,
            (ast_node*)p_eof); // TODO: we need to end scope at compile time but we dont really care about pops because
                               // vm will automatically handle that, so this is just extra code bloat remove it.
  return res;
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
  end_scope(p_compiler, (ast_node*)p_compound);
  return status;
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

      default:;
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

static bool compile_postfix_unary_expression(compiler* p_compiler, ast_postfix_unary_expression* p_expression) {
}

static bool compile_boolean_expression(compiler* p_compiler, ast_boolean_expression* p_boolean) {
  return emit_byte(p_compiler, ast_boolean_expression_get_value(p_boolean) ? OP_TRUE : OP_FALSE, (ast_node*)p_boolean);
}

static bool compile_null_expression(compiler* p_compiler, ast_null_expression* p_null) {
  return emit_byte(p_compiler, OP_NULL, (ast_node*)p_null);
}
