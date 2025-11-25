#ifndef OK_TOKEN_HPP
#define OK_TOKEN_HPP

#include <string>
#include <vector>

namespace ok
{
  enum class token_type
  {
    // idk
    tok_error = 0,         // error message
    tok_illegal,           // not allowed (invalid utf8 sequences)
    tok_eof,               // \0
    tok_assign,            // =
    tok_plus,              // +
    tok_minus,             // -
    tok_asterisk,          // *
    tok_slash,             // /
    tok_modulo,            // %
    tok_caret,             // ^
    tok_ampersand,         // &
    tok_bar,               // |
    tok_plus_plus,         // ++
    tok_minus_minus,       // --
    tok_plus_equal,        // +=
    tok_minus_equal,       // -=
    tok_asterisk_equal,    // *=
    tok_slash_equal,       // /=
    tok_modulo_equal,      // %=
    tok_caret_equal,       // ^=
    tok_ampersand_equal,   // &=
    tok_bar_equal,         // |=
    tok_shift_left_equal,  // <<=
    tok_shift_right_equal, // >>=
    tok_shift_left,        // <<
    tok_shift_right,       // >>
    tok_comma,             // ,
    tok_colon,             // :
    tok_semicolon,         // ;
    tok_dot,               // .
    tok_question,          // ?
    tok_bang,              // !
    tok_bang_equal,        // !=
    tok_equal,             // ==
    tok_less_equal,        // <=
    tok_greater_equal,     // >=
    tok_less,              // <
    tok_greater,           // >
    tok_left_paren,        // (
    tok_right_paren,       // )
    tok_left_brace,        // {
    tok_right_brace,       // }
    tok_left_bracket,      // [
    tok_right_bracket,     // ]
    tok_arrow,             // ->

    // literals
    tok_identifier, // oklang
    tok_number,     // 15
    tok_string,     // "oklang"

    // keywords
    tok_import,
    tok_as,
    tok_fu,
    tok_print,
    tok_let,
    tok_while,
    tok_for,
    tok_break,
    tok_continue,
    tok_if,
    tok_elif,
    tok_else,
    tok_and,
    tok_or,
    tok_class,
    tok_super,
    tok_inherits,
    tok_this,
    tok_null,
    tok_true,
    tok_false,
    tok_return,
    tok_not,
    tok_ok,
    tok_operator,
    tok_glob,
    tok_export,
    tok_mut,
    tok_static,
    tok_async,
    tok_try,
    tok_catch,
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
      return "+"sv;
    case token_type::tok_minus:
      return "-"sv;
    case token_type::tok_asterisk:
      return "*"sv;
    case token_type::tok_slash:
      return "/"sv;
    case token_type::tok_caret:
      return "^"sv;
    case token_type::tok_modulo:
      return "%"sv;
    case token_type::tok_ampersand:
      return "&"sv;
    case token_type::tok_bar:
      return "|"sv;
    case token_type::tok_shift_left:
      return "<<"sv;
    case token_type::tok_shift_right:
      return ">>"sv;
    case token_type::tok_plus_plus:
      return "++"sv;
    case token_type::tok_minus_minus:
      return "--"sv;
    case token_type::tok_plus_equal:
      return "+="sv;
    case token_type::tok_minus_equal:
      return "-="sv;
    case token_type::tok_asterisk_equal:
      return "*="sv;
    case token_type::tok_slash_equal:
      return "/="sv;
    case token_type::tok_caret_equal:
      return "^="sv;
    case token_type::tok_modulo_equal:
      return "%="sv;
    case token_type::tok_ampersand_equal:
      return "&="sv;
    case token_type::tok_bar_equal:
      return "|="sv;
    case token_type::tok_shift_left_equal:
      return "<<="sv;
    case token_type::tok_shift_right_equal:
      return ">>="sv;
    case token_type::tok_comma:
      return ","sv;
    case token_type::tok_colon:
      return ":"sv;
    case token_type::tok_semicolon:
      return ";"sv;
    case token_type::tok_dot:
      return "."sv;
    case token_type::tok_question:
      return "?"sv;
    case token_type::tok_bang:
      return "!"sv;
    case token_type::tok_bang_equal:
      return "!="sv;
    case token_type::tok_equal:
      return "="sv;
    case token_type::tok_less_equal:
      return "<="sv;
    case token_type::tok_greater_equal:
      return ">="sv;
    case token_type::tok_less:
      return "<"sv;
    case token_type::tok_greater:
      return ">"sv;
    case token_type::tok_left_paren:
      return "("sv;
    case token_type::tok_right_paren:
      return ")"sv;
    case token_type::tok_left_brace:
      return "{"sv;
    case token_type::tok_right_brace:
      return "}"sv;
    case token_type::tok_left_bracket:
      return "["sv;
    case token_type::tok_right_bracket:
      return "]"sv;
    case token_type::tok_arrow:
      return "->"sv;
    case token_type::tok_identifier:
      return "identifier"sv;
    case token_type::tok_number:
      return "number"sv;
    case token_type::tok_string:
      return "\""sv;
    case token_type::tok_import:
      return "import"sv;
    case token_type::tok_as:
      return "as"sv;
    case token_type::tok_fu:
      return "fu"sv;
    case token_type::tok_print:
      return "print"sv;
    case token_type::tok_let:
      return "let"sv;
    case token_type::tok_while:
      return "while"sv;
    case token_type::tok_for:
      return "for"sv;
    case token_type::tok_break:
      return "break"sv;
    case token_type::tok_continue:
      return "continue"sv;
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
    case token_type::tok_inherits:
      return "inherits"sv;
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
    case token_type::tok_operator:
      return "operator"sv;
    case token_type::tok_glob:
      return "glob"sv;
    case token_type::tok_export:
      return "export"sv;
    case token_type::tok_mut:
      return "mut"sv;
    case token_type::tok_static:
      return "static"sv;
    case token_type::tok_async:
      return "async"sv;
    case token_type::tok_try:
      return "try"sv;
    case token_type::tok_catch:
      return "catch"sv;
    }
    return "unknown"sv;
  }
} // namespace ok

#endif // OK_TOKEN_HPP