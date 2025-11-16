#ifndef OK_PARSER_HPP
#define OK_PARSER_HPP

#include "ast.hpp"
#include "operator.hpp"
#include "token.hpp"
#include "utility.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace ok
{
  struct precedence
  {
    enum prec
    {
      prec_lowest = 0,
      prec_assignment,  // x = 1
      prec_conditional, // y = x == null ? 1 : 2
      prec_or,          // false or true
      prec_and,         // true and false
      prec_bitwise_or,  // x | y
      prec_bitwise_xor, // x ^ y
      prec_bitwise_and, // x & y
      prec_equality,    // x == y
      prec_comparision, // x < y, x > y, x <= y, x >= y
      prec_shift,       // x << y, x >> y
      prec_as,
      prec_sum,       // 2 + 2
      prec_product,   // 3 * 3
      prec_exponent,  // 2 ** 6
      prec_prefix,    // !x
      prec_postfix,   // x++
      prec_call,      // x()
      prec_member,    // foo.bar
      prec_subscript, // x[]
    };
  };

  class parser
  {
  public:
    struct error
    {
      enum class code
      {
        no_prefix_parse_function,
        expected_token,
        expected_identifier,
        malformed_number,
        invalid_expression,
        expected_statement,
        expected_expression,
        unexpected_token,
        illegal_declaration_modifier,
        illegal_binding_modifier,
        illegal_operator_override,
      };
      code error_code;
      std::string message;

      static constexpr std::string_view code_to_string(code p_code)
      {
        switch(p_code)
        {
        case code::no_prefix_parse_function:
          return "no_prefix_parse_function";
        case code::expected_token:
          return "expected_token";
        case code::expected_identifier:
          return "expected_identifier";
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
    parser(token_array& p_token_array);
    std::unique_ptr<ast::program> parse_program();
    std::unique_ptr<ast::expression> parse_expression(int p_precedence = precedence::prec_lowest);
    ast::binding_modifier parse_binding_modifiers();

    template <typename... Args>
    void parse_error(error::code code, std::format_string<Args...> fmt, Args&&... args)
    {
      if(m_paranoia)
        return;

      m_paranoia = true;
      m_errors.errs.emplace_back(code, std::format(fmt, std::forward<Args>(args)...));
    }

    bool advance();
    bool expect_next(token_type p_type);

    token current_token() const;
    token lookahead_token() const;
    const errors& get_errors() const;

  private:
    int get_precedence(token_type p_type);
    std::unique_ptr<ast::statement> parse_statement();
    std::unique_ptr<ast::expression_statement> parse_expression_statement();
    std::unique_ptr<ast::statement> parse_declaration();
    std::unique_ptr<ast::print_statement> parse_print_statement();
    std::unique_ptr<ast::block_statement> parse_block_statement();
    std::unique_ptr<ast::if_statement> parse_if_statement();
    std::unique_ptr<ast::while_statement> parse_while_statement();
    std::unique_ptr<ast::for_statement> parse_for_statement();
    std::unique_ptr<ast::control_flow_statement> parse_control_flow_statement();
    std::unique_ptr<ast::return_statement> parse_return_statement();

    ast::class_declaration::method_declaration parse_operator_overload(ast::declaration_modifier p_dm);

    std::unique_ptr<ast::let_declaration> parse_let_declaration(ast::declaration_modifier p_modifiers);
    std::unique_ptr<ast::function_declaration> parse_function_declaration(ast::declaration_modifier p_modifiers);
    std::unique_ptr<ast::class_declaration> parse_class_declaration(ast::declaration_modifier p_modifiers);
    std::unique_ptr<ast::function_declaration> parse_function_declaration_impl(
        token p_trigger,
        ast::declaration_modifier p_modifiers,
        ast::binding_modifier p_allowed_binding_mods,
        std::string_view p_callit = "function",
        unique_overridable_operator_type p_allowed_overrides = unique_overridable_operator_type::uoot_none,
        unique_overridable_operator_type* p_out_op_type = nullptr);

    void sync_state();
    void munch_extra_semicolons();
    ast::declaration_modifier parse_declaration_modifiers();
    ast::declaration_modifier parse_declaration_modifier(token_type p_tok) const;
    bool is_declaration_modifier(token_type p_tok) const;

    ast::binding_modifier parse_binding_modifier(token_type p_tok) const;
    bool is_binding_modifier(token_type p_tok) const;

  private:
    token_array& m_token_array;
    size_t m_current_token = 0;
    size_t m_lookahead_token = 0;
    bool m_paranoia = false;
    errors m_errors;

    static constexpr ast::declaration_modifier let_declmods = ast::declaration_modifier::dm_global;
    static constexpr ast::binding_modifier let_bindmods = ast::binding_modifier::bm_mut;

    static constexpr ast::declaration_modifier function_declmods = ast::declaration_modifier::dm_global |
                                                                   ast::declaration_modifier::dm_export |
                                                                   ast::declaration_modifier::dm_async;
    static constexpr ast::binding_modifier function_bindmods = ast::binding_modifier::bm_mut;

    static constexpr ast::declaration_modifier class_declmods =
        ast::declaration_modifier::dm_global | ast::declaration_modifier::dm_export;
    static constexpr ast::binding_modifier class_bindmods = ast::binding_modifier::bm_mut;

    static constexpr ast::declaration_modifier method_declmods = ast::declaration_modifier::dm_static;

    static constexpr ast::declaration_modifier field_declmods = ast::declaration_modifier::dm_static;
    static constexpr ast::binding_modifier field_bindmods = ast::binding_modifier::bm_mut;

    static constexpr ast::binding_modifier loop_init_bindmods = ast::binding_modifier::bm_mut;
    static constexpr ast::binding_modifier function_param_bindmods = ast::binding_modifier::bm_mut;
  };
} // namespace ok

#endif // OK_PARSER_HPP