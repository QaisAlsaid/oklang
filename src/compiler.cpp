#include "compiler.hpp"
#include "ast.hpp"
#include "chunk.hpp"
#include "debug.hpp"
#include "lexer.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "parser.hpp"
#include "token.hpp"
#include "utf8.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ok
{
  template <typename Class>
  struct scope_guard
  {
    using func = std::function<void(Class*)>;
    scope_guard(func ctor, func dtor, Class* cls) : ctor(ctor), dtor(dtor), cls(cls)
    {
      ctor(cls);
    }
    ~scope_guard()
    {
      dtor(cls);
    }
    func ctor;
    func dtor;
    Class* cls;
  };

  function_object* compiler::compile(const std::string_view p_src, string_object* p_function_name, uint32_t p_vm_id)
  {
    m_compiled = true;
    m_vm_id = p_vm_id;
    lexer lx;
    auto arr = lx.lex(p_src);
#ifdef PARANOID
    TRACELN("lexer output: {{");
    for(auto tok : arr)
      TRACELN("token: type: '{}', raw: '{}'", token_type_to_string(tok.type), tok.raw_literal);
    TRACELN("\n}}");
#endif
    parser prs{arr};
    auto root = prs.parse_program();
    m_parse_errors = prs.get_errors();
    if(!m_parse_errors.errs.empty() || root == nullptr)
      return nullptr;
    TRACELN("{}", root->to_string());
    // top level script function
    push_function({function_object::create<function_object>(0, p_function_name), compile_function::type::script});
    compile(root.get());
    current_chunk()->write(opcode::op_null, 0);
    current_chunk()->write(opcode::op_return, 0);
#ifdef PARANOID
    debug::disassembler::disassemble_chunk(*current_chunk(), current_function().function->name->chars);
#endif
    return m_errors.errs.empty() ? current_function().function : nullptr;
  }

  void compiler::compile(ast::node* p_node)
  {
    ASSERT(p_node); // its a bug if this function called with nullptr, its required to be handled outside
    switch(p_node->get_type())
    {
    case ast::node_type::nt_program:
      compile((ast::program*)(p_node));
      return;
    case ast::node_type::nt_infix_binary_expr:
      compile((ast::infix_binary_expression*)(p_node));
      return;
    case ast::node_type::nt_prefix_expr:
      compile((ast::prefix_unary_expression*)(p_node));
      return;
    case ast::node_type::nt_number_expr:
      compile((ast::number_expression*)(p_node));
      return;
    case ast::node_type::nt_string_expr:
      compile((ast::string_expression*)p_node);
      return;
    case ast::node_type::nt_expression_statement_stmt:
      compile((ast::expression_statement*)p_node);
      return;
    case ast::node_type::nt_boolean_expr:
      compile((ast::boolean_expression*)p_node);
      return;
    case ast::node_type::nt_null_expr:
      compile((ast::null_expression*)p_node);
      return;
    case ast::node_type::nt_print_stmt:
      compile((ast::print_statement*)p_node);
      return;
    case ast::node_type::nt_let_decl:
      compile((ast::let_declaration*)p_node);
      return;
    case ast::node_type::nt_identifier_expr:
      compile((ast::identifier_expression*)p_node);
      return;
    case ast::node_type::nt_assign_expr:
      compile((ast::assign_expression*)p_node);
      return;
    case ast::node_type::nt_block_stmt:
      compile((ast::block_statement*)p_node);
      return;
    case ast::node_type::nt_if_stmt:
      compile((ast::if_statement*)p_node);
      return;
    case ast::node_type::nt_while_stmt:
      compile((ast::while_statement*)p_node);
      return;
    case ast::node_type::nt_for_stmt:
      compile((ast::for_statement*)p_node);
      return;
    case ast::node_type::nt_control_flow_stmt:
      compile((ast::control_flow_statement*)p_node);
      return;
    case ast::node_type::nt_function_decl:
      compile((ast::function_declaration*)p_node);
      return;
    case ast::node_type::nt_call_expr:
      compile((ast::call_expression*)p_node);
      return;
    case ast::node_type::nt_return_stmt:
      compile((ast::return_statement*)p_node);
      return;
    default:
      // TODO(Qais): node_type_to_string
      compile_error(error::code::invalid_compilation_target,
                    "compile called with an incompatible node: {}",
                    (uint32_t)p_node->get_type());
      return;
    }
  }

  void compiler::compile(ast::program* p_program)
  {
    for(const auto& stmt : p_program->get_statements())
    {
      compile(stmt.get());
    }
  }

  void compiler::compile(ast::expression_statement* p_expr_stmt)
  {
    const auto& expr = p_expr_stmt->get_expression();
    compile(expr.get());
    current_chunk()->write(opcode::op_pop, p_expr_stmt->get_offset());
  }

  void compiler::compile(ast::prefix_unary_expression* p_unary)
  {
    compile(p_unary->get_right().get());
    switch(p_unary->get_operator())
    {
    case operator_type::op_plus:
      return; // TODO(Qais): vm supports this but not opcode!
    case operator_type::op_minus:
      current_chunk()->write(opcode::op_negate, p_unary->get_offset());
      return;
    case operator_type::op_bang:
      current_chunk()->write(opcode::op_not, p_unary->get_offset());
      return;
    default:
      return;
    }
  }

  void compiler::compile(ast::infix_binary_expression* p_binary)
  {
    const auto& op = p_binary->get_operator();
    if(op == operator_type::op_and || op == operator_type::op_or)
    {
      compile_logical_operator(p_binary);
      return;
    }
    compile(p_binary->get_left().get());
    compile(p_binary->get_right().get());

    auto current = current_chunk();

    switch(p_binary->get_operator())
    {
    case operator_type::op_plus:
      current->write(opcode::op_add, p_binary->get_offset());
      break;
    case operator_type::op_minus:
      current->write(opcode::op_subtract, p_binary->get_offset());
      break;
    case operator_type::op_asterisk:
      current->write(opcode::op_multiply, p_binary->get_offset());
      break;
    case operator_type::op_slash:
      current->write(opcode::op_divide, p_binary->get_offset());
      break;
    case operator_type::op_equal:
      current->write(opcode::op_equal, p_binary->get_offset());
      break;
    case operator_type::op_bang_equal:
      current->write(opcode::op_not_equal, p_binary->get_offset());
      break;
    case operator_type::op_less:
      current->write(opcode::op_less, p_binary->get_offset());
      break;
    case operator_type::op_greater:
      current->write(opcode::op_greater, p_binary->get_offset());
      break;
    case operator_type::op_less_equal:
    {
      current->write(opcode::op_less_equal, p_binary->get_offset());
      break;
    }
    case operator_type::op_greater_equal:
    {
      current->write(opcode::op_greater_equal, p_binary->get_offset());
      break;
    }
    default:
      return;
    }
  }

  void compiler::compile(ast::let_declaration* p_let_decl)
  {
    const auto& str_ident = p_let_decl->get_identifier()->get_value();
    if(p_let_decl->get_value()->get_type() == ast::node_type::nt_identifier_expr &&
       ((ast::identifier_expression*)p_let_decl->get_value().get())->get_value() == str_ident)
    {
      compile_error(
          error::code::variable_in_own_initializer, "variable '{}' cant appear in its own initializer", str_ident);
      return;
    }
    compile((ast::node*)p_let_decl->get_value().get());
    auto offset = p_let_decl->get_offset();
    auto opt = declare_variable(str_ident, offset);
    if(opt.has_value())
    {
      auto res = opt.value();
      if(res.first)
      {
        current_chunk()->write(opcode::op_define_global_long, offset);
        const auto span = encode_int<size_t, 3>(res.second);
        current_chunk()->write(span, offset);
      }
      else
      {
        current_chunk()->write(opcode::op_define_global, offset);
        current_chunk()->write(res.second, offset);
      }
    }
  }

  void compiler::compile(ast::function_declaration* p_function_declaration)
  {
    auto& ident = p_function_declaration->get_identifier();
    if(ident == nullptr)
    {
      return; // TODO(Qais): warning declaration doesnt declare anything
    }
    const auto& str_name = ident->get_value();
    auto offset = ident->get_offset();
    auto opt = declare_variable(str_name, ident->get_offset());
    auto& params = p_function_declaration->get_parameters();
    // TODO(Qais): move this into own function
    push_function(
        {function_object::create<function_object>(params.size(), string_object::create<string_object>(str_name)),
         compile_function::type::function});
    scope_guard<compiler> guard{&compiler::being_scope, &compiler::end_scope, this};

    for(const auto& param : params)
    {
      auto opt = declare_variable(param->get_value(), param->get_offset());
      if(opt.has_value())
      {
        auto res = opt.value();
        if(res.first)
        {
          current_chunk()->write(opcode::op_define_global_long, offset);
          const auto span = encode_int<size_t, 3>(res.second);
          current_chunk()->write(span, offset);
        }
        else
        {
          current_chunk()->write(opcode::op_define_global, offset);
          current_chunk()->write(res.second, offset);
        }
      }
    }
    compile(p_function_declaration->get_body().get());
    current_chunk()->write(opcode::op_null, p_function_declaration->get_body()->get_offset());
    current_chunk()->write(opcode::op_return, p_function_declaration->get_body()->get_offset());
    auto fun = current_function();
    fun.function->arity = params.size();
    pop_function();
    current_chunk()->write_constant(value_t{fun.function}, ident->get_offset());
    if(opt.has_value())
    {
      auto res = opt.value();
      if(res.first)
      {
        current_chunk()->write(opcode::op_define_global_long, offset);
        const auto span = encode_int<size_t, 3>(res.second);
        current_chunk()->write(span, offset);
      }
      else
      {
        current_chunk()->write(opcode::op_define_global, offset);
        current_chunk()->write(res.second, offset);
      }
    }
  }

  void compiler::compile(ast::call_expression* p_call_expression)
  {
    compile(p_call_expression->get_callable().get());
    auto i = compile_arguments_list(p_call_expression->get_arguments());
    current_chunk()->write(opcode::op_call, p_call_expression->get_offset());
    current_chunk()->write(i, p_call_expression->get_offset());
  }

  void compiler::compile(ast::block_statement* p_block_stmt)
  {
    {
      scope_guard<compiler> guard{&compiler::being_scope, &compiler::end_scope, this};
      for(const auto& stmt : p_block_stmt->get_statement())
        compile(stmt.get());
    }
  }

  void compiler::compile(ast::if_statement* p_if_statement)
  {
    compile(p_if_statement->get_expression().get());
    auto offset = p_if_statement->get_offset();

    if(!m_loop_stack.empty())
    {
      m_loop_stack.back().pops_required++;
    }

    auto if_jump = emit_jump(opcode::op_conditional_jump, offset);
    compile(p_if_statement->get_consequence().get());
    auto else_jump = emit_jump(opcode::op_jump, offset);
    patch_jump(if_jump, current_chunk()->code.size());
    const auto& alt = p_if_statement->get_alternative();
    if(alt != nullptr)
    {
      compile(alt.get());
    }
    patch_jump(else_jump, current_chunk()->code.size());
    current_chunk()->write(opcode::op_pop, offset);
  }

  void compiler::compile(ast::while_statement* p_while_statement)
  {
    auto loop_start = current_chunk()->code.size();
    m_loop_stack.emplace_back();
    m_loop_stack.back().scope_depth = m_scope_depth;
    compile(p_while_statement->get_expression().get());
    auto offset = p_while_statement->get_offset();
    auto exit_jump = emit_jump(opcode::op_conditional_jump, offset);
    current_chunk()->write(opcode::op_pop, offset); // pop expression on first path
    auto body_start = current_chunk()->code.size();
    compile(p_while_statement->get_body().get());
    emit_loop(loop_start, offset);
    m_loop_stack.back().break_target = current_chunk()->code.size();
    patch_jump(exit_jump, current_chunk()->code.size());
    current_chunk()->write(opcode::op_pop, offset); // pop expression second path
    m_loop_stack.back().continue_target = loop_start;
    patch_loop_context();
    m_loop_stack.pop_back();
  }

  void compiler::compile(ast::for_statement* p_for_statement)
  {
    scope_guard<compiler> guard{&compiler::being_scope, &compiler::end_scope, this};
    {
      const auto& init = p_for_statement->get_initializer();
      if(init != nullptr)
        compile(init.get());
    }
    auto loop_start = current_chunk()->code.size();
    m_loop_stack.emplace_back();
    m_loop_stack.back().scope_depth = m_scope_depth;
    long long exit_jump = -1;
    auto offset = p_for_statement->get_offset();

    {
      const auto& cond = p_for_statement->get_condition();
      if(cond != nullptr)
      {
        compile(cond.get());
        exit_jump = emit_jump(opcode::op_conditional_jump, offset);
        current_chunk()->write(opcode::op_pop, offset);
      }
    }

    const auto& inc = p_for_statement->get_increment();
    // make sure correct jump gets written(for continue statement) so we dont patch instructions, cuz it feels wrong to
    // do so
    if(inc != nullptr)
      m_loop_stack.back().continue_forward = true;

    compile(p_for_statement->get_body().get());
    m_loop_stack.back().continue_target = loop_start;
    {
      if(inc != nullptr)
      {
        m_loop_stack.back().continue_target = current_chunk()->code.size();
        compile(inc.get());
        current_chunk()->write(opcode::op_pop, offset);
      }
    }
    emit_loop(loop_start, offset);
    m_loop_stack.back().break_target = current_chunk()->code.size();
    if(exit_jump != -1)
    {
      patch_jump(exit_jump, current_chunk()->code.size());
      current_chunk()->write(opcode::op_pop, offset);
    }
    patch_loop_context();
    m_loop_stack.pop_back();
  }

  void compiler::compile(ast::control_flow_statement* p_control_flow_statement)
  {
    ASSERT(false); // control flow statements arent supported yet!
    auto cft = p_control_flow_statement->get_control_flow_type();
    ASSERT(cft != ast::control_flow_statement::cftype::cf_invalid);
    if(m_loop_stack.empty())
    {
      compile_error(error::code::control_flow_outside_loop,
                    "invalid usage of control flow: '{}', outside of loop",
                    ast::control_flow_statement::control_flow_type_to_string(cft));
      return;
    }

    auto offset = p_control_flow_statement->get_offset();
    auto& loop_ctx = m_loop_stack.back();

    if(m_scope_depth > loop_ctx.scope_depth)
    {
      auto prev_scope_depth = m_scope_depth;
      m_scope_depth = loop_ctx.scope_depth;
      clean_scope_garbage();
      m_scope_depth = prev_scope_depth;
    }

    if(cft == ast::control_flow_statement::cftype::cf_break)
    {
      auto jump = emit_jump(opcode::op_jump, offset);
      loop_ctx.breaks.push_back(jump);
    }
    else
    {
      emit_pops(loop_ctx.pops_required);
      loop_ctx.pops_required = 0;
      auto jump = emit_jump(loop_ctx.continue_forward ? opcode::op_jump : opcode::op_loop, offset);
      loop_ctx.continues.push_back(jump);
    }
  }

  void compiler::compile(ast::return_statement* p_return_statement)
  {
    if(p_return_statement->get_expression() == nullptr)
    {
      current_chunk()->write(opcode::op_null, p_return_statement->get_offset());
      current_chunk()->write(opcode::op_return, p_return_statement->get_offset());
    }
    else
    {
      compile(p_return_statement->get_expression().get());
      current_chunk()->write(opcode::op_return, p_return_statement->get_offset());
    }
  }

  void compiler::compile(ast::identifier_expression* p_ident_expr)
  {
    const auto& str_val = p_ident_expr->get_value();
    auto offset = p_ident_expr->get_offset();
    auto opt = resolve_variable(str_val, offset);
    const auto op = variable_operation::vo_get;
    const auto tp = opt.first.has_value() ? variable_type::vt_global : variable_type::vt_local;
    const auto w = tp == variable_type::vt_global
                       ? opt.first.value().first ? variable_width::vw_long : variable_width::vw_short
                   : opt.second.value() > UINT8_MAX ? variable_width::vw_long
                                                    : variable_width::vw_short;
    const auto value = tp == variable_type::vt_global ? opt.first.value().second : opt.second.value();
    write_variable(op, tp, w, value, p_ident_expr->get_offset());
  }

  void compiler::compile(ast::assign_expression* p_assignment_expr)
  {
    compile(p_assignment_expr->get_right().get());
    const auto& str_val = p_assignment_expr->get_identifier();
    auto offset = p_assignment_expr->get_offset();
    auto opt = resolve_variable(str_val, offset);

    const auto op = variable_operation::vo_set;
    const auto tp = opt.first.has_value() ? variable_type::vt_global : variable_type::vt_local;
    const auto w = tp == variable_type::vt_global
                       ? opt.first.value().first ? variable_width::vw_long : variable_width::vw_short
                   : opt.second.value() > UINT8_MAX ? variable_width::vw_long
                                                    : variable_width::vw_short;
    const auto value = tp == variable_type::vt_global ? opt.first.value().second : opt.second.value();
    write_variable(op, tp, w, value, p_assignment_expr->get_offset());
  }

  void compiler::compile(ast::number_expression* p_number)
  {
    current_chunk()->write_constant(value_t{p_number->get_value()}, p_number->get_offset());
  }

  void compiler::compile(ast::string_expression* p_string)
  {
    const auto& src_str = p_string->to_string();
    std::vector<byte> dest;
    dest.reserve(src_str.size() + 1); // -1 remove size of one quote and the other is for the null terminator
    for(auto* c = src_str.c_str(); c > src_str.c_str() + src_str.size() + 1; c = utf8::advance(c))
    {
      // TODO(Qais): bruh for god sake wont you just write a function that does this, instead of hacking your way
      // through the validation function!

      auto ret = utf8::validate_codepoint((const uint8_t*)c, (const uint8_t*)src_str.c_str());
      // TODO(Qais): does that have to be in compiler, and just for strings, or is it better to support escape sequence
      // at lexer level? (btw this code looks like shit)
      if(ret.value() == 1) // ascii is all we need here
      {
        if(*c == '\\')
        {
          c = utf8::advance(c);
          switch(*c)
          {
          case '\\':
            dest.push_back('\\');
            break;
          case 'n':
            dest.push_back('\n');
            break;
          case 't':
            dest.push_back('\t');
            break;
          case 'r':
            dest.push_back('\r');
            break;
          default:
            // skip both
            break; // is this what its supposed to do when we escape something?
          }
        }
        else
        {
          // ascii as is
          dest.push_back(*c);
        }
      }
      else
      {
        // non ascii always as is
        dest.push_back(*c);
      }
    }
    dest.push_back('\0');
    current_chunk()->write_constant(value_t{p_string->get_value().c_str(), p_string->get_value().size()},
                                    p_string->get_offset());
  }

  void compiler::compile(ast::boolean_expression* p_boolean)
  {
    current_chunk()->write(p_boolean->get_value() ? opcode::op_true : opcode::op_false, p_boolean->get_offset());
  }

  void compiler::compile(ast::null_expression* p_null)
  {
    current_chunk()->write(opcode::op_null, p_null->get_offset());
  }

  void compiler::compile(ast::print_statement* p_print_stmt)
  {
    compile(p_print_stmt->get_expression().get());
    current_chunk()->write(opcode::op_print, p_print_stmt->get_offset());
  }

  void compiler::compile_logical_operator(ast::infix_binary_expression* p_logical_operator)
  {
    auto offset = p_logical_operator->get_offset();
    compile(p_logical_operator->get_left().get());
    auto jump_inst = p_logical_operator->get_operator() == operator_type::op_and ? opcode::op_conditional_jump
                                                                                 : opcode::op_conditional_truthy_jump;
    auto jump = emit_jump(jump_inst, offset);
    current_chunk()->write(opcode::op_pop, offset);
    compile(p_logical_operator->get_right().get());
    patch_jump(jump, current_chunk()->code.size());
  }

  uint8_t compiler::compile_arguments_list(const std::list<std::unique_ptr<ast::expression>>& p_list)
  {
    if(p_list.size() > UINT8_MAX)
    {
      compile_error(error::code::arguments_count_exceeds_limit,
                    "arguments count: {} exceeds limit which is: {}",
                    p_list.size(),
                    UINT8_MAX);
      return 0;
    }
    for(auto& arg : p_list)
      compile(arg.get());
    return p_list.size();
  }

  void compiler::push_function(compile_function p_function)
  {
    push_locals();
    m_functions.push_back(p_function);
    get_locals().push_back(local{"", 0});
  }

  void compiler::pop_function()
  {
    ASSERT(!m_functions.empty());
    m_functions.pop_back();
    pop_locals();
  }

  auto compiler::current_function() -> compile_function
  {
    ASSERT(!m_functions.empty());
    return m_functions.back();
  }

  void compiler::being_scope()
  {
    m_scope_depth++;
  }

  void compiler::end_scope()
  {
    m_scope_depth--;
    clean_scope_garbage();
  }

  // we separate end_scope, from clean_scope_garbage, because end_scope always decrements the scope_depth on compile
  // time even when executing a jump instruction while the clean up is runtime dependant and it might get skipped by the
  // jump instruction so we need to call it once again in other execution paths think of continue -> we compile the body
  // -> body on exit indeed calls end scope which decrements the scope_depth on compile time and it emits pop
  // instructions, then continue call skips those pop instructions so we try to compensate for that by calling end_scope
  // again in compile_control_flow_statement but that would result in another decrement to the m_scope_depth, and then
  // it will emit wrong pop instructions that will clean the target scope and yet another scope above it.
  void compiler::clean_scope_garbage()
  {
    uint32_t removed_count = 0;
    auto& curr_locals = get_locals();
    while(!curr_locals.empty() && curr_locals.back().depth > m_scope_depth)
    {
      removed_count++;
      curr_locals.pop_back();
    }
    emit_pops(removed_count);
  }

  void compiler::emit_pops(uint32_t p_count)
  {
    auto batches = p_count / UINT8_MAX;
    auto reminder = p_count % UINT8_MAX;
    for(auto i = 0; i < batches; ++i)
    {
      // TODO(Qais): fix offset, aint doing it now it requires either global state or forwarding
      current_chunk()->write(opcode::op_pop_n, 0);
      current_chunk()->write(UINT8_MAX, 0);
    }
    if(reminder > 0)
    {
      current_chunk()->write(opcode::op_pop_n, 0);
      current_chunk()->write(reminder, 0);
    }
  }

  std::optional<std::pair<bool, uint32_t>> compiler::declare_variable(const std::string& p_str_ident, size_t p_offset)
  {
    // local
    if(m_scope_depth > 0)
    {
      auto& curr_locals = get_locals();
      for(long i = curr_locals.size() - 1; i > -1; --i)
      {
        auto& loc = curr_locals[i];
        if(loc.depth < m_scope_depth)
          break;
        if(p_str_ident == loc.name)
        {
          compile_error(error::code::local_redefinition, "redefinition of '{}' in same scope", p_str_ident);
          return {};
        }
      }
      if(curr_locals.size() >= uint24_max)
        compile_error(
            error::code::local_count_exceeds_limit, "too many local variables, exceeds limit which is: {}", uint24_max);
      curr_locals.emplace_back(p_str_ident, m_scope_depth);
      return {};
    }
    // global
    return add_global(value_t{p_str_ident.c_str(), p_str_ident.size()}, p_offset);
  }

  std::pair<std::optional<std::pair<bool, uint32_t>>, std::optional<uint32_t>>
  compiler::resolve_variable(const std::string& p_str_ident, size_t p_offset)
  {
    auto& curr_locals = get_locals();
    for(long i = curr_locals.size() - 1; i > -1; i--)
    {
      auto& loc = curr_locals[i];
      if(p_str_ident == loc.name)
      {
        return {{}, i};
      }
    }

    // if not found local assume global
    return {get_or_add_global(value_t{p_str_ident.c_str(), p_str_ident.size()}, p_offset), {}};
  }

  void compiler::write_variable(
      variable_operation p_op, variable_type p_t, variable_width p_w, uint32_t p_value, size_t p_offset)
  {
    auto op_code = get_variable_opcode(p_op, p_t, p_w);
    if(p_w == variable_width::vw_short)
    {
      current_chunk()->write(op_code, p_offset);
      current_chunk()->write(p_value, p_offset);
    }
    else
    {
      current_chunk()->write(op_code, p_offset);
      const auto span = encode_int<size_t, 3>(p_value);
      current_chunk()->write(span, p_offset);
    }
  }

  std::pair<bool, uint32_t> compiler::get_or_add_global(value_t p_global, size_t p_offset)
  {
    ASSERT(p_global.type == value_type::object_val && p_global.as.obj->type == object_type::obj_string);
    auto* str_glob = (string_object*)p_global.as.obj;
    auto it = m_globals.find(str_glob);
    if(m_globals.end() == it)
    {
      // so we support late binding of globals, if this were strict get that could fail it will fail on first late bound
      // global
      return add_global(p_global, p_offset);
    }

    // TODO(Qais): this slow and started to be very messy, but idk this hack will fix identifiers not in the function
    // identifiers table for now
    for(const auto& g : current_function().function->associated_chunk.identifiers)
    {
      if((string_object*)g.as.obj == str_glob)
      {
        goto RET;
      }
    }
    add_global(p_global, p_offset);
  RET:
    return it->second;
  }

  std::pair<bool, uint32_t> compiler::add_global(value_t p_global, size_t p_offset)
  {
    ASSERT(p_global.type == value_type::object_val && p_global.as.obj->type == object_type::obj_string);
    auto* str_glob = (string_object*)p_global.as.obj;
    auto glob = current_chunk()->add_global(p_global, p_offset);
    m_globals[str_glob] = glob;
    if(glob.second >= uint24_max)
      compile_error(
          error::code::global_count_exceeds_limit, "too many global variables, exceeds limit which is: {}", uint24_max);
    return glob;
  }

  opcode compiler::get_variable_opcode(variable_operation p_op, variable_type p_t, variable_width p_w)
  {
    auto key = _make_variable_key(p_op, p_t, p_w);
    switch(key)
    {
    case _make_variable_key(variable_operation::vo_get, variable_type::vt_global, variable_width::vw_short):
      return opcode::op_get_global;
    case _make_variable_key(variable_operation::vo_get, variable_type::vt_global, variable_width::vw_long):
      return opcode::op_get_global_long;
    case _make_variable_key(variable_operation::vo_get, variable_type::vt_local, variable_width::vw_short):
      return opcode::op_get_local;
    case _make_variable_key(variable_operation::vo_get, variable_type::vt_local, variable_width::vw_long):
      return opcode::op_get_local_long;
    case _make_variable_key(variable_operation::vo_set, variable_type::vt_global, variable_width::vw_short):
      return opcode::op_set_global;
    case _make_variable_key(variable_operation::vo_set, variable_type::vt_global, variable_width::vw_long):
      return opcode::op_set_global_long;
    case _make_variable_key(variable_operation::vo_set, variable_type::vt_local, variable_width::vw_short):
      return opcode::op_set_local;
    case _make_variable_key(variable_operation::vo_set, variable_type::vt_local, variable_width::vw_long):
      return opcode::op_set_local_long;
    }
    return opcode::op_invalid;
  }

  void compiler::emit_loop(size_t p_loop_start, size_t p_offset)
  {
    constexpr auto OPERANDS_WIDTH = 3;
    current_chunk()->write(opcode::op_loop, p_offset);
    const auto current = current_chunk()->code.size();
    const auto loop_length = current - p_loop_start + OPERANDS_WIDTH;
    if(loop_length > uint24_max)
    {
      compile_error(error::code::jump_width_exceeds_limit,
                    "loop instruction with operand of: {}, exceeds max loop limit, which is: {}",
                    loop_length,
                    uint24_max);
    }
    current_chunk()->write(encode_int<uint32_t, OPERANDS_WIDTH>(loop_length), p_offset);
  }

  size_t compiler::emit_jump(opcode jump_instruction, size_t p_offset)
  {
    constexpr auto OPERANDS_WIDTH = 3;
    current_chunk()->write(jump_instruction, p_offset);
    current_chunk()->write(encode_int<uint32_t, OPERANDS_WIDTH>(0), p_offset);
    auto jump_start = current_chunk()->code.size() - OPERANDS_WIDTH;
    return jump_start;
  }

  void compiler::patch_jump(size_t start_position, size_t jump_position)
  {
    constexpr auto OPERANDS_WIDTH = 3;
    auto& code = current_chunk()->code;
    uint32_t jump = jump_position - start_position - OPERANDS_WIDTH;
    if(jump > uint24_max)
    {
      compile_error(error::code::jump_width_exceeds_limit,
                    "jump instruction with operand of: {}, exceeds max jump limit, which is: {}",
                    jump,
                    uint24_max);
    }

    current_chunk()->patch(encode_int<uint32_t, OPERANDS_WIDTH>(jump), start_position);
  }

  void compiler::patch_loop(size_t start_position, size_t loop_position)
  {
    constexpr auto OPERANDS_WIDTH = 3;
    auto& code = current_chunk()->code;
    const auto loop_length = start_position - loop_position + OPERANDS_WIDTH;
    if(loop_length > uint24_max)
    {
      compile_error(error::code::jump_width_exceeds_limit,
                    "loop instruction with operand of: {}, exceeds max loop limit, which is: {}",
                    loop_length,
                    uint24_max);
    }

    current_chunk()->patch(encode_int<uint32_t, OPERANDS_WIDTH>(loop_length), start_position);
  }

  void compiler::patch_loop_context()
  {
    ASSERT(!m_loop_stack.empty());
    auto ctx = m_loop_stack.back();
    auto chunk = current_chunk();
    for(auto b : ctx.breaks)
    {
      patch_jump(b, ctx.break_target);
    }
    for(auto c : ctx.continues)
    {
      if(ctx.continue_forward)
        patch_jump(c, ctx.continue_target);
      else
        patch_loop(c, ctx.continue_target);
    }
  }

  void compiler::errors::show() const
  {
    for(const auto& err : errs)
    {
      ERRORLN("compile error: {}: {}", error::code_to_string(err.error_code), err.message);
    }
  }

} // namespace ok