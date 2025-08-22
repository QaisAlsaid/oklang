#ifndef OK_AST_HPP
#define OK_AST_HPP

#include "token.hpp"
#include <list>
#include <memory>
#include <sstream>
#include <string>
namespace ok
{
  // TODO: statements
  enum class node_type
  {
    nt_node,
    nt_statement,
    nt_expression,
    nt_identifier_expr,
    nt_number_expr,
    nt_string_expr,
    nt_prefix_expr,
    nt_infix_binary_expr,
    nt_infix_unary_expr,
    nt_call_expr,
    nt_assign_expr,
    nt_operator_expr,
    nt_conditional_expr,
  };

  class node
  {
  public:
    virtual ~node() = default;
    virtual std::string token_literal() = 0;
    virtual std::string to_string() = 0;

    inline node_type get_type()
    {
      return m_type;
    }

  protected:
    node() = delete;
    node(node_type p_nt) : m_type(p_nt)
    {
    }

  private:
    node_type m_type = node_type::nt_node;
  };

  class expression : public node
  {
  public:
    expression() : node(node_type::nt_expression)
    {
    }
    expression(node_type p_nt) : node(p_nt)
    {
    }
  };

  class identifier_expression : public expression
  {
  public:
    identifier_expression() = default;
    identifier_expression(token p_tok, const std::string& p_value)
        : expression(node_type::nt_identifier_expr), m_token(p_tok), m_value(p_value)
    {
    }

    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      return m_value;
    }

  private:
    token m_token;
    std::string m_value;
  };

  class number_expression : public expression
  {
  public:
    number_expression(token p_tok, const double p_value)
        : expression(node_type::nt_number_expr), m_token(p_tok), m_value(p_value)
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      return m_token.raw_literal;
    }

  private:
    token m_token;
    double m_value;
  };

  class string_expression : public expression
  {
  public:
    string_expression(token p_tok, const std::string& p_value)
        : expression(node_type::nt_string_expr), m_token(p_tok), m_value(p_value)
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      return m_value;
    }

  public:
    token m_token;
    std::string m_value;
  };

  class prefix_expression : public expression
  {
  public:
    prefix_expression(token p_tok, const std::string& p_operator, std::unique_ptr<expression>& p_right)
        : expression(node_type::nt_prefix_expr), m_token(p_tok), m_operator(p_operator), m_right(std::move(p_right))
    {
    }
    std::string token_literal() override;
    std::string to_string() override;

  private:
    token m_token;
    std::string m_operator;
    std::unique_ptr<expression> m_right;
  };

  class infix_unary_expression : public expression
  {
  public:
    infix_unary_expression(token p_tok, const std::string& p_operator, std::unique_ptr<expression> p_left)
        : expression(node_type::nt_infix_unary_expr), m_token(p_tok), m_operator(p_operator), m_left(std::move(p_left))
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << m_operator << m_left->to_string() << ")";
      return ss.str();
    }

  private:
    token m_token;
    std::string m_operator;
    std::unique_ptr<expression> m_left;
  };

  class infix_binary_expression : public expression
  {
  public:
    infix_binary_expression(token p_tok,
                            const std::string& p_operator,
                            std::unique_ptr<expression> p_left,
                            std::unique_ptr<expression> p_right)
        : expression(node_type::nt_infix_binary_expr), m_token(p_tok), m_operator(p_operator),
          m_left(std::move(p_left)), m_right(std::move(p_right))
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << m_left->to_string() << " " << m_operator << " " << m_right->to_string() << ")";
      return ss.str();
    }

  private:
    token m_token;
    std::string m_operator;
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };

  class assign_expression : public expression
  {
  public:
    assign_expression(token p_tok, const std::string& p_ident, std::unique_ptr<expression> p_right)
        : expression(node_type::nt_assign_expr), m_token(p_tok), m_identifier(p_ident), m_right(std::move(p_right))
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_identifier << "=" << m_right->to_string();
      return ss.str();
    }

  private:
    token m_token;
    std::string m_identifier;
    std::unique_ptr<expression> m_right;
  };

  class call_expression : public expression
  {
  public:
    call_expression(token p_tok,
                    std::unique_ptr<expression> p_fun,
                    const std::list<std::unique_ptr<expression>>& p_args)
        : expression(node_type::nt_call_expr), m_token(p_tok), m_function(std::move(p_fun)), m_arguments(p_args)
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_function->to_string() << "(";
      for(auto i = 0; const auto& arg : m_arguments)
      {
        ss << arg->to_string();
        if(i < m_arguments.size() - 1)
          ss << ", ";
        ss << ")";
        i++;
      }
      return ss.str();
    }

  private:
    token m_token;
    std::unique_ptr<expression> m_function;
    std::list<std::unique_ptr<expression>> m_arguments;
  };

  class conditional_expression : public expression
  {
  public:
    conditional_expression(token p_token,
                           std::unique_ptr<expression> p_expr,
                           std::unique_ptr<expression> p_left,
                           std::unique_ptr<expression> p_right)
        : expression(node_type::nt_conditional_expr), m_token(p_token), m_expression(std::move(p_expr)),
          m_left(std::move(p_left)), m_right(std::move(p_right))
    {
    }
    std::string token_literal() override
    {
      return m_token.raw_literal;
    }
    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_expression << " ? " << m_left->to_string() << " : " << m_right->to_string();
      return ss.str();
    }

  private:
    token m_token;
    std::unique_ptr<expression> m_expression;
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };

  class operator_expression : public expression
  {
  public:
    operator_expression(token p_tok, std::unique_ptr<expression> p_left, std::unique_ptr<expression> p_right)
        : expression(node_type::nt_operator_expr), m_token(p_tok), m_left(std::move(p_left)),
          m_right(std::move(p_right))
    {
    }

    std::string token_literal() override
    {
      return m_token.raw_literal;
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_left->to_string() << m_token.raw_literal << m_right->to_string();
      return ss.str();
    }

  private:
    token m_token;
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };
} // namespace ok

#endif // OK_AST_HPP