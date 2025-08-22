#include "parser.hpp"
#include "ast.hpp"
#include "parsers.hpp"
#include "token.hpp"
#include <memory>
#include <unordered_map>

namespace ok
{
  using prefix_parse_map = std::unordered_map<token_type, std::unique_ptr<prefix_parser_base>>;
  using infix_parse_map = std::unordered_map<token_type, std::unique_ptr<infix_parser_base>>;

  prefix_parse_map s_prefix_parse_map = {
      {token_type::tok_identifier, std::make_unique<identifier_parser>()},
      {token_type::tok_plus, std::make_unique<prefix_parser>()},
      {token_type::tok_minus, std::make_unique<prefix_parser>()},
      {token_type::tok_bang, std::make_unique<prefix_parser>()},
  };

  infix_parse_map s_infix_parse_map = {};

  parser::parser(const token_array* p_token_array)
  {
    if(!p_token_array || p_token_array->empty() || p_token_array->back().type != token_type::tok_eof)
      return;
  }

  std::unique_ptr<expression> parser::parse_expression()
  {
    if(!m_token_array)
      error({error_code{}, "invalid token array provided!"});
    const auto& real_token_array = *m_token_array;
    if(m_current_token++ >= real_token_array.size())
      return 0;
    m_lookahead_token = m_current_token;
    auto tok = real_token_array[m_current_token];

    auto prefix_it = s_prefix_parse_map.find(tok.type);
    if(s_prefix_parse_map.end() == prefix_it)
    {
      error({error_code{}, "failed to parse!"});
      return nullptr;
    }

    auto left = prefix_it->second->parse(*this, tok);

    if(m_lookahead_token++ >= real_token_array.size())
      return 0;
    tok = real_token_array[m_lookahead_token];
    const auto infix_it = s_infix_parse_map.find(tok.type);
    if(s_infix_parse_map.end() == infix_it)
      return left; // rvo does this magic i suppose!
    m_current_token = m_lookahead_token;
    return infix_it->second->parse(*this, tok, std::move(left));
  }

  void parser::error(error_type p_err)
  {
    m_errors.push_back(p_err);
  }

} // namespace ok