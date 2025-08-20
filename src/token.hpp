#ifndef OK_TOKEN_HPP
#define OK_TOKEN_HPP

#include <string>
#include <vector>

namespace ok
{
  enum class token_type
  {
    // idk
    tok_error,         // error message
    tok_illegal,       // not allowed (invalid utf8 sequences)
    tok_eof,           // \0
    tok_assign,        // =
    tok_plus,          // +
    tok_minus,         // -
    tok_asterisk,      // *
    tok_slash,         // /
    tok_comma,         // ,
    tok_colon,         // :
    tok_semicolon,     // ;
    tok_dot,           // .
    tok_question,      // ?
    tok_bang,          // !
    tok_bang_equal,    // !=
    tok_equal,         // ==
    tok_less_equal,    // <=
    tok_greater_equal, // >=
    tok_less,          // <
    tok_greater,       // >
    tok_left_paren,    // (
    tok_right_paren,   // )
    tok_left_brace,    // {
    tok_right_brace,   // }
    tok_left_bracket,  // [
    tok_right_bracket, // ]

    // literals
    tok_identifier, // oklang
    tok_number,     // 15
    tok_string,     // "detected to all human beings"

    // keywords
    tok_import,
    tok_as,
    tok_fun,
    tok_let,
    tok_letdown,
    tok_while,
    tok_for,
    tok_if,
    tok_elif,
    tok_else,
    tok_and,
    tok_or,
    tok_class,
    tok_super,
    tok_this,
    tok_null,
    tok_true,
    tok_false,
    tok_return,
    tok_not,
    tok_ok,
  };
  struct token
  {
    token_type type;
    std::string raw_literal; // copy and own = no headache, slight overhead on compile time nothing to worry about!
    // Question(Qais): stupid if you have offset and line, then you must keep the original string for errors,
    // thus copy above is redundant! unless you went with the not showing source route then you
    // are annoying!
    size_t line;
    size_t offset;
  };

  token_type lookup_identifier(const std::string_view p_raw);
  using token_array = std::vector<token>;
  constexpr std::string_view token_type_to_string(const token_type p_type)
  {
    using namespace std::string_view_literals;
    switch(p_type)
    {
    case token_type::tok_error:
      return "error"sv;
    case token_type::tok_illegal:
      return "illegal"sv;
    case token_type::tok_eof:
      return "eof"sv;
    case token_type::tok_assign:
      return "assign"sv;
    case token_type::tok_plus:
      return "plus"sv;
    case token_type::tok_minus:
      return "minus"sv;
    case token_type::tok_asterisk:
      return "asterisk"sv;
    case token_type::tok_slash:
      return "slash"sv;
    case token_type::tok_comma:
      return "comma"sv;
    case token_type::tok_colon:
      return "colon"sv;
    case token_type::tok_semicolon:
      return "semicolon"sv;
    case token_type::tok_dot:
      return "dot"sv;
    case token_type::tok_question:
      return "question"sv;
    case token_type::tok_bang:
      return "bang"sv;
    case token_type::tok_bang_equal:
      return "bang_equal"sv;
    case token_type::tok_equal:
      return "equal"sv;
    case token_type::tok_less_equal:
      return "less_equal"sv;
    case token_type::tok_greater_equal:
      return "greater_equal"sv;
    case token_type::tok_less:
      return "less"sv;
    case token_type::tok_greater:
      return "greater"sv;
    case token_type::tok_left_paren:
      return "left_paren"sv;
    case token_type::tok_right_paren:
      return "right_paren"sv;
    case token_type::tok_left_brace:
      return "left_brace"sv;
    case token_type::tok_right_brace:
      return "right_brace"sv;
    case token_type::tok_left_bracket:
      return "left_bracket"sv;
    case token_type::tok_right_bracket:
      return "right_bracket"sv;
    case token_type::tok_identifier:
      return "identifier"sv;
    case token_type::tok_number:
      return "number"sv;
    case token_type::tok_string:
      return "string"sv;
    case token_type::tok_import:
      return "import"sv;
    case token_type::tok_as:
      return "as"sv;
    case token_type::tok_fun:
      return "fun"sv;
    case token_type::tok_let:
      return "let"sv;
    case token_type::tok_letdown:
      return "letdown"sv;
    case token_type::tok_while:
      return "while"sv;
    case token_type::tok_for:
      return "for"sv;
    case token_type::tok_if:
      return "if"sv;
    case token_type::tok_elif:
      return "elif"sv;
    case token_type::tok_else:
      return "else"sv;
    case token_type::tok_and:
      return "and"sv;
    case token_type::tok_or:
      return "or"sv;
    case token_type::tok_class:
      return "class"sv;
    case token_type::tok_super:
      return "super"sv;
    case token_type::tok_this:
      return "this"sv;
    case token_type::tok_null:
      return "null"sv;
    case token_type::tok_true:
      return "true"sv;
    case token_type::tok_false:
      return "false"sv;
    case token_type::tok_return:
      return "return"sv;
    case token_type::tok_not:
      return "not"sv;
    case token_type::tok_ok:
      return "ok"sv;
    }
    return "unknown"sv;
  }
} // namespace ok

#endif // OK_TOKEN_HPP