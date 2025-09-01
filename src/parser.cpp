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
    advance();
  }

  // TODO(Qais): you pice of shit of a developer, check where this function returns,
  // and by that i mean the current token state, and does it guarantee it on all execution paths?
  // ok update, i might've gone too harsh on myself, while its clear(from the behavior of parse_expression_statement)
  // that it skips all tokens that are related to the current expression, but still is that a guarantee or just an
  // assumption?
  // TODO(Qais): i think this off by one problem is a common problem in parsers. so if you really want to design this
  // in an actually bullet proof/clean way, you might want to introduce something like advancement policy and state
  // synchronizer or santi checker or something like that idk tbh
  std::unique_ptr<ast::expression> parser::parse_expression(int p_precedence)
  {
    auto tok = current_token();

    auto prefix_it = s_prefix_parse_map.find(tok.type);
    if(s_prefix_parse_map.end() == prefix_it)
    {
      // TODO(Qais): wtf?
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
      lookahead = lookahead_token();
    }
    return left;
  }

  std::unique_ptr<ast::program> parser::parse_program()
  {
    if(m_token_array.empty())
      return nullptr;
    auto tok = current_token();
    std::list<std::unique_ptr<ast::statement>> list;

    while(tok.type != token_type::tok_eof)
    {
      if(auto stmt = parse_declaration())
        list.push_back(std::move(stmt));
      advance();
      tok = current_token();
    }

    return std::make_unique<ast::program>(std::move(list));
  }

  std::unique_ptr<ast::statement> parser::parse_declaration()
  {
    std::unique_ptr<ast::statement> ret;
    switch(current_token().type)
    {
    case token_type::tok_let:
      ret = parse_let_declaration();
      break;
    default:
      ret = parse_statement();
      break;
    }
    if(m_paranoia)
      sync_state();
    return ret;
  }

  std::unique_ptr<ast::statement> parser::parse_statement()
  {
    switch(current_token().type)
    {
    case token_type::tok_print:
      return parse_print_statement();
    default:
      return parse_expression_statement();
    }
  }

  std::unique_ptr<ast::expression_statement> parser::parse_expression_statement()
  {
    auto trigger_tok = current_token();
    auto expr = parse_expression();
    auto expr_stmt = std::make_unique<ast::expression_statement>(trigger_tok, std::move(expr));
    if(lookahead_token().type != token_type::tok_semicolon)
      error({{}, "expected ';'"});
    advance();
    return expr_stmt;
  }

  std::unique_ptr<ast::print_statement> parser::parse_print_statement()
  {
    auto print_tok = current_token();
    advance();
    auto expr = parse_expression();
    if(lookahead_token().type != token_type::tok_semicolon)
      error({{}, "expected ';'"});
    advance();

    return std::make_unique<ast::print_statement>(print_tok, std::move(expr));
  }

  std::unique_ptr<ast::let_declaration> parser::parse_let_declaration()
  {
    // ugly ugly ugly
    auto let_tok = current_token();
    advance();
    std::unique_ptr<ast::assign_expression> assign;
    auto ident = parse_expression();
    if(ident->get_type() == ast::node_type::nt_assign_expr)
      assign = std::unique_ptr<ast::assign_expression>((ast::assign_expression*)ident.release());
    else if(ident->get_type() == ast::node_type::nt_identifier_expr)
    {
      auto ident_identifier = std::unique_ptr<ast::identifier_expression>((ast::identifier_expression*)ident.release());
      const auto& tok = ident_identifier->get_token();
      assign = std::make_unique<ast::assign_expression>(
          tok, ident_identifier->get_value(), std::make_unique<ast::null_expression>(tok));
    }
    else
      error({{}, "expected identifier"});

    // std::unique_ptr<ast::expression> expr;
    // advance();
    // if(current_token().type == token_type::tok_equal)
    // {
    //   expr = parse_expression();
    //   advance();
    // }
    advance();
    if(current_token().type != token_type::tok_semicolon)
      error({{}, "expected ';'"});
    return std::make_unique<ast::let_declaration>(
        let_tok,
        std::make_unique<ast::identifier_expression>(let_tok, assign->get_identifier()),
        std::move(assign->get_right()));
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
    if(m_paranoia)
      return;

    m_paranoia = true;
    m_errors.push_back(p_err);
  }

  void parser::sync_state()
  {
    m_paranoia = false;
    while(lookahead_token().type != token_type::tok_eof)
    {
      if(current_token().type == token_type::tok_semicolon)
        return;
      switch(current_token().type)
      {
      case token_type::tok_class:
      case token_type::tok_fun:
      case token_type::tok_let:
      case token_type::tok_letdown:
      case token_type::tok_for:
      case token_type::tok_if:
      case token_type::tok_while:
      case token_type::tok_print:
      case token_type::tok_return:
        return;
      default:;
      }
      advance();
    }
  }

  auto parser::get_errors() const -> const errors&
  {
    return m_errors;
  }

} // namespace ok