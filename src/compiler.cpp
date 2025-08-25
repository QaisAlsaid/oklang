#include "compiler.hpp"
#include "ast.hpp"
#include "chunk.hpp"
#include "debug.hpp"
#include "lexer.hpp"
#include "parser.hpp"

namespace ok
{
  bool compiler::compile(const std::string_view p_src, chunk* p_chunk)
  {
    if(p_chunk == nullptr)
      return false;

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
    case ok::ast::node_type::nt_expression_statement_stmt:
      compile((ast::expression_statement*)p_node);
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
    current_chunk()->write_constant(p_number->get_value(), p_number->get_offset());
  }

  void compiler::compile(ast::prefix_unary_expression* p_unary)
  {
    compile(p_unary->get_right().get());
    // TODO(Qais): fix the hardcoded negate!
    current_chunk()->write(opcode::op_negate, p_unary->get_offset());
  }

  void compiler::compile(ast::infix_binary_expression* p_binary)
  {
    compile(p_binary->get_left().get());
    compile(p_binary->get_right().get());

    const auto& op = p_binary->get_operator();
    auto current = current_chunk();

    // TODO(Qais): i dont have to say anything atp
    if(op == "+")
      current->write(opcode::op_add, p_binary->get_offset());
    else if(op == "-")
      current->write(opcode::op_subtract, p_binary->get_offset());
    else if(op == "*")
      current->write(opcode::op_multiply, p_binary->get_offset());
    else if(op == "/")
      current->write(opcode::op_divide, p_binary->get_offset());
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