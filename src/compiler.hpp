#ifndef OK_COMPILER_HPP
#define OK_COMPILER_HPP

#include "ast.hpp"
#include "chunk.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "parser.hpp"
#include <cstdint>
#include <format>
#include <unordered_map>
#include <vector>

namespace ok
{
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
        control_flow_outside_loop,
        arguments_count_exceeds_limit,
        upvalue_count_exceeds_limit,
        invalid_usage_of_this_outside_of_class,
        invalid_usage_of_super_outside_of_class,
        invalid_return_from_non_returning_method,
        self_inheritance,
        invalid_usage_of_super_in_non_inherited_class,
      };
      code error_code;
      std::string message;

      static constexpr std::string_view code_to_string(code p_code)
      {
        switch(p_code)
        {
        case code::invalid_compilation_target:
          return "invalid_compilation_target";
        case code::local_redefinition:
          return "local_redefinition";
        case code::variable_in_own_initializer:
          return "variable_in_own_initializer";
        case code::local_count_exceeds_limit:
          return "local_count_exceeds_limit";
        case code::global_count_exceeds_limit:
          return "global_count_exceeds_limit";
        case code::jump_width_exceeds_limit:
          return "jump_width_exceeds_limit";
        case code::loop_width_exceeds_limit:
          return "loop_width_exceeds_limit";
        }
        return "unknown";
      }
    };
    struct errors
    {
      std::vector<error> errs;
      void show() const;
    };

    struct loop_context
    {
      // this starting to get ugly with those flags
      size_t scope_depth;
      size_t continue_target;
      size_t break_target;
      bool continue_forward{false};
      std::vector<size_t> breaks;
      std::vector<size_t> continues;
    };

    struct compile_function
    {
      enum class type
      {
        script,
        function,
        method,
        initializer,
        deinitializer
      };
      function_object* function;
      type function_type;
    };

    struct upvalue
    {
      uint32_t index; // 24 bit integer index, and 1 bit for is_local boolean
      inline uint32_t get_index() const
      {
        return index & 0x00ffffff;
      }

      inline void set_index(uint32_t p_index)
      {
        ASSERT(p_index <= uint24_max);
        index = (index & 0xff000000) | (p_index & 0x00ffffff);
      }

      inline bool is_local() const
      {
        return (index >> 24) & 0x00ffffff;
      }

      inline void set_local(bool p_is_local)
      {
        p_is_local ? index |= (1 << 24) : index &= ~(1 << 24);
      }
    };

  public:
    // type is always string the name will determine the script being ran and the future namespace also the main
    function_object* compile(const std::string_view p_src, string_object* p_function_name, uint32_t p_vm_id);
    chunk* current_chunk() const
    {
      ASSERT(m_compiled);
      ASSERT(!m_function_contexts.empty());
      return &m_function_contexts.back().function.function->associated_chunk;
    }

    // void push_locals()
    // {
    //   // m_locals.emplace_back();
    // }

    // void pop_locals()
    // {
    //   // m_locals.pop_back();
    //   get_locals().pop_back();
    // }

    std::vector<local>& get_locals()
    {
      ASSERT(m_compiled);
      // ASSERT(!m_locals.empty());
      // return m_locals.back();
      // return m_locals.back();
      return m_function_contexts.back().locals;
    }

    const std::vector<local>& get_locals() const
    {
      ASSERT(m_compiled);
      // ASSERT(!m_locals.empty());
      // return m_locals.back();
      // return m_locals.back();
      return m_function_contexts.back().locals;
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

    struct function_context
    {
      compile_function function;
      std::vector<local> locals;
      std::vector<upvalue> upvalues;
    };

    struct class_context
    {
      bool has_super = false;
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
    void compile(ast::this_expression* p_this);
    void compile(ast::super_expression* p_super);
    void compile(ast::string_expression* p_string);
    void compile(ast::print_statement* p_print_stmt);
    void compile(ast::let_declaration* p_let_decl);
    void compile(ast::identifier_expression* p_ident_expr);
    void compile(ast::assign_expression* p_assignment_expr);
    void compile(ast::block_statement* p_block_stmt);
    void compile(ast::if_statement* p_if_statement);
    void compile(ast::while_statement* p_while_statement);
    void compile(ast::for_statement* p_for_statement);
    void compile(ast::control_flow_statement* p_control_flow_statement);
    void compile(ast::function_declaration* p_function_declaration);
    void compile(ast::call_expression* p_call_expression);
    void compile(ast::return_statement* p_return_statement);
    void compile(ast::class_declaration* p_class_declaration);
    void compile(ast::access_expression* p_access_expression);

    void do_compile_function(ast::function_declaration* p_function_declaration, compile_function::type p_type);
    void push_function_context(compile_function p_function);
    void pop_function_context();
    compile_function current_function();
    function_context& current_context();

    void push_class_context(class_context p_ctx);
    void pop_class_context();
    class_context* current_class_context();

    // std::optional<std::pair<bool, uint32_t>> declare_variable(const std::string& str_ident, size_t offset);
    // std::pair<std::optional<std::pair<bool, uint32_t>>, std::optional<uint32_t>>
    // resolve_variable(const std::string& str_ident, size_t offset);
    uint32_t get_or_add_global(value_t p_global, size_t p_offset);
    uint32_t add_global(value_t p_global, size_t p_offset, uint32_t p_identifiers_table_index = UINT32_MAX);
    // void
    // write_variable(variable_operation p_op, variable_type p_t, variable_width p_w, uint32_t p_value, size_t
    // p_offset); opcode get_variable_opcode(variable_operation p_op, variable_type p_t, variable_width p_w);

    void write_variable(opcode p_op, uint32_t p_value, size_t p_offset);
    void named_variable(const std::string& str_ident, size_t offset, variable_operation op);
    void declare_variable(const std::string& str_ident, size_t offset);
    std::optional<uint32_t>
    declare_variable_late(const std::string& str_ident, size_t offset, uint32_t p_identifiers_table_index = UINT32_MAX);
    uint32_t resolve_local(const std::string& str_ident, size_t offset, const function_context& p_context);
    uint32_t resolve_upvalue(const std::string& str_ident, size_t offset, int64_t p_function_context_reverse_index);
    uint32_t add_upvalue(uint32_t p_local, bool p_is_local, size_t offset, function_context& p_context);

    void being_scope();
    void end_scope();
    void clean_scope_garbage();

    void pop_locals();
    void emit_pops(uint32_t p_count);

    void emit_return(size_t p_offset);
    void emit_loop(size_t p_loop_start, size_t p_offset);
    size_t emit_jump(opcode jump_instruction, size_t p_offset);
    void patch_jump(size_t start_position, size_t jump_position);
    void patch_loop(size_t start_position, size_t loop_position);

    void patch_loop_context();

    void compile_logical_operator(ast::infix_binary_expression* p_logical_operator);
    uint8_t compile_arguments_list(const std::list<std::unique_ptr<ast::expression>>& p_list);

    template <typename... Args>
    void compile_error(error::code code, std::format_string<Args...> fmt, Args&&... args)
    {
      m_errors.errs.emplace_back(code, std::format(fmt, std::forward<Args>(args)...));
    }

    static constexpr uint8_t _make_variable_key(variable_operation p_op, variable_type p_t, variable_width p_w)
    {
      return static_cast<uint8_t>(p_op) << 2 | static_cast<uint8_t>(p_t) << 1 | static_cast<uint8_t>(p_w);
    }

    static constexpr bool is_long(uint32_t p_index)
    {
      return !(p_index < op_constant_max_count + 1);
    }

  private:
    uint32_t m_vm_id;
    std::vector<function_context> m_function_contexts;
    std::vector<class_context> m_class_contexts;

    std::unordered_map<string_object*, uint32_t> m_globals;
    std::vector<loop_context> m_loop_stack;
    errors m_errors;
    parser::errors m_parse_errors;
    bool m_compiled = false;
    size_t m_locals_count = 0;
    size_t m_scope_depth = 0;

  private:
    friend class gc;
  };
} // namespace ok

#endif // OK_COMPILER_HPP
