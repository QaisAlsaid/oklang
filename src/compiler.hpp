#ifndef OK_COMPILER_HPP
#define OK_COMPILER_HPP

#include "ast.hpp"
#include "chunk.hpp"
#include <optional>

namespace ok
{
  struct local
  {
    std::string name;
    int depth;
  };
  class compiler
  {
  public:
    bool compile(const std::string_view p_src, chunk* p_chunk, uint32_t p_vm_id);
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
    void compile(ast::boolean_expression* p_boolean);
    void compile(ast::null_expression* p_null);
    void compile(ast::string_expression* p_string);
    void compile(ast::print_statement* p_print_stmt);
    void compile(ast::let_declaration* p_let_decl);
    void compile(ast::identifier_expression* p_ident_expr);
    void compile(ast::assign_expression* p_assignment_expr);
    void compile(ast::block_statement* p_block_stmt);

    const std::vector<local>& get_locals() const
    {
      return m_locals;
    }

  private:
    std::optional<std::pair<bool, uint32_t>> declare_variable(const std::string& str_ident, size_t offset);
    std::pair<std::optional<std::pair<bool, uint32_t>>, std::optional<uint32_t>>
    resolve_variable(const std::string& str_ident, size_t offset);
    void being_scope();
    void end_scope();

  private:
    chunk* m_current_chunk; // maybe temp
    uint32_t m_vm_id;
    std::vector<local> m_locals;
    size_t m_locals_count = 0;
    size_t m_scope_depth = 0;
  };
} // namespace ok

#endif // OK_COMPILER_HPP
