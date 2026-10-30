#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "okcompiler.h"
#include "okdebug.h"
#include "okdef.h"
#include "okgc.h"
#include "okobject.h"
#include "okutils.h"
#include "okvalue.h"

#define ARE_KEYS_EQUAL(lhs, rhs)                                                                                       \
  (string_get_length(&lhs.string) == string_get_length(&rhs.string) &&                                                 \
   strncmp(lhs.string.chars, rhs.string.chars, string_get_length(&lhs.string)) == 0)
#define GET_HASH(key) (key.hash)

ARRAY_DEFINE_DEFAULT(top_level_declarations, variable_declaration, ARRAY_DEFAULT_TYPE_DEINIT);

TABLE_DEFINE_DEFAULT(scope_registers,
                     hashed_string,
                     uint16_t,
                     hash_t,
                     ARE_KEYS_EQUAL,
                     hashed_string_deinit,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     GET_HASH)

static void register_allocator_init(register_allocator* p_regalloc, allocators* p_alloc) {
  p_regalloc->registers_count = 0;
  p_regalloc->local_top = 0;
  p_regalloc->temp_top = 0;
  p_regalloc->alloc = p_alloc;
  registers_init(&p_regalloc->registers, p_alloc);
}

static void register_allocator_deinit(register_allocator* p_regalloc) {
  registers_deinit(&p_regalloc->registers, p_regalloc->alloc);
  register_allocator_init(p_regalloc, p_regalloc->alloc);
}

static bool register_allocator_allocate_local(register_allocator* p_regalloc, _register* p_out) {
  if (p_regalloc->local_top >= REGISTERS_MAX) {
    p_out->errors = REGISTER_OVERFLOW;
    return false;
  }
  p_regalloc->registers.count = p_regalloc->local_top;
  p_out->index = p_regalloc->local_top;
  p_out->flags |= REGISTER_FLAG_LOCAL; // preserve incoming flags (mutability)
  p_out->errors = REGISTER_OK;

  if (!registers_append(&p_regalloc->registers, *p_out)) {
    p_out->errors = REGISTER_ALLOCATION_FAILED;
    return false;
  }
  p_regalloc->temp_top = ++p_regalloc->local_top;
  p_regalloc->registers_count++;
  return true;
}

static bool register_allocator_allocate_temp(register_allocator* p_regalloc, _register* p_out) {
  if (p_regalloc->registers_count >= REGISTERS_MAX) {
    p_out->errors = REGISTER_OVERFLOW;
    return false;
  }
  p_out->index = p_regalloc->temp_top;
  p_out->flags = REGISTER_FLAG_NONE;
  p_out->errors = REGISTER_OK;
  if (!registers_append(&p_regalloc->registers, *p_out)) {
    p_out->errors = REGISTER_ALLOCATION_FAILED;
    return false;
  }
  if (++p_regalloc->temp_top > p_regalloc->registers_count) {
    p_regalloc->registers_count = p_regalloc->temp_top;
  }
  return true;
}

static void register_allocator_rollback_local(register_allocator* p_regalloc) {
  p_regalloc->registers.count = p_regalloc->temp_top = --p_regalloc->local_top;
}

static void register_allocator_rollback_temp(register_allocator* p_regalloc) {
  // keep metadata array in sync with the register index, otherwise
  // the next temp append would write metadata at the wrong slot (index drift).
  p_regalloc->registers.count = --p_regalloc->temp_top;
}

static void register_allocator_reset_temps(register_allocator* p_regalloc, uint32_t p_checkpoint) {
  p_regalloc->temp_top = p_checkpoint;
  p_regalloc->registers.count = p_checkpoint;
}

static void register_allocator_reset_locals(register_allocator* p_regalloc, uint32_t p_checkpoint) {
  p_regalloc->local_top = p_checkpoint;
  p_regalloc->temp_top = p_checkpoint;
  p_regalloc->registers.count = p_checkpoint;
}

static void scope_init(scope* p_scope, uint32_t p_checkpoint, allocators* p_alloc) {
  scope_registers_init(&p_scope->registers_table, p_alloc);
  uint16_array_init(&p_scope->registers, p_alloc);
  p_scope->registers_checkpoint = p_checkpoint;
  p_scope->captures_any = false;
}

static void scope_deinit(scope* p_scope, allocators* p_alloc) {
  scope_registers_deinit(&p_scope->registers_table);
  uint16_array_deinit(&p_scope->registers, p_alloc);
  p_scope->registers_checkpoint = 0;
}

static void loop_context_init(loop_context* p_context, allocators* p_alloc) {
  p_context->break_target = 0;
  p_context->continue_target = 0;
  p_context->continue_forward = false;
  uint16_array_init(&p_context->continues, p_alloc);
  uint16_array_init(&p_context->breaks, p_alloc);
}

static void loop_context_deinit(loop_context* p_context, allocators* p_alloc) {
  uint16_array_deinit(&p_context->continues, p_alloc);
  uint16_array_deinit(&p_context->breaks, p_alloc);
  loop_context_init(p_context, p_alloc);
}

static inline void pop_loop_context(compile_context* p_ctx, allocators* p_alloc) {
  // loop_stack_pop only decrements count. the popped loop_context owns the
  // continues/breaks arrays and must deinit it or they leak.
  loop_context_deinit(&p_ctx->loop_stack.data[p_ctx->loop_stack.count - 1], p_alloc);
  ASSERT(loop_stack_pop(&p_ctx->loop_stack));
}

static void compile_context_init(compile_context* p_context,
                                 object_function* p_obj_function,
                                 function_type p_type,
                                 allocators* p_alloc) {
  register_allocator_init(&p_context->register_allocator, p_alloc);
  upvalues_init(&p_context->upvalues, p_alloc);
  scopes_init(&p_context->scopes, p_alloc);
  loop_stack_init(&p_context->loop_stack, p_alloc);
  p_context->function.function = p_obj_function;
  p_context->function.type = p_type;
}

static void compile_context_deinit(compile_context* p_context, allocators* p_alloc) {
  loop_stack_deinit(&p_context->loop_stack, p_alloc);
  scopes_deinit(&p_context->scopes, p_alloc);
  upvalues_deinit(&p_context->upvalues, p_alloc);
  register_allocator_deinit(&p_context->register_allocator);
  p_context->function.function = NULL;
  p_context->function.type = FUNCTION_NONE;
}

