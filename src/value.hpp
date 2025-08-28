#ifndef OK_VALUE_HPP
#define OK_VALUE_HPP

#include "operator.hpp"
#include "utility.hpp"
#include <cstdint>
#include <expected>
#include <print>
#include <vector>

namespace ok
{
  class object;
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

    explicit value_t(const char* p_str, size_t p_length, uint32_t p_vm_id);
    explicit value_t(object* p_object);

    bool is(const value_type p_type) const;
    void print() const;
    bool is_falsy() const;

    // we wont use c++ operator overload so it doesnt look out of place if introduce functions for operators not
    // supported by c++
    std::expected<value_t, value_error> operator_prefix_unary(const operator_type p_operator) const;

    std::expected<value_t, value_error>
    operator_infix_binary(const operator_type p_operator, const value_t p_other, uint32_t p_vm_id) const;

  private:
  };
  using value_array = std::vector<value_t>;
} // namespace ok

#endif // OK_VALUE_HPP