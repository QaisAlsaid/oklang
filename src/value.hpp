#ifndef OK_VALUE_HPP
#define OK_VALUE_HPP

#include "copy.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

#define OK_VALUE_TYPE(v) (v.type)

#define OK_IS_VALUE_BOOL(v) ((v).type == value_type::bool_val)
#define OK_IS_VALUE_NULL(v) ((v).type == value_type::null_val)
#define OK_IS_VALUE_NUMBER(v) ((v).type == value_type::number_val)
#define OK_IS_VALUE_NATIVE_FUNCTION(v) ((v).type == value_type::native_function_val)
// #define OK_IS_VALUE_NATIVE_METHOD(v) ((v).type == value_type::native_method_val)
#define OK_IS_VALUE_OBJECT(v) ((v).type == value_type::object_val)

#define OK_VALUE_AS_BOOL(v) (v).as.boolean
#define OK_VALUE_AS_NUMBER(v) (v).as.number
#define OK_VALUE_AS_NATIVE_FUNCTION(v) ((native_function)(v).as.pointer)
// #define OK_VALUE_AS_NATIVE_METHOD(v) ((native_function)(v).as.pointer) // alias
#define OK_VALUE_AS_OBJECT(v) ((object*)(v).as.pointer)

namespace ok
{
  class object;
  class function_object;
  class closure_object;
  class string_object;
  class vm;
  struct value_t;
  // struct native_function_return_type;
  // struct native_method_return_type;

  // 3bits are avilable for this which allows up to 9 types
  enum class value_type : uint8_t // need this for stable hash not as storage optimization
  {
    bool_val = 0,
    null_val,
    number_val,
    native_function_val,
    object_val,
  };

  enum class value_error_code
  {
    ok,
    undefined_operation,
    division_by_zero,
    arguments_mismatch,
    call_error,
    internal_propagated_error,
    unknown_type,
    invalid_return_type,
    stackoverflow,
  };

  struct native_return_type;

  // argc number of passed arguments *it does not include the "this" pointer*
  typedef native_return_type (*native_function)(vm* p_vm, value_t p_this, uint8_t p_argc);
  // typedef native_method_return_type (*native_method)(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  template <typename T>
  concept has_object_header = requires(T t) {
    t.up;
    std::is_same_v<decltype(t.up), object>;
  };
  struct value_t
  {
    value_type type;
    union
    {
      bool boolean;
      double number;
      void* pointer;
    } as;

    explicit value_t(bool p_bool);

    explicit value_t(double p_number);
    explicit value_t();

    // explicit value_t(object* p_object);
    explicit value_t(const char* p_str, size_t p_length);
    explicit value_t(std::string_view p_str);

    explicit value_t(uint8_t p_arity, string_object* p_name);

    explicit value_t(native_function p_native_function, bool is_free_function);
    // explicit value_t(native_method p_native_method);

    // "copy" constructors wont invoke new allocation, rather they use existing one and wrap it in a value_t
    template <typename T>
      requires(has_object_header<T> || std::is_same_v<T, object>)
    explicit value_t(copy<T*> p_object) : type(value_type::object_val), as({.pointer = (object*)p_object.t})
    {
    }
  };

  using value_array = std::vector<value_t>;

  struct value_error
  {
    value_error_code code = value_error_code::ok;
    value_t payload{};
    bool recoverable = true;
  };

  enum class native_return_code : uint8_t
  {
    nrc_none = 0,
    nrc_return = 1 << 0,
    nrc_subcall = 1 << 1,
    nrc_error = 1 << 2,
    nrc_print_exit = 1 << 3,
    nrc_ok = nrc_return | nrc_subcall | nrc_print_exit,
  };

  struct native_return_type
  {
    native_return_code code;
    value_error error; // if type is nrtt_error
  };

  // constexpr uint64_t _make_value_key(uint32_t p_lhs_all, uint32_t p_rhs_all)
  // {
  //   return static_cast<uint32_t>(p_lhs_all) << 32 | static_cast<uint32_t>(p_rhs_all);
  // }

  // constexpr uint32_t _make_value_key(const operator_type p_operator, const value_type p_operand)
  // {
  //   return static_cast<uint32_t>(to_utype(p_operator)) << 8 | static_cast<uint32_t>(to_utype(p_operand));
  // }

} // namespace ok

#endif // OK_VALUE_HPP
