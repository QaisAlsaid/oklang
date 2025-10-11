#ifndef OK_AST_HPP
#define OK_AST_HPP

#include "operator.hpp"
#include "token.hpp"
#include <list>
#include <memory>
#include <sstream>
#include <string>

namespace ok::ast
{
  // TODO: statements
  enum class node_type
  {
    // base classes
    nt_node,
    nt_expression,
    nt_statement,
    nt_declaration, // technically a statement, but meh who cares

    // program root
    nt_program,

    // expressions
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
    nt_boolean_expr,
    nt_null_expr,
    nt_access_expr,
    nt_this_expr,
    nt_super_expr,

    // statements
    nt_expression_statement_stmt,
    nt_print_stmt,
    nt_block_stmt,
    nt_if_stmt,
    nt_while_stmt,
    nt_for_stmt,
    nt_control_flow_stmt,
    nt_return_stmt,
    // declarations
    nt_let_decl,
    nt_function_decl,
    nt_class_decl,
  };

  /**
   * base classes
   **/

  class node
  {
  public:
    virtual ~node() = default;
    virtual std::string to_string() = 0;

    inline node_type get_type() const
    {
      return m_type;
    }

    inline std::string token_literal() const
    {
      return m_token.raw_literal;
    }

    inline token get_token() const
    {
      return m_token;
    }

    inline size_t get_offset() const
    {
      return m_token.offset;
    }

    inline size_t get_line() const
    {
      return m_token.line;
    }

  protected:
    node() = delete;
    node(node_type p_nt, token p_tok) : m_type(p_nt), m_token(p_tok)
    {
    }

  protected:
    token m_token; // forgive the usage of m_ here
  private:
    node_type m_type = node_type::nt_node;
  };

  class expression : public node
  {
  public:
    expression(token p_tok) : node(node_type::nt_expression, p_tok)
    {
    }

    expression(node_type p_nt, token p_tok) : node(p_nt, p_tok)
    {
    }
  };

  class statement : public node
  {
  public:
    statement(token p_tok) : node(node_type::nt_statement, p_tok)
    {
    }

    statement(node_type p_nt, token p_tok) : node(p_nt, p_tok)
    {
    }
  };

  class declaration : public statement
  {
  public:
    declaration(token p_tok) : statement(node_type::nt_declaration, p_tok)
    {
    }

    declaration(node_type p_nt, token p_tok) : statement(p_nt, p_tok)
    {
    }
  };

  /**
   * program root
   **/

  class program : public node
  {
  public:
    program(std::list<std::unique_ptr<statement>>&& p_statements)
        : node(node_type::nt_program, p_statements.empty() ? token{} : p_statements.front()->get_token()),
          m_statements(std::move(p_statements))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      for(size_t ctr = 0; const auto& stmt : m_statements)
      {
        ss << stmt->to_string();
        if(m_statements.size() > 1 && ctr++ < m_statements.size() - 1)
          ss << " ";
      }
      return ss.str();
    }

    const std::list<std::unique_ptr<statement>>& get_statements() const
    {
      return m_statements;
    }

