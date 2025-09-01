#include "token.hpp"
#include <string_view>
#include <unordered_map>

namespace ok
{
  using namespace std::string_view_literals;
  static std::unordered_map<std::string_view, token_type> s_keywords =

      {

          {"import"sv, token_type::tok_import},
          {"as"sv, token_type::tok_as},
          {"fun"sv, token_type::tok_fun},
          {"print"sv, token_type::tok_print},
          {"let"sv, token_type::tok_let},
          {"letdown"sv, token_type::tok_letdown},
          {"while"sv, token_type::tok_while},
          {"for"sv, token_type::tok_for},
          {"if"sv, token_type::tok_if},
          {"elif"sv, token_type::tok_elif},
          {"else"sv, token_type::tok_else},
          {"and"sv, token_type::tok_and},
          {"or"sv, token_type::tok_or},
          {"class"sv, token_type::tok_class},
          {"super"sv, token_type::tok_super},
          {"this"sv, token_type::tok_this},
          {"null"sv, token_type::tok_null},
          {"true"sv, token_type::tok_true},
          {"false"sv, token_type::tok_false},
          {"return"sv, token_type::tok_return},
          {"not", token_type::tok_not},
          {"ok"sv, token_type::tok_ok}

  };

  token_type lookup_identifier(const std::string_view p_raw)
  {
    auto it = s_keywords.find(p_raw);
    return it != s_keywords.end() ? it->second : token_type::tok_identifier;
  }
} // namespace ok