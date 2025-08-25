#ifndef OK_COMPILER_HPP
#define OK_COMPILER_HPP

#include "ast.hpp"
#include "chunk.hpp"
#include <type_traits>

namespace ok
{
  class compiler
  {
  public:
    bool compile(const std::string_view p_src, chunk* p_chunk);
    chunk* current_chunk();

    // take by pointer to avoid moving and invalidating, since we compile directly and dont hold any reference this is
    // totally fine.
    // just call compile with the ast node type it will auto dispatch at compile time. nice!
    // TODO(Qais): can this be done with parseres with a compile time data structure for the tokens or a wrapper classes
    // for each token type?!
    // or even can this be done for vm with typed byte code type shit
    void compile(ast::node* p_node);
    void compile(ast::program* p_program);
    void compile(ast::expression_statement* p_expr_stmt);
    void compile(ast::number_expression* p_number);
    void compile(ast::prefix_unary_expression* p_unary);
    void compile(ast::infix_binary_expression* p_binary);

  private:
    chunk* m_current_chunk; // maybe temp
  };
} // namespace ok

#endif // OK_COMPILER_HPP
