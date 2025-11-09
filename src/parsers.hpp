#ifndef OK_PARSERS_HPP
#define OK_PARSERS_HPP

#include "ast.hpp"
#include "operator.hpp"
#include "parser.hpp"
#include "token.hpp"
#include <cstdlib>
#include <list>
#include <memory>
#include <unordered_map>

// is the ood best fit here? or just plain parse functions will do?
namespace ok
{
  inline std::list<std::unique_ptr<ast::expression>> parse_expression_list(parser& p_parser, token expected_end)
  {
    std::list<std::unique_ptr<ast::expression>> args;
    while(p_parser.lookahead_token().type != expected_end.type &&
          p_parser.lookahead_token().type != token_type::tok_eof)
    {
      p_parser.advance();
      args.push_back(p_parser.parse_expression());
      if(p_parser.lookahead_token().type == token_type::tok_comma)
      {
        p_parser.advance();
      }
      else if(p_parser.lookahead_token().type != expected_end.type)
      {
        p_parser.parse_error(
            parser::error::code::expected_token, "expected '{}', after expression list", expected_end.raw_literal);
        break;
      }
    }
    if(p_parser.lookahead_token().type != expected_end.type) // account for eof
    {
      p_parser.parse_error(
          parser::error::code::expected_token, "expected '{}', after expression list", expected_end.raw_literal);
    }
    else // not strictleay necessary since advance on eof produces eof
    {
      p_parser.advance();
    }
    return std::move(args);
  }

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
        p_parser.parse_error(parser::error::code::malformed_number, "error converting: '{}', to a number", c_str);
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

