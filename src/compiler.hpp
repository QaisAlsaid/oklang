#ifndef OK_COMPILER_HPP
#define OK_COMPILER_HPP

#include "ast.hpp"
#include "chunk.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "parser.hpp"
#include <format>
#include <optional>
#include <unordered_map>

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
    struct error
    {
      enum class code
      {
        invalid_compilation_target,
        local_redefinition,
        variable_in_own_initializer,
        local_count_exceeds_limit,
        global_count_exceeds_limit,
        jump_width_exceeds_limit,
        loop_width_exceeds_limit,
      };
      code error_code;
      std::string message;

      static constexpr std::string_view code_to_string(code p_code)
      {
        switch(p_code)
        {
        case code::local_redefinition:
          return "local_redefinition";
        case code::variable_in_own_initializer:
          return "variable_in_own_initializer";
        case code::local_count_exceeds_limit:
          return "local_count_exceeds_limit";
        }
        return "unknown";
      }
    };
    struct errors
    {
      std::vector<error> errs;
      void show() const;
    };

  public:
    bool compile(const std::string_view p_src, chunk* p_chunk, uint32_t p_vm_id);
    chunk* current_chunk()
    {
      ASSERT(m_compiled);
      return m_current_chunk;
    }

    const std::vector<local>& get_locals() const
    {
      ASSERT(m_compiled);
      return m_locals;
    }

    const errors& get_compile_errors() const
    {
      ASSERT(m_compiled);
      return m_errors;
    }

    const parser::errors& get_parse_errors() const
    {
      ASSERT(m_compiled);
      return m_parse_errors;
    }

  private:
    enum class variable_operation : bool
    {
      vo_set,
      vo_get
    };
    enum class variable_type : bool
    {
      vt_local,
      vt_global
    };
    enum class variable_width : bool
    {
      vw_long,
      vw_short
    };

  private:
    // take by pointer to avoid moving and invalidating, since we compile directly and dont hold any reference this is
    // totally fine.
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
    void compile(ast::if_statement* p_if_statement);
    void compile(ast::while_statement* p_while_statement);

    std::optional<std::pair<bool, uint32_t>> declare_variable(const std::string& str_ident, size_t offset);
    std::pair<std::optional<std::pair<bool, uint32_t>>, std::optional<uint32_t>>
    resolve_variable(const std::string& str_ident, size_t offset);
    std::pair<bool, uint32_t> get_or_add_global(value_t p_global, size_t p_offset);
    std::pair<bool, uint32_t> add_global(value_t p_global, size_t p_offset);
    void
    write_variable(variable_operation p_op, variable_type p_t, variable_width p_w, uint32_t p_value, size_t p_offset);
    opcode get_variable_opcode(variable_operation p_op, variable_type p_t, variable_width p_w);
    void being_scope();
    void end_scope();

    void emit_loop(size_t p_loop_start, size_t p_offset);
    size_t emit_jump(opcode jump_instruction, size_t p_offset);
    void patch_jump(size_t start_position);

    void compile_logical_operator(ast::infix_binary_expression* p_logical_operator);

    template <typename... Args>
    void compile_error(error::code code, std::format_string<Args...> fmt, Args&&... args)
    {
      m_errors.errs.emplace_back(code, std::format(fmt, std::forward<Args>(args)...));
    }

    static constexpr uint8_t _make_variable_key(variable_operation p_op, variable_type p_t, variable_width p_w)
    {
      return static_cast<uint8_t>(p_op) << 2 | static_cast<uint8_t>(p_t) << 1 | static_cast<uint8_t>(p_w);
    }

  private:
    chunk* m_current_chunk; // maybe temp
    uint32_t m_vm_id;
    std::vector<local> m_locals;
    std::unordered_map<string_object*, std::pair<bool, uint32_t>> m_globals;
    errors m_errors;
    parser::errors m_parse_errors;
    bool m_compiled = false;
    size_t m_locals_count = 0;
    size_t m_scope_depth = 0;
  };
} // namespace ok

#endif // OK_COMPILER_HPP
