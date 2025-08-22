#ifndef OK_PARSERS_HPP
#define OK_PARSERS_HPP

#include "ast.hpp"
#include "parser.hpp"
#include "token.hpp"
#include <cstdlib>
#include <memory>

namespace ok
{
  struct prefix_parser_base
  {
    virtual ~prefix_parser_base() = default;
    virtual std::unique_ptr<expression> parse(parser& p_parser, token p_tok) const = 0;
  };

  struct identifier_parser : public prefix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<identifier_expression>(p_tok, p_tok.raw_literal);
    }
  };

  struct number_parser : public prefix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok) const override
    {
      auto c_str = p_tok.raw_literal.c_str();
      char* end = (char*)(c_str + p_tok.raw_literal.size());
      double ret = std::strtod(c_str, &end);
      if(ret == 0 && end == c_str) // error
      {
        // TODO(Qais): emit error
        return nullptr;
      }
      return std::make_unique<number_expression>(p_tok, ret);
    }
  };

  struct string_parser : public prefix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok) const override
    {
      return std::make_unique<string_expression>(p_tok, p_tok.raw_literal);
    }
  };

  struct prefix_parser : public prefix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok) const override
    {
      auto opperand = p_parser.parse_expression();
      return std::make_unique<prefix_expression>(p_tok, p_tok.raw_literal, opperand);
    }
  };

  struct infix_parser_base
  {
    virtual ~infix_parser_base() = default;
    virtual std::unique_ptr<expression>
    parse(parser& p_parser, token p_tok, std::unique_ptr<expression> p_left) const = 0;
  };

  struct infix_parser_unary : public infix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok, std::unique_ptr<expression> p_left) const override
    {
      return std::make_unique<infix_unary_expression>(p_tok, p_tok.raw_literal, std::move(p_left));
    }
  };

  struct infix_parser_binary : public infix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok, std::unique_ptr<expression> p_left) const override
    {
      return std::make_unique<infix_binary_expression>(
          p_tok, p_tok.raw_literal, std::move(p_left), p_parser.parse_expression());
    }
  };

  struct conditional_parser : public infix_parser_base
  {
    std::unique_ptr<expression> parse(parser& p_parser, token p_tok, std::unique_ptr<expression> p_left) const override
    {
      auto left = p_parser.parse_expression();
      // TODO(Qais): FIXME: consume if token is ':', in general better consumption would be nice!
      auto right = p_parser.parse_expression();
      return std::make_unique<conditional_expression>(p_tok, std::move(p_left), std::move(left), std::move(right));
    }
  };

} // namespace ok

#endif // OK_PARSERS_HPP