#include <vector>

namespace ok
{
  struct token
  {
    enum class type
    {
      // idk
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
      tok_question,      // ?
      tok_bang,          // !
      tok_bang_equal,    // !=
      tok_equal,         // ==
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
      toke_false,
      tok_return,
      tok_ok,
    };
  };

  using token_array = std::vector<token>;
} // namespace ok