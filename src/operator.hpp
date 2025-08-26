#ifndef OK_OPERATOR_HPP
#define OK_OPERATOR_HPP

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace ok
{
  enum class operator_type : uint8_t
  {
    undefined = 0,
    plus,
    minus,
    asterisk,
    slash,
    bang,
    assign,
    equal,
    bang_equal,
    greater,
    less,
    greater_equal,
    less_equal
  };

  constexpr std::string_view operator_type_to_string(const operator_type p_operator)
  {
    using namespace std::string_view_literals;
    switch(p_operator)
    {
    case operator_type::plus:
      return "+"sv;
    case operator_type::minus:
      return "-"sv;
    case operator_type::asterisk:
      return "*"sv;
    case operator_type::slash:
      return "/"sv;
    case operator_type::bang:
      return "!"sv;
    case operator_type::assign:
      return "="sv;
    case operator_type::equal:
      return "=="sv;
    case operator_type::bang_equal:
      return "!="sv;
    case operator_type::greater:
      return ">"sv;
    case operator_type::less:
      return "<"sv;
    case operator_type::greater_equal:
      return ">="sv;
    case operator_type::less_equal:
      return "<="sv;
    default:
      return "[undefined operator]"sv;
    }
  }

  // fake!
  constexpr operator_type operator_type_from_string(const std::string_view p_operator)
  {
    static const std::unordered_map<std::string_view, operator_type> s_operator_map = {

        {"+", operator_type::plus},
        {"-", operator_type::minus},
        {"*", operator_type::asterisk},
        {"/", operator_type::slash},
        {"!", operator_type::bang},
        {"=", operator_type::assign},
        {"==", operator_type::equal},
        {"!=", operator_type::bang_equal},
        {">", operator_type::greater},
        {"<", operator_type::less},
        {">=", operator_type::greater_equal},
        {"<=", operator_type::less_equal}};

    auto it = s_operator_map.find(p_operator);
    if(s_operator_map.end() == it)
      return operator_type::undefined;
    return it->second;
  }
} // namespace ok

#endif // OK_OPERATOR_HPP