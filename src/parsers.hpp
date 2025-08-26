#ifndef OK_PARSERS_HPP
#define OK_PARSERS_HPP

#include "ast.hpp"
#include "operator.hpp"
#include "parser.hpp"
#include "token.hpp"
#include <cstdlib>
#include <list>
#include <memory>

// is the ood best fit here? or just plain parse functions will do?
namespace ok
{
  // TODO(Qais): since we are not using exceptions, then parse function should be of type std::expected<up<expr>, error>
  struct prefix_parser_base
  {
    prefix_parser_base() = default;
    virtual ~prefix_parser_base() = default;
    virtual std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const = 0;
  };

  struct identifier_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<ast::identifier_expression>(p_tok, p_tok.raw_literal);
    }
  };

  struct number_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      auto c_str = p_tok.raw_literal.c_str();
      auto end = (char*)(c_str + p_tok.raw_literal.size());
      double ret = std::strtod(c_str, &end);
      if(ret == 0 && end == c_str) // error
      {
        // TODO(Qais): emit error
        return nullptr;
      }
      return std::make_unique<ast::number_expression>(p_tok, ret);
    }
  };

  struct string_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<ast::string_expression>(p_tok, p_tok.raw_literal);
    }
  };

  struct boolean_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<ast::boolean_expression>(p_tok, p_tok.raw_literal == "true" ? true : false);
    }
  };

  struct null_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<ast::null_expression>(p_tok);
    }
  };

  struct group_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      p_parser.advance();
      auto expr = p_parser.parse_expression();
      return p_parser.expect_next(token_type::tok_right_paren) ? std::move(expr) : nullptr;
    }
  };

  struct prefix_unary_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      p_parser.advance();
      auto opperand = p_parser.parse_expression(precedence::prefix);
      return std::make_unique<ast::prefix_unary_expression>(
          p_tok, operator_type_from_string(p_tok.raw_literal), std::move(opperand));
    }
  };

  struct infix_parser_base
  {
    infix_parser_base() = default;
    virtual ~infix_parser_base() = default;
    virtual std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const = 0;
    virtual int get_precedence() const = 0;
  };

  struct infix_parser_unary : public infix_parser_base
  {
    infix_parser_unary(int p_precedence) : m_precedence(p_precedence)
    {
    }

    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      return std::make_unique<ast::infix_unary_expression>(
          p_tok, operator_type_from_string(p_tok.raw_literal), std::move(p_left));
    }

    int get_precedence() const override
    {
      return m_precedence;
    }

  private:
    int m_precedence;
  };

  struct infix_parser_binary : public infix_parser_base
  {
    infix_parser_binary(int p_precedence, bool p_is_right) : m_precedence(p_precedence), m_is_right(p_is_right)
    {
    }

    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      p_parser.advance();
      auto right = p_parser.parse_expression(m_precedence - (m_is_right ? 1 : 0));
      return std::make_unique<ast::infix_binary_expression>(
          p_tok, operator_type_from_string(p_tok.raw_literal), std::move(p_left), std::move(right));
    }

    int get_precedence() const override
    {
      return m_precedence;
    }

  private:
    int m_precedence;
    bool m_is_right;
  };

  struct conditional_parser : public infix_parser_base
  {
    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      p_parser.advance();
      auto left = p_parser.parse_expression();
      p_parser.expect_next(token_type::tok_colon);
      // skip colon
      p_parser.advance();
      // TODO(Qais): check!
      // p_parser.advance_if_equals(token_type::tok_colon);
      auto right = p_parser.parse_expression(precedence::conditional - 1);
      return std::make_unique<ast::conditional_expression>(p_tok, std::move(p_left), std::move(left), std::move(right));
    }

    int get_precedence() const override
    {
      return precedence::conditional;
    }
  };

  struct call_parser : public infix_parser_base
  {
    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      std::list<std::unique_ptr<ast::expression>> args;
      token_type expected_end;
      if(p_tok.type == token_type::tok_left_paren) // is this good enough!?, yes for now, later when you have something
                                                   // else that needs expr list parse abstract to another function
        expected_end = token_type::tok_right_paren;

      if(p_parser.lookahead_token().type == expected_end)
      {
        p_parser.advance();
        return std::make_unique<ast::call_expression>(p_tok, std::move(p_left), std::move(args));
      }

      p_parser.advance();
      args.push_back(p_parser.parse_expression());
      while(p_parser.lookahead_token().type == token_type::tok_comma)
      {
        // skip comma
        p_parser.advance();
        p_parser.advance();
        args.push_back(p_parser.parse_expression());
      }
      if(!p_parser.expect_next(expected_end))
        return nullptr; // TODO(Qais): error handling for god sake!
      // this is wrong in cases of a(b)(c) this will allow cursed syntax like a(b);c to be parsed just fine lol
      // if(p_parser.current_token().type == token_type::tok_semicolon)
      //  p_parser.advance();
      return std::make_unique<ast::call_expression>(p_tok, std::move(p_left), std::move(args));
    }

    int get_precedence() const override
    {
      return precedence::call;
    }
  };

  struct assign_parser : public infix_parser_base
  {
    virtual std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const
    {
      p_parser.advance();
      auto right = p_parser.parse_expression(precedence::assignment - 1); // always right associative, to allow a=b=c
      if(p_left->get_type() != ast::node_type::nt_identifier_expr)
        return nullptr; // TODO(Qais): bitch, error handling aghhh!
      auto real_lhs = static_cast<ast::identifier_expression*>(p_left.get());
      return std::make_unique<ast::assign_expression>(
          ast::assign_expression(p_tok, real_lhs->token_literal(), std::move(right)));
    }

    virtual int get_precedence() const
    {
      return precedence::assignment;
    }
  };

} // namespace ok

#endif // OK_PARSERS_HPP