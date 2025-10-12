#ifndef OK_VALUE_HPP
#define OK_VALUE_HPP

#include "copy.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

#define OK_IS_VALUE_BOOL(v) (v.type == value_type::bool_val)
#define OK_IS_VALUE_NULL(v) (v.type == value_type::null_val)
#define OK_IS_VALUE_NUMBER(v) (v.type == value_type::number_val)
#define OK_IS_VALUE_OBJECT(v) (v.type == value_type::object_val)

#define OK_VALUE_AS_BOOL(v) v.as.boolean
#define OK_VALUE_AS_NUMBER(v) v.as.number
#define OK_VALUE_AS_OBJECT(v) v.as.obj

namespace ok
{
  class object;
  class function_object;
  class closure_object;
  class string_object;
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
    call_error,
    internal_propagated_error,
    unknown_type,
    invalid_return_type,
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

    // explicit value_t(object* p_object);
    explicit value_t(const char* p_str, size_t p_length);
    explicit value_t(std::string_view p_str);

    explicit value_t(uint8_t p_arity, string_object* p_name);
    explicit value_t(native_function p_native_function);

    // "copy" constructors wont invoke new allocation, rather they use existing one and wrap it in a value_t
    explicit value_t(copy<object*> p_object);
    // explicit value_t(copy<function_object*> p_function);
    // explicit value_t(copy<closure_object*> p_closure);
    // explicit value_t
  };

  using value_array = std::vector<value_t>;

  constexpr uint64_t _make_value_key(uint32_t p_lhs_all, uint32_t p_rhs_all)
  {
    return static_cast<uint32_t>(p_lhs_all) << 32 | static_cast<uint32_t>(p_rhs_all);
  }

  constexpr uint32_t _make_value_key(const operator_type p_operator, const value_type p_operand)
  {
    return static_cast<uint32_t>(to_utype(p_operator)) << 8 | static_cast<uint32_t>(to_utype(p_operand));
  }

} // namespace ok

#endif // OK_VALUE_HPP