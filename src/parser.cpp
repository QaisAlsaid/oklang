#include "parser.hpp"
#include "ast.hpp"
#include "macros.hpp"
#include "parsers.hpp"
#include "token.hpp"
#include "vm_stack.hpp"
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
    s_infix_parse_map.emplace(token_type::tok_and, std::make_unique<infix_parser_binary>(precedence::prec_and, false));
    s_infix_parse_map.emplace(token_type::tok_or, std::make_unique<infix_parser_binary>(precedence::prec_or, false));
    s_infix_parse_map.emplace(token_type::tok_plus, std::make_unique<infix_parser_binary>(precedence::prec_sum, false));
    s_infix_parse_map.emplace(token_type::tok_minus,
                              std::make_unique<infix_parser_binary>(precedence::prec_sum, false));
    s_infix_parse_map.emplace(token_type::tok_asterisk,
                              std::make_unique<infix_parser_binary>(precedence::prec_product, false));
    s_infix_parse_map.emplace(token_type::tok_slash,
                              std::make_unique<infix_parser_binary>(precedence::prec_product, false));
    s_infix_parse_map.emplace(token_type::tok_question, std::make_unique<conditional_parser>());
    s_infix_parse_map.emplace(token_type::tok_assign, std::make_unique<assign_parser>());
    s_infix_parse_map.emplace(token_type::tok_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_equality, false));
    s_infix_parse_map.emplace(token_type::tok_bang_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_equality, false));
    s_infix_parse_map.emplace(token_type::tok_less,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_less_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
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
      parse_error(
          error::code::no_prefix_parse_function, "[{}] no prefix parser found for: {}", tok.offset, tok.raw_literal);
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
      // if(current_token().type != token_type::tok_semicolon)
      //   parse_error(error::code::expected_token, "expected ';', after: {}", ret->token_literal());
      // munch_extra_semicolons();
      break;
    case token_type::tok_fu:
      ret = parse_function_declaration();
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
    while(current_token().type == token_type::tok_semicolon)
      advance();
    switch(current_token().type)
    {
    case token_type::tok_print:
      return parse_print_statement();
    case token_type::tok_left_brace:
      return parse_block_statement();
    case token_type::tok_if:
      return parse_if_statement();
    case token_type::tok_while:
      return parse_while_statement();
    case token_type::tok_for:
      return parse_for_statement();
    case token_type::tok_break:
    case token_type::tok_continue:
      return parse_control_flow_statement();
    case token_type::tok_return:
      return parse_return_statement();
    case token_type::tok_eof:
      return nullptr;
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
      parse_error(error::code::expected_token, "expected ';', after: {}", expr_stmt->token_literal());
    // munch_extra_semicolons();

    advance();
    return expr_stmt;
  }

  std::unique_ptr<ast::print_statement> parser::parse_print_statement()
  {
    auto print_tok = current_token();
    advance();
    auto expr = parse_expression();
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", expr->token_literal());
    // munch_extra_semicolons();

    advance();

    return std::make_unique<ast::print_statement>(print_tok, std::move(expr));
  }

  std::unique_ptr<ast::control_flow_statement> parser::parse_control_flow_statement()
  {
    auto cf_tok = current_token();
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", cf_tok.raw_literal);
    advance();
    return std::make_unique<ast::control_flow_statement>(cf_tok);
  }

  std::unique_ptr<ast::return_statement> parser::parse_return_statement()
  {
    auto ret_tok = current_token();
    std::unique_ptr<ast::expression> expr = nullptr;
    if(lookahead_token().type != token_type::tok_semicolon)
    {
      advance();
      expr = parse_expression();
    }
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after return statement");
    advance();
    return std::make_unique<ast::return_statement>(ret_tok, std::move(expr));
  }

  std::unique_ptr<ast::block_statement> parser::parse_block_statement()
  {
    auto trigger_tok = current_token();
    std::list<std::unique_ptr<ast::statement>> statements;
    while(lookahead_token().type != token_type::tok_right_brace && lookahead_token().type != token_type::tok_eof)
    {
      advance();
      auto decl = parse_declaration();
      statements.push_back(std::move(decl));
    }
    advance();
    if(current_token().type != token_type::tok_right_brace)
      parse_error(error::code::expected_token, "expected '}}', after {}", statements.back()->token_literal());
    if(lookahead_token().type == token_type::tok_semicolon)
      advance(); // skip semicolon if present
    return std::make_unique<ast::block_statement>(trigger_tok, std::move(statements));
  }

  std::unique_ptr<ast::if_statement> parser::parse_if_statement()
  {
    auto if_token = current_token();
    advance();
    auto expr = parse_expression();
    if(!expr)
    {
      parse_error(error::code::invalid_expression, "expected expression after 'if'");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after expression: {}", expr->to_string());
      return nullptr;
    }
    advance(); // skip '->'
    auto cons = parse_statement();
    if(!cons)
    {
      parse_error(error::code::expected_statement, "expected statement after '{}'", "->");
      return nullptr;
    }
    std::unique_ptr<ast::statement> alt = nullptr;
    auto lat = lookahead_token().type;
    if(lat == token_type::tok_else)
    {
      advance();
      advance();
      if(current_token().type == token_type::tok_arrow)
        advance();
      alt = parse_statement();
      if(alt == nullptr)
      {
        parse_error(error::code::expected_statement, "expected statement after 'else'");
        return nullptr;
      }
    }
    return std::make_unique<ast::if_statement>(if_token, std::move(expr), std::move(cons), std::move(alt));
  }

  std::unique_ptr<ast::while_statement> parser::parse_while_statement()
  {
    auto while_token = current_token();
    advance();
    auto expr = parse_expression();
    if(!expr)
    {
      parse_error(error::code::invalid_expression, "expected expression after 'while'");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after expression: {}", expr->to_string());
      return nullptr;
    }
    advance(); // skip '->'
    auto body = parse_statement();
    if(!body)
    {
      parse_error(error::code::expected_statement, "expected statement after '{}'", "->");
      return nullptr;
    }
    return std::make_unique<ast::while_statement>(while_token, std::move(expr), std::move(body));
  }

  std::unique_ptr<ast::for_statement> parser::parse_for_statement()
  {
    auto for_token = current_token();
    std::unique_ptr<ast::statement> body;
    std::unique_ptr<ast::statement> init = nullptr;
    std::unique_ptr<ast::expression> cond = nullptr;
    std::unique_ptr<ast::expression> inc = nullptr;
    advance();
    if(current_token().type == token_type::tok_semicolon)
    {
      // stays nullptr
    }
    else if(current_token().type == token_type::tok_let)
    {
      init = parse_let_declaration(); // checks semicolon
    }
    else
    {
      init = parse_expression_statement(); // checks semicolon
    }
    advance();
    if(current_token().type != token_type::tok_semicolon) // there must be a condition
    {
      cond = parse_expression();
      if(cond == nullptr)
      {
        parse_error(error::code::expected_expression, "expected expression as 'for' condition");
        return nullptr;
      }
      advance();
    }
    if(current_token().type != token_type::tok_semicolon)
    {
      parse_error(error::code::expected_token, "expected ';' after condition");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow) // there must be an increment
    {
      inc = parse_expression();
      if(inc == nullptr)
      {
        parse_error(error::code::expected_expression, "expected expression as 'for' increment");
        return nullptr;
      }
      advance();
    }
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after for clauses");
      return nullptr;
    }
    advance();
    body = parse_statement();
    if(body == nullptr)
    {
      parse_error(error::code::expected_statement, "expected statement as 'for' body");
      return nullptr;
    }
    return std::make_unique<ast::for_statement>(
        for_token, std::move(body), std::move(init), std::move(cond), std::move(inc));
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
      parse_error(error::code::expected_identifier, "expected identifier before: {}", ident->token_literal());

    // std::unique_ptr<ast::expression> expr;
    // advance();
    // if(current_token().type == token_type::tok_equal)
    // {
    //   expr = parse_expression();
    //   advance();
    // }
    advance();
    //// move outside for compatibility with for initializer not requiring ';'
    if(current_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", assign->token_literal());
    munch_extra_semicolons();
    return std::make_unique<ast::let_declaration>(
        let_tok,
        std::make_unique<ast::identifier_expression>(let_tok, assign->get_identifier()),
        std::move(assign->get_right()));
  }

  std::unique_ptr<ast::function_declaration> parser::parse_function_declaration()
  {
    // fu do_stuff() -> {  }
    // let f = fu () -> {  }
    auto fu_token = current_token();
    advance();
    std::unique_ptr<ast::identifier_expression>
        ident; // TODO(Qais): take as optional parameter for function declared using let
    if(current_token().type == token_type::tok_identifier && ident == nullptr) // no identifier supplied
    {
      auto t = current_token();
      ident = std::make_unique<ast::identifier_expression>(t, t.raw_literal);
      advance();
    }
    else if(ident != nullptr)
    {
      // ident = ident;
    }
    // else // not in let context and no name provided parser doesnt care but it will be a compile error
    if(current_token().type != token_type::tok_left_paren)
      parse_error(error::code::expected_token, "expected '(' in function declaration");
    advance();
    std::list<std::unique_ptr<ast::identifier_expression>> params;
    if(current_token().type != token_type::tok_right_paren)
    {
      auto tok = current_token();
      params.push_back(std::make_unique<ast::identifier_expression>(tok, tok.raw_literal));
      while(lookahead_token().type == token_type::tok_comma)
      {
        advance();
        advance();
        auto tok = current_token();
        if(tok.type != token_type::tok_identifier)
        {
          parse_error(error::code::expected_identifier, "expected identifier as function parameter");
        }
        params.push_back(std::make_unique<ast::identifier_expression>(tok, tok.raw_literal));
      }
      // do
      // {
      //   auto tok = current_token();
      //   if(tok.type != token_type::tok_identifier)
      //   {
      //     parse_error(error::code::expected_identifier, "expected identifier as function parameter");
      //   }
      //   params.push_back(std::make_unique<ast::identifier_expression>(tok, tok.raw_literal));
      // } while(current_token().type == token_type::tok_comma && advance());
      advance();
    }

    if(current_token().type != token_type::tok_right_paren)
      parse_error(error::code::expected_token, "expected ')' after function parameters");
    advance();

    if(current_token().type != token_type::tok_arrow)
      parse_error(error::code::expected_token, "expected '->' in function declaration");
    advance();

    if(current_token().type != token_type::tok_left_brace)
      parse_error(error::code::expected_token, "expected '{{' before function body");

    auto body = parse_block_statement();

    return std::make_unique<ast::function_declaration>(fu_token, std::move(body), std::move(ident), std::move(params));
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

    parse_error(error::code::expected_token,
                "expected token of type: {}, at: {}",
                token_type_to_string(p_type),
                lookahead_token().offset);
    return false;
  }

  int parser::get_precedence(token_type p_type)
  {
    auto infix_it = s_infix_parse_map.find(p_type);
    if(s_infix_parse_map.end() != infix_it)
      return infix_it->second->get_precedence();
    return precedence::prec_lowest;
  }

  token parser::current_token() const
  {
    return m_token_array[m_current_token];
  }

  token parser::lookahead_token() const
  {
    return m_token_array[m_lookahead_token];
  }

  void parser::sync_state()
  {
    m_paranoia = false;
    while(lookahead_token().type != token_type::tok_eof)
    {
      if(current_token().type == token_type::tok_semicolon)
      {
        munch_extra_semicolons();
        return;
      }
      switch(current_token().type)
      {
      case token_type::tok_class:
      case token_type::tok_fu:
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

  void parser::munch_extra_semicolons()
  {
    // while(lookahead_token().type == token_type::tok_semicolon)
    //{
    //   advance();
    // }
  }

  void parser::errors::show() const
  {
    for(const auto& err : errs)
      ERRORLN("parse error: {}:{}", error::code_to_string(err.error_code), err.message);
  }

} // namespace ok