  private:
    std::list<std::unique_ptr<statement>> m_statements;
  };

  /**
   * expressions
   **/

  class identifier_expression : public expression
  {
  public:
    identifier_expression() = default;
    identifier_expression(token p_tok, const std::string& p_value)
        : expression(node_type::nt_identifier_expr, p_tok), m_value(p_value)
    {
    }

    // TODO(Qais): this isnt correct either make it for all classes or find another way
    const token& get_token() const
    {
      return m_token;
    }

    std::string to_string() override
    {
      return m_value;
    }

    const std::string& get_value() const
    {
      return m_value;
    }

  private:
    std::string m_value;
  };

  class number_expression : public expression
  {
  public:
    number_expression(token p_tok, const double p_value)
        : expression(node_type::nt_number_expr, p_tok), m_value(p_value)
    {
    }

    std::string to_string() override
    {
      return m_token.raw_literal;
    }

    double get_value() const
    {
      return m_value;
    }

  private:
    double m_value;
  };

  class string_expression : public expression
  {
  public:
    string_expression(token p_tok, const std::string& p_value)
        : expression(node_type::nt_string_expr, p_tok), m_value(p_value)
    {
    }

    std::string to_string() override
    {
      return m_value;
    }

    std::string& get_value()
    {
      return m_value;
    }

    const std::string& get_value() const
    {
      return m_value;
    }

  private:
    std::string m_value; // redundant copy!
  };

  class boolean_expression : public expression
  {
  public:
    boolean_expression(token p_tok, bool p_value) : expression(node_type::nt_boolean_expr, p_tok), m_value(p_value)
    {
    }

    std::string to_string() override
    {
      return m_token.raw_literal;
    }

    bool get_value() const
    {
      return m_value;
    }

  private:
    bool m_value;
  };

  class null_expression : public expression
  {
  public:
    null_expression(token p_tok) : expression(node_type::nt_null_expr, p_tok)
    {
    }

    std::string to_string() override
    {
      return "null";
    }
  };

  class prefix_unary_expression : public expression
  {
  public:
    prefix_unary_expression(token p_tok, const operator_type& p_operator, std::unique_ptr<expression> p_right)
        : expression(node_type::nt_prefix_expr, p_tok), m_operator(p_operator), m_right(std::move(p_right))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << operator_type_to_string(m_operator) << m_right->to_string() << ")";
      return ss.str();
    }

    inline const std::unique_ptr<expression>& get_right() const
    {
      return m_right;
    }

    inline operator_type get_operator() const
    {
      return m_operator;
    }

  private:
    operator_type m_operator;
    std::unique_ptr<expression> m_right;
  };

  class infix_unary_expression : public expression
  {
  public:
    infix_unary_expression(token p_tok, const operator_type p_operator, std::unique_ptr<expression> p_left)
        : expression(node_type::nt_infix_unary_expr, p_tok), m_operator(p_operator), m_left(std::move(p_left))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << operator_type_to_string(m_operator) << m_left->to_string() << ")";
      return ss.str();
    }

    inline operator_type get_operator() const
    {
      return m_operator;
    }

  private:
    operator_type m_operator;
    std::unique_ptr<expression> m_left;
  };

  class infix_binary_expression : public expression
  {
  public:
    infix_binary_expression(token p_tok,
                            const operator_type p_operator,
                            std::unique_ptr<expression> p_left,
                            std::unique_ptr<expression> p_right)
        : expression(node_type::nt_infix_binary_expr, p_tok), m_operator(p_operator), m_left(std::move(p_left)),
          m_right(std::move(p_right))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << m_left->to_string() << operator_type_to_string(m_operator) << m_right->to_string() << ")";
      return ss.str();
    }

    const std::unique_ptr<expression>& get_left() const
    {
      return m_left;
    }

    const std::unique_ptr<expression>& get_right() const
    {
      return m_right;
    }

    // TODO(Qais): operators not strings!
    operator_type get_operator() const
    {
      return m_operator;
    }

  private:
    operator_type m_operator;
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };

  // TODO(Qais): why is assign expr expecting left to be an identifier only!?
  class assign_expression : public expression
  {
  public:
    assign_expression(token p_tok, const std::string& p_ident, std::unique_ptr<expression> p_right)
        : expression(node_type::nt_assign_expr, p_tok), m_identifier(p_ident), m_right(std::move(p_right))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << m_identifier << "=" << m_right->to_string() << ")";
      return ss.str();
    }

    const std::string& get_identifier() const
    {
      return m_identifier;
    }

    std::unique_ptr<expression>& get_right()
    {
      return m_right;
    }

  private:
    std::string m_identifier;
    std::unique_ptr<expression> m_right;
  };

  class call_expression : public expression
  {
  public:
    call_expression(token p_tok, std::unique_ptr<expression> p_fun, std::list<std::unique_ptr<expression>>&& p_args)
        : expression(node_type::nt_call_expr, p_tok), m_function(std::move(p_fun)), m_arguments(std::move(p_args))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_function->to_string() << "(";
      for(size_t ctr = 0; const auto& arg : m_arguments)
      {
        ss << arg->to_string();
        if(m_arguments.size() > 1 && ctr++ < m_arguments.size() - 1)
          ss << ", ";
      }
      ss << ")";
      return ss.str();
    }

    const std::unique_ptr<expression>& get_callable() const
    {
      return m_function;
    }

    const std::list<std::unique_ptr<expression>>& get_arguments() const
    {
      return m_arguments;
    }

  private:
    std::unique_ptr<expression> m_function;
    std::list<std::unique_ptr<expression>> m_arguments;
  };

  class conditional_expression : public expression
  {
  public:
    conditional_expression(token p_tok,
                           std::unique_ptr<expression> p_expr,
                           std::unique_ptr<expression> p_left,
                           std::unique_ptr<expression> p_right)
        : expression(node_type::nt_conditional_expr, p_tok), m_expression(std::move(p_expr)), m_left(std::move(p_left)),
          m_right(std::move(p_right))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "(" << m_expression->to_string() << "?" << m_left->to_string() << ":" << m_right->to_string() << ")";
      return ss.str();
    }

  private:
    std::unique_ptr<expression> m_expression;
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };

  class operator_expression : public expression
  {
  public:
    operator_expression(token p_tok, std::unique_ptr<expression> p_left, std::unique_ptr<expression> p_right)
        : expression(node_type::nt_operator_expr, p_tok), m_left(std::move(p_left)), m_right(std::move(p_right))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_left->to_string() << m_token.raw_literal << m_right->to_string();
      return ss.str();
    }

  private:
    std::unique_ptr<expression> m_left;
    std::unique_ptr<expression> m_right;
  };

  class access_expression : public expression
  {
  public:
    access_expression(token p_tok,
                      std::unique_ptr<expression> p_target,
                      std::unique_ptr<identifier_expression> p_property,
                      std::unique_ptr<expression> p_value = nullptr,
                      std::list<std::unique_ptr<expression>>&& p_args = {},
                      bool p_is_invoke = false)
        : expression(node_type::nt_access_expr, p_tok), m_target(std::move(p_target)),
          m_property(std::move(p_property)), m_value(std::move(p_value)), m_arguments(std::move(p_args)),
          m_is_invoke(p_is_invoke)
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_target->to_string() << "." << m_property->to_string();
      if(m_value)
      {
        ss << " = " << m_value->to_string();
      }
      return ss.str();
    }

    const std::unique_ptr<expression>& get_target() const
    {
      return m_target;
    }

    const std::unique_ptr<identifier_expression>& get_property() const
    {
      return m_property;
    }

    const std::unique_ptr<expression>& get_value() const
    {
      return m_value;
    }

    const bool is_invoke() const
    {
      return m_is_invoke;
    }

    const std::list<std::unique_ptr<expression>>& get_arguments_list() const
    {
      return m_arguments;
    }

  private:
    std::unique_ptr<expression> m_target;
    std::unique_ptr<identifier_expression> m_property;
    std::unique_ptr<expression> m_value;
    std::list<std::unique_ptr<expression>> m_arguments;
    bool m_is_invoke = false;
  };

  class this_expression : public expression
  {
  public:
    this_expression(token p_tok) : expression(node_type::nt_this_expr, p_tok)
    {
    }

    std::string to_string() override
    {
      return m_token.raw_literal;
    }
  };

  class super_expression : public expression
  {
  public:
    super_expression(token p_tok,
                     std::unique_ptr<identifier_expression> p_method,
                     std::list<std::unique_ptr<expression>>&& p_args = {},
                     bool p_is_invoke = false)
        : expression(node_type::nt_super_expr, p_tok), m_method(std::move(p_method)), m_arguments(std::move(p_args)),
          m_is_invoke(p_is_invoke)
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_token.raw_literal << "." << m_method->token_literal();
      return ss.str();
    }

    const std::unique_ptr<identifier_expression>& get_method() const
    {
      return m_method;
    }

    const std::list<std::unique_ptr<expression>>& get_arguments() const
    {
      return m_arguments;
    }

    bool is_invoke() const
    {
      return m_is_invoke;
    }

  private:
    std::unique_ptr<identifier_expression> m_method;
    std::list<std::unique_ptr<expression>> m_arguments;
    bool m_is_invoke = false;
  };

  /**
   * statements
   **/

  class expression_statement : public statement
  {
  public:
    expression_statement(token p_tok, std::unique_ptr<expression> p_expr)
        : statement(node_type::nt_expression_statement_stmt, p_tok), m_expression(std::move(p_expr))
    {
    }

    std::string to_string() override
    {
      return m_expression == nullptr ? "" : m_expression->to_string();
    }

    const std::unique_ptr<expression>& get_expression() const
    {
      return m_expression;
    }

  private:
    std::unique_ptr<expression> m_expression;
  };

  class print_statement : public statement
  {
  public:
    print_statement(token p_tok, std::unique_ptr<expression> p_expr)
        : statement(node_type::nt_print_stmt, p_tok), m_expression(std::move(p_expr))
    {
    }

    std::string to_string() override
    {
      // ugly
      return "print" + (m_expression == nullptr ? std::string("null") : m_expression->to_string());
    }

    const std::unique_ptr<expression>& get_expression() const
    {
      return m_expression;
    }

  private:
    std::unique_ptr<expression> m_expression;
  };

  class block_statement : public statement
  {
  public:
    block_statement(token p_tok, std::list<std::unique_ptr<statement>>&& p_statements)
        : statement(node_type::nt_block_stmt, p_tok), m_statements(std::move(p_statements))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "{";
      for(auto ctr = 0; const auto& stmt : m_statements)
      {
        ss << stmt->to_string();
        if(m_statements.size() > 1 && ctr++ < m_statements.size() - 1)
          ss << "\n";
      }
      ss << "}";
      return ss.str();
    }

    const std::list<std::unique_ptr<statement>>& get_statement() const
    {
      return m_statements;
    }

  private:
    std::list<std::unique_ptr<statement>> m_statements;
  };

  class if_statement : public statement
  {
  public:
    if_statement(token p_tok,
                 std::unique_ptr<expression> p_expr,
                 std::unique_ptr<statement> p_consequences,
                 std::unique_ptr<statement> p_alternative)
        : statement(node_type::nt_if_stmt, p_tok), m_expression(std::move(p_expr)),
          m_consequence(std::move(p_consequences)), m_alternative(std::move(p_alternative))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "if " << m_expression->to_string() << " -> ";
      ss << m_consequence->to_string();
      if(m_alternative != nullptr)
        ss << " else -> " << m_alternative->to_string();
      return ss.str();
    }

    const std::unique_ptr<expression>& get_expression() const
    {
      return m_expression;
    }

    const std::unique_ptr<statement>& get_consequence() const
    {
      return m_consequence;
    }

    const std::unique_ptr<statement>& get_alternative() const
    {
      return m_alternative;
    }

  private:
    std::unique_ptr<expression> m_expression;
    std::unique_ptr<statement> m_consequence;
    std::unique_ptr<statement> m_alternative;
  };

  class while_statement : public statement
  {
  public:
    while_statement(token p_tok, std::unique_ptr<expression> p_expr, std::unique_ptr<statement> p_body)
        : statement(node_type::nt_while_stmt, p_tok), m_expression(std::move(p_expr)), m_body(std::move(p_body))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "while " << m_expression->to_string() << " -> ";
      ss << m_body->to_string();
      return ss.str();
    }

    const std::unique_ptr<expression>& get_expression() const
    {
      return m_expression;
    }

    const std::unique_ptr<statement>& get_body() const
    {
      return m_body;
    }

  private:
    std::unique_ptr<expression> m_expression;
    std::unique_ptr<statement> m_body;
  };

  class for_statement : public statement
  {
  public:
    for_statement(token p_tok,
                  std::unique_ptr<statement> p_body,
                  std::unique_ptr<statement> p_initializer = nullptr,
                  std::unique_ptr<expression> p_condition = nullptr,
                  std::unique_ptr<expression> p_increment = nullptr)
        : statement(node_type::nt_for_stmt, p_tok), m_body(std::move(p_body)), m_initializer(std::move(p_initializer)),
          m_condition(std::move(p_condition)), m_increment(std::move(p_increment))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "for " << (m_initializer != nullptr ? m_initializer->to_string() : "") << " ; ";
      ss << (m_condition != nullptr ? m_condition->to_string() : "") << " ; ";
      ss << (m_increment != nullptr ? m_increment->to_string() : "");
      ss << "-> " << m_body->to_string();
      return ss.str();
    }

    const std::unique_ptr<statement>& get_body() const
    {
      return m_body;
    }

    const std::unique_ptr<statement>& get_initializer() const
    {
      return m_initializer;
    }

    const std::unique_ptr<expression>& get_condition() const
    {
      return m_condition;
    }

    const std::unique_ptr<expression>& get_increment() const
    {
      return m_increment;
    }

  private:
    std::unique_ptr<statement> m_body;
    std::unique_ptr<statement> m_initializer;
    std::unique_ptr<expression> m_condition;
    std::unique_ptr<expression> m_increment;
  };

  class control_flow_statement : public statement
  {
  public:
    enum class cftype
    {
      cf_invalid,
      cf_break,
      cf_continue
    };

    static constexpr std::string_view control_flow_type_to_string(cftype cft)
    {
      using namespace std::string_view_literals;
      switch(cft)
      {
      case cftype::cf_break:
        return "break"sv;
      case cftype::cf_continue:
        return "continue"sv;
      default:
        return "invalid"sv;
      }
    }

  public:
    control_flow_statement(token p_tok) : statement(node_type::nt_control_flow_stmt, p_tok)
    {
      switch(p_tok.type)
      {
      case token_type::tok_break:
        m_cf_type = cftype::cf_break;
        break;
      case token_type::tok_continue:
        m_cf_type = cftype::cf_continue;
        break;
      default:
        m_cf_type = cftype::cf_invalid;
        break;
      }
    }

    std::string to_string() override
    {
      return m_token.raw_literal;
    }

    cftype get_control_flow_type() const
    {
      return m_cf_type;
    }

  private:
    cftype m_cf_type;
  };

  class return_statement : public statement
  {
  public:
    return_statement(token p_tok, std::unique_ptr<expression> p_expr = nullptr)
        : statement(node_type::nt_return_stmt, p_tok), m_expression(std::move(p_expr))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << m_token.raw_literal << "  " << (m_expression == nullptr ? m_expression->to_string() : "null");
      return ss.str();
    }

    const std::unique_ptr<expression>& get_expression() const
    {
      return m_expression;
    }

  private:
    std::unique_ptr<expression> m_expression;
  };

  /**
   * declarations
   **/

  class let_declaration : public declaration
  {
  public:
    let_declaration(token p_tok, std::unique_ptr<identifier_expression> p_ident, std::unique_ptr<expression> p_value)
        : declaration(node_type::nt_let_decl, p_tok), m_identifier(std::move(p_ident)), m_value(std::move(p_value))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "let " + m_identifier->to_string() << " " << m_value->to_string();
      return ss.str();
    }

    const std::unique_ptr<expression>& get_value() const
    {
      return m_value;
    }

    const std::unique_ptr<identifier_expression>& get_identifier() const
    {
      return m_identifier;
    }

  private:
    std::unique_ptr<identifier_expression> m_identifier;
    std::unique_ptr<expression> m_value;
  };

  class function_declaration : public declaration
  {
  public:
    function_declaration(token p_tok,
                         std::unique_ptr<statement> p_body,
                         std::unique_ptr<identifier_expression> p_identifier = nullptr,
                         std::list<std::unique_ptr<identifier_expression>>&& p_parameatres = {})
        : declaration(node_type::nt_function_decl, p_tok), m_identifier(std::move(p_identifier)),
          m_parameters(std::move(p_parameatres)), m_body(std::move(p_body))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "fu ";
      ss << (m_identifier == nullptr ? "" : m_identifier->to_string());
      ss << "(";
      for(auto i = 0; const auto& p : m_parameters)
      {
        ss << p->to_string();
        if(i < m_parameters.size() - 1)
          ss << ", ";
      }
      ss << ")";
      ss << m_body->to_string();
      return ss.str();
    }

    const std::unique_ptr<identifier_expression>& get_identifier() const
    {
      return m_identifier;
    }

    const std::list<std::unique_ptr<identifier_expression>>& get_parameters() const
    {
      return m_parameters;
    }

    const std::unique_ptr<statement>& get_body() const
    {
      return m_body;
    }

  private:
    std::unique_ptr<identifier_expression> m_identifier;
    std::list<std::unique_ptr<identifier_expression>> m_parameters;
    std::unique_ptr<statement> m_body;
  };

  class class_declaration : public declaration
  {
  public:
    class_declaration(token p_tok,
                      std::unique_ptr<identifier_expression> p_identifier,
                      std::list<std::unique_ptr<function_declaration>>&& p_methods,
                      std::unique_ptr<identifier_expression> p_super = nullptr)
        : declaration(node_type::nt_class_decl, p_tok), m_identifier(std::move(p_identifier)),
          m_methods(std::move(p_methods)), m_super(std::move(p_super))
    {
    }

    std::string to_string() override
    {
      std::stringstream ss;
      ss << "class ";
      ss << m_identifier->to_string();
      ss << (m_super == nullptr ? "" : "inherits " + m_super->to_string());
      ss << " {\n";
      for(auto i = 0; const auto& m : m_methods)
      {
        ss << m->to_string() << "\n";
      }
      ss << "}\n";
      return ss.str();
    }

    const std::unique_ptr<identifier_expression>& get_identifier() const
    {
      return m_identifier;
    }

    const std::list<std::unique_ptr<function_declaration>>& get_methods() const
    {
      return m_methods;
    }

    const std::unique_ptr<identifier_expression>& get_super() const
    {
      return m_super;
    }

  private:
    std::unique_ptr<identifier_expression> m_identifier;
    std::list<std::unique_ptr<function_declaration>> m_methods;
    std::unique_ptr<identifier_expression> m_super;
  };

  /**
   * misc
   **/

  constexpr std::string_view node_type_to_string(node_type p_nt)
  {
    using namespace std::string_view_literals;
    switch(p_nt)
    {
    case node_type::nt_node:
      return "node"sv;
    case node_type::nt_expression:
      return "expression"sv;
    case node_type::nt_statement:
      return "statement"sv;
    case node_type::nt_declaration:
      return "declaration"sv;
    case node_type::nt_program:
      return "program"sv;
    case node_type::nt_identifier_expr:
      return "identifier_expression"sv;
    case node_type::nt_number_expr:
      return "number_expression"sv;
    case node_type::nt_string_expr:
      return "string_expression"sv;
    case node_type::nt_prefix_expr:
      return "prefix_expression"sv;
    case node_type::nt_infix_binary_expr:
      return "infix_binary_expression"sv;
    case node_type::nt_infix_unary_expr:
      return "infix_unary_expression"sv;
    case node_type::nt_call_expr:
      return "call_expression"sv;
    case node_type::nt_assign_expr:
      return "assign_expression"sv;
    case node_type::nt_operator_expr:
      return "operator_expression"sv;
    case node_type::nt_conditional_expr:
      return "conditional_expression"sv;
    case node_type::nt_boolean_expr:
      return "boolean_expression"sv;
    case node_type::nt_null_expr:
      return "null_expression"sv;
    case node_type::nt_expression_statement_stmt:
      return "expression_statement_statement"sv;
    case node_type::nt_print_stmt:
      return "print_statement"sv;
    case node_type::nt_block_stmt:
      return "block_statement"sv;
    case node_type::nt_if_stmt:
      return "if_statement"sv;
    case node_type::nt_while_stmt:
      return "while_statement"sv;
    case node_type::nt_for_stmt:
      return "for_statement"sv;
    case node_type::nt_control_flow_stmt:
      return "control_flow"sv;
    case node_type::nt_let_decl:
      return "let_declaration"sv;
    }
    return "unknown_node"sv;
  }
} // namespace ok::ast

#endif // OK_AST_HPP