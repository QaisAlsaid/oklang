#ifndef OK_OPERATOR_HPP
#define OK_OPERATOR_HPP

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace ok
{
  enum class operator_type : uint8_t
  {
    op_undefined = 0,
    op_plus,
    op_minus,
    op_asterisk,
    op_slash,
    op_bang,
    op_assign,
    op_equal,
    op_bang_equal,
    op_greater,
    op_less,
    op_greater_equal,
    op_less_equal,
    op_call,

    // special operators
    op_bool,
    op_and,
    op_or,
    op_print,

  };

  namespace overridable_operator_type
  {
    enum overridable_operator_type : uint8_t
    {
      oot_unary_prefix_plus = 0,    // +x
      oot_unary_prefix_minus,       // -x
      oot_unary_prefix_bang,        // !x
      oot_unary_prefix_tiled,       // ~x
      oot_unary_prefix_plus_plus,   // ++x
      oot_unary_prefix_minus_minus, // --x

      oot_unary_postfix_plus_plus,   // x++
      oot_unary_postfix_minus_minus, // x--

      oot_unary_postfix_call,      // x()
      oot_unary_postfix_subscript, // x[]

      oot_binary_infix_plus,        // x + y
      oot_binary_infix_minus,       // x - y
      oot_binary_infix_asterisk,    // x * y
      oot_binary_infix_slash,       // x / y
      oot_binary_infix_caret,       // x ^ y
      oot_binary_infix_modulo,      // x % y
      oot_binary_infix_bitwise_and, // x & y
      oot_binary_infix_bitwise_or,  // x | y

      oot_binary_infix_greater,       // x > y
      oot_binary_infix_less,          // x < y
      oot_binary_infix_equal,         // x == y
      oot_binary_infix_bang_equal,    // x != y
      oot_binary_infix_greater_equal, // x >= u
      oot_binary_infix_less_equal,    // x <= y

      oot_binary_infix_plus_equal,        // x += y
      oot_binary_infix_minus_equal,       // x -= y
      oot_binary_infix_asterisk_equal,    // x *= y
      oot_binary_infix_slash_equal,       // x /= y
      oot_binary_infix_caret_equal,       // x ^= y
      oot_binary_infix_modulo_equal,      // x %= y
      oot_binary_infix_bitwise_and_equal, // x &= y
      oot_binary_infix_bitwise_or_equal,  // x |= y

      oot_binary_infix_shift_left,  // x << y
      oot_binary_infix_shift_right, // x >> y

      oot_binary_infix_equal_raw,      // x ==? y
      oot_binary_infix_bang_equal_raw, // x !=? y

      oot_ctor,
      oot_dtor,

      oot_print,
      oot_count,
    };
  }

  enum class method_type
  {
    mt_method = 0,
    mt_unary_prefix_plus,        // +x
    mt_unary_prefix_minus,       // -x
    mt_unary_prefix_bang,        // !x
    mt_unary_prefix_tiled,       // ~x
    mt_unary_prefix_plus_plus,   // ++x
    mt_unary_prefix_minus_minus, // --x

    mt_unary_postfix_plus_plus,   // x++
    mt_unary_postfix_minus_minus, // x--

    mt_unary_postfix_call,      // x()
    mt_unary_postfix_subscript, // x[]

    mt_binary_infix_plus,        // x + y
    mt_binary_infix_minus,       // x - y
    mt_binary_infix_asterisk,    // x * y
    mt_binary_infix_slash,       // x / y
    mt_binary_infix_caret,       // x ^ y
    mt_binary_infix_modulo,      // x % y
    mt_binary_infix_bitwise_and, // x & y
    mt_binary_infix_bitwise_or,  // x | y

    mt_binary_infix_greater,       // x > y
    mt_binary_infix_less,          // x < y
    mt_binary_infix_equal,         // x == y
    mt_binary_infix_bang_equal,    // x != y
    mt_binary_infix_greater_equal, // x >= u
    mt_binary_infix_less_equal,    // x <= y

    mt_binary_infix_plus_equal,        // x += y
    mt_binary_infix_minus_equal,       // x -= y
    mt_binary_infix_asterisk_equal,    // x *= y
    mt_binary_infix_slash_equal,       // x /= y
    mt_binary_infix_caret_equal,       // x ^= y
    mt_binary_infix_modulo_equal,      // x %= y
    mt_binary_infix_bitwise_and_equal, // x &= y
    mt_binary_infix_bitwise_or_equal,  // x |= y

    mt_binary_infix_shift_left,  // x << y
    mt_binary_infix_shift_right, // x >> y

    mt_binary_infix_equal_raw,      // x ==? y
    mt_binary_infix_bang_equal_raw, // x !=? y

    mt_ctor,
    mt_dtor,
    mt_print,
  };

  constexpr std::string_view operator_type_to_string(const operator_type p_operator)
  {
    using namespace std::string_view_literals;
    switch(p_operator)
    {
    case operator_type::op_plus:
      return "+"sv;
    case operator_type::op_minus:
      return "-"sv;
    case operator_type::op_asterisk:
      return "*"sv;
    case operator_type::op_slash:
      return "/"sv;
    case operator_type::op_bang:
      return "!"sv;
    case operator_type::op_assign:
      return "="sv;
    case operator_type::op_equal:
      return "=="sv;
    case operator_type::op_bang_equal:
      return "!="sv;
    case operator_type::op_greater:
      return ">"sv;
    case operator_type::op_less:
      return "<"sv;
    case operator_type::op_greater_equal:
      return ">="sv;
    case operator_type::op_less_equal:
      return "<="sv;
    case operator_type::op_and:
      return "and"sv;
    case operator_type::op_or:
      return "or"sv;
    default:
      return "[undefined operator]"sv;
    }
  }

  // fake!
  constexpr operator_type operator_type_from_string(const std::string_view p_operator)
  {
    static const std::unordered_map<std::string_view, operator_type> s_operator_map = {

        {"+", operator_type::op_plus},
        {"-", operator_type::op_minus},
        {"*", operator_type::op_asterisk},
        {"/", operator_type::op_slash},
        {"!", operator_type::op_bang},
        {"=", operator_type::op_assign},
        {"==", operator_type::op_equal},
        {"!=", operator_type::op_bang_equal},
        {">", operator_type::op_greater},
        {"<", operator_type::op_less},
        {">=", operator_type::op_greater_equal},
        {"<=", operator_type::op_less_equal},
        {"and", operator_type::op_and},
        {"or", operator_type::op_or}};

    auto it = s_operator_map.find(p_operator);
    if(s_operator_map.end() == it)
      return operator_type::op_undefined;
    return it->second;
  }
} // namespace ok

#endif // OK_OPERATOR_HPP