ARRAY_DEFINE_DEFAULT(registers, _register, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(upvalues, upvalue, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(scopes, scope, scope_deinit)
ARRAY_DEFINE_DEFAULT(compile_contexts, compile_context, compile_context_deinit)
ARRAY_DEFINE_DEFAULT(uint16_array, uint16_t, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(loop_stack, loop_context, loop_context_deinit)

static void error_at_noted(compiler* p_compiler,
                           const ast_node* p_author,
                           const ok_string_view p_message,
                           const ok_string_view p_note) {
  report_at_noted(&p_compiler->panic,
                  &p_compiler->had_error,
                  p_author->node_type == AST_EOF_STATEMENT,
                  p_compiler->source,
                  &p_author->token,
                  REPORT_SEVERITY_ERROR,
                  p_message,
                  p_note);
}

static void error_at(compiler* p_compiler, const ast_node* p_author, const ok_string_view p_message) {
  error_at_noted(p_compiler, p_author, p_message, ok_create_string_view(NULL, 0, false));
}

static void report_oom(compiler* p_compiler, const ast_node* p_author) {
  error_at(p_compiler, p_author, ok_create_string_view("out of memory.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
}

static inline compile_context* current_context(compiler* p_compiler) {
  ASSERT(p_compiler->contexts.count > 0);
  return &p_compiler->contexts.data[p_compiler->contexts.count - 1];
}

static inline scope* current_scope(compiler* p_compiler) {
  compile_context* ctx = current_context(p_compiler);
  ASSERT(ctx != NULL);
  ASSERT(ctx->scopes.count > 0);
  return &ctx->scopes.data[ctx->scopes.count - 1];
}

static inline chunk* current_chunk(compiler* p_compiler) {
  compile_context* ctx = current_context(p_compiler);
  ASSERT(ctx != NULL);
  ASSERT(ctx->function.function != NULL);
  return &ctx->function.function->chunk;
}

static inline bool emit_word(compiler* p_compiler, word p_word, ast_node* p_author) {
  chunk* ck = current_chunk(p_compiler);
  return chunk_write_word(ck, p_word, p_author->token.line_info);
}

static inline bool emit_word2(compiler* p_compiler, word2 p_words, ast_node* p_author) {
  chunk* ck = current_chunk(p_compiler);
  return chunk_write_word2(ck, p_words, p_author->token.line_info);
}

static bool emit_default_return(compiler* p_compiler, ast_node* p_author) {
  return emit_word(p_compiler, encode_oppAe(OP_RETURN_1, true, 0, RETURNED_NULL), p_author);
}

static bool begin_scope(compiler* p_compiler, ast_node* p_author) {
  scope scp;
  scope_init(&scp, current_context(p_compiler)->register_allocator.local_top, p_compiler->alloc);
  if (!scopes_append(&current_context(p_compiler)->scopes, scp)) {
    report_oom(p_compiler, p_author);
    scope_deinit(&scp, p_compiler->alloc);
    return false;
  }
  return true;
}

static bool cleanup_scope(compiler* p_compiler, scope* p_scope, ast_node* p_author) {
  if (p_scope->captures_any) {
    if (!emit_word(p_compiler, encode_oppA(OP_CLOSE_UPVALUES, false, p_scope->registers_checkpoint), p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  }
  return true;
}

static bool end_scope(compiler* p_compiler, ast_node* p_author) {
  compile_context* ctx = current_context(p_compiler);
  scope* scp = current_scope(p_compiler);
  bool status = true;
  if (!cleanup_scope(p_compiler, scp, p_author)) {
    status = false;
  }
  register_allocator_reset_locals(&ctx->register_allocator, scp->registers_checkpoint);
  // scopes_pop only decrements count. the popped scope owns its registers_table
  // (with hashed_string keys) and registers array and must deinit it or they leak.
  scope_deinit(scp, p_compiler->alloc);
  ASSERT(scopes_pop(&ctx->scopes));
  return status;
}

static bool
begin_context(compiler* p_compiler, ok_string_view p_name, function_type p_type, uint8_t p_arity, ast_node* p_author) {
  compile_context ctx;
  allocators* alloc = p_compiler->alloc;
  object_specs s = {alloc, p_compiler->objects_store};
  object_string* obj_str = create_object_string(p_name, &s);
  if (obj_str == NULL) {
    report_oom(p_compiler, p_author);
    return false;
  }
  gc_guard_up(alloc->gc, OBJECT_AS_VALUE(obj_str));
  object_function* obj_fu = create_object_function(obj_str, p_arity, &s);
  gc_guard_down(alloc->gc);
  if (obj_fu == NULL) {
    report_oom(p_compiler, p_author);
    return false;
  }
  gc_guard_up(alloc->gc, OBJECT_AS_VALUE(obj_fu));
  compile_context_init(&ctx, obj_fu, p_type, alloc);
  const bool res = compile_contexts_append(&p_compiler->contexts, ctx);
  gc_guard_down(alloc->gc);
  if (!res) {
    compile_context_deinit(&ctx, alloc);
    report_oom(p_compiler, p_author);
  }
  return res;
}

static inline void end_context(compiler* p_compiler) {
  // compile_contexts_pop only decrements count. the popped context owns its
  // scopes/loop_stack/upvalues/register metadata and must deinit it or they leak.
  compile_context_deinit(&p_compiler->contexts.data[p_compiler->contexts.count - 1], p_compiler->alloc);
  ASSERT(compile_contexts_pop(&p_compiler->contexts));
}

static _register
add_local(compiler* p_compiler, const variable_declaration p_declration, bool p_initialized, ast_node* p_author) {
  // full flag set must be computed before any allocation: the metadata array copies the
  // register at append time inside allocate_local, later changes to the local copy will drift.
  register_flags flags =
      REGISTER_FLAG_LOCAL |
      (p_initialized ? REGISTER_FLAG_NONE : REGISTER_FLAG_UNINITIALIZED) |
      ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0 ? REGISTER_FLAG_MUTABLE : REGISTER_FLAG_NONE);
  _register reg = {0, flags, REGISTER_OK};
  allocators* alloc = p_compiler->alloc;
  hashed_string hs = create_hashed_string_hash(p_declration.identifier, alloc);
  if (hs.string.chars == NULL) {
    report_oom(p_compiler, p_author);
    reg.errors = REGISTER_ALLOCATION_FAILED;
    return reg;
  }
  compile_context* ctx = current_context(p_compiler);
  scope* scp = current_scope(p_compiler);
  uint16_t* resolved = scope_registers_get(&scp->registers_table, hs);
  if (resolved == NULL) {
    register_allocator_allocate_local(&ctx->register_allocator, &reg);
    if (reg.errors != REGISTER_OK) {
      if (reg.errors == REGISTER_OVERFLOW) {
        string note = asprint(alloc, "limit is: %d.", REGISTERS_MAX);
        error_at_noted(
            p_compiler,
            p_author,
            ok_create_string_view("too many local variables in function.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
            ok_create_string_view_from_string(note));
        string_deinit(&note, alloc);
        hashed_string_deinit(&hs, alloc);
        reg.errors = REGISTER_OVERFLOW;
        return reg;
      } else if (reg.errors == REGISTER_ALLOCATION_FAILED) {
        report_oom(p_compiler, p_author);
        hashed_string_deinit(&hs, alloc);
        return reg;
      }
    }
    reg.flags |=
        (p_initialized ? REGISTER_FLAG_NONE : REGISTER_FLAG_UNINITIALIZED) |
        ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0 ? REGISTER_FLAG_MUTABLE : REGISTER_FLAG_NONE);

    if (!uint16_array_append(&scp->registers, reg.index)) {
      report_oom(p_compiler, p_author);
      hashed_string_deinit(&hs, alloc);
      register_allocator_rollback_local(&ctx->register_allocator);
      return reg;
    }
    if (!scope_registers_set(&scp->registers_table, hs, reg.index)) {
      report_oom(p_compiler, p_author);
      hashed_string_deinit(&hs, alloc);
      uint16_array_remove(&scp->registers, scp->registers.count - 1, scp->registers.count - 1);
      register_allocator_rollback_local(&ctx->register_allocator);
      return reg;
    }
    return reg;
  }
  string name = create_string_from_string_view(p_declration.identifier, alloc);
  string message = asprint(alloc, "redefining local: '%s' in the same scope.", name.chars);
  error_at(p_compiler, p_author, ok_create_string_view_from_string(message));
  string_deinit(&message, alloc);
  string_deinit(&name, alloc);
  hashed_string_deinit(&hs, alloc);
  reg.errors = REGISTER_REDEFINED_LOCAL;
  return reg;
}

static bool compile_root(compiler* p_compiler, ast_root* p_root);
static bool compile_statement(compiler* p_compiler, ast_statement* p_statement);
static bool compile_expression(compiler* p_compiler,
                               ast_expression* p_expression,
                               _register p_dest,
                               uint32_t p_requested_count,
                               expression_result* p_out);

static bool compile_let_declration(compiler* p_compiler, ast_let_declaration* p_let);
static bool compile_function_declaration(compiler* p_compiler, ast_function_declaration* p_fu);

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement);
static bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof);
static bool compile_print_statement(compiler* p_compiler, ast_print_statement* p_print);
static bool compile_compound_statement(compiler* p_compiler, ast_compound_statement* p_compound);
static bool compile_if_statement(compiler* p_compiler, ast_if_statement* p_if);
static bool compile_while_statement(compiler* p_compiler, ast_while_statement* p_while);
static bool compile_for_statement(compiler* p_compiler, ast_for_statement* p_for);
static bool compile_for_in_statement(compiler* p_compiler, ast_for_in_statement* p_for_in);
static bool compile_for_numeric_statement(compiler* p_compiler, ast_for_numeric_statement* p_for_numeric);
static bool compile_control_flow_statement(compiler* p_compiler, ast_control_flow_statement* p_control);
static bool compile_return_statement(compiler* p_compiler, ast_return_statement* p_return);

static bool compile_identifier(compiler* p_compiler,
                               ast_identifier_expression* p_identifier,
                               _register p_dest,
                               uint32_t p_requested_count,
                               expression_result* p_out);
static bool compile_number_expression(compiler* p_compiler,
                                      ast_number_expression* p_number,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out);
static bool compile_string_expression(compiler* p_compiler,
                                      ast_string_expression* p_string,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out);
static bool compile_prefix_unary_expression(compiler* p_compiler,
                                            ast_prefix_unary_expression* p_prefix,
                                            _register p_dest,
                                            uint32_t p_requested_count,
                                            expression_result* p_out);
static bool compile_infix_binary_expression(compiler* p_compiler,
                                            ast_infix_binary_expression* p_infix,
                                            _register p_dest,
                                            uint32_t p_requested_count,
                                            expression_result* p_out);
static bool compile_assign_expression(compiler* p_compiler,
                                      ast_assign_expression* p_assign,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out);
static bool compile_conditional_expression(compiler* p_compiler,
                                           ast_conditional_expression* p_conditional,
                                           _register p_dest,
                                           uint32_t p_requested_count,
                                           expression_result* p_out);
static bool compile_call_expression(compiler* p_compiler,
                                    ast_call_expression* p_call,
                                    _register p_dest,
                                    uint32_t p_requested_count,
                                    expression_result* p_out);
static bool compile_postfix_unary_expression(compiler* p_compiler,
                                             ast_postfix_unary_expression* p_postfix,
                                             _register p_dest,
                                             uint32_t p_requested_count,
                                             expression_result* p_out);
static bool compile_boolean_expression(compiler* p_compiler,
                                       ast_boolean_expression* p_boolean,
                                       _register p_dest,
                                       uint32_t p_requested_count,
                                       expression_result* p_out);
static bool compile_null_expression(compiler* p_compiler,
                                    ast_null_expression* p_null,
                                    _register p_dest,
                                    uint32_t p_requested_count,
                                    expression_result* p_out);
static bool compile_function_expression(compiler* p_compiler,
                                        ast_function_expression* p_fu,
                                        _register p_dest,
                                        uint32_t p_requested_count,
                                        expression_result* p_out);
static bool compile_array_expression(compiler* p_compiler,
                                     ast_array_expression* p_array,
                                     _register p_dest,
                                     uint32_t p_requested_count,
                                     expression_result* p_out);
static bool compile_subscript_expression(compiler* p_compiler,
                                         ast_subscript_expression* p_subscript,
                                         _register p_dest,
                                         uint32_t p_requested_count,
                                         expression_result* p_out);
static bool compile_compound_assign_expression(compiler* p_compiler,
                                               ast_compound_assign_expression* p_compound,
                                               _register p_dest,
                                               uint32_t p_requested_count,
                                               expression_result* p_out);

static bool compile_function_impl(compiler* p_compiler,
                                  ok_string_view p_name,
                                  function_type p_type,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  _register p_dest,
                                  expression_result* p_out,
                                  ast_node* p_author);

void compiler_init(compiler* p_compiler, compiler_specs p_specs) {
  p_compiler->objects_store = p_specs.objects_store;
  p_compiler->had_error = false;
  p_compiler->panic = false;
  p_compiler->alloc = p_specs.alloc;
  p_compiler->globals_store = p_specs.globals_store;
  compile_contexts_init(&p_compiler->contexts, p_compiler->alloc);
}

void compiler_deinit(compiler* p_compiler) {
  p_compiler->source = NULL;
  p_compiler->objects_store = NULL;
  p_compiler->globals_store = NULL;
  p_compiler->had_error = false;
  p_compiler->panic = false;
  compile_contexts_deinit(&p_compiler->contexts, p_compiler->alloc);
}

compile_result compiler_compile(compiler* p_compiler, compile_specs p_specs) {
  compile_result result;
  result.function = NULL;
  result.status = COMPILE_ERROR;
  p_compiler->source = p_specs.source;
  variable_declaration decl = {.identifier = "", .flags = VARIABLE_DECLRATION_FLAGS_NONE};
  if (!begin_context(p_compiler,
                     ok_create_string_view("<main>", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                     FUNCTION_SCRIPT,
                     0,
                     (ast_node*)p_specs.root)) {
    goto ret;
  }
  if (!begin_scope(p_compiler, (ast_node*)p_specs.root)) {
    goto ret;
  }
  if (!IS_REGISTER_VALID(add_local(p_compiler, decl, true, (ast_node*)p_specs.root))) {
    goto ret;
  }
  if (!begin_scope(p_compiler, (ast_node*)p_specs.root)) { // hidden scope
    goto ret;
  }
  for (uint32_t i = 0; i < p_specs.top_level_declarations.count; ++i) {
    if (!IS_REGISTER_VALID(
            add_local(p_compiler, p_specs.top_level_declarations.data[i], true, (ast_node*)p_specs.root))) {
      goto ret;
    }
  }

  if (!begin_scope(p_compiler, (ast_node*)p_specs.root)) { // user top-level scope
    goto ret;
  }

  bool res = compile_root(p_compiler, p_specs.root);
  result.function = current_context(p_compiler)->function.function;
  result.function->registers = current_context(p_compiler)->register_allocator.registers_count;
  res &= emit_default_return(p_compiler, (ast_node*)p_specs.root);
  res &= end_scope(p_compiler, (ast_node*)p_specs.root); // user top-level scope
  res &= end_scope(p_compiler, (ast_node*)p_specs.root); // hidden scope
  res &= end_scope(p_compiler, (ast_node*)p_specs.root);
  end_context(p_compiler);
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
ret: {
  compiler_specs specs = {.alloc = p_compiler->alloc,
                          .objects_store = p_compiler->objects_store,
                          .globals_store = p_compiler->globals_store};
  compiler_deinit(p_compiler);
  compiler_init(p_compiler, specs);
  return result;
}
}

constant add_constant(compiler* p_compiler, value p_value, ast_node* p_author) {
  chunk* chunk = current_chunk(p_compiler);
  constant index = chunk_add_constant(chunk, p_value, p_author->token.line_info);
  allocators* alloc = p_compiler->alloc;
  if (!IS_CONSTANT_VALID(index)) {
    if (index.errors == CONSTANT_OVERFLOW) {
      string note = asprint(alloc, "limit is: %d", CONSTANTS_MAX);
      error_at_noted(
          p_compiler,
          p_author,
          ok_create_string_view("too many constants in single function.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
          ok_create_string_view_from_string(note));
      string_deinit(&note, alloc);
    } else if (index.errors == CONSTANT_ALLOCATION_FAILED) {
      report_oom(p_compiler, p_author);
    }
  }
  return index;
}

static bool compile_root(compiler* p_compiler, ast_root* p_root) {
  bool res = true;
  for (uint32_t i = 0; i < p_root->statements.count; ++i) {
    res &= compile_statement(p_compiler, (ast_statement*)p_root->statements.data[i]);
    p_compiler->panic = false;
  }
  return res;
}

static bool compile_statement(compiler* p_compiler, ast_statement* p_statement) {
  ASSERT_MSG(p_statement->node.node_type >= AST_RANGE_STATEMENT_BEGIN &&
                 p_statement->node.node_type <= AST_RANGE_STATEMENT_END,
             "invalid statement.");
  // statements own the temps they allocate: reset the temps after each one.
  // reset to local_top: the statement may have added locals,
  // and a stale checkpoint would truncate metadata below the locals.
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  bool result;
  switch (p_statement->node.node_type) {
  case AST_EMPTY_STATEMENT:
    ASSERT_MSG(false, "empty statement leaked to compiler.");
    result = false;
    break;
  case AST_EXPRESSION_STATEMENT:
    result = compile_expression_statement(p_compiler, (ast_expression_statement*)p_statement);
    break;
  case AST_PRINT_STATEMENT:
    result = compile_print_statement(p_compiler, (ast_print_statement*)p_statement);
    break;
  case AST_COMPOUND_STATEMENT:
    result = compile_compound_statement(p_compiler, (ast_compound_statement*)p_statement);
    break;
  case AST_IF_STATEMENT:
    result = compile_if_statement(p_compiler, (ast_if_statement*)p_statement);
    break;
  case AST_WHILE_STATEMENT:
    result = compile_while_statement(p_compiler, (ast_while_statement*)p_statement);
    break;
  case AST_FOR_STATEMENT:
    result = compile_for_statement(p_compiler, (ast_for_statement*)p_statement);
    break;
  case AST_FOR_IN_STATEMENT:
    result = compile_for_in_statement(p_compiler, (ast_for_in_statement*)p_statement);
    break;
  case AST_FOR_NUMERIC_STATEMENT:
    result = compile_for_numeric_statement(p_compiler, (ast_for_numeric_statement*)p_statement);
    break;
  case AST_CONTROL_FLOW_STATEMENT:
    result = compile_control_flow_statement(p_compiler, (ast_control_flow_statement*)p_statement);
    break;
  case AST_RETURN_STATEMENT:
    result = compile_return_statement(p_compiler, (ast_return_statement*)p_statement);
    break;
  case AST_LET_DECLARATION:
    result = compile_let_declration(p_compiler, (ast_let_declaration*)p_statement);
    break;
  case AST_FUNCTION_DECLARATION:
    result = compile_function_declaration(p_compiler, (ast_function_declaration*)p_statement);
    break;
  case AST_EOF_STATEMENT:
    result = compile_eof_statement(p_compiler, (ast_eof_statement*)p_statement);
    break;
  default:
    CRASH_MSG(false, "unimplemented statement.");
    result = false;
  }
  register_allocator_reset_temps(regalloc, regalloc->local_top);
  return result;
}

static bool compile_expression(compiler* p_compiler,
                               ast_expression* p_expression,
                               _register p_dest,
                               uint32_t p_requested_count,
                               expression_result* p_out) {
  ASSERT_MSG(p_expression->node.node_type >= AST_RANGE_EXPRESSION_BEGIN &&
                 p_expression->node.node_type <= AST_RANGE_EXPRESSION_END,
             "invalid expression.");
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  const uint32_t checkpoint = regalloc->temp_top;
  bool result = false;
  switch (p_expression->node.node_type) {
  case AST_IDENTIFIER_EXPRESSION:
    result = compile_identifier(p_compiler, (ast_identifier_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_NUMBER_EXPRESSION:
    result =
        compile_number_expression(p_compiler, (ast_number_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_STRING_EXPRESSION:
    result =
        compile_string_expression(p_compiler, (ast_string_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_PREFIX_UNARY_EXPRESSION:
    result = compile_prefix_unary_expression(
        p_compiler, (ast_prefix_unary_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_INFIX_BINARY_EXPRESSION:
    result = compile_infix_binary_expression(
        p_compiler, (ast_infix_binary_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_POSTFIX_UNARY_EXPRESSION:
    result = compile_postfix_unary_expression(
        p_compiler, (ast_postfix_unary_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_CALL_EXPRESSION:
    result = compile_call_expression(p_compiler, (ast_call_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_ASSIGN_EXPRESSION:
    result =
        compile_assign_expression(p_compiler, (ast_assign_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_CONDITIONAL_EXPRESSION:
    result = compile_conditional_expression(
        p_compiler, (ast_conditional_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_BOOLEAN_EXPRESSION:
    result =
        compile_boolean_expression(p_compiler, (ast_boolean_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_NULL_EXPRESSION:
    result = compile_null_expression(p_compiler, (ast_null_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_FUNCTION_EXPRESSION:
    result = compile_function_expression(
        p_compiler, (ast_function_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_ARRAY_EXPRESSION:
    result =
        compile_array_expression(p_compiler, (ast_array_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_SUBSCRIPT_EXPRESSION:
    result = compile_subscript_expression(
        p_compiler, (ast_subscript_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  case AST_COMPOUND_ASSIGN_EXPRESSION:
    result = compile_compound_assign_expression(
        p_compiler, (ast_compound_assign_expression*)p_expression, p_dest, p_requested_count, p_out);
    break;
  default: {
    CRASH_MSG(false, "unimplemented expression.");
  }
  }
  register_allocator_reset_temps(regalloc, checkpoint);
  return result;
}

static bool materialize_value(compiler* p_compiler, _register p_dest, location p_value, ast_node* p_author) {
  if (p_value.kind == LOCATION_KIND_REGISTER && p_value.index == p_dest.index) {
    return true; // already there: no self move
  }
  opcode op;
  opcode op_long;
  switch (p_value.kind) {
  case LOCATION_KIND_REGISTER:
    op = OP_MOVE;
    op_long = OP_MOVE_LONG;
    break;
  case LOCATION_KIND_CONSTANT:
    op = OP_LOAD_CONSTANT;
    op_long = OP_LOAD_CONSTANT_LONG;
    break;
  case LOCATION_KIND_GLOBAL:
    op = OP_LOAD_GLOBAL;
    op_long = OP_LOAD_GLOBAL_LONG;
    break;
  case LOCATION_KIND_UPVALUE:
    op = OP_LOAD_UPVALUE;
    op_long = OP_LOAD_UPVALUE_LONG;
    break;
  default:
    CRASH_MSG(false, "unimplemented rhs location.");
    break;
  }
  if (p_dest.index > aprime_MAX || p_value.index > bprime_MAX) {
    if (!emit_word2(p_compiler, encode_OPPABCQ(op_long, false, p_dest.index, p_value.index, 0, 0), p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  } else {
    if (!emit_word(p_compiler, encode_oppabprime(op, false, p_dest.index, p_value.index), p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  }
  return true;
}

static bool write_local(compiler* p_compiler, _register p_local, expression_result p_value, ast_node* p_author) {
  ASSERT_MSG(p_value.count == 1, "write local only accepts one value.");
  return materialize_value(p_compiler, p_local, p_value.value, p_author);
}

// guarantee the value lives in p_dest (move between registers, load constants/globals/upvalues)
static bool materialize_into(compiler* p_compiler, location* p_location, _register p_dest, ast_node* p_author) {
  if (p_location->kind == LOCATION_KIND_REGISTER && p_location->index == p_dest.index) {
    return true;
  }
  if (!materialize_value(p_compiler, p_dest, *p_location, p_author)) {
    return false;
  }
  p_location->index = p_dest.index;
  p_location->kind = LOCATION_KIND_REGISTER;
  p_location->error = LOCATION_OK;
  return true;
}

static bool
materialize_into_if_not_register(compiler* p_compiler, location* p_location, _register p_dest, ast_node* p_author) {
  if (p_location->kind == LOCATION_KIND_REGISTER) {
    return true;
  }
  if (!materialize_value(p_compiler, p_dest, *p_location, p_author)) {
    return false;
  }
  p_location->index = p_dest.index;
  p_location->kind = LOCATION_KIND_REGISTER;
  p_location->error = LOCATION_OK;
  return true;
}

static bool materialize_if_not_register(compiler* p_compiler, location* p_location, ast_node* p_author) {
  if (p_location->kind == LOCATION_KIND_REGISTER) {
    return true;
  }
  _register temp;
  if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &temp)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  if (!materialize_value(p_compiler, temp, *p_location, p_author)) {
    return false;
  }
  p_location->index = temp.index;
  p_location->kind = LOCATION_KIND_REGISTER;
  p_location->error = LOCATION_OK;
  return true;
}

static bool materialize_into_if_not_common(compiler* p_compiler,
                                           bool* p_is_common,
                                           location_kind p_common_kind,
                                           location* p_location,
                                           _register p_dest,
                                           ast_node* p_author) {
  *p_is_common = false;
  switch (p_location->kind) {
  case LOCATION_KIND_CONSTANT:
    if (p_common_kind == LOCATION_KIND_CONSTANT) {
      *p_is_common = true;
      break;
    }
    goto materialize;
  case LOCATION_KIND_GLOBAL: {
    if (p_common_kind == LOCATION_KIND_GLOBAL) {
      *p_is_common = true;
      break;
    }
    goto materialize;
  }
  case LOCATION_KIND_UPVALUE: {
    if (p_common_kind == LOCATION_KIND_UPVALUE) {
      *p_is_common = true;
      break;
    }
    goto materialize;
  }
  case LOCATION_KIND_REGISTER:
    break;
  default: {
  materialize:
    if (!materialize_value(p_compiler, p_dest, *p_location, p_author)) {
      return false;
    }
    p_location->index = p_dest.index;
    p_location->kind = LOCATION_KIND_REGISTER;
    p_location->error = LOCATION_OK;
    break;
  }
  }
  return true;
}

static bool
materialize_if_not_common(compiler* p_compiler, bool* p_is_constant, location* p_location, ast_node* p_author) {
  *p_is_constant = false;
  switch (p_location->kind) {
  case LOCATION_KIND_CONSTANT:
    *p_is_constant = true;
    break;
  case LOCATION_KIND_REGISTER:
    break;
  default: {
    _register temp;
    if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &temp)) {
      report_oom(p_compiler, p_author);
      return false;
    }
    if (!materialize_value(p_compiler, temp, *p_location, p_author)) {
      return false;
    }
    p_location->index = temp.index;
    p_location->kind = LOCATION_KIND_REGISTER;
    p_location->error = LOCATION_OK;
    break;
  }
  }
  return true;
}

static bool write_global(compiler* p_compiler, global_id p_global, expression_result p_value, ast_node* p_author) {
  ASSERT_MSG(p_value.count == 1, "write global only accepts one value.");
  bool is_constant = false;
  global glob = p_compiler->globals_store->global_descriptors.data[p_global.index];
  if (!materialize_if_not_common(p_compiler, &is_constant, &p_value.value, p_author)) {
    return false;
  }
  if (p_global.index > aprime_MAX || p_value.value.index > bprime_MAX) {
    if (!emit_word2(p_compiler,
                    encode_OPPABCQ(OP_STORE_GLOBAL_LONG, is_constant, glob.index, p_value.value.index, 0, 0),
                    p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  } else {
    if (!emit_word(
            p_compiler, encode_oppabprime(OP_STORE_GLOBAL, is_constant, glob.index, p_value.value.index), p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  }
  return true;
}

static bool write_upvalue(compiler* p_compiler, upvalue p_upvalue, expression_result p_value, ast_node* p_author) {
  ASSERT_MSG(p_value.count == 1, "write upvalue only accepts one value.");
  bool is_constant = false;
  if (!materialize_if_not_common(p_compiler, &is_constant, &p_value.value, p_author)) {
    return false;
  }
  if (p_upvalue.index > aprime_MAX || p_value.value.index > bprime_MAX) {
    if (!emit_word2(p_compiler,
                    encode_OPPABCQ(OP_STORE_UPVALUE_LONG, is_constant, p_upvalue.index, p_value.value.index, 0, 0),
                    p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  } else {
    if (!emit_word(p_compiler,
                   encode_oppabprime(OP_STORE_UPVALUE, is_constant, p_upvalue.index, p_value.value.index),
                   p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  }
  return true;
}

static global add_global(compiler* p_compiler, variable_declaration p_declaration, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  chunk* chunk = current_chunk(p_compiler);
  global index = globals_store_add(p_compiler->globals_store,
                                   p_declaration.identifier,
                                   (p_declaration.flags & VARIABLE_DECLARATION_FLAGS_MUTABLE) != 0);
  if (!IS_GLOBAL_VALID(index)) {
    if (index.errors == GLOBAL_ALLOCATION_FAILED) {
      report_oom(p_compiler, p_author);
      return index;
    } else if (index.errors == GLOBAL_OVERFLOW) {
      string note = asprint(alloc, "limit is: %u", GLOBAL_MAX);
      error_at_noted(p_compiler,
                     p_author,
                     ok_create_string_view("too many identifiers.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                     ok_create_string_view_from_string(note));
      string_deinit(&note, alloc);
    }
  }
  return index;
}

static bool define_local(compiler* p_compiler, location p_local, expression_result p_value, ast_node* p_author) {
  compile_context* ctx = current_context(p_compiler);
  _register* local = &ctx->register_allocator.registers.data[p_local.index];
  local->flags &= ~REGISTER_FLAG_UNINITIALIZED;
  if (p_value.value.kind == LOCATION_KIND_REGISTER && p_value.value.index == local->index) {
    return true;
  }
  return write_local(p_compiler, *local, p_value, p_author);
}

static bool define_global(compiler* p_compiler, location p_global, expression_result p_value, ast_node* p_author) {
  return write_global(p_compiler, (global){p_global.index, GLOBAL_FLAG_NONE, GLOBAL_OK}, p_value, p_author);
}

static _register* resolve_local(compile_context* p_context, hashed_string* p_identifier, scope** p_owning) {
  uint16_t* sindex = NULL;
  uint32_t depth = p_context->scopes.count;
  while (sindex == NULL && depth != 0) {
    scope* scp = &p_context->scopes.data[--depth];

    // the table value IS the register slot (scope_registers_set stores reg.index directly),
    sindex = scope_registers_get(&scp->registers_table, *p_identifier);
    // TODO: report that we skipped if we couldn't resolve other local with same name.
    if (sindex != NULL &&
        (p_context->register_allocator.registers.data[*sindex].flags & REGISTER_FLAG_UNINITIALIZED) != 0) {
      // let x; { let x = x; /* skip the inner */ }
      sindex = NULL;
    }
  }
  if (sindex == NULL) {
    return NULL;
  }
  *p_owning = &p_context->scopes.data[depth];
  return &p_context->register_allocator.registers.data[*sindex];
}

static location declare_variable(compiler* p_compiler, const variable_declaration p_declration, ast_node* p_node) {
  if ((p_declration.flags & VARIABLE_DECLARATION_FLAGS_GLOBAL) == 0) {
    _register local = add_local(p_compiler, p_declration, false, p_node);
    return (location){local.index, LOCATION_KIND_REGISTER, IS_REGISTER_VALID(local) ? LOCATION_OK : LOCATION_INVALID};
  }
  global glob = add_global(p_compiler, p_declration, p_node);
  return (location){glob.index, LOCATION_KIND_GLOBAL, IS_GLOBAL_VALID(glob) ? LOCATION_OK : LOCATION_INVALID};
}

static bool define_variable(compiler* p_compiler, location p_variable, expression_result p_value, ast_node* p_author) {
  if (p_variable.kind == LOCATION_KIND_REGISTER) {
    return define_local(p_compiler, p_variable, p_value, p_author);
  }
  ASSERT(p_variable.kind == LOCATION_KIND_GLOBAL);
  return define_global(p_compiler, p_variable, p_value, p_author);
}

static bool register_for_location(compiler* p_compiler, location p_location, _register* p_dest) {
  if (p_location.kind == LOCATION_KIND_REGISTER) {
    *p_dest = (_register){p_location.index, REGISTER_FLAG_NONE, REGISTER_OK};
    return true;
  }
  return register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, p_dest);
}

static variable_declaration_flags_t variable_declaration_flags_from_modifiers(ast_declaration_modifiers_t p_declmods,
                                                                              ast_binding_modifiers_t p_bindmods) {
  return ((p_declmods & DECLARATION_GLOB) != 0 ? VARIABLE_DECLARATION_FLAGS_GLOBAL : VARIABLE_DECLRATION_FLAGS_NONE) |
         ((p_bindmods & BINDING_MUT) != 0 ? VARIABLE_DECLARATION_FLAGS_MUTABLE : VARIABLE_DECLRATION_FLAGS_NONE);
}

static bool compile_let_declration(compiler* p_compiler, ast_let_declaration* p_let) {
  ok_string_view name = ast_identifier_expression_get_value((ast_identifier_expression*)p_let->binding->lvalue);
  variable_declaration vardecl = {
      .identifier = name,
      .flags = variable_declaration_flags_from_modifiers(p_let->declaration.modifiers, p_let->binding->modifiers)};
  location variable = declare_variable(p_compiler, vardecl, (ast_node*)p_let);
  if (variable.error != LOCATION_OK) {
    return false;
  }
  expression_result value;
  _register dest;
  if (!register_for_location(p_compiler, variable, &dest)) {
    report_oom(p_compiler, (ast_node*)p_let);
    return false;
  }
  if (!compile_expression(p_compiler, p_let->value, dest, 1, &value)) {
    return false;
  }
  return define_variable(p_compiler, variable, value, (ast_node*)p_let);
}

static bool compile_function_declaration(compiler* p_compiler, ast_function_declaration* p_fu) {
  ok_string_view name = ast_identifier_expression_get_value((ast_identifier_expression*)p_fu->binding->lvalue);
  variable_declaration vardecl = {
      .identifier = name,
      .flags = variable_declaration_flags_from_modifiers(p_fu->declaration.modifiers, p_fu->binding->modifiers)};
  location variable = declare_variable(p_compiler, vardecl, (ast_node*)p_fu);
  if (variable.error != LOCATION_OK) {
    return false;
  }
  expression_result value;
  _register dest;
  if (!register_for_location(p_compiler, variable, &dest)) {
    report_oom(p_compiler, (ast_node*)p_fu);
    return false;
  }
  if (!compile_function_impl(
          p_compiler, name, FUNCTION_FUNCTION, p_fu->parameters, p_fu->body, dest, &value, (ast_node*)p_fu)) {
    return false;
  }
  return define_variable(p_compiler, variable, value, (ast_node*)p_fu);
}

static bool compile_expression_statement(compiler* p_compiler, ast_expression_statement* p_expression_statement) {
  expression_result value;
  _register devnull;
  register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &devnull);
  devnull.flags |= REGISTER_FLAG_DEVNULL;
  return compile_expression(p_compiler, p_expression_statement->expression, devnull, 0, &value);
}

static bool
store_global(compiler* p_compiler, location p_global_location, expression_result p_value, ast_node* p_author) {
  const global_id glob = p_compiler->globals_store->global_descriptors.data[p_global_location.index];
  if ((glob.flags & GLOBAL_FLAG_MUTABLE) == 0) {
    // TODO abstract
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view(
                       "attempting to mutate an immutable global variable.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view("did you forget to declare it 'mut'?", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  return write_global(p_compiler, glob, p_value, p_author);
}

static bool
store_local(compiler* p_compiler, location p_local_location, expression_result p_value, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  _register local = current_context(p_compiler)->register_allocator.registers.data[p_local_location.index];
  if ((local.flags & REGISTER_FLAG_MUTABLE) == 0) {
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view(
                       "attempting to mutate an immutable local variable.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view("did you forget to declare it 'mut'?", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  return write_local(p_compiler, local, p_value, p_author);
}

static upvalue add_upvalue(compiler* p_compiler, compile_context* p_ctx, upvalue p_upvalue, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  // the array stores semantic indices (captured local's slot, or parent's upvalue position),
  // but the returned index must be the position in this context's upvalues array, because
  // callers use it to index the array (store_upvalue / load paths).
  for (uint32_t i = 0; i < p_ctx->upvalues.count; ++i) {
    upvalue up = p_ctx->upvalues.data[i];
    if (up.index == p_upvalue.index && up.flags == p_upvalue.flags) {
      p_upvalue.index = (ok_index)i;
      p_upvalue.errors = UPVALUE_OK;
      return p_upvalue;
    }
  }
  if (p_ctx->upvalues.count >= UPVALUES_MAX) {
    p_upvalue.error = UPVALUE_OVERFLOW;
    string note = asprint(alloc, "limit is: %d.", UPVALUES_MAX);
    error_at_noted(
        p_compiler,
        p_author,
        ok_create_string_view("too many upvalues in a single function.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
        ok_create_string_view_from_string(note));
    string_deinit(&note, alloc);
    return p_upvalue;
  }
  if (!upvalues_append(&p_ctx->upvalues, p_upvalue)) {
    p_upvalue.errors = UPVALUE_ALLOCATION_FAILED;
    report_oom(p_compiler, p_author);
    return p_upvalue;
  }
  p_upvalue.index = (ok_index)(p_ctx->upvalues.count - 1);
  p_upvalue.errors = UPVALUE_OK;
  return p_upvalue;
}

static upvalue
resolve_upvalue(compiler* p_compiler, uint32_t p_context_index, hashed_string p_identifier, ast_node* p_author) {
  const uint32_t contexts = p_compiler->contexts.count;
  upvalue u = {0, UPVALUE_FLAG_LOCAL, UPVALUE_OK};
  if (contexts < p_context_index + 2) {
    u.error = UPVALUE_UNDEFINED;
    return u;
  }

  compile_context* current = &p_compiler->contexts.data[contexts - p_context_index - 1];
  compile_context* up = &p_compiler->contexts.data[contexts - p_context_index - 2];
  scope* scp;
  _register* local_reg = resolve_local(up, &p_identifier, &scp);
  if (local_reg != NULL) {
    local_reg->flags |= REGISTER_FLAG_CAPTURED;
    u.index = local_reg->index;
    u.flags = UPVALUE_FLAG_LOCAL;
    scp->captures_any = true;

    if ((local_reg->flags & REGISTER_FLAG_MUTABLE) != 0) {
      u.flags |= UPVALUE_FLAG_MUTABLE;
    }
    return add_upvalue(p_compiler, current, u, p_author);
  }
  u = resolve_upvalue(p_compiler, p_context_index + 1, p_identifier, p_author);
  if (u.error != UPVALUE_UNDEFINED) {
    u.flags = (u.flags & UPVALUE_FLAG_MUTABLE) != 0 ? UPVALUE_FLAG_MUTABLE | UPVALUE_FLAG_UP : UPVALUE_FLAG_UP;
    return add_upvalue(p_compiler, current, u, p_author);
  }
  return u;
}

static bool store_upvalue(compiler* p_compiler, location p_upvalue, expression_result p_value, ast_node* p_author) {
  upvalue up = current_context(p_compiler)->upvalues.data[p_upvalue.index];
  if ((up.flags & UPVALUE_FLAG_MUTABLE) == 0) {
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view(
                       "attempting to mutate an immutable upvalue variable.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view("did you forget to declare it 'mut'?", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  // the operand must be the position in this function's upvalue array (the location index),
  // it should not be the stored entry's semantic index (the captured local's slot),
  // thus we construct and upvalue.
  return write_upvalue(
      p_compiler, (upvalue){.index = p_upvalue.index, .flags = up.flags, .errors = UPVALUE_OK}, p_value, p_author);
}

static location resolve_variable(compiler* p_compiler, const ok_string_view p_identifier, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  location invalid = {0, LOCATION_KIND_INVALID, LOCATION_INVALID};
  hashed_string hs = create_hashed_string_hash(p_identifier, alloc);

  if (hs.string.chars == NULL) {
    report_oom(p_compiler, p_author);
    return invalid;
  }

  {
    scope* scp;
    _register* local = resolve_local(current_context(p_compiler), &hs, &scp);

    if (local != NULL) {
      hashed_string_deinit(&hs, alloc);
      return (location){local->index, LOCATION_KIND_REGISTER, LOCATION_OK};
    }
  }

  {
    upvalue up = resolve_upvalue(p_compiler, 0, hs, p_author);
    if (up.error == UPVALUE_OK) {
      hashed_string_deinit(&hs, alloc);
      return (location){up.index, LOCATION_KIND_UPVALUE, LOCATION_OK};
    }
    if (up.error != UPVALUE_UNDEFINED) {
      hashed_string_deinit(&hs, alloc);
      return invalid;
    }
  }

  {
    // TODO make globals store accept direct hs
    const global glob = globals_store_get(p_compiler->globals_store, p_identifier);
    if (IS_GLOBAL_VALID(glob)) {
      hashed_string_deinit(&hs, alloc);
      return (location){glob.index, LOCATION_KIND_GLOBAL, LOCATION_OK};
    }
  }
  string message =
      asprint(alloc, "undefined variable: %.*s", ok_string_view_get_length(&p_identifier), p_identifier.chars);
  error_at(p_compiler, p_author, ok_create_string_view_from_string(message));
  string_deinit(&message, alloc);
  hashed_string_deinit(&hs, alloc);
  return invalid;
}

// conservative purity check used by the container-elision guard in
// resolve_lvalue: an expression is inert when evaluating it cannot change any
// local's value. literals and identifiers trivially qualify; pure reads
// (subscripts, binops, unary) propagate inertness; array literals allocate but
// never reseat locals (the gc is non-moving, a local's reference survives a
// collection unchanged); lambda literals are inert because creating a closure
// runs no body - only calling it can mutate, and any call anywhere fails the
// check anyway. calls and assignments are the reseating writers we guard
// against: a call may mutate a captured local, an assignment (compound or not)
// may target the container itself.
static bool expression_is_inert(const ast_expression* p_expression) {
  switch (p_expression->node.node_type) {
  case AST_IDENTIFIER_EXPRESSION:
  case AST_NUMBER_EXPRESSION:
  case AST_STRING_EXPRESSION:
  case AST_BOOLEAN_EXPRESSION:
  case AST_NULL_EXPRESSION:
  case AST_FUNCTION_EXPRESSION:
    return true;
  case AST_PREFIX_UNARY_EXPRESSION:
    return expression_is_inert(((const ast_prefix_unary_expression*)p_expression)->right);
  case AST_INFIX_BINARY_EXPRESSION: {
    const ast_infix_binary_expression* infix = (const ast_infix_binary_expression*)p_expression;
    return expression_is_inert(infix->left) && expression_is_inert(infix->right);
  }
  case AST_SUBSCRIPT_EXPRESSION: {
    const ast_subscript_expression* subscript = (const ast_subscript_expression*)p_expression;
    return expression_is_inert(subscript->left) && expression_is_inert(subscript->index);
  }
  case AST_ARRAY_EXPRESSION: {
    const ast_array_expression* array = (const ast_array_expression*)p_expression;
    for (uint32_t i = 0; i < array->elements.count; ++i) {
      if (!expression_is_inert(array->elements.data[i])) {
        return false;
      }
    }
    return true;
  }
  case AST_CONDITIONAL_EXPRESSION: {
    const ast_conditional_expression* conditional = (const ast_conditional_expression*)p_expression;
    return expression_is_inert(conditional->condition) && expression_is_inert(conditional->then_arm) &&
           expression_is_inert(conditional->otherwise_arm);
  }
  default:
    return false; // calls, assignments, anything unknown: assume it can write
  }
}

// p_pending_rhs is the expression that will be compiled after this lvalue is
// resolved (the right side of an assignment), or NULL for pure reads: the
// subscript read path resolves and immediately emits its GET.
static lvalue resolve_lvalue(compiler* p_compiler, ast_expression* p_lvalue_expr, const ast_expression* p_pending_rhs) {
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  switch (p_lvalue_expr->node.node_type) {
  case AST_IDENTIFIER_EXPRESSION: {
    ast_identifier_expression* ident = (ast_identifier_expression*)p_lvalue_expr;
    location var = resolve_variable(p_compiler, ast_identifier_expression_get_value(ident), (ast_node*)p_lvalue_expr);
    if (var.error != LOCATION_OK) {
      return (lvalue){LVALUE_INVALID};
    }
    return (lvalue){LVALUE_VARIABLE, var};
  }
  case AST_SUBSCRIPT_EXPRESSION: {
    // the 3 temp discipline: base and index are evaluated once, into their own
    // registers, above the assignment's checkpoint. they must survive
    // compilation of the rhs: compile_expression resets temps to its
    // entry checkpoint, which sits above these two, so they do.
    // the index is snapshotted even when it names a mutable local: a[i] = f()
    // uses the i from evaluation time, not whatever f left behind (left to right).
    ast_subscript_expression* sub = (ast_subscript_expression*)p_lvalue_expr;
    ast_node* author = (ast_node*)p_lvalue_expr;
    _register base;
    if (!register_allocator_allocate_temp(regalloc, &base)) {
      report_oom(p_compiler, author);
      return (lvalue){LVALUE_INVALID};
    }
    expression_result base_result;
    if (!compile_expression(p_compiler, sub->left, base, 1, &base_result)) {
      return (lvalue){LVALUE_INVALID};
    }
    // container elision (the compound-assign disassembly follow-up): when the
    // container compiled to a plain local's register - compilation emitted
    // nothing, identifiers resolve to their own slots - and nothing that will
    // be compiled before the GET/SET can reseat it, reference the local's
    // register directly and drop the staging move. reseating writers inside
    // the index or the pending rhs: assignments, and calls (a closure may
    // mutate a captured local; the scope's captures_any flag cannot be trusted
    // at this point, a lambda compiled later in the function captures just the
    // same). the elided base also relies on the caller-side invariant that no
    // assignment expression is ever compiled with a live local as its result
    // register - the same invariant the assign/binop peephole already depends
    // on (p_dest is the devnull or a temp, never a local).
    const bool elide_container = base_result.value.kind == LOCATION_KIND_REGISTER &&
                                 base_result.value.index != base.index && expression_is_inert(sub->index) &&
                                 (p_pending_rhs == NULL || expression_is_inert(p_pending_rhs));
    location base_location;
    if (elide_container) {
      register_allocator_rollback_temp(regalloc); // hand the staging temp back
      base_location = base_result.value;
    } else {
      // the base must live in its own register: the operand pair (base, index) is
      // what GET/SET address, and both must be stable across the rhs compile.
      if (!materialize_into(p_compiler, &base_result.value, base, author)) {
        return (lvalue){LVALUE_INVALID};
      }
      base_location = (location){base.index, LOCATION_KIND_REGISTER, LOCATION_OK};
    }
    _register index;
    if (!register_allocator_allocate_temp(regalloc, &index)) {
      report_oom(p_compiler, author);
      return (lvalue){LVALUE_INVALID};
    }
    expression_result index_result;
    if (!compile_expression(p_compiler, sub->index, index, 1, &index_result)) {
      return (lvalue){LVALUE_INVALID};
    }
    if (index_result.value.kind != LOCATION_KIND_CONSTANT &&
        !materialize_into(p_compiler, &index_result.value, index, author)) {
      return (lvalue){LVALUE_INVALID};
    }
    lvalue lv;
    lv.kind = LVALUE_SUBSCRIPT;
    lv.subscript.base = base_location;
    lv.subscript.index = index_result.value; // register (== the index temp) or constant
    return lv;
  }
  default:
    error_at(p_compiler,
             (ast_node*)p_lvalue_expr,
             ok_create_string_view("invalid lvalue.", OK_CSTRING_VIEW_CALCULATE_LENGTH, true));
  }
  return (lvalue){LVALUE_INVALID};
}

static bool store_variable(compiler* p_compiler, location p_variable, expression_result p_value, ast_node* p_author) {
  switch (p_variable.kind) {
  case LOCATION_KIND_REGISTER:
    return store_local(p_compiler, p_variable, p_value, p_author);
  case LOCATION_KIND_UPVALUE:
    return store_upvalue(p_compiler, p_variable, p_value, p_author);
  case LOCATION_KIND_GLOBAL:
    return store_global(p_compiler, p_variable, p_value, p_author);
  default:
    CRASH_MSG(false, "invalid variable location.");
  }
  return false;
}

static bool store_lvalue(compiler* p_compiler, lvalue p_lvalue, expression_result p_value, ast_node* p_author) {
  switch (p_lvalue.kind) {
  case LVALUE_VARIABLE:
    return store_variable(p_compiler, p_lvalue.variable, p_value, p_author);
  case LVALUE_SUBSCRIPT: {
    // constants may flow through the long form's q flag. the narrow form has no
    // value const room (p is spent on the index), so constant values force long.
    bool is_value_constant = false;
    location value = p_value.value;
    if (!materialize_if_not_common(p_compiler, &is_value_constant, &value, p_author)) {
      return false;
    }
    const bool is_index_constant = p_lvalue.subscript.index.kind == LOCATION_KIND_CONSTANT;
    const uint16_t base_index = p_lvalue.subscript.base.index;
    const uint16_t index_operand = p_lvalue.subscript.index.index;
    if (!is_value_constant && base_index <= a_MAX && index_operand <= b_MAX && value.index <= c_MAX) {
      if (!emit_word(p_compiler,
                     encode_oppabc(OP_ARRAY_SET, is_index_constant, base_index, index_operand, value.index),
                     p_author)) {
        report_oom(p_compiler, p_author);
        return false;
      }
      return true;
    }
    if (!emit_word2(
            p_compiler,
            encode_OPPABCQ(
                OP_ARRAY_SET_LONG, is_index_constant, base_index, index_operand, value.index, is_value_constant),
            p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
    return true;
  }
  case LVALUE_INVALID:
    ASSERT_MSG(false, "invalid lvalue.");
    return false;
  default:
    CRASH_MSG(false, "unimplemented lvalue.");
  }
  return false;
}

static bool compile_identifier(compiler* p_compiler,
                               ast_identifier_expression* p_identifier,
                               _register p_dest,
                               uint32_t p_requested_count,
                               expression_result* p_out) {
  ASSERT_MSG(p_requested_count <= 1, "invalid requested count to compile_identifier.");
  if (p_requested_count == 0) {
    return true;
  }

  ok_string_view ident_str = ast_identifier_expression_get_value(p_identifier);
  location loc = resolve_variable(p_compiler, ident_str, (ast_node*)p_identifier);
  if (loc.error != LOCATION_OK) {
    return false;
  }
  p_out->type = EXPRESSION_VALUE | EXPRESSION_LVALUE;
  p_out->lvalue = (lvalue){LVALUE_VARIABLE, loc};
  p_out->value = loc;
  p_out->count = 1;
  return true;
}

bool compile_eof_statement(compiler* p_compiler, ast_eof_statement* p_eof) {
  return true;
}

bool compile_print_statement(compiler* p_compiler, ast_print_statement* p_print) {
  expression_result result;
  _register dest;
  if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &dest)) {
    report_oom(p_compiler, (ast_node*)p_print);
    return false;
  }

  if (compile_expression(p_compiler, p_print->expression, dest, 1, &result)) {
    bool is_constant = false;
    if (!materialize_if_not_common(p_compiler, &is_constant, &result.value, (ast_node*)p_print)) {
      return false;
    }
    if (!emit_word(p_compiler, encode_oppA(OP_PRINT, is_constant, result.value.index), (ast_node*)p_print)) {
      return false;
    }
    return true;
  }
  return false;
}

bool compile_compound_statement(compiler* p_compiler, ast_compound_statement* p_compound) {
  if (!begin_scope(p_compiler, (ast_node*)p_compound)) {
    return false;
  }
  bool status = true;
  for (uint32_t i = 0; i < p_compound->statements.count; ++i) {
    if (!compile_statement(p_compiler, p_compound->statements.data[i])) {
      status = false;
      break;
    }
  }
  if (!end_scope(p_compiler, (ast_node*)p_compound)) {
    return false;
  }
  return status;
}

#define JUMP_MAX UINT16_MAX

static bool emit_incomplete_jump(compiler* p_compiler, opcode p_op, uint32_t* p_out, ast_node* p_author) {
  *p_out = current_chunk(p_compiler)->code.count;
  if (!emit_word(p_compiler, encode_oppA(p_op, false, 0), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

static bool emit_loop(compiler* p_compiler, const uint32_t p_loop_start, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  chunk* chunk = current_chunk(p_compiler);
  // OP_LOOP reads its offset from the ip after instruction, which is (this word index + 1)
  const uint32_t loop_length = (chunk->code.count + 1) - p_loop_start;
  if (loop_length > JUMP_MAX) {
    string note = asprint(p_compiler->alloc, "limit is: %d words.", JUMP_MAX);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("loop too wide.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, p_compiler->alloc);
    return false;
  }
  return emit_word(p_compiler, encode_oppA(OP_LOOP, false, loop_length), p_author);
}

static bool emit_incomplete_jump_if(
    compiler* p_compiler, bool p_truthy, _register p_condition, uint32_t* p_out, ast_node* p_author) {
  *p_out = current_chunk(p_compiler)->code.count;
  if (!emit_word2(p_compiler, encode_OPPABCQ(OP_JUMP_IF, p_truthy, 0, p_condition.index, 0, 0), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

static bool emit_incomplete_jump_if_set(
    compiler* p_compiler, bool p_truthy, _register p_condition, uint32_t* p_out, ast_node* p_author) {
  *p_out = current_chunk(p_compiler)->code.count;
  if (!emit_word2(p_compiler, encode_OPPABCQ(OP_JUMP_IF_SET, p_truthy, 0, p_condition.index, 0, 0), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

// defaults ladder guard: jump forward when the argument for parameter position
// p_position was supplied (frame->argc > position). patched with patch_jump_if,
// which preserves B (the position) while rewriting A.
static bool emit_incomplete_jump_if_argc(
    compiler* p_compiler, bool p_supplied, uint16_t p_position, uint32_t* p_out, ast_node* p_author) {
  *p_out = current_chunk(p_compiler)->code.count;
  if (!emit_word2(p_compiler, encode_OPPABCQ(OP_JUMP_IF_ARGC, p_supplied, 0, p_position, 0, 0), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

// numeric for jump pair: both carry their offset in A (PREP forwards to the loop
// exit, LOOP backwards to the body top) and the counter register in B, with the
// limit and step at B + 1 and B + 2. patched with patch_jump_if, which preserves
// B (and p) while rewriting A - same contract as OP_JUMP_IF. p is only consumed
// by FOR_LOOP: p == 1 is the compiler's immutability proof for the counter.
static bool emit_incomplete_for_jump(
    compiler* p_compiler, opcode p_op, bool p_imm, _register p_counter, uint32_t* p_out, ast_node* p_author) {
  *p_out = current_chunk(p_compiler)->code.count;
  if (!emit_word2(p_compiler, encode_OPPABCQ(p_op, p_imm, 0, p_counter.index, 0, 0), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

static bool
patch_jump(compiler* p_compiler, bool p_is_loop, uint32_t p_jump_instruction, uint32_t p_jump_end, ast_node* p_author) {
  // offsets are relative to the word after the 1 word jump instruction
  const uint32_t jmp = p_is_loop ? (p_jump_instruction + 1) - p_jump_end : p_jump_end - (p_jump_instruction + 1);
  if (jmp > JUMP_MAX) {
    string note = asprint(p_compiler->alloc, "limit is: %d words.", JUMP_MAX);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("jump too wide.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, p_compiler->alloc);
    return false;
  }
  word* jump = current_chunk(p_compiler)->code.data + p_jump_instruction;
  opcode op = op_of(*jump);
  bool p = p_of(*jump);
  *jump = encode_oppA(op, p, jmp);
  return true;
}

static bool patch_jump_if(
    compiler* p_compiler, bool p_is_loop, uint32_t p_jump_instruction, uint32_t p_jump_end, ast_node* p_author) {
  // offsets are relative to the word after the 2 word jump instruction
  const uint32_t jmp = p_is_loop ? (p_jump_instruction + 2) - p_jump_end : p_jump_end - (p_jump_instruction + 2);
  if (jmp > JUMP_MAX) {
    string note = asprint(p_compiler->alloc, "limit is: %d words.", JUMP_MAX);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("jump too wide.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, p_compiler->alloc);
    return false;
  }
  // portable rejoin of the two words (endian/alignment safe)
  word* words = current_chunk(p_compiler)->code.data + p_jump_instruction;
  word2 joined = ((word2)words[0]) | ((word2)words[1] << 32u);
  const opcode op = op_of(words[0]);
  const bool p = p_of(words[0]);
  const uint16_t B = B_of(joined);
  const uint16_t C = C_of(joined);
  const bool Q = Q_of(joined);
  word2 patched = encode_OPPABCQ(op, p, jmp, B, C, Q);
  words[0] = (word)(patched & UINT64_C(0xffffffff));
  words[1] = (word)(patched >> 32u);
  return true;
}

static bool patch_jump_if_set(compiler* p_compiler,
                              bool p_is_loop,
                              uint32_t p_jump_instruction,
                              uint32_t p_jump_end,
                              _register p_target,
                              ast_node* p_author) {
  // offsets are relative to the word after the 2 word jump instruction
  const uint32_t jmp = p_is_loop ? (p_jump_instruction + 2) - p_jump_end : p_jump_end - (p_jump_instruction + 2);
  if (jmp > JUMP_MAX) {
    string note = asprint(p_compiler->alloc, "limit is: %d words.", JUMP_MAX);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("jump too wide.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, p_compiler->alloc);
    return false;
  }
  // portable rejoin of the two words (endian/alignment safe)
  word* words = current_chunk(p_compiler)->code.data + p_jump_instruction;
  word2 joined = ((word2)words[0]) | ((word2)words[1] << 32u);
  const opcode op = op_of(words[0]);
  const bool p = p_of(words[0]);
  const uint16_t B = B_of(joined);
  const uint16_t C = C_of(joined);
  const bool Q = Q_of(joined);
  word2 patched = encode_OPPABCQ(op, p, jmp, B, p_target.index, Q);
  words[0] = (word)(patched & UINT64_C(0xffffffff));
  words[1] = (word)(patched >> 32u);
  return true;
}

bool compile_if_statement(compiler* p_compiler, ast_if_statement* p_if) {
  expression_result condition;
  _register condition_register;
  if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &condition_register)) {
    report_oom(p_compiler, (ast_node*)p_if);
    return false;
  }
  if (!compile_expression(p_compiler, p_if->condition, condition_register, 1, &condition)) {
    return false;
  }
  if (!materialize_into_if_not_register(p_compiler, &condition.value, condition_register, (ast_node*)p_if)) {
    return false;
  }
  condition_register.error = REGISTER_OK;
  condition_register.index = condition.value.index;
  condition_register.flags = REGISTER_FLAG_NONE; // for all that it matters this is a temp

  uint32_t cons_jump;
  if (!emit_incomplete_jump_if(p_compiler, false, condition_register, &cons_jump, (ast_node*)p_if)) {
    return false;
  }

  if (!compile_statement(p_compiler, p_if->consequence)) {
    return false;
  }

  chunk* chunk = current_chunk(p_compiler);
  uint32_t alt_jump;
  if (!emit_incomplete_jump(p_compiler, OP_JUMP, &alt_jump, (ast_node*)p_if)) {
    return false;
  }

  if (!patch_jump_if(p_compiler, false, cons_jump, chunk->code.count, (ast_node*)p_if)) {
    return false;
  }

  if (p_if->alternative != NULL) {
    if (!compile_statement(p_compiler, p_if->alternative)) {
      return false;
    }
  }
  return patch_jump(p_compiler, false, alt_jump, chunk->code.count, (ast_node*)p_if);
}

static bool patch_loop_context(compiler* p_compiler, loop_context* p_ctx, ast_node* p_author) {
  for (uint32_t i = 0; i < p_ctx->breaks.count; ++i) {
    if (!patch_jump(p_compiler, false, p_ctx->breaks.data[i], p_ctx->break_target, p_author)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < p_ctx->continues.count; ++i) {
    uint32_t cont = p_ctx->continues.data[i];
    if (!patch_jump(p_compiler, !p_ctx->continue_forward, cont, p_ctx->continue_target, p_author)) {
      return false;
    }
  }
  scope* scp = &current_context(p_compiler)->scopes.data[p_ctx->scope_depth];
  return cleanup_scope(p_compiler, scp, p_author);
}

bool compile_while_statement(compiler* p_compiler, ast_while_statement* p_while) {
  allocators* alloc = p_compiler->alloc;
  compile_context* ctx = current_context(p_compiler);
  {
    loop_context lctx;
    loop_context_init(&lctx, alloc);
    if (!loop_stack_append(&ctx->loop_stack, lctx)) {
      report_oom(p_compiler, (ast_node*)p_while);
      loop_context_deinit(&lctx, alloc);
      return false;
    }
  }

#define CTX ctx->loop_stack.data[ctx->loop_stack.count - 1]
  CTX.scope_depth = ctx->scopes.count - 1;
  chunk* chunk = current_chunk(p_compiler);
  const uint32_t loop_start = chunk->code.count;
  expression_result condition;
  _register condition_reg;
  if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &condition_reg)) {
    report_oom(p_compiler, (ast_node*)p_while);
    return false;
  }
  if (!compile_expression(p_compiler, p_while->condition, condition_reg, 1, &condition)) {
    return false;
  }
  if (!materialize_into_if_not_register(p_compiler, &condition.value, condition_reg, (ast_node*)p_while)) {
    return false;
  }

  condition_reg.error = REGISTER_OK;
  condition_reg.index = condition.value.index;
  condition_reg.flags = REGISTER_FLAG_NONE;

  uint32_t exit_jmp;
  if (!emit_incomplete_jump_if(p_compiler, false, condition_reg, &exit_jmp, (ast_node*)p_while)) {
    return false;
  }

  if (!compile_statement(p_compiler, p_while->body)) {
    return false;
  }

  if (!emit_loop(p_compiler, loop_start, (ast_node*)p_while)) {
    return false;
  }

  if (!patch_jump_if(p_compiler, false, exit_jmp, current_chunk(p_compiler)->code.count, (ast_node*)p_while)) {
    return false;
  }
  CTX.continue_target = loop_start;
  CTX.break_target = chunk->code.count;
  if (!patch_loop_context(p_compiler, &CTX, (ast_node*)p_while)) {
    return false;
  }
  pop_loop_context(ctx, alloc);
#undef CTX
  return true;
}

bool compile_for_statement(compiler* p_compiler, ast_for_statement* p_for) {
  allocators* alloc = p_compiler->alloc;
  compile_context* ctx = current_context(p_compiler);
  {
    loop_context lctx;
    loop_context_init(&lctx, alloc);
    if (!loop_stack_append(&ctx->loop_stack, lctx)) {
      loop_context_deinit(&lctx, alloc);
      report_oom(p_compiler, (ast_node*)p_for);
      return false;
    }
  }
#define CTX ctx->loop_stack.data[ctx->loop_stack.count - 1]
  if (!begin_scope(p_compiler, (ast_node*)p_for)) {
    pop_loop_context(ctx, alloc);
    return false;
  }
  chunk* chunk = current_chunk(p_compiler);
  CTX.scope_depth = ctx->scopes.count - 1;
  if (p_for->initializer != NULL && !compile_statement(p_compiler, p_for->initializer)) {
    goto fail;
  }
  bool is_exit_set = false;
  uint32_t exit_jump;
  const uint32_t loop_start = chunk->code.count;
  if (p_for->condition != NULL) {
    expression_result condition;
    _register condition_reg;
    if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &condition_reg)) {
      report_oom(p_compiler, (ast_node*)p_for);
      return false;
    }
    if (!compile_expression(p_compiler, p_for->condition, condition_reg, 1, &condition)) {
      goto fail;
    }
    if (!materialize_into_if_not_register(p_compiler, &condition.value, condition_reg, (ast_node*)p_for)) {
      goto fail;
    }

    condition_reg.error = REGISTER_OK;
    condition_reg.index = condition.value.index;
    condition_reg.flags = REGISTER_FLAG_NONE;

    if (!emit_incomplete_jump_if(p_compiler, false, condition_reg, &exit_jump, (ast_node*)p_for->condition)) {
      goto fail;
    }
    is_exit_set = true;
  }
  if (p_for->update != NULL) {
    CTX.continue_forward = true;
  }
  if (!compile_statement(p_compiler, p_for->body)) {
    goto fail;
  }

  CTX.continue_target = loop_start;
  if (p_for->update != NULL) {
    CTX.continue_target = chunk->code.count;
    expression_result result;
    _register devnull;
    if (!register_allocator_allocate_temp(&ctx->register_allocator, &devnull)) {
      return false;
    }
    devnull.flags |= REGISTER_FLAG_DEVNULL;
    if (!compile_expression(p_compiler, p_for->update, devnull, 0, &result)) {
      goto fail;
    }
  }
  if (!emit_loop(p_compiler, loop_start, (ast_node*)p_for)) {
    goto fail;
  }
  if (is_exit_set) {
    if (!patch_jump_if(p_compiler, false, exit_jump, current_chunk(p_compiler)->code.count, (ast_node*)p_for)) {
      goto fail;
    }
  }
  CTX.break_target = chunk->code.count;
  if (!patch_loop_context(p_compiler, &CTX, (ast_node*)p_for)) {
    return false;
  }
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, (ast_node*)p_for));
  return true;
fail:
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, (ast_node*)p_for));
  return false;

#undef CTX
}

bool compile_control_flow_statement(compiler* p_compiler, ast_control_flow_statement* p_control) {
  compile_context* ctx = current_context(p_compiler);
  if (ctx->loop_stack.count == 0) {
    error_at(p_compiler,
             (ast_node*)p_control,
             ok_create_string_view(
                 "illegal control flow without an enclosing loop.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  loop_context* lctx = &ctx->loop_stack.data[ctx->loop_stack.count - 1];
  if (p_control->type == CONTROL_FLOW_BREAK) {
    uint32_t jmp;
    if (!emit_incomplete_jump(p_compiler, OP_JUMP, &jmp, (ast_node*)p_control)) {
      return false;
    }
    if (!uint16_array_append(&lctx->breaks, jmp)) {
      report_oom(p_compiler, (ast_node*)p_control);
      return false;
    }
  } else if (p_control->type == CONTROL_FLOW_CONTINUE) {
    uint32_t jmp;
    if (!emit_incomplete_jump(p_compiler, lctx->continue_forward ? OP_JUMP : OP_LOOP, &jmp, (ast_node*)p_control)) {
      return false;
    }
    if (!uint16_array_append(&lctx->continues, jmp)) {
      report_oom(p_compiler, (ast_node*)p_control);
      return false;
    }
  }
  return true;
}

static bool compile_return_statement(compiler* p_compiler, ast_return_statement* p_return) {
  // TODO: multiple returns when required by langauge.
  if (p_return->returned == NULL) {
    return emit_default_return(p_compiler, (ast_node*)p_return);
  }
  expression_result returned;
  _register dest; // TODO: cant we do this in place or this a tailcall thingy?
  if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &dest)) {
    return false;
  }
  if (!compile_expression(p_compiler, p_return->returned, dest, 1, &returned)) {
    return false;
  }
  uint8_t e = RETURNED_NULL;
  chunk* chunk = current_chunk(p_compiler);
  switch (returned.value.kind) {
  case LOCATION_KIND_CONSTANT: {
    value con = chunk->constants.data[returned.value.index];
    if (IS_VALUE_STRICTLY_BOOL(con)) {
      e = VALUE_AS_BOOL(con) ? RETURNED_KTRUE : RETURNED_KFALSE;
      break;
    }
    e = RETURNED_CONSTANT;
    break;
  }
  case LOCATION_KIND_GLOBAL:
    e = RETURNED_GLOBAL;
    break;
  case LOCATION_KIND_UPVALUE:
    e = RETURNED_UPVALUE;
    break;
  case LOCATION_KIND_REGISTER:
    e = RETURNED_REGISTER;
    break;
  }

  return emit_word(p_compiler, encode_oppAe(OP_RETURN_1, false, returned.value.index, e), (ast_node*)p_return);
}

static bool compile_number_expression(compiler* p_compiler,
                                      ast_number_expression* p_number,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  if (p_requested_count == 0) {
    return true;
  }

  double number = ast_number_expression_get_value(p_number);

  if (number == AST_NUMBER_EXPRESSION_PARSE_ERROR) {
    error_at(p_compiler,
             (ast_node*)p_number,
             ok_create_string_view("failed to parse number.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }

  constant con = add_constant(p_compiler, NUMBER_AS_VALUE(number), (ast_node*)p_number);
  if (!IS_CONSTANT_VALID(con)) {
    return false;
  }

  p_out->type = EXPRESSION_VALUE;
  p_out->value = (location){con.index, LOCATION_KIND_CONSTANT, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_INVALID, (location){0, LOCATION_KIND_INVALID, LOCATION_OK}};
  p_out->count = 1;
  return true;
}

static bool compile_string_expression(compiler* p_compiler,
                                      ast_string_expression* p_string,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  if (p_requested_count == 0) {
    return true;
  }
  ok_string_view parsed_str = ast_string_expression_get_value(p_string);
  if (parsed_str.chars == NULL) {
    error_at(p_compiler,
             (ast_node*)p_string,
             ok_create_string_view("failed to parse string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  allocators* alloc = p_compiler->alloc;
  object_specs s = {alloc, p_compiler->objects_store};
  object_string* object_str = create_object_string(parsed_str, &s);
  if (object_str == NULL) {
    report_oom(p_compiler, (ast_node*)p_string);
    return false;
  }
  gc_guard_up(alloc->gc, OBJECT_AS_VALUE(object_str));
  constant con = add_constant(p_compiler, OBJECT_AS_VALUE(object_str), (ast_node*)p_string);
  if (!IS_CONSTANT_VALID(con)) {
    gc_guard_down(alloc->gc);
    return false;
  }

  p_out->type = EXPRESSION_VALUE;
  p_out->value = (location){con.index, LOCATION_KIND_CONSTANT, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_INVALID, (location){0, LOCATION_KIND_INVALID, LOCATION_OK}};
  p_out->count = 1;
  gc_guard_down(alloc->gc);
  return true;
}

static bool compile_prefix_unary_expression(compiler* p_compiler,
                                            ast_prefix_unary_expression* p_expression,
                                            _register p_dest,
                                            uint32_t p_requested_count,
                                            expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  expression_result rhs;
  if (!compile_expression(p_compiler, p_expression->right, p_dest, 1, &rhs)) {
    return false;
  }
  bool is_constant = false;
  if (!materialize_into_if_not_common(
          p_compiler, &is_constant, LOCATION_KIND_CONSTANT, &rhs.value, p_dest, (ast_node*)p_expression)) {
    return false;
  }

  opcode op;
  opcode op_long;
  switch (p_expression->_operator) {
  case OPERATOR_PRE_MINUS:
    op = OP_NEGATE;
    op_long = OP_NEGATE_LONG;
    break;
  case OPERATOR_PRE_NOT:
  case OPERATOR_PRE_BANG:
    op = OP_NOT;
    op_long = OP_NOT_LONG;
    break;
  default:
    CRASH_MSG(false, "unimplemented prefix operator.");
  }
  if (p_dest.index > aprime_MAX || rhs.value.index > bprime_MAX) {
    if (!emit_word2(p_compiler,
                    encode_OPPABCQ(op_long, is_constant, p_dest.index, rhs.value.index, 0, 0),
                    (ast_node*)p_expression)) {
      report_oom(p_compiler, (ast_node*)p_expression);
      return false;
    }
  } else {
    if (!emit_word(
            p_compiler, encode_oppabprime(op, is_constant, p_dest.index, rhs.value.index), (ast_node*)p_expression)) {
      return false;
    }
  }

  p_out->type = EXPRESSION_VALUE;
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_VARIABLE, (location){0, LOCATION_KIND_INVALID, LOCATION_OK}};
  p_out->count = 1;
  return true;
}

static bool compile_logical_operator(compiler* p_compiler,
                                     ast_infix_binary_expression* p_logical,
                                     _register p_dest,
                                     expression_result* p_result) {
  expression_result lhs;
  expression_result rhs;
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;

  if (!compile_expression(p_compiler, p_logical->left, p_dest, 1, &lhs)) {
    return false;
  }
  if (!materialize_into_if_not_register(p_compiler, &lhs.value, p_dest, (ast_node*)p_logical)) {
    return false;
  }
  // the condition register is wherever the lhs actually lives (constants were moved to p_dest)
  _register condition = {.index = lhs.value.index, .flags = REGISTER_FLAG_NONE, .errors = REGISTER_OK};

  uint32_t jmp;
  if (!emit_incomplete_jump_if_set(
          p_compiler, p_logical->_operator == OPERATOR_LOG_OR, condition, &jmp, (ast_node*)p_logical)) {
    return false;
  }
  if (!compile_expression(p_compiler, p_logical->right, p_dest, 1, &rhs)) {
    return false;
  }

  if (!materialize_into_if_not_register(p_compiler, &rhs.value, p_dest, (ast_node*)p_logical)) {
    return false;
  }

  p_result->type = EXPRESSION_VALUE;
  p_result->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_result->lvalue = (lvalue){LVALUE_INVALID};
  p_result->count = 1;
  return patch_jump_if_set(p_compiler, false, jmp, current_chunk(p_compiler)->code.count, p_dest, (ast_node*)p_logical);
}

// `condition then a otherwise b`. zero new opcodes: the shape is the if
// statement's, folded into an expression. the condition evaluates into its own
// temp; both arms compile into the caller's p_dest - the same result register -
// which is exactly the invariant the calling convention already guarantees
// (callers allocate result registers, compile_expression targets them). a
// conditional is never an lvalue; is_lvalue's whitelist keeps it that way.
static bool compile_conditional_expression(compiler* p_compiler,
                                           ast_conditional_expression* p_conditional,
                                           _register p_dest,
                                           uint32_t p_requested_count,
                                           expression_result* p_out) {
  CRASH_MSG(p_requested_count <= 1, "unimplemented multi-result.");
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  ast_node* author = (ast_node*)p_conditional;

  _register condition;
  if (!register_allocator_allocate_temp(regalloc, &condition)) {
    report_oom(p_compiler, author);
    return false;
  }
  expression_result condition_result;
  if (!compile_expression(p_compiler, p_conditional->condition, condition, 1, &condition_result)) {
    return false;
  }
  // force into the temp: an identifier condition resolves to its own register
  // without emitting anything, and the jump would read an unwritten temp.
  if (!materialize_into(p_compiler, &condition_result.value, condition, author)) {
    return false;
  }

  // falsy (false/null, the vm's is_falsy) jumps to the otherwise arm
  uint32_t jump_to_otherwise;
  if (!emit_incomplete_jump_if(p_compiler, false, condition, &jump_to_otherwise, author)) {
    return false;
  }

  expression_result arm_result;
  if (!compile_expression(p_compiler, p_conditional->then_arm, p_dest, 1, &arm_result)) {
    return false;
  }
  // arms may resolve to constants or their own registers without emitting
  // anything (numbers, identifiers, in-place compound assigns): land them in
  // the result register so the taken arm always writes p_dest.
  if (!materialize_into(p_compiler, &arm_result.value, p_dest, author)) {
    return false;
  }

  uint32_t jump_to_end;
  if (!emit_incomplete_jump(p_compiler, OP_JUMP, &jump_to_end, author)) {
    return false;
  }

  if (!patch_jump_if(p_compiler, false, jump_to_otherwise, current_chunk(p_compiler)->code.count, author)) {
    return false;
  }
  if (!compile_expression(p_compiler, p_conditional->otherwise_arm, p_dest, 1, &arm_result)) {
    return false;
  }
  if (!materialize_into(p_compiler, &arm_result.value, p_dest, author)) {
    return false;
  }
  if (!patch_jump(p_compiler, false, jump_to_end, current_chunk(p_compiler)->code.count, author)) {
    return false;
  }

  p_out->type = EXPRESSION_VALUE;
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->count = 1;
  return true;
}

static bool emit_binop(compiler* p_compiler,
                       opcode p_op,
                       opcode p_long_op,
                       _register p_result,
                       location p_lhs,
                       location p_rhs,
                       bool p_is_lhs_p,
                       bool p_is_rhs_p,
                       ast_node* p_author) {
  bool is_long = p_result.index > a_MAX || p_lhs.index > b_MAX || p_rhs.index > c_MAX;
  if (is_long) {
    if (!emit_word2(p_compiler,
                    encode_OPPABCQ(p_long_op, p_is_rhs_p, p_result.index, p_lhs.index, p_rhs.index, p_is_lhs_p),
                    p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
  } else if (!emit_word(
                 p_compiler, encode_oppabc(p_op, p_is_rhs_p, p_result.index, p_lhs.index, p_rhs.index), p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

static bool compile_binary_value_operator(compiler* p_compiler,
                                          ast_infix_binary_expression* p_binexpr,
                                          _register p_dest,
                                          expression_result* p_result) {
  expression_result lhs;
  expression_result rhs;
  _register rhs_dest;
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;

  if (!compile_expression(p_compiler, p_binexpr->left, p_dest, 1, &lhs)) {
    return false;
  }

  if (!register_allocator_allocate_temp(regalloc, &rhs_dest)) {
    report_oom(p_compiler, (ast_node*)p_binexpr);
    return false;
  }

  if (!compile_expression(p_compiler, p_binexpr->right, rhs_dest, 1, &rhs)) {
    return false;
  }

  const location_kind lhs_common_kind = LOCATION_KIND_CONSTANT;
  location_kind rhs_common_kind = p_binexpr->_operator == OPERATOR_BIN_AS ? LOCATION_KIND_GLOBAL : LOCATION_KIND_CONSTANT;
  bool is_long = p_dest.index > a_MAX || lhs.value.index > b_MAX || rhs.value.index > c_MAX;
  bool is_lhs_p = false;
  bool is_rhs_p = false;

  // conditional materialization based on dest opcode to allow for more optimized emission
  if (is_long) {
    if (!materialize_into_if_not_common(
            p_compiler, &is_lhs_p, lhs_common_kind, &lhs.value, p_dest, (ast_node*)p_binexpr)) {
      return false;
    }
  } else {
    if (!materialize_into_if_not_register(p_compiler, &lhs.value, p_dest, (ast_node*)p_binexpr)) {
      return false;
    }
  }
  if (!materialize_into_if_not_common(
          p_compiler, &is_rhs_p, rhs_common_kind, &rhs.value, rhs_dest, (ast_node*)p_binexpr)) {
    return false;
  }

  bool result = false;
  switch (p_binexpr->_operator) {
  case OPERATOR_BIN_PLUS: {
    result = emit_binop(
        p_compiler, OP_ADD, OP_ADD_LONG, p_dest, lhs.value, rhs.value, is_lhs_p, is_rhs_p, (ast_node*)p_binexpr);
    break;
  }
  case OPERATOR_BIN_MINUS: {
    result = emit_binop(p_compiler,
                        OP_SUBTRACT,
                        OP_SUBTRACT_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_ASTERISK: {
    result = emit_binop(p_compiler,
                        OP_MULTIPLY,
                        OP_MULTIPLY_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_SLASH: {
    result = emit_binop(
        p_compiler, OP_DIVIDE, OP_DIVIDE_LONG, p_dest, lhs.value, rhs.value, is_lhs_p, is_rhs_p, (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_MODULO: {
    result = emit_binop(p_compiler,
                        OP_MODULUS,
                        OP_MODULUS_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_EQUALS: {
    result = emit_binop(
        p_compiler, OP_EQUALS, OP_EQUALS_LONG, p_dest, lhs.value, rhs.value, is_lhs_p, is_rhs_p, (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_BANG_EQUAL: {
    result = emit_binop(p_compiler,
                        OP_NOT_EQUAL,
                        OP_NOT_EQUAL_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_LESS: {
    result = emit_binop(
        p_compiler, OP_LESS, OP_LESS_LONG, p_dest, lhs.value, rhs.value, is_lhs_p, is_rhs_p, (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_GREATER: {
    result = emit_binop(p_compiler,
                        OP_GREATER,
                        OP_GREATER_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_LESS_EQUAL: {
    result = emit_binop(p_compiler,
                        OP_LESS_EQUAL,
                        OP_LESS_EQUAL_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_GREATER_EQUAL: {
    result = emit_binop(p_compiler,
                        OP_GREATER_EQUAL,
                        OP_GREATER_EQUAL_LONG,
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  case OPERATOR_BIN_AS: {
    result = emit_binop(p_compiler,
                        OP_AS,
                        OP_AS_LONG, // looks poetic
                        p_dest,
                        lhs.value,
                        rhs.value,
                        is_lhs_p,
                        is_rhs_p,
                        (ast_node*)p_binexpr);

    break;
  }
  default:
    CRASH_MSG(false, "unimplemented binary value operator.");
  }
  p_result->type = EXPRESSION_VALUE;
  p_result->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_result->lvalue = (lvalue){LVALUE_INVALID};
  p_result->count = 1;
  return result;
}

static bool compile_infix_binary_expression(compiler* p_compiler,
                                            ast_infix_binary_expression* p_expression,
                                            _register p_dest,
                                            uint32_t p_requested_count,
                                            expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  if (p_expression->_operator >= OPERATOR_RANGE_LOGICAL_BEGIN &&
      p_expression->_operator <= OPERATOR_RANGE_LOGICAL_END) {
    return compile_logical_operator(p_compiler, p_expression, p_dest, p_out);
  } else if (p_expression->_operator >= OPERATOR_RANGE_BIN_VALUE_BEGIN &&
             p_expression->_operator <= OPERATOR_RANGE_BIN_VALUE_END) {
    return compile_binary_value_operator(p_compiler, p_expression, p_dest, p_out);
  }
  ASSERT_MSG(false, "invalid binary operator.");
  return false;
}

static bool compile_assign_expression(compiler* p_compiler,
                                      ast_assign_expression* p_assign,
                                      _register p_dest,
                                      uint32_t p_requested_count,
                                      expression_result* p_out) {
  CRASH_MSG(p_requested_count <= 1, "unimplemented multi-assign.");
  lvalue lv = resolve_lvalue(p_compiler, p_assign->left, p_assign->right);
  if (lv.kind == LVALUE_INVALID) {
    return false;
  }
  // `local = <simple> op <rhs>`: compile the rhs straight into the variable's
  // register and skip the temp + move-back pair (the local_pressure finding:
  // `a = a + 1` cost ADD-to-temp + MOVE, 2 dispatches where 1 suffices). safe
  // because value binops write their destination exactly once, at the final
  // op, reading both operands at emit time - the destination may alias an
  // operand. the left operand must stay simple (identifier/literal): shapes
  // like `a = (a + 1) + a` or `a = f() + a` re-read the variable after the
  // left side may have staged intermediate state in the destination, so they
  // keep the scratch-then-store path. store_lvalue still runs afterwards:
  // the mutability gate and the (now no-op) self move check both still apply.
  _register rhs_dest = p_dest;
  if (lv.kind == LVALUE_SUBSCRIPT) {
    // the rhs compiles into its own temp ABOVE the lvalue's staged base/index.
    // a call rhs opens the callee's register window at its destination, and the
    // callee's frame ([dest, dest + fu->registers)) null-fills every slot above
    // it - the staged base/index temps sit above p_dest, so compiling the rhs
    // there let the callee erase them (arr[0] = reseat(), where reseat captures
    // and reseats arr, died with "expected an array"). found by the elision-gate
    // regression hunt; compound assigns were always safe because their cur/rhs
    // temps are allocated after the staging.
    if (!register_allocator_allocate_temp(&current_context(p_compiler)->register_allocator, &rhs_dest)) {
      report_oom(p_compiler, (ast_node*)p_assign);
      return false;
    }
  }
  if (lv.kind == LVALUE_VARIABLE && lv.variable.kind == LOCATION_KIND_REGISTER && p_assign->right != NULL &&
      p_assign->right->node.node_type == AST_INFIX_BINARY_EXPRESSION) {
    ast_infix_binary_expression* bin = (ast_infix_binary_expression*)p_assign->right;
    if (bin->_operator >= OPERATOR_RANGE_BIN_VALUE_BEGIN && bin->_operator <= OPERATOR_RANGE_BIN_VALUE_END) {
      const ast_node_type left_type = bin->left->node.node_type;
      if (left_type == AST_IDENTIFIER_EXPRESSION || left_type == AST_NUMBER_EXPRESSION ||
          left_type == AST_BOOLEAN_EXPRESSION || left_type == AST_STRING_EXPRESSION) {
        rhs_dest.errors = REGISTER_OK;
        rhs_dest.index = lv.variable.index;
        rhs_dest.flags = REGISTER_FLAG_NONE;
      }
    }
  }
  expression_result right;
  if (!compile_expression(p_compiler, p_assign->right, rhs_dest, 1, &right)) {
    return false;
  }
  if (!materialize_into_if_not_register(p_compiler, &right.value, rhs_dest, (ast_node*)p_assign)) {
    return false;
  }
  if (!store_lvalue(p_compiler, lv, right, (ast_node*)p_assign)) {
    return false;
  }
  *p_out = right;
  return true;
}

static bool compile_expressions_list(
    compiler* p_compiler, ast_expressions_list* p_list, uint32_t p_limit, _register* p_args_start, ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  if (p_list->count > p_limit) {
    string note = asprint(alloc, "limit is: %d.", p_limit);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("too many arguments.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, alloc);
    return false;
  }
  for (uint32_t i = 0; i < p_list->count; ++i) {
    _register argreg;
    if (!register_allocator_allocate_temp(regalloc, &argreg)) {
      return false;
    }
    ast_expression* arg = p_list->data[i];
    expression_result result;
    if (!compile_expression(p_compiler, arg, argreg, 1, &result)) {
      return false;
    }
    // arguments must be materialized into the window register, even register valued ones
    if (!materialize_into(p_compiler, &result.value, argreg, p_author)) {
      return false;
    }
    if (i == 0) {
      *p_args_start = argreg;
    }
  }
  return true;
}

bool compile_call_expression(compiler* p_compiler,
                             ast_call_expression* p_call,
                             _register p_dest,
                             uint32_t p_requested_count,
                             expression_result* p_out) {
  CRASH_MSG(p_requested_count <= 1, "unimplemented multi-result.");

  allocators* alloc = p_compiler->alloc;
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  expression_result callable;
  if (!compile_expression(p_compiler, p_call->callable, p_dest, 1, &callable)) {
    return false;
  }
  // the call window is [p_dest, p_dest + argc]: the callable must be moved into p_dest
  // even when it already lives in another register (e.g. a local or the function's own name)
  if (!materialize_into(p_compiler, &callable.value, p_dest, (ast_node*)p_call)) {
    return false;
  }
  _register args;
  args.index = 0;
  args.flags = REGISTER_FLAG_NONE;
  args.errors = REGISTER_OK;
  if (!compile_expressions_list(p_compiler, &p_call->arguments, A_MAX, &args, (ast_node*)p_call)) {
    return false;
  }
  const uint32_t argc = p_call->arguments.count;
  bool is_long = p_dest.index > a_MAX || (argc > 0 && args.index > a_MAX) || argc > b_MAX;
  if (is_long) {
    if (!emit_word2(p_compiler, encode_OPPABCQ(OP_CALL_LONG, false, p_dest.index, argc, 2, 0), (ast_node*)p_call)) {
      report_oom(p_compiler, (ast_node*)p_call);
      return false;
    }
  } else {
    if (!emit_word(p_compiler, encode_oppabc(OP_CALL, false, p_dest.index, argc, 2), (ast_node*)p_call)) {
      report_oom(p_compiler, (ast_node*)p_call);
      return false;
    }
  }
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->count = 1;
  p_out->type = EXPRESSION_VALUE;
  return true;
}

// r[p_dest] = r[p_base][p_index]. base is always a register (resolve_lvalue
// materializes it), index may be a constant (GET carries a const-index flag).
static bool
emit_array_get(compiler* p_compiler, _register p_dest, location p_base, location p_index, ast_node* p_author) {
  const bool is_index_constant = p_index.kind == LOCATION_KIND_CONSTANT;
  if (p_dest.index > a_MAX || p_base.index > b_MAX || p_index.index > c_MAX) {
    if (!emit_word2(
            p_compiler,
            encode_OPPABCQ(OP_ARRAY_GET_LONG, is_index_constant, p_dest.index, p_base.index, p_index.index, false),
            p_author)) {
      report_oom(p_compiler, p_author);
      return false;
    }
    return true;
  }
  if (!emit_word(p_compiler,
                 encode_oppabc(OP_ARRAY_GET, is_index_constant, p_dest.index, p_base.index, p_index.index),
                 p_author)) {
    report_oom(p_compiler, p_author);
    return false;
  }
  return true;
}

static bool compile_array_expression(compiler* p_compiler,
                                     ast_array_expression* p_array,
                                     _register p_dest,
                                     uint32_t p_requested_count,
                                     expression_result* p_out) {
  (void)p_requested_count;
  ast_node* author = (ast_node*)p_array;
  // elements compile into consecutive temps at the start (compile_expressions_list
  // allocates them one by one), so they end up exactly at [start .. start + count).
  // NEW's (dest, src, count) operands are independent: the array lands directly in
  // p_dest. a local slot, a temp, or the devnull, with no move.
  _register elements_start = {0, REGISTER_FLAG_NONE, REGISTER_OK};
  if (!compile_expressions_list(p_compiler, &p_array->elements, C_MAX, &elements_start, author)) {
    return false;
  }
  const uint32_t count = p_array->elements.count;
  const uint16_t src = count > 0 ? (uint16_t)elements_start.index : (uint16_t)p_dest.index; // unused when count == 0
  if (p_dest.index > a_MAX || src > b_MAX || count > c_MAX) {
    if (!emit_word2(p_compiler, encode_OPPABCQ(OP_ARRAY_NEW_LONG, false, p_dest.index, src, count, false), author)) {
      report_oom(p_compiler, author);
      return false;
    }
  } else if (!emit_word(p_compiler, encode_oppabc(OP_ARRAY_NEW, false, p_dest.index, src, (uint8_t)count), author)) {
    report_oom(p_compiler, author);
    return false;
  }
  p_out->type = EXPRESSION_VALUE;
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->count = 1;
  return true;
}

static bool compile_subscript_expression(compiler* p_compiler,
                                         ast_subscript_expression* p_subscript,
                                         _register p_dest,
                                         uint32_t p_requested_count,
                                         expression_result* p_out) {
  (void)p_requested_count;
  // read path goes through the same resolve_lvalue as writes: base and index are
  // evaluated identically in both, and a[i] compiles side effects even when its
  // value is discarded (matching compile_call_expression's behavior at count 0).
  // no pending rhs: only the index can disqualify container elision here.
  lvalue lv = resolve_lvalue(p_compiler, (ast_expression*)p_subscript, NULL);
  if (lv.kind == LVALUE_INVALID) {
    return false;
  }
  if (!emit_array_get(p_compiler, p_dest, lv.subscript.base, lv.subscript.index, (ast_node*)p_subscript)) {
    return false;
  }
  p_out->type = EXPRESSION_VALUE | EXPRESSION_SUBSCRIPT;
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  p_out->lvalue = lv;
  p_out->count = 1;
  return true;
}

static bool compile_compound_assign_expression(compiler* p_compiler,
                                               ast_compound_assign_expression* p_compound,
                                               _register p_dest,
                                               uint32_t p_requested_count,
                                               expression_result* p_out) {
  CRASH_MSG(p_requested_count <= 1, "unimplemented multi-assign.");
  register_allocator* regalloc = &current_context(p_compiler)->register_allocator;
  ast_node* author = (ast_node*)p_compound;

  opcode op;
  opcode op_long;
  switch (p_compound->_operator) {
  case OPERATOR_PLUS_EQUAL:
    op = OP_ADD;
    op_long = OP_ADD_LONG;
    break;
  case OPERATOR_MINUS_EQUAL:
    op = OP_SUBTRACT;
    op_long = OP_SUBTRACT_LONG;
    break;
  case OPERATOR_ASTERISK_EQUAL:
    op = OP_MULTIPLY;
    op_long = OP_MULTIPLY_LONG;
    break;
  case OPERATOR_SLASH_EQUAL:
    op = OP_DIVIDE;
    op_long = OP_DIVIDE_LONG;
    break;
  case OPERATOR_MODULO_EQUAL:
    op = OP_MODULUS;
    op_long = OP_MODULUS_LONG;
    break;
  default:
    error_at(p_compiler,
             author,
             ok_create_string_view("unsupported compound assignment operator.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }

  // resolve the target once: base/index (subscripts) are evaluated here and never
  // again, so a[f()] += g() calls f exactly once.
  lvalue lv = resolve_lvalue(p_compiler, p_compound->left, p_compound->right);
  if (lv.kind == LVALUE_INVALID) {
    return false;
  }

  // current value: locals update in place (they are registers), everything else
  // loads into a temp first. the local mutability check happens before any code
  // is emitted for it.
  _register cur;
  location cur_loc;
  if (lv.kind == LVALUE_VARIABLE && lv.variable.kind == LOCATION_KIND_REGISTER) {
    const _register local = current_context(p_compiler)->register_allocator.registers.data[lv.variable.index];
    if ((local.flags & REGISTER_FLAG_MUTABLE) == 0) {
      error_at_noted(
          p_compiler,
          author,
          ok_create_string_view(
              "attempting to mutate an immutable local variable.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
          ok_create_string_view("did you forget to declare it 'mut'?", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return false;
    }
    cur = (_register){lv.variable.index, REGISTER_FLAG_NONE, REGISTER_OK};
    cur_loc = lv.variable;
  } else {
    if (!register_allocator_allocate_temp(regalloc, &cur)) {
      report_oom(p_compiler, author);
      return false;
    }
    if (lv.kind == LVALUE_VARIABLE) {
      if (!materialize_value(p_compiler, cur, lv.variable, author)) {
        return false;
      }
    } else if (!emit_array_get(p_compiler, cur, lv.subscript.base, lv.subscript.index, author)) {
      return false;
    }
    cur_loc = (location){cur.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  }

  // right side into its own temp: evaluation order is base, index, current, rhs
  _register rhs_reg;
  if (!register_allocator_allocate_temp(regalloc, &rhs_reg)) {
    report_oom(p_compiler, author);
    return false;
  }
  expression_result rhs;
  if (!compile_expression(p_compiler, p_compound->right, rhs_reg, 1, &rhs)) {
    return false;
  }
  bool is_rhs_constant = false;
  if (!materialize_into_if_not_common(
          p_compiler, &is_rhs_constant, LOCATION_KIND_CONSTANT, &rhs.value, rhs_reg, author)) {
    return false;
  }

  // cur = cur OP rhs (in place for locals: one ADD, per the desugaring table)
  if (!emit_binop(p_compiler, op, op_long, cur, cur_loc, rhs.value, false, is_rhs_constant, author)) {
    return false;
  }

  // store back through the same target
  if (lv.kind == LVALUE_VARIABLE) {
    if (lv.variable.kind == LOCATION_KIND_REGISTER) {
      // updated in place
    } else {
      expression_result stored = {EXPRESSION_VALUE, cur_loc, (lvalue){LVALUE_INVALID}, 1};
      if (!store_variable(p_compiler, lv.variable, stored, author)) {
        return false; // mutability of the global/upvalue is checked here
      }
    }
  } else {
    // subscript: value is the cur register, store_lvalue handles narrow/long
    expression_result stored = {EXPRESSION_VALUE, cur_loc, (lvalue){LVALUE_INVALID}, 1};
    if (!store_lvalue(p_compiler, lv, stored, author)) {
      return false;
    }
  }

  *p_out = (expression_result){EXPRESSION_VALUE, cur_loc, (lvalue){LVALUE_INVALID}, 1};
  return true;
}

static bool compile_for_in_statement(compiler* p_compiler, ast_for_in_statement* p_for_in) {
  allocators* alloc = p_compiler->alloc;
  compile_context* ctx = current_context(p_compiler);
  register_allocator* regalloc = &ctx->register_allocator;
  ast_node* author = (ast_node*)p_for_in;
  {
    loop_context lctx;
    loop_context_init(&lctx, alloc);
    if (!loop_stack_append(&ctx->loop_stack, lctx)) {
      report_oom(p_compiler, author);
      loop_context_deinit(&lctx, alloc);
      return false;
    }
  }
  if (!begin_scope(p_compiler, author)) {
    pop_loop_context(ctx, alloc);
    return false;
  }
  chunk* chunk = current_chunk(p_compiler);
#define CTX ctx->loop_stack.data[ctx->loop_stack.count - 1]
  CTX.scope_depth = ctx->scopes.count - 1;

  // hidden state must be locals, not temps: every body statement resets temps to
  // local_top, which sits below any temp allocated here. the registers would be
  // handed out to the body's own expressions and clobbered. the leading space makes
  // the names untypeable, and this loop's dedicated scope makes nesting safe.
  // the counter, limit and step are declared adjacently because the FOR ops read
  // the limit at counter + 1 and the step at counter + 2 (the same fused back edge
  // the numeric for uses - one increment/test dispatch per iteration).
  variable_declaration counter_decl = {.identifier = ok_create_string_view(" i", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                       .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register counter = add_local(p_compiler, counter_decl, false, author);
  if (!IS_REGISTER_VALID(counter)) {
    goto fail;
  }
  variable_declaration limit_decl = {.identifier =
                                         ok_create_string_view(" limit", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                     .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register limit = add_local(p_compiler, limit_decl, false, author);
  if (!IS_REGISTER_VALID(limit)) {
    goto fail;
  }
  variable_declaration step_decl = {.identifier = ok_create_string_view(" step", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                    .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register step = add_local(p_compiler, step_decl, false, author);
  if (!IS_REGISTER_VALID(step)) {
    goto fail;
  }
  ASSERT(limit.index == counter.index + 1 && step.index == counter.index + 2);
  variable_declaration arr_decl = {.identifier = ok_create_string_view(" arr", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                   .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register arr = add_local(p_compiler, arr_decl, false, author);
  if (!IS_REGISTER_VALID(arr)) {
    goto fail;
  }
  variable_declaration var_decl = {
      .identifier = ast_identifier_expression_get_value((ast_identifier_expression*)p_for_in->binding->lvalue),
      .flags = variable_declaration_flags_from_modifiers(DECLARATION_NONE, p_for_in->binding->modifiers)};
  _register var = add_local(p_compiler, var_decl, false, (ast_node*)p_for_in->binding);
  if (!IS_REGISTER_VALID(var)) {
    goto fail;
  }
  // the loop variable is defined by the first GET: clear the uninit flag so body
  // resolution finds it (resolve_local skips uninitialized locals for shadowing).
  regalloc->registers.data[var.index].flags &= ~REGISTER_FLAG_UNINITIALIZED;

  // arr = <iterable> (evaluated exactly once, at loop entry)
  {
    expression_result iterable;
    if (!compile_expression(p_compiler, p_for_in->iterable, arr, 1, &iterable)) {
      goto fail;
    }
    if (!materialize_into(p_compiler, &iterable.value, arr, author)) {
      goto fail;
    }
  }

  {
    const global len_global = globals_store_get(p_compiler->globals_store,
                                                ok_create_string_view(" len", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    if (!IS_GLOBAL_VALID(len_global)) {
      error_at_noted(p_compiler,
                     author,
                     ok_create_string_view("undefined variable: len.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                     ok_create_string_view("ranged for needs the 'len' builtin. ok_create registers it.",
                                           OK_STRING_VIEW_CALCULATE_LENGTH,
                                           true));
      goto fail;
    }
    _register t_len;
    if (!register_allocator_allocate_temp(regalloc, &t_len)) {
      report_oom(p_compiler, author);
      goto fail;
    }
    if (t_len.index > aprime_MAX || len_global.index > bprime_MAX) {
      if (!emit_word2(
              p_compiler, encode_OPPABCQ(OP_LOAD_GLOBAL_LONG, false, t_len.index, len_global.index, 0, 0), author)) {
        report_oom(p_compiler, author);
        goto fail;
      }
    } else if (!emit_word(
                   p_compiler, encode_oppabprime(OP_LOAD_GLOBAL, false, t_len.index, len_global.index), author)) {
      report_oom(p_compiler, author);
      goto fail;
    }
    // stage the call window: callable at t_len, argument at t_len + 1
    _register t_arg;
    if (!register_allocator_allocate_temp(regalloc, &t_arg)) {
      report_oom(p_compiler, author);
      goto fail;
    }
    if (!materialize_value(p_compiler, t_arg, (location){arr.index, LOCATION_KIND_REGISTER, LOCATION_OK}, author)) {
      goto fail;
    }
    if (t_len.index > a_MAX || t_arg.index > a_MAX) {
      if (!emit_word2(p_compiler, encode_OPPABCQ(OP_CALL_LONG, false, t_len.index, 1, 2, 0), author)) {
        report_oom(p_compiler, author);
        goto fail;
      }
    } else if (!emit_word(p_compiler, encode_oppabc(OP_CALL, false, t_len.index, 1, 2), author)) {
      report_oom(p_compiler, author);
      goto fail;
    }
    // t_len now holds the count: move it into the hidden limit local
    if (!materialize_value(p_compiler, limit, (location){t_len.index, LOCATION_KIND_REGISTER, LOCATION_OK}, author)) {
      goto fail;
    }
  }

  // counter = 0, step = 1: constants feeding the fused FOR pair
  {
    constant zero = add_constant(p_compiler, NUMBER_AS_VALUE(0), author);
    if (!IS_CONSTANT_VALID(zero)) {
      goto fail;
    }
    if (counter.index > aprime_MAX || zero.index > bprime_MAX) {
      if (!emit_word2(
              p_compiler, encode_OPPABCQ(OP_LOAD_CONSTANT_LONG, false, counter.index, zero.index, 0, 0), author)) {
        report_oom(p_compiler, author);
        goto fail;
      }
    } else if (!emit_word(p_compiler, encode_oppabprime(OP_LOAD_CONSTANT, false, counter.index, zero.index), author)) {
      report_oom(p_compiler, author);
      goto fail;
    }
  }
  {
    constant one = add_constant(p_compiler, NUMBER_AS_VALUE(1), author);
    if (!IS_CONSTANT_VALID(one)) {
      goto fail;
    }
    if (step.index > aprime_MAX || one.index > bprime_MAX) {
      if (!emit_word2(p_compiler, encode_OPPABCQ(OP_LOAD_CONSTANT_LONG, false, step.index, one.index, 0, 0), author)) {
        report_oom(p_compiler, author);
        goto fail;
      }
    } else if (!emit_word(p_compiler, encode_oppabprime(OP_LOAD_CONSTANT, false, step.index, one.index), author)) {
      report_oom(p_compiler, author);
      goto fail;
    }
  }

  // entry: FOR_PREP runs the first bounds test (0 < len) and skips the whole
  // loop for empty iterables. per iteration from here on: one ARRAY_GET at the
  // body top, one FOR_LOOP at the bottom - the counter machinery is the same
  // fused back edge as the numeric for.
  uint32_t prep;
  if (!emit_incomplete_for_jump(p_compiler, OP_FOR_PREP, false, counter, &prep, author)) {
    goto fail;
  }

  const uint32_t body_start = chunk->code.count;

  // var = arr[i] (fresh each iteration through the same slot; the body may
  // capture it. same shared-slot semantics as the C style for today)
  if (!emit_array_get(p_compiler,
                      var,
                      (location){arr.index, LOCATION_KIND_REGISTER, LOCATION_OK},
                      (location){counter.index, LOCATION_KIND_REGISTER, LOCATION_OK},
                      author)) {
    goto fail;
  }

  CTX.continue_forward = true; // continue jumps to the increment, like C for update
  if (!compile_statement(p_compiler, p_for_in->body)) {
    goto fail;
  }

  // the back edge's p flag: the for-in counter is a nameless hidden local,
  // unwritable by construction - the raw-read fast path always applies.
  const bool counter_immutable = true;

  CTX.continue_target = chunk->code.count;
  uint32_t back_edge;
  if (!emit_incomplete_for_jump(p_compiler, OP_FOR_LOOP, counter_immutable, counter, &back_edge, author)) {
    goto fail;
  }
  if (!patch_jump_if(p_compiler, true, back_edge, body_start, author)) {
    goto fail;
  }
  if (!patch_jump_if(p_compiler, false, prep, chunk->code.count, author)) {
    goto fail;
  }
  CTX.break_target = chunk->code.count;

  if (!patch_loop_context(p_compiler, &CTX, author)) {
    goto fail;
  }
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, author));
  return true;
fail:
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, author));
  return false;
#undef CTX
}

// `for let (mut)? i = <start> : <limit> (: <step>)? <statement>` - the fused
// numeric loop. start, limit and step are evaluated once, left to right, before
// the first bounds test; the limit is frozen for the whole trip (lua style). the
// counter is a normal binding: assignable when `mut` (reseats the current
// iteration's value, never the trip), captureable with shared slot semantics.
static bool compile_for_numeric_statement(compiler* p_compiler, ast_for_numeric_statement* p_for_numeric) {
  allocators* alloc = p_compiler->alloc;
  compile_context* ctx = current_context(p_compiler);
  register_allocator* regalloc = &ctx->register_allocator;
  ast_node* author = (ast_node*)p_for_numeric;
  {
    loop_context lctx;
    loop_context_init(&lctx, alloc);
    if (!loop_stack_append(&ctx->loop_stack, lctx)) {
      report_oom(p_compiler, author);
      loop_context_deinit(&lctx, alloc);
      return false;
    }
  }
  if (!begin_scope(p_compiler, author)) {
    pop_loop_context(ctx, alloc);
    return false;
  }
  chunk* chunk = current_chunk(p_compiler);
#define CTX ctx->loop_stack.data[ctx->loop_stack.count - 1]
  CTX.scope_depth = ctx->scopes.count - 1;

  // the counter is the user-visible binding, declared first so the two hidden
  // locals can sit directly after it: the FOR ops read the limit at B + 1 and
  // the step at B + 2, so the three declarations must stay adjacent (asserted).
  // the hidden names' leading space makes them untypeable, and this loop's
  // dedicated scope makes nesting safe. hidden state must be locals, not temps:
  // every body statement resets temps to local_top and would clobber them.
  variable_declaration counter_decl = {
      .identifier = ast_identifier_expression_get_value((ast_identifier_expression*)p_for_numeric->binding->lvalue),
      .flags = variable_declaration_flags_from_modifiers(DECLARATION_NONE, p_for_numeric->binding->modifiers)};
  _register counter = add_local(p_compiler, counter_decl, false, (ast_node*)p_for_numeric->binding);
  if (!IS_REGISTER_VALID(counter)) {
    goto fail;
  }
  variable_declaration limit_decl = {.identifier =
                                         ok_create_string_view(" limit", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                     .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register limit = add_local(p_compiler, limit_decl, false, author);
  if (!IS_REGISTER_VALID(limit)) {
    goto fail;
  }
  variable_declaration step_decl = {.identifier = ok_create_string_view(" step", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                    .flags = VARIABLE_DECLARATION_FLAGS_MUTABLE};
  _register step = add_local(p_compiler, step_decl, false, author);
  if (!IS_REGISTER_VALID(step)) {
    goto fail;
  }
  ASSERT(limit.index == counter.index + 1 && step.index == counter.index + 2);

  // counter = <start>. the counter stays uninitialized while the start resolves,
  // so it behaves like a let initializer: `for let i = i : ...` sees the outer i.
  {
    expression_result start;
    if (!compile_expression(p_compiler, p_for_numeric->start, counter, 1, &start)) {
      goto fail;
    }
    if (!materialize_into(p_compiler, &start.value, counter, author)) {
      goto fail;
    }
  }
  // from here the counter is live: the limit and step see it, like sequential lets.
  regalloc->registers.data[counter.index].flags &= ~REGISTER_FLAG_UNINITIALIZED;

  // limit = <limit expr> (evaluated once, frozen for the whole trip)
  {
    expression_result limit_value;
    if (!compile_expression(p_compiler, p_for_numeric->limit, limit, 1, &limit_value)) {
      goto fail;
    }
    if (!materialize_into(p_compiler, &limit_value.value, limit, author)) {
      goto fail;
    }
  }

  // step = <step expr> or the default 1
  if (p_for_numeric->step != NULL) {
    expression_result step_value;
    if (!compile_expression(p_compiler, p_for_numeric->step, step, 1, &step_value)) {
      goto fail;
    }
    if (!materialize_into(p_compiler, &step_value.value, step, author)) {
      goto fail;
    }
  } else {
    constant one = add_constant(p_compiler, NUMBER_AS_VALUE(1), author);
    if (!IS_CONSTANT_VALID(one)) {
      goto fail;
    }
    if (step.index > aprime_MAX || one.index > bprime_MAX) {
      if (!emit_word2(p_compiler, encode_OPPABCQ(OP_LOAD_CONSTANT_LONG, false, step.index, one.index, 0, 0), author)) {
        report_oom(p_compiler, author);
        goto fail;
      }
    } else if (!emit_word(p_compiler, encode_oppabprime(OP_LOAD_CONSTANT, false, step.index, one.index), author)) {
      report_oom(p_compiler, author);
      goto fail;
    }
  }

  // the back edge's p flag: a plain `let` counter is statically unwritable
  // (immutability is enforced on every store path), so the VM drops the
  // per-trip counter gate; `let mut` keeps it.
  const bool counter_immutable = (regalloc->registers.data[counter.index].flags & REGISTER_FLAG_MUTABLE) == 0;

  // entry: validate + first bounds test, skip the whole loop when it fails
  uint32_t prep;
  if (!emit_incomplete_for_jump(p_compiler, OP_FOR_PREP, false, counter, &prep, author)) {
    goto fail;
  }

  // body. continue lands on the FOR_LOOP back edge (the increment site, same
  // discipline as the C style for's update clause); break lands after it.
  const uint32_t body_start = chunk->code.count;
  CTX.continue_forward = true;
  if (!compile_statement(p_compiler, p_for_numeric->body)) {
    goto fail;
  }

  CTX.continue_target = chunk->code.count;
  uint32_t back_edge;
  if (!emit_incomplete_for_jump(p_compiler, OP_FOR_LOOP, counter_immutable, counter, &back_edge, author)) {
    goto fail;
  }
  if (!patch_jump_if(p_compiler, true, back_edge, body_start, author)) {
    goto fail;
  }
  if (!patch_jump_if(p_compiler, false, prep, chunk->code.count, author)) {
    goto fail;
  }
  CTX.break_target = chunk->code.count;

  if (!patch_loop_context(p_compiler, &CTX, author)) {
    goto fail;
  }
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, author));
  return true;
fail:
  pop_loop_context(ctx, alloc);
  ASSERT(end_scope(p_compiler, author));
  return false;
#undef CTX
}

static bool compile_postfix_unary_expression(compiler* p_compiler,
                                             ast_postfix_unary_expression* p_expression,
                                             _register p_dest,
                                             uint32_t p_requested_count,
                                             expression_result* p_out) {
  CRASH_MSG(false, "unimplemented postfix expression.");
  return false;
}

static bool compile_boolean_expression(compiler* p_compiler,
                                       ast_boolean_expression* p_boolean,
                                       _register p_dest,
                                       uint32_t p_requested_count,
                                       expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  if (p_requested_count == 0) {
    return true;
  }
  if (!emit_word(
          p_compiler,
          encode_oppA(ast_boolean_expression_get_value(p_boolean) ? OP_LOAD_TRUE : OP_LOAD_FALSE, false, p_dest.index),
          (ast_node*)p_boolean)) {
    report_oom(p_compiler, (ast_node*)p_boolean);
    return false;
  }
  p_out->count = 1;
  p_out->type = EXPRESSION_VALUE;
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  return true;
}

static bool compile_null_expression(compiler* p_compiler,
                                    ast_null_expression* p_null,
                                    _register p_dest,
                                    uint32_t p_requested_count,
                                    expression_result* p_out) {
  if (p_requested_count == 0) {
    return true;
  }
  if (!emit_word2(
          p_compiler, encode_OPPABCQ(OP_LOAD_NULL, false, p_dest.index, p_requested_count, 0, 0), (ast_node*)p_null)) {
    report_oom(p_compiler, (ast_node*)p_null);
    return false;
  }
  p_out->count = p_requested_count;
  p_out->type = EXPRESSION_VALUE;
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  return true;
}

bool compile_function_expression(compiler* p_compiler,
                                 ast_function_expression* p_fu,
                                 _register p_dest,
                                 uint32_t p_requested_count,
                                 expression_result* p_out) {
  ASSERT(p_requested_count <= 1);
  if (p_requested_count == 0) {
    return true;
  }

  return compile_function_impl(p_compiler,
                               ok_create_string_view("<lambda>", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                               FUNCTION_FUNCTION,
                               p_fu->parameters,
                               p_fu->body,
                               p_dest,
                               p_out,
                               (ast_node*)p_fu);
}

static bool compile_function_impl(compiler* p_compiler,
                                  ok_string_view p_name,
                                  function_type p_type,
                                  ast_bindings_list p_params,
                                  ast_statement* p_body,
                                  _register p_dest,
                                  expression_result* p_out,
                                  ast_node* p_author) {
  allocators* alloc = p_compiler->alloc;
  if (p_params.count > UINT8_MAX) {
    string note = asprint(alloc, "limit is: %d", UINT8_MAX);
    error_at_noted(p_compiler,
                   p_author,
                   ok_create_string_view("too many function parameters.", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                   ok_create_string_view_from_string(note));
    string_deinit(&note, alloc);
    return false;
  }
  if (!begin_context(p_compiler, p_name, p_type, p_params.count, p_author)) {
    return false;
  }
  if (!begin_scope(p_compiler, p_author)) {
    end_context(p_compiler);
    return false;
  }
  variable_declaration decl = {.identifier = p_name, .flags = DECLARATION_NONE};
  _register funtion_name = add_local(p_compiler, decl, false, p_author);
  if (!IS_REGISTER_VALID(funtion_name)) {
    end_scope(p_compiler, p_author);
    end_context(p_compiler);
    return false;
  }
  location loc = {funtion_name.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  if (!define_variable(
          p_compiler,
          loc,
          (expression_result){EXPRESSION_LVALUE | EXPRESSION_VALUE, loc, (lvalue){LVALUE_VARIABLE, loc}, 1},
          p_author)) {
    end_scope(p_compiler, p_author);
    end_context(p_compiler);
    return false;
  }

  location param_locations[UINT8_MAX + 1];
  for (uint32_t i = 0; i < p_params.count; ++i) {
    ast_binding* binding = p_params.data[i];
    variable_declaration decl = {
        .identifier = ast_identifier_expression_get_value((ast_identifier_expression*)binding->lvalue),
        .flags = variable_declaration_flags_from_modifiers(DECLARATION_NONE, binding->modifiers)};
    _register param = declare_variable(p_compiler, decl, (ast_node*)binding);
    if (!IS_REGISTER_VALID(param)) {
      end_scope(p_compiler, p_author);
      end_context(p_compiler);
      return false;
    }
    location loc = {param.index, LOCATION_KIND_REGISTER, LOCATION_OK};
    param_locations[i] = loc;

    if (!define_variable(
            p_compiler,
            loc,
            (expression_result){EXPRESSION_LVALUE | EXPRESSION_VALUE, loc, (lvalue){LVALUE_VARIABLE, loc}, 1},
            (ast_node*)binding)) {
      end_scope(p_compiler, p_author);
      end_context(p_compiler);
      return false;
    }
  }

  // the defaults ladder, as the function's first code: one guard per defaulted
  // param, skipping the default when the argument was supplied. call-time
  // evaluation, c++ style. fresh mutable defaults per call, earlier params in
  // scope. functions without defaults emit nothing at all.
  {
    uint16_t ndefaults = 0;
    for (uint32_t i = 0; i < p_params.count; ++i) {
      if (p_params.data[i]->default_value != NULL) {
        ndefaults = (uint16_t)(p_params.count - i); // defaults are trailing, parser enforced
        break;
      }
    }
    current_context(p_compiler)->function.function->ndefaults = ndefaults;
    for (uint32_t i = p_params.count - ndefaults; i < p_params.count; ++i) {
      ast_binding* binding = p_params.data[i];
      uint32_t guard;
      if (!emit_incomplete_jump_if_argc(p_compiler, true, (uint16_t)i, &guard, (ast_node*)binding)) {
        end_scope(p_compiler, p_author);
        end_context(p_compiler);
        return false;
      }
      _register param_reg = {param_locations[i].index, REGISTER_FLAG_NONE, REGISTER_OK};
      expression_result filled;
      if (!compile_expression(p_compiler, binding->default_value, param_reg, 1, &filled)) {
        end_scope(p_compiler, p_author);
        end_context(p_compiler);
        return false;
      }
      if (!materialize_into(p_compiler, &filled.value, param_reg, (ast_node*)binding)) {
        end_scope(p_compiler, p_author);
        end_context(p_compiler);
        return false;
      }
      if (!patch_jump_if(p_compiler, false, guard, current_chunk(p_compiler)->code.count, (ast_node*)binding)) {
        end_scope(p_compiler, p_author);
        end_context(p_compiler);
        return false;
      }
    }
  }
  if (!compile_statement(p_compiler, p_body)) {
    end_scope(p_compiler, p_author);
    end_context(p_compiler);
    return false;
  }

  if (!emit_default_return(p_compiler, (ast_node*)p_body)) {
    end_scope(p_compiler, p_author);
    end_context(p_compiler);
    return false;
  }

  if (!end_scope(p_compiler, p_author)) {
    end_context(p_compiler);
    return false;
  }

  compile_context* function_ctx = current_context(p_compiler);
  object_function* proto = function_ctx->function.function;
  proto->registers = function_ctx->register_allocator.registers_count; // frame size for the vm
  gc_guard_up(alloc->gc, OBJECT_AS_VALUE(proto));

  if (function_ctx->upvalues.count > 0 &&
      !upvalue_descriptor_array_grow(&proto->upvalues, function_ctx->upvalues.count)) {
    end_context(p_compiler);
    report_oom(p_compiler, p_author);
    gc_guard_down(alloc->gc);
    return false;
  }

  for (uint32_t i = 0; i < function_ctx->upvalues.count; ++i) {
    upvalue up = function_ctx->upvalues.data[i];
    upvalue_descriptor_array_append(&proto->upvalues,
                                    (upvalue_descriptor){up.index, (up.flags & UPVALUE_FLAG_LOCAL) != 0});
  }

#if defined(OK_DEBUG_DUMP_CODE) // this is really useful
  disassembler disassembler;
  disassembler_specs specs = {.chunk = &proto->chunk, .globals_store = p_compiler->globals_store};
  disassembler_init(&disassembler, specs);
  debug_disassemble_chunk(&disassembler, proto->name->string.chars);
  disassembler_deinit(&disassembler);
#endif // defined (OK_DEBUG_DUMP_CODE)

  // the descriptors were copied into proto->upvalues above, the compile-side
  // context is pure compile-time state: release it before popping (leak fix).
  compile_context_deinit(function_ctx, p_compiler->alloc);
  ASSERT(compile_contexts_pop(&p_compiler->contexts));

  constant kproto = add_constant(p_compiler, OBJECT_AS_VALUE(proto), p_author);
  gc_guard_down(alloc->gc);

  if (!IS_CONSTANT_VALID(kproto)) {
    return false;
  }

  bool is_long = p_dest.index > aprime_MAX || kproto.index > bprime_MAX;

  if (is_long) {
    if (!emit_word2(p_compiler, encode_OPPABCQ(OP_CLOSURE_LONG, false, p_dest.index, kproto.index, 0, 0), p_author)) {
      return false;
    }
  } else {
    if (!emit_word(p_compiler, encode_oppabprime(OP_CLOSURE, false, p_dest.index, kproto.index), p_author)) {
      return false;
    }
  }
  p_out->type = EXPRESSION_VALUE;
  p_out->lvalue = (lvalue){LVALUE_INVALID};
  p_out->count = 1;
  p_out->value = (location){p_dest.index, LOCATION_KIND_REGISTER, LOCATION_OK};
  return true;
}