  struct this_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<ast::this_expression>(p_tok);
    }
  };

  struct super_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      if(p_parser.lookahead_token().type != token_type::tok_dot)
      {
        p_parser.parse_error(parser::error::code::expected_token, "expected '.' after 'super'");
      }
      p_parser.advance();
      if(p_parser.lookahead_token().type != token_type::tok_identifier)
      {
        p_parser.parse_error(parser::error::code::expected_identifier, "expected superclass method name");
      }
      p_parser.advance();
      auto method_tok = p_parser.current_token();
      bool is_invoke = false;
      std::list<std::unique_ptr<ast::expression>> args;
      if(p_parser.lookahead_token().type == token_type::tok_left_paren)
      {
        p_parser.advance(); // skip to the paren
        args = parse_expression_list(p_parser, {token_type::tok_right_paren, "("});
        is_invoke = true;
      }
      return std::make_unique<ast::super_expression>(
          p_tok,
          std::make_unique<ast::identifier_expression>(method_tok, method_tok.raw_literal),
          std::move(args),
          is_invoke);
    }
  };

  struct array_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      auto elements = parse_expression_list(p_parser, {token_type::tok_right_bracket, "["});
      return std::make_unique<ast::array_expression>(p_tok, std::move(elements));
    }
  };

  struct map_parser : public prefix_parser_base
  {
    std::unique_ptr<ast::expression> parse(parser& p_parser, token p_tok) const override
    {
      std::unordered_map<std::unique_ptr<ast::expression>, std::unique_ptr<ast::expression>> entries;
      while(p_parser.lookahead_token().type != token_type::tok_right_brace &&
            p_parser.lookahead_token().type != token_type::tok_eof)
      {
        p_parser.advance();
        auto key = p_parser.parse_expression();
        if(p_parser.lookahead_token().type != token_type::tok_colon)
        {
          p_parser.parse_error(parser::error::code::expected_token, "expected ':', after map key");
        }
        p_parser.advance();
        p_parser.advance();
        auto value = p_parser.parse_expression();
        entries[std::move(key)] = std::move(value);
        if(p_parser.lookahead_token().type == token_type::tok_comma)
        {
          p_parser.advance();
        }
        else if(p_parser.lookahead_token().type != token_type::tok_right_brace)
        {
          p_parser.parse_error(parser::error::code::expected_token, "expected '}}', after map entries");
          break;
        }
      }
      if(p_parser.lookahead_token().type != token_type::tok_right_brace) // account for eof
      {
        p_parser.parse_error(parser::error::code::expected_token, "expected '}}', after map entries");
      }
      else // not strictleay necessary since advance on eof produces eof
      {
        p_parser.advance();
      }
      return std::make_unique<ast::map_expression>(p_tok, std::move(entries));
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
      auto opperand = p_parser.parse_expression(precedence::prec_prefix);
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

  struct access_parser : public infix_parser_base
  {
    access_parser(int p_precedence, bool p_is_right) : m_precedence(p_precedence), m_is_right(p_is_right)
    {
    }

    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      p_parser.advance();
      auto name_tok = p_parser.current_token();
      if(name_tok.type != token_type::tok_identifier)
      {
        p_parser.parse_error(parser::error::code::expected_identifier,
                             "expected property name after '.' access operator");
        return nullptr;
      }

      auto property_expr = std::make_unique<ast::identifier_expression>(name_tok, name_tok.raw_literal);
      std::unique_ptr<ast::expression> val_expr = nullptr;
      std::list<std::unique_ptr<ast::expression>> args = {};
      bool is_invoke = false;

      if(p_parser.lookahead_token().type == token_type::tok_assign)
      {
        p_parser.advance(); // skip identifier
        p_parser.advance(); // skip =
        val_expr = p_parser.parse_expression(precedence::prec_assignment - 1);
      }
      else if(p_parser.lookahead_token().type == token_type::tok_left_paren)
      {
        p_parser.advance(); // skip to the paren
        args = parse_expression_list(p_parser, {token_type::tok_right_paren, "("});
        is_invoke = true;
      }
      return std::make_unique<ast::access_expression>(
          name_tok, std::move(p_left), std::move(property_expr), std::move(val_expr), std::move(args), is_invoke);
    }

    int get_precedence() const override
    {
      return m_precedence;
    }

  private:
    int m_precedence;
    bool m_is_right;
  };

  struct subscript_parser : public infix_parser_base
  {
    subscript_parser(int p_precedence, bool p_is_right) : m_precedence(p_precedence), m_is_right(p_is_right)
    {
    }

    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      p_parser.advance();
      auto right = p_parser.parse_expression();
      if(p_parser.lookahead_token().type != token_type::tok_right_bracket)
      {
        p_parser.parse_error(parser::error::code::expected_token, "expected ']' after subscript operator expression");
      }
      p_parser.advance();
      return std::make_unique<ast::subscript_expression>(p_tok, std::move(p_left), std::move(right));
    }

    int get_precedence() const override
    {
      return precedence::prec_subscript;
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
      auto right = p_parser.parse_expression(precedence::prec_conditional - 1);
      return std::make_unique<ast::conditional_expression>(p_tok, std::move(p_left), std::move(left), std::move(right));
    }

    int get_precedence() const override
    {
      return precedence::prec_conditional;
    }
  };

  struct call_parser : public infix_parser_base
  {
    std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const override
    {
      auto args = parse_expression_list(p_parser, {token_type::tok_right_paren, "("});

      // return nullptr; // TODO(Qais): error handling for god sake!
      // this is wrong in cases of a(b)(c) this will allow cursed syntax like a(b);c to be parsed just fine lol
      // if(p_parser.current_token().type == token_type::tok_semicolon)
      //  p_parser.advance();
      return std::make_unique<ast::call_expression>(p_tok, std::move(p_left), std::move(args));
    }

    int get_precedence() const override
    {
      return precedence::prec_call;
    }
  };

  struct assign_parser : public infix_parser_base
  {
    virtual std::unique_ptr<ast::expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<ast::expression> p_left) const
    {
      p_parser.advance();
      const auto mods = p_parser.parse_binding_modifiers();
      auto right =
          p_parser.parse_expression(precedence::prec_assignment - 1); // always right associative, to allow a=b=c
      if(p_left->get_type() != ast::node_type::nt_identifier_expr)
        p_parser.parse_error(
            parser::error::code::expected_identifier, "expected identifier before: {}", right->token_literal());
      // return nullptr; // TODO(Qais): bitch, error handling aghhh!
      auto real_lhs = static_cast<ast::identifier_expression*>(p_left.get());
      return std::make_unique<ast::assign_expression>(
          ast::assign_expression(p_tok,
                                 std::make_unique<ast::binding>(real_lhs->get_token(), real_lhs->token_literal(), mods),
                                 std::move(right)));
    }

    virtual int get_precedence() const
    {
      return precedence::prec_assignment;
    }
  };
} // namespace ok

#endif // OK_PARSERS_HPP