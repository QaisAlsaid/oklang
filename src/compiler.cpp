#include "compiler.hpp"
#include "ast.hpp"
#include "chunk.hpp"
#include "debug.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "utf8.hpp"
#include "value.hpp"
#include <string>
#include <vector>

// TODO(Qais): bitch why dont you have compile errors!

namespace ok
{
  bool compiler::compile(const std::string_view p_src, chunk* p_chunk, uint32_t p_vm_id)
  {
    if(p_chunk == nullptr)
      return false;

    m_vm_id = p_vm_id;
    lexer lx;
    auto arr = lx.lex(p_src);
    parser prs{arr};
    auto root = prs.parse_program();
    auto errors = prs.get_errors();
    if(!errors.empty() || root == nullptr)
      return false; // TODO(Qais): parse error with info

    m_current_chunk = p_chunk;
    compile(root.get());
    debug::disassembler::disassemble_chunk(*m_current_chunk, "debug");
    return true;
  }

  chunk* compiler::current_chunk()
  {
    return m_current_chunk;
  }

  void compiler::compile(ast::node* p_node)
  {
    // static_assert(false, "runtime dispatch not supported currently");
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
    case ast::node_type::nt_node:
    case ast::node_type::nt_expression:
    case ast::node_type::nt_statement:
    default:
      return;
    }
  }

  void compiler::compile(ast::expression_statement* p_expr_stmt)
  {
    const auto& expr = p_expr_stmt->get_expression();
    compile(expr.get());
  }

  // void compiler::compile(ast::statement* p_stmt)
  // {
  //   switch(p_stmt->get_type())
  //   {
  //   case ast::node_type::nt_expression_statement_stmt:
  //     compile((ast::expression_statement*)p_stmt);
  //     return;
  //   case ast::node_type::nt_statement:
  //   default:
  //     return;
  //   }
  // }

  void compiler::compile(ast::program* p_program)
  {
    for(const auto& stmt : p_program->get_statements())
    {
      compile(stmt.get());
    }
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

  void compiler::compile(ast::prefix_unary_expression* p_unary)
  {
    compile(p_unary->get_right().get());
    switch(p_unary->get_operator())
    {
    case operator_type::plus:
      return; // TODO(Qais): vm supports this but not opcode!
    case operator_type::minus:
      current_chunk()->write(opcode::op_negate, p_unary->get_offset());
      return;
    case operator_type::bang:
      current_chunk()->write(opcode::op_not, p_unary->get_offset());
      return;
    default:
      return;
    }
  }

  void compiler::compile(ast::infix_binary_expression* p_binary)
  {
    compile(p_binary->get_left().get());
    compile(p_binary->get_right().get());

    const auto& op = p_binary->get_operator();
    auto current = current_chunk();

    switch(p_binary->get_operator())
    {
    case operator_type::plus:
      current->write(opcode::op_add, p_binary->get_offset());
      break;
    case operator_type::minus:
      current->write(opcode::op_subtract, p_binary->get_offset());
      break;
    case operator_type::asterisk:
      current->write(opcode::op_multiply, p_binary->get_offset());
      break;
    case operator_type::slash:
      current->write(opcode::op_divide, p_binary->get_offset());
      break;
    case operator_type::equal:
      current->write(opcode::op_equal, p_binary->get_offset());
      break;
    case operator_type::bang_equal:
      current->write(opcode::op_not_equal, p_binary->get_offset());
      break;
    case operator_type::less:
      current->write(opcode::op_less, p_binary->get_offset());
      break;
    case operator_type::greater:
      current->write(opcode::op_greater, p_binary->get_offset());
      break;
    case operator_type::less_equal:
    {
      // std::array<const byte, 2> inst = {to_utype(opcode::op_greater), to_utype(opcode::op_not)};
      current->write(opcode::op_less_equal, p_binary->get_offset());
      break;
    }
    case operator_type::greater_equal:
    {
      // std::array<const byte, 2> inst = {to_utype(opcode::op_less), to_utype(opcode::op_not)};
      current->write(opcode::op_greater_equal, p_binary->get_offset());
      break;
    }
    default:
      return;
    }
  }

  void compiler::compile(ast::boolean_expression* p_boolean)
  {
    current_chunk()->write(p_boolean->get_value() ? opcode::op_true : opcode::op_false, p_boolean->get_offset());
  }

  void compiler::compile(ast::null_expression* p_null)
  {
    current_chunk()->write(opcode::op_null, p_null->get_offset());
  }

  // void compiler::compile(ast::expression* p_expr)
  // {
  //   switch(p_expr->get_type())
  //   {
  //   case ast::node_type::nt_infix_binary_expr:
  //     compile((ast::infix_binary_expression*)(p_expr));
  //     return;
  //   case ast::node_type::nt_prefix_expr:
  //     compile((ast::prefix_unary_expression*)(p_expr));
  //     return;
  //   case ast::node_type::nt_number_expr:
  //     compile((ast::number_expression*)(p_expr));
  //     return;
  //   case ast::node_type::nt_expression:
  //   default:
  //     return;
  //   }
  // }

} // namespace ok