#ifndef OK_PARSER_HPP
#define OK_PARSER_HPP

#include "ast.hpp"
#include "token.hpp"
#include <memory>
#include <string>
#include <vector>
namespace ok
{
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
    parser(const token_array* p_token_array);
    std::unique_ptr<expression> parse_expression();
    void error(error_type p_err);
    bool pump_token();

  private:
    const token_array* m_token_array;
    size_t m_current_token = 0;
    size_t m_lookahead_token = 0;
    errors m_errors;
  };
} // namespace ok

#endif // OK_PARSER_HPP