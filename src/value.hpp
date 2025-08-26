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
  enum class value_type : uint8_t // need this for stable hash not as storage optimization
  {
    bool_val,
    null_val,
    number_val,
  };

  enum class value_error
  {
    undefined_operation,
    division_by_zero,
  };

  constexpr uint32_t _make_key(const value_type p_lhs, const operator_type p_operator, const value_type p_rhs)
  {
    return static_cast<uint32_t>(to_utype(p_lhs)) << 16 | static_cast<uint32_t>(to_utype(p_operator)) << 8 |
           static_cast<uint32_t>(to_utype(p_rhs));
  }

  constexpr uint32_t _make_key(const operator_type p_operator, const value_type p_operand)
  {
    return static_cast<uint32_t>(to_utype(p_operator)) << 8 | static_cast<uint32_t>(to_utype(p_operand));
  }

  struct value_t
  {
    value_type type;
    union
    {
      bool boolean;
      double number;
    } as;

    explicit value_t(bool p_bool) : type(value_type::bool_val), as({.boolean = p_bool})
    {
    }

    explicit value_t(double p_number) : type(value_type::number_val), as({.number = p_number})
    {
    }

    explicit value_t() : type(value_type::null_val), as({.number = 0})
    {
    }

    bool is(const value_type p_type) const
    {
      return p_type == type;
    }

    void print() const
    {
      switch(type)
      {
      case value_type::number_val:
        std::print("{}", as.number);
        break;
      case value_type::bool_val:
        std::print("{}", as.boolean ? "true" : "false");
        break;
      case value_type::null_val:
        std::print("null");
      default:
        std::print("{}", as.number); // TODO(Qais): print as pointer
        break;
      }
    }

    bool is_falsy() const
    {
      return type == value_type::null_val || (type == value_type::bool_val && !as.boolean) ||
             (type == value_type::number_val && as.number == 0);
    }

    // we wont use c++ operator overload so it doesnt look out of place if introduce functions for operators not
    // supported by c++
    std::expected<value_t, value_error> operator_prefix_unary(const operator_type p_operator) const
    {
      auto key = _make_key(p_operator, type);
      switch(key)
      {
      case _make_key(operator_type::plus, value_type::number_val):
        return *this;
      case _make_key(operator_type::minus, value_type::number_val):
        return value_t{-as.number};
      case _make_key(operator_type::bang, value_type::bool_val):
        return value_t{!as.boolean};
      default:
        if(p_operator == operator_type::bang)
          return value_t{is_falsy()};
        return std::unexpected(value_error::undefined_operation);
      }
    }

    std::expected<value_t, value_error> operator_infix_binary(const operator_type p_operator,
                                                              const value_t p_other) const
    {
      auto key = _make_key(type, p_operator, p_other.type);
      switch(key)
      {
      case _make_key(value_type::number_val, operator_type::plus, value_type::number_val):
        return value_t{as.number + p_other.as.number};
      case _make_key(value_type::number_val, operator_type::minus, value_type::number_val):
        return value_t{as.number - p_other.as.number};
      case _make_key(value_type::number_val, operator_type::asterisk, value_type::number_val):
        return value_t{as.number * p_other.as.number};
      case _make_key(value_type::number_val, operator_type::slash, value_type::number_val):
      {
        if(p_other.as.number == 0)
          return std::unexpected(value_error::division_by_zero);
        return value_t{as.number / p_other.as.number};
      }
      case _make_key(value_type::number_val, operator_type::equal, value_type::number_val):
        return value_t{as.number == p_other.as.number};
      case _make_key(value_type::number_val, operator_type::bang_equal, value_type::number_val):
        return value_t{as.number != p_other.as.number};
      case _make_key(value_type::number_val, operator_type::less, value_type::number_val):
        return value_t{as.number < p_other.as.number};
      case _make_key(value_type::number_val, operator_type::greater, value_type::number_val):
        return value_t{as.number > p_other.as.number};
      case _make_key(value_type::number_val, operator_type::less_equal, value_type::number_val):
        return value_t{as.number <= p_other.as.number};
      case _make_key(value_type::number_val, operator_type::greater_equal, value_type::number_val):
        return value_t{as.number >= p_other.as.number};
      case _make_key(value_type::bool_val, operator_type::equal, value_type::bool_val):
        return value_t{as.boolean == p_other.as.boolean};
      case _make_key(value_type::bool_val, operator_type::bang_equal, value_type::bool_val):
        return value_t{as.boolean != p_other.as.boolean};
      case _make_key(value_type::null_val, operator_type::equal, value_type::null_val):
        return value_t{true};
      case _make_key(value_type::null_val, operator_type::bang_equal, value_type::null_val):
        return value_t{false};
      default:
      {
        if(p_operator == operator_type::equal)
        {
          return value_t{is_falsy() == p_other.is_falsy()};
        }
        else if(p_operator == operator_type::bang_equal)
        {
          return value_t{is_falsy() != p_other.is_falsy()};
        }
        return std::unexpected(value_error::undefined_operation);
      }
      }
    }

  private:
  };
  using value_array = std::vector<value_t>;
} // namespace ok

#endif // OK_VALUE_HPP