#ifndef OK_PARSER_HPP
#define OK_PARSER_HPP

#include "ast.hpp"
#include "token.hpp"
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
      prec_assignment,
      prec_or,
      prec_and,
      prec_conditional,
      prec_equality,
      prec_comparision,
      prec_sum,
      prec_product,
      prec_exponent,
      prec_prefix,
      prec_infix,
      prec_call
    };
    // constexpr static const int lowest = 0, assignment = 1, p_and = 2, p_or = 3, conditional = 4, equality = 5,
    //                            comparision = 6, sum = 7, product = 8, exponent = 9, prefix = 10, infix = 11, call =
    //                            12;
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
    std::unique_ptr<ast::let_declaration> parse_let_declaration();
    std::unique_ptr<ast::block_statement> parse_block_statement();
    std::unique_ptr<ast::if_statement> parse_if_statement();
    std::unique_ptr<ast::while_statement> parse_while_statement();
    std::unique_ptr<ast::for_statement> parse_for_statement();

    void sync_state();
    void munch_extra_semicolons();

  private:
    token_array& m_token_array;
    size_t m_current_token = 0;
    size_t m_lookahead_token = 0;
    bool m_paranoia = false;
    errors m_errors;
  };
} // namespace ok

#endif // OK_PARSER_HPP