#ifndef OK_OPERATOR_HPP
#define OK_OPERATOR_HPP

#include "token.hpp"
#include "utility.hpp"
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <utility>

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
    op_subscript,
    op_tiled,
    op_modulo,
    op_ampersand,
    op_caret,
    op_bar,
    op_plus_plus,
    op_minus_minus,
    op_plus_equal,
    op_minus_equal,
    op_asterisk_equal,
    op_slash_equal,
    op_caret_equal,
    op_modulo_equal,
    op_bitwise_and_equal,
    op_bitwise_or_equal,
    op_shift_left_equal,
    op_shift_right_equal,
    op_shift_left,
    op_shift_right,

    // special operators
    op_and,
    op_or,
    op_as,
    op_print,
  };

  // namespace overridable_operator_type
  // {
  //   enum overridable_operator_type : uint8_t
  //   {
  //     oot_none = 0,
  //     oot_unary_prefix_plus,        // +x
  //     oot_unary_prefix_minus,       // -x
  //     oot_unary_prefix_bang,        // !x
  //     oot_unary_prefix_tiled,       // ~x
  //     oot_unary_prefix_plus_plus,   // ++x
  //     oot_unary_prefix_minus_minus, // --x

  //     oot_unary_postfix_plus_plus,   // x++
  //     oot_unary_postfix_minus_minus, // x--

  //     oot_unary_postfix_call,      // x()
  //     oot_unary_postfix_subscript, // x[]

  //     oot_binary_infix_plus,        // x + y
  //     oot_binary_infix_minus,       // x - y
  //     oot_binary_infix_asterisk,    // x * y
  //     oot_binary_infix_slash,       // x / y
  //     oot_binary_infix_caret,       // x ^ y
  //     oot_binary_infix_modulo,      // x % y
  //     oot_binary_infix_bitwise_and, // x & y
  //     oot_binary_infix_bitwise_or,  // x | y

  //     oot_binary_infix_greater,       // x > y
  //     oot_binary_infix_less,          // x < y
  //     oot_binary_infix_equal,         // x == y
  //     oot_binary_infix_bang_equal,    // x != y
  //     oot_binary_infix_greater_equal, // x >= u
  //     oot_binary_infix_less_equal,    // x <= y

  //     oot_binary_infix_plus_equal,        // x += y
  //     oot_binary_infix_minus_equal,       // x -= y
  //     oot_binary_infix_asterisk_equal,    // x *= y
  //     oot_binary_infix_slash_equal,       // x /= y
  //     oot_binary_infix_caret_equal,       // x ^= y
  //     oot_binary_infix_modulo_equal,      // x %= y
  //     oot_binary_infix_bitwise_and_equal, // x &= y
  //     oot_binary_infix_bitwise_or_equal,  // x |= y

  //     oot_binary_infix_shift_left,  // x << y
  //     oot_binary_infix_shift_right, // x >> y

  //     oot_binary_infix_equal_raw,      // x ==? y
  //     oot_binary_infix_bang_equal_raw, // x !=? y

  //     oot_ctor,
  //     oot_dtor,
  //     oot_print,
  //     oot_convert,
  //     oot_count,
  //   };
  // }

  enum class unique_overridable_operator_type : uint64_t
  {
    uoot_none = 0ul,
    uoot_unary_prefix_plus = 1ul << 0,        // +x
    uoot_unary_prefix_minus = 1ul << 1,       // -x
    uoot_unary_prefix_bang = 1ul << 2,        // !x
    uoot_unary_prefix_tiled = 1ul << 3,       // ~x
    uoot_unary_prefix_plus_plus = 1ul << 4,   // ++x
    uoot_unary_prefix_minus_minus = 1ul << 5, // --x

    uoot_group_unary_prefix_arithmetic =
        uoot_unary_prefix_plus | uoot_unary_prefix_minus | uoot_unary_prefix_plus_plus | uoot_unary_prefix_minus_minus,
    uoot_group_unary_prefix = uoot_group_unary_prefix_arithmetic | uoot_unary_prefix_bang | uoot_unary_prefix_tiled,

    uoot_unary_postfix_plus_plus = 1ul << 6,   // x++
    uoot_unary_postfix_minus_minus = 1ul << 7, // x--
    uoot_unary_postfix_call = 1ul << 8,        // x()
    uoot_unary_postfix_subscript = 1ul << 9,   // x[]

    uoot_group_unary_postfix_arithmetic = uoot_unary_postfix_plus_plus | uoot_unary_postfix_minus_minus,
    uoot_group_unary_postfix_calllike = uoot_unary_postfix_call | uoot_unary_postfix_subscript,
    uoot_group_unary_postfix = uoot_group_unary_postfix_arithmetic | uoot_group_unary_postfix_calllike,

    uoot_binary_infix_plus = 1ul << 10,        // x + y
    uoot_binary_infix_minus = 1ul << 11,       // x - y
    uoot_binary_infix_asterisk = 1ul << 12,    // x * y
    uoot_binary_infix_slash = 1ul << 13,       // x / y
    uoot_binary_infix_caret = 1ul << 14,       // x ^ y
    uoot_binary_infix_modulo = 1ul << 15,      // x % y
    uoot_binary_infix_bitwise_and = 1ul << 16, // x & y
    uoot_binary_infix_bitwise_or = 1ul << 17,  // x | y
    uoot_binary_infix_shift_left = 1ul << 18,  // x << y
    uoot_binary_infix_shift_right = 1ul << 19, // x >> y

    uoot_binary_infix_greater = 1ul << 20,       // x > y
    uoot_binary_infix_less = 1ul << 21,          // x < y
    uoot_binary_infix_equal = 1ul << 22,         // x == y
    uoot_binary_infix_bang_equal = 1ul << 23,    // x != y
    uoot_binary_infix_greater_equal = 1ul << 24, // x >= u
    uoot_binary_infix_less_equal = 1ul << 25,    // x <= y

    uoot_binary_infix_plus_equal = 1ul << 26,        // x += y
    uoot_binary_infix_minus_equal = 1ul << 27,       // x -= y
    uoot_binary_infix_asterisk_equal = 1ul << 28,    // x *= y
    uoot_binary_infix_slash_equal = 1ul << 29,       // x /= y
    uoot_binary_infix_caret_equal = 1ul << 30,       // x ^= y
    uoot_binary_infix_modulo_equal = 1ul << 31,      // x %= y
    uoot_binary_infix_bitwise_and_equal = 1ul << 32, // x &= y
    uoot_binary_infix_bitwise_or_equal = 1ul << 33,  // x |= y

    uoot_binary_infix_shift_left_equal = 1ul << 34,
    uoot_binary_infix_shift_right_equal = 1ul << 35,

    uoot_binary_infix_equal_raw = 1ul << 36,      // x ==? y
    uoot_binary_infix_bang_equal_raw = 1ul << 37, // x !=? y

    uoot_group_binary_infix_arithmetic = uoot_binary_infix_plus | uoot_binary_infix_minus | uoot_binary_infix_asterisk |
                                         uoot_binary_infix_slash | uoot_binary_infix_caret | uoot_binary_infix_modulo,
    uoot_group_binary_infix_comparision = uoot_binary_infix_greater | uoot_binary_infix_less | uoot_binary_infix_equal |
                                          uoot_binary_infix_bang_equal | uoot_binary_infix_greater_equal |
                                          uoot_binary_infix_less_equal,
    uoot_group_binary_infix_bitwise = uoot_binary_infix_bitwise_and | uoot_binary_infix_bitwise_or |
                                      uoot_binary_infix_shift_left | uoot_binary_infix_shift_right,
    uoot_group_binary_infix_assigning_arithmetic = uoot_binary_infix_plus_equal | uoot_binary_infix_minus_equal |
                                                   uoot_binary_infix_asterisk_equal | uoot_binary_infix_slash_equal |
                                                   uoot_binary_infix_caret_equal | uoot_binary_infix_modulo_equal,
    uoot_group_binary_infix_assigning_bitwise =
        uoot_binary_infix_bitwise_and_equal | uoot_binary_infix_bitwise_or_equal | uoot_binary_infix_shift_left_equal |
        uoot_binary_infix_shift_right_equal,
    uoot_group_binary_infix_assigning =
        uoot_group_binary_infix_assigning_arithmetic | uoot_group_binary_infix_assigning_bitwise,
    uoot_group_binary_infix = uoot_group_binary_infix_arithmetic | uoot_group_binary_infix_comparision |
                              uoot_group_binary_infix_bitwise | uoot_group_binary_infix_assigning,

    uoot_method = 1ul << 38,
    uoot_ctor = 1ul << 39,
    uoot_dtor = 1ul << 40,
    uoot_print = 1ul << 41,

    uoot_group_special_method = uoot_ctor | uoot_dtor | uoot_print,

    uoot_convert = 1ul << 42,

    uoot_group_unary = uoot_group_unary_prefix | uoot_group_unary_postfix,
    uoot_group_binary = uoot_group_binary_infix,

    uoot_group_all_operator = uoot_group_unary | uoot_group_binary_infix,
    uoot_group_all_special = uoot_group_special_method | uoot_convert,
    uoot_group_all = uoot_group_all_operator | uoot_group_all_special,

    uoot_group_ambiguous_plus = uoot_unary_prefix_plus | uoot_binary_infix_plus,
    uoot_group_ambiguous_minus = uoot_unary_prefix_minus | uoot_binary_infix_minus,
    uoot_group_ambiguous_plus_plus = uoot_unary_prefix_plus_plus | uoot_unary_postfix_plus_plus,
    uoot_group_ambiguous_minus_minus = uoot_unary_prefix_minus_minus | uoot_unary_postfix_minus_minus,

    uoot_group_ambiguous = uoot_group_ambiguous_plus | uoot_group_ambiguous_minus | uoot_group_ambiguous_plus_plus |
                           uoot_group_ambiguous_minus_minus,

    uoot_group_zero_param = uoot_group_unary | uoot_dtor | uoot_print,
    uoot_group_single_param =
        uoot_group_binary | uoot_convert | uoot_unary_postfix_plus_plus | uoot_unary_postfix_minus_minus,
    uoot_group_special_variable_param = uoot_ctor | uoot_group_unary_postfix_calllike,
  };

  namespace method_type
  {
    enum method_type : uint8_t
    {
      mt_none = 0,
      mt_method,
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

      mt_binary_infix_shift_left,  // x << y
      mt_binary_infix_shift_right, // x >> y

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

      mt_binary_infix_shift_left_equal,  // <<=
      mt_binary_infix_shift_right_equal, // >>=

      mt_binary_infix_equal_raw,      // x ==? y
      mt_binary_infix_bang_equal_raw, // x !=? y

      mt_ctor,
      mt_dtor,
      mt_print,
      mt_convert,
      mt_count,
    };
  }

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

        {"!", operator_type::op_bang},
        {"~", operator_type::op_tiled},
        {"++", operator_type::op_plus_plus},
        {"--", operator_type::op_minus_minus},
        {"+", operator_type::op_plus},
        {"-", operator_type::op_minus},
        {"*", operator_type::op_asterisk},
        {"/", operator_type::op_slash},
        {"^", operator_type::op_caret},
        {"&", operator_type::op_ampersand},
        {"|", operator_type::op_bar},
        {"%", operator_type::op_modulo},
        {"=", operator_type::op_assign},
        {"==", operator_type::op_equal},
        {"!=", operator_type::op_bang_equal},
        {">", operator_type::op_greater},
        {"<", operator_type::op_less},
        {">=", operator_type::op_greater_equal},
        {"<=", operator_type::op_less_equal},
        {"+=", operator_type::op_plus_equal},
        {"-=", operator_type::op_minus_equal},
        {"*=", operator_type::op_asterisk_equal},
        {"/=", operator_type::op_slash_equal},
        {"^=", operator_type::op_caret_equal},
        {"%=", operator_type::op_modulo_equal},
        {"&=", operator_type::op_bitwise_and_equal},
        {"|=", operator_type::op_bitwise_or_equal},
        {"<<=", operator_type::op_shift_left_equal},
        {">>=", operator_type::op_shift_right_equal},
        {"<<", operator_type::op_shift_left},
        {">>", operator_type::op_shift_right},
        {"(", operator_type::op_call},      //!
        {"[", operator_type::op_subscript}, //!
        {"and", operator_type::op_and},
        {"or", operator_type::op_or},
        {"as", operator_type::op_as},
        {"print", operator_type::op_print}, //!
    };

    auto it = s_operator_map.find(p_operator);
    if(s_operator_map.end() == it)
      return operator_type::op_undefined;
    return it->second;
  }

  constexpr std::string_view method_type_to_string(uint8_t p_mt)
  {
    using namespace std::string_view_literals;
    switch(p_mt)
    {
    case method_type::mt_method:
      return "method"sv;
    case method_type::mt_unary_prefix_plus:
      return "+ <expr>"sv;
    case method_type::mt_unary_prefix_minus:
      return "- <expr>"sv;
    case method_type::mt_unary_prefix_bang:
      return "! <expr>"sv;
    case method_type::mt_unary_prefix_tiled:
      return "~ <expr>"sv;
    case method_type::mt_unary_prefix_plus_plus:
      return "++ <expr>"sv;
    case method_type::mt_unary_prefix_minus_minus:
      return "-- <expr>"sv;

    case method_type::mt_unary_postfix_plus_plus:
      return "<expr> ++";
    case method_type::mt_unary_postfix_minus_minus:
      return "<expr> --";
    case method_type::mt_unary_postfix_call:
      return "<expr> ()";
    case method_type::mt_unary_postfix_subscript:
      return "<expr> []";

    case method_type::mt_binary_infix_plus:
      return "<expr> + <expr>";
    case method_type::mt_binary_infix_minus:
      return "<expr> - <expr>";
    case method_type::mt_binary_infix_asterisk:
      return "<expr> * <expr>";
    case method_type::mt_binary_infix_slash:
      return "<expr> / <expr>";
    case method_type::mt_binary_infix_caret:
      return "<expr> ^ <expr>";
    case method_type::mt_binary_infix_modulo:
      return "<expr> % <expr>";
    case method_type::mt_binary_infix_bitwise_and:
      return "<expr> & <expr>";
    case method_type::mt_binary_infix_bitwise_or:
      return "<expr> | <expr>";

    case method_type::mt_binary_infix_shift_left:
      return "<expr> << <expr>";
    case method_type::mt_binary_infix_shift_right:
      return "<expr> >> <expr>";

    case method_type::mt_binary_infix_greater:
      return "<expr> > <expr>";
    case method_type::mt_binary_infix_less:
      return "<expr> < <expr>";
    case method_type::mt_binary_infix_equal:
      return "<expr> == <expr>";
    case method_type::mt_binary_infix_bang_equal:
      return "<expr> != <expr>";
    case method_type::mt_binary_infix_greater_equal:
      return "<expr> >= <expr>";
    case method_type::mt_binary_infix_less_equal:
      return "<expr> <= <expr>";

    case method_type::mt_binary_infix_plus_equal:
      return "<expr> += <expr>";
    case method_type::mt_binary_infix_minus_equal:
      return "<expr> -= <expr>";
    case method_type::mt_binary_infix_asterisk_equal:
      return "<expr> *= <expr>";
    case method_type::mt_binary_infix_slash_equal:
      return "<expr> /= <expr>";
    case method_type::mt_binary_infix_caret_equal:
      return "<expr> ^= <expr>";
    case method_type::mt_binary_infix_modulo_equal:
      return "<expr> %= <expr>";
    case method_type::mt_binary_infix_bitwise_and_equal:
      return "<expr> &= <expr>";
    case method_type::mt_binary_infix_bitwise_or_equal:
      return "<expr> |= <expr>";

    case method_type::mt_binary_infix_shift_left_equal:
      return "<expr> <<= <expr>";
    case method_type::mt_binary_infix_shift_right_equal:
      return "<expr> >>= <expr>";

    case method_type::mt_ctor:
      return "<ctor>";
    case method_type::mt_dtor:
      return "<dtor>";
    case method_type::mt_print:
      return "<print>";
    case method_type::mt_convert:
      return "<convert>";

    case method_type::mt_none:
    default:
      return "<none>"sv;
    }
    return "<none>"sv;
  }

  constexpr uint8_t unique_overridable_operator_type_to_method_type(unique_overridable_operator_type p_uoot)
  {
    switch(p_uoot)
    {
    case unique_overridable_operator_type::uoot_none:
      return method_type::mt_none;
    case unique_overridable_operator_type::uoot_unary_prefix_plus:
      return method_type::mt_unary_prefix_plus;
    case unique_overridable_operator_type::uoot_unary_prefix_minus:
      return method_type::mt_unary_prefix_minus;
    case unique_overridable_operator_type::uoot_unary_prefix_bang:
      return method_type::mt_unary_prefix_bang;
    case unique_overridable_operator_type::uoot_unary_prefix_tiled:
      return method_type::mt_unary_prefix_tiled;
    case unique_overridable_operator_type::uoot_unary_prefix_plus_plus:
      return method_type::mt_unary_prefix_plus_plus;
    case unique_overridable_operator_type::uoot_unary_prefix_minus_minus:
      return method_type::mt_unary_prefix_minus_minus;

    case unique_overridable_operator_type::uoot_unary_postfix_plus_plus:
      return method_type::mt_unary_postfix_plus_plus;
    case unique_overridable_operator_type::uoot_unary_postfix_minus_minus:
      return method_type::mt_unary_postfix_minus_minus;
    case unique_overridable_operator_type::uoot_unary_postfix_call:
      return method_type::mt_unary_postfix_call;
    case unique_overridable_operator_type::uoot_unary_postfix_subscript:
      return method_type::mt_unary_postfix_subscript;

    case unique_overridable_operator_type::uoot_binary_infix_plus:
      return method_type::mt_binary_infix_plus;
    case unique_overridable_operator_type::uoot_binary_infix_minus:
      return method_type::mt_binary_infix_minus;
    case unique_overridable_operator_type::uoot_binary_infix_asterisk:
      return method_type::mt_binary_infix_asterisk;
    case unique_overridable_operator_type::uoot_binary_infix_slash:
      return method_type::mt_binary_infix_slash;
    case unique_overridable_operator_type::uoot_binary_infix_caret:
      return method_type::mt_binary_infix_caret;
    case unique_overridable_operator_type::uoot_binary_infix_modulo:
      return method_type::mt_binary_infix_modulo;
    case unique_overridable_operator_type::uoot_binary_infix_bitwise_and:
      return method_type::mt_binary_infix_bitwise_and;
    case unique_overridable_operator_type::uoot_binary_infix_bitwise_or:
      return method_type::mt_binary_infix_bitwise_or;
    case unique_overridable_operator_type::uoot_binary_infix_shift_left:
      return method_type::mt_binary_infix_shift_left;
    case unique_overridable_operator_type::uoot_binary_infix_shift_right:
      return method_type::mt_binary_infix_shift_right;

    case unique_overridable_operator_type::uoot_binary_infix_greater:
      return method_type::mt_binary_infix_greater;
    case unique_overridable_operator_type::uoot_binary_infix_less:
      return method_type::mt_binary_infix_less;
    case unique_overridable_operator_type::uoot_binary_infix_equal:
      return method_type::mt_binary_infix_equal;
    case unique_overridable_operator_type::uoot_binary_infix_bang_equal:
      return method_type::mt_binary_infix_bang_equal;
    case unique_overridable_operator_type::uoot_binary_infix_greater_equal:
      return method_type::mt_binary_infix_greater_equal;
    case unique_overridable_operator_type::uoot_binary_infix_less_equal:
      return method_type::mt_binary_infix_less_equal;

    case unique_overridable_operator_type::uoot_binary_infix_plus_equal:
      return method_type::mt_binary_infix_plus_equal;
    case unique_overridable_operator_type::uoot_binary_infix_minus_equal:
      return method_type::mt_binary_infix_minus_equal;
    case unique_overridable_operator_type::uoot_binary_infix_asterisk_equal:
      return method_type::mt_binary_infix_asterisk_equal;
    case unique_overridable_operator_type::uoot_binary_infix_slash_equal:
      return method_type::mt_binary_infix_slash_equal;
    case unique_overridable_operator_type::uoot_binary_infix_caret_equal:
      return method_type::mt_binary_infix_caret_equal;
    case unique_overridable_operator_type::uoot_binary_infix_modulo_equal:
      return method_type::mt_binary_infix_modulo_equal;
    case unique_overridable_operator_type::uoot_binary_infix_bitwise_and_equal:
      return method_type::mt_binary_infix_bitwise_and_equal;
    case unique_overridable_operator_type::uoot_binary_infix_bitwise_or_equal:
      return method_type::mt_binary_infix_bitwise_or_equal;
    case unique_overridable_operator_type::uoot_binary_infix_shift_left_equal:
      return method_type::mt_binary_infix_shift_left_equal;
    case unique_overridable_operator_type::uoot_binary_infix_shift_right_equal:
      return method_type::mt_binary_infix_shift_right_equal;

    case unique_overridable_operator_type::uoot_ctor:
      return method_type::mt_ctor;
    case unique_overridable_operator_type::uoot_dtor:
      return method_type::mt_dtor;
    case unique_overridable_operator_type::uoot_print:
      return method_type::mt_print;
    case unique_overridable_operator_type::uoot_convert:
      return method_type::mt_convert;
    default:
      return method_type::mt_none;
    }
    return method_type::mt_none;
  }

  constexpr std::pair<unique_overridable_operator_type, uint8_t> // token count
  unique_overridable_operator_type_from_string(const std::string_view p_operator)
  {
    static const std::unordered_map<std::string_view, unique_overridable_operator_type> s_operator_map = {
        {"+", unique_overridable_operator_type::uoot_group_ambiguous_plus},
        {"-", unique_overridable_operator_type::uoot_group_ambiguous_minus},
        {"++", unique_overridable_operator_type::uoot_group_ambiguous_plus_plus},
        {"--", unique_overridable_operator_type::uoot_group_ambiguous_minus_minus},
        {"!", unique_overridable_operator_type::uoot_unary_prefix_bang},
        {"~", unique_overridable_operator_type::uoot_unary_prefix_tiled},
        {"(", unique_overridable_operator_type::uoot_unary_postfix_call},
        {"[", unique_overridable_operator_type::uoot_unary_postfix_subscript},
        {"*", unique_overridable_operator_type::uoot_binary_infix_asterisk},
        {"/", unique_overridable_operator_type::uoot_binary_infix_slash},
        {"^", unique_overridable_operator_type::uoot_binary_infix_caret},
        {"%", unique_overridable_operator_type::uoot_binary_infix_modulo},
        {"&", unique_overridable_operator_type::uoot_binary_infix_bitwise_and},
        {"|", unique_overridable_operator_type::uoot_binary_infix_bitwise_or},
        {">", unique_overridable_operator_type::uoot_binary_infix_greater},
        {"<", unique_overridable_operator_type::uoot_binary_infix_less},
        {"==", unique_overridable_operator_type::uoot_binary_infix_equal},
        {"!=", unique_overridable_operator_type::uoot_binary_infix_bang_equal},
        {">=", unique_overridable_operator_type::uoot_binary_infix_greater_equal},
        {"<=", unique_overridable_operator_type::uoot_binary_infix_less_equal},
        {"+=", unique_overridable_operator_type::uoot_binary_infix_asterisk_equal},
        {"-=", unique_overridable_operator_type::uoot_binary_infix_minus_equal},
        {"*=", unique_overridable_operator_type::uoot_binary_infix_asterisk_equal},
        {"/=", unique_overridable_operator_type::uoot_binary_infix_slash_equal},
        {"^=", unique_overridable_operator_type::uoot_binary_infix_caret_equal},
        {"%=", unique_overridable_operator_type::uoot_binary_infix_modulo_equal},
        {"&=", unique_overridable_operator_type::uoot_binary_infix_bitwise_and_equal},
        {"|=", unique_overridable_operator_type::uoot_binary_infix_bitwise_or_equal},
        {"<<", unique_overridable_operator_type::uoot_binary_infix_shift_left},
        {">>", unique_overridable_operator_type::uoot_binary_infix_shift_right}};

    auto it = s_operator_map.find(p_operator);
    if(s_operator_map.end() == it)
      return {unique_overridable_operator_type::uoot_none, 0};
    if(is_in_group(it->second, unique_overridable_operator_type::uoot_group_unary_postfix_calllike))
    {
      return {it->second, 2};
    }
    return {it->second, 1};
  }
} // namespace ok

#endif // OK_OPERATOR_HPP