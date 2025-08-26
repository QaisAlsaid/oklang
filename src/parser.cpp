#include "parser.hpp"
#include "ast.hpp"
#include "parsers.hpp"
#include "token.hpp"
#include <memory>
#include <print>
#include <unordered_map>

namespace ok
{
  using prefix_parse_map = std::unordered_map<token_type, std::unique_ptr<prefix_parser_base>>;
  using infix_parse_map = std::unordered_map<token_type, std::unique_ptr<infix_parser_base>>;

  static prefix_parse_map s_prefix_parse_map;
  static const auto _prefix_dummy = [] -> bool
  {
    s_prefix_parse_map.emplace(token_type::tok_identifier, std::make_unique<identifier_parser>());
    s_prefix_parse_map.emplace(token_type::tok_string, std::make_unique<string_parser>());
    s_prefix_parse_map.emplace(token_type::tok_number, std::make_unique<number_parser>());
    s_prefix_parse_map.emplace(token_type::tok_plus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_minus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_bang, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_left_paren, std::make_unique<group_parser>());
    s_prefix_parse_map.emplace(token_type::tok_true, std::make_unique<boolean_parser>());
    s_prefix_parse_map.emplace(token_type::tok_false, std::make_unique<boolean_parser>());
    s_prefix_parse_map.emplace(token_type::tok_null, std::make_unique<null_parser>());
    return true;
  }();

  static infix_parse_map s_infix_parse_map;

  static const auto infix_dummy = [] -> bool
  {
    s_infix_parse_map.emplace(token_type::tok_left_paren, std::make_unique<call_parser>());
    s_infix_parse_map.emplace(token_type::tok_plus, std::make_unique<infix_parser_binary>(precedence::sum, false));
    s_infix_parse_map.emplace(token_type::tok_minus, std::make_unique<infix_parser_binary>(precedence::sum, false));
    s_infix_parse_map.emplace(token_type::tok_asterisk,
                              std::make_unique<infix_parser_binary>(precedence::product, false));
    s_infix_parse_map.emplace(token_type::tok_slash, std::make_unique<infix_parser_binary>(precedence::product, false));
    s_infix_parse_map.emplace(token_type::tok_question, std::make_unique<conditional_parser>());
    s_infix_parse_map.emplace(token_type::tok_assign, std::make_unique<assign_parser>());
    s_infix_parse_map.emplace(token_type::tok_equal,
                              std::make_unique<infix_parser_binary>(precedence::equality, false));
    s_infix_parse_map.emplace(token_type::tok_bang_equal,
                              std::make_unique<infix_parser_binary>(precedence::equality, false));
    s_infix_parse_map.emplace(token_type::tok_less,
                              std::make_unique<infix_parser_binary>(precedence::comparision, false));
    s_infix_parse_map.emplace(token_type::tok_less_equal,
                              std::make_unique<infix_parser_binary>(precedence::comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater,
                              std::make_unique<infix_parser_binary>(precedence::comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater_equal,
                              std::make_unique<infix_parser_binary>(precedence::comparision, false));

    return true;
  }();

  parser::parser(token_array& p_token_array) : m_token_array(p_token_array)
  {
    // useless!
    // if(p_token_array.empty() || p_token_array.back().type != token_type::tok_eof)
    //  return;
    std::println("prefix_funs:");
    for(const auto& it : s_prefix_parse_map)
    {
      auto str = token_type_to_string(it.first);
      std::println("tp: {}, parser: {}", str, it.second != nullptr);
    }
    std::println("infix_funs:");
    for(const auto& it : s_infix_parse_map)
    {
      auto str = token_type_to_string(it.first);
      std::println("tp: {}, parser: {}", str, it.second != nullptr);
    }
    // m_current = 0, m_lookahead = 1;
    advance();
  }

  std::unique_ptr<ast::expression> parser::parse_expression(int p_precedence)
  {
    auto tok = current_token();

    auto prefix_it = s_prefix_parse_map.find(tok.type);
    if(s_prefix_parse_map.end() == prefix_it)
    {
      error({error_code{}, "failed to parse!"});
      return nullptr;
    }

    auto left = prefix_it->second->parse(*this, tok);

    auto lookahead = lookahead_token();
    while(lookahead.type != token_type::tok_semicolon && p_precedence < get_precedence(lookahead.type))
    {
      const auto infix_it = s_infix_parse_map.find(lookahead.type);
      if(s_infix_parse_map.end() == infix_it)
        return left;

      advance();
      tok = current_token();
      lookahead = lookahead_token();

      left = infix_it->second->parse(*this, tok, std::move(left));
      // tok = current_token();
      lookahead = lookahead_token(); // TODO(Qais): dumu this is horrible design please make it a global source!
    }
    return left;
  }

  std::unique_ptr<ast::program> parser::parse_program()
  {
    auto tok = current_token();
    std::list<std::unique_ptr<ast::statement>> list;

    while(tok.type != token_type::tok_eof)
    {
      if(auto stmt = parse_statement())
        list.push_back(std::move(stmt));
      advance();
      tok = current_token();
    }

    return std::make_unique<ast::program>(std::move(list));
  }

  std::unique_ptr<ast::statement> parser::parse_statement()
  {
    switch(current_token().type)
    {
    default:
      return parse_expression_statement();
    }
  }

  std::unique_ptr<ast::expression_statement> parser::parse_expression_statement()
  {
    auto expr = parse_expression();
    auto expr_stmt = std::make_unique<ast::expression_statement>(current_token(), std::move(expr));
    if(lookahead_token().type == token_type::tok_semicolon)
      advance();
    return expr_stmt;
  }

  bool parser::advance()
  {
    if(m_lookahead_token >= m_token_array.size() - 1)
    {
      m_current_token = m_lookahead_token;
      return false; // cant advance, no more tokens!
    }
    m_current_token = m_lookahead_token++;
    return true;
  }

  bool parser::expect_next(token_type p_type)
  {
    if(lookahead_token().type == p_type)
      return advance();

    // TODO(Qais): error unexpected token!
    return false;
  }

  // TODO(Qais): remove dups
  bool parser::advance_if_equals(token_type p_type)
  {
    if(current_token().type == p_type)
      return advance();
    return false;
  }

  int parser::get_precedence(token_type p_type)
  {
    auto infix_it = s_infix_parse_map.find(p_type);
    if(s_infix_parse_map.end() != infix_it)
      return infix_it->second->get_precedence();
    return precedence::lowest;
  }

  token parser::current_token() const
  {
    return m_token_array[m_current_token];
  }

  token parser::lookahead_token() const
  {
    return m_token_array[m_lookahead_token];
  }

  void parser::error(error_type p_err)
  {
    if(m_panic)
      return;

    m_panic = true;
    m_errors.push_back(p_err);
  }

  auto parser::get_errors() const -> const errors&
  {
    return m_errors;
  }

} // namespace ok