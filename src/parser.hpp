#ifndef OK_PARSER_HPP
#define OK_PARSER_HPP

#include "ast.hpp"
#include "token.hpp"
#include <memory>
#include <string>
#include <vector>
namespace ok
{
  struct precedence
  {
    constexpr static const int lowest = 0, assignment = 1, conditional = 2, equality = 3, comparision = 4, sum = 5,
                               product = 6, exponent = 7, prefix = 8, infix = 9, call = 10;
  };

  class parser
  {
  public:
    enum class error_code
    {
    };
    struct error_type
    {
      error_code code;
      std::string message;
    };
    using errors = std::vector<error_type>;

  public:
    parser(token_array& p_token_array);
    std::unique_ptr<ast::program> parse_program();
    std::unique_ptr<ast::expression> parse_expression(int p_precedence = precedence::lowest);
    void error(error_type p_err);

    bool advance();
    bool advance_if_equals(token_type p_type);
    bool expect_next(token_type p_type);

    token current_token() const;
    token lookahead_token() const;
    const errors& get_errors() const;

  private:
    int get_precedence(token_type p_type);
    std::unique_ptr<ast::statement> parse_statement();
    std::unique_ptr<ast::expression_statement> parse_expression_statement();

  private:
    token_array& m_token_array;
    size_t m_current_token = 0;
    size_t m_lookahead_token = 0;
    bool m_panic = false;
    errors m_errors;
  };
} // namespace ok

#endif // OK_PARSER_HPP