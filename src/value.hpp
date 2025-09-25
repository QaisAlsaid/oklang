#ifndef OK_VALUE_HPP
#define OK_VALUE_HPP

#include "operator.hpp"
#include "utility.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace ok
{
  class object;
  class function_object;
  class closure_object;
  struct value_t;
  typedef value_t (*native_function)(uint8_t argc, value_t* argv);

  enum class value_type : uint8_t // need this for stable hash not as storage optimization
  {
    bool_val,
    null_val,
    number_val,
    object_val,
  };

  enum class value_error
  {
    undefined_operation,
    division_by_zero,
    arguments_mismatch,
  };

  struct value_t
  {
    value_type type;
    union
    {
      bool boolean;
      double number;
      object* obj;
    } as;

    explicit value_t(bool p_bool);

    explicit value_t(double p_number);
    explicit value_t();

    explicit value_t(object* p_object);
    explicit value_t(const char* p_str, size_t p_length);
    explicit value_t(std::string_view p_str);

    explicit value_t(function_object* p_function);
    explicit value_t(native_function p_native_function);
    explicit value_t(closure_object* p_closure);
  };

  using value_array = std::vector<value_t>;

  constexpr uint32_t _make_value_key(const value_type p_lhs, const operator_type p_operator, const value_type p_rhs)
  {
    return static_cast<uint32_t>(to_utype(p_lhs)) << 16 | static_cast<uint32_t>(to_utype(p_operator)) << 8 |
           static_cast<uint32_t>(to_utype(p_rhs));
  }

  constexpr uint32_t _make_value_key(const operator_type p_operator, const value_type p_operand)
  {
    return static_cast<uint32_t>(to_utype(p_operator)) << 8 | static_cast<uint32_t>(to_utype(p_operand));
  }

} // namespace ok

#endif // OK_VALUE_HPP