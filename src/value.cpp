#include "value.hpp"
#include "object.hpp"
#include <print>

namespace ok
{
  constexpr uint32_t _make_key(const value_type p_lhs,
                               const operator_type p_operator,
                               const value_type p_rhs,
                               const object_type p_object = object_type::none)
  {
    return static_cast<uint32_t>(to_utype(p_lhs)) << 24 | static_cast<uint32_t>(to_utype(p_operator)) << 16 |
           static_cast<uint32_t>(to_utype(p_rhs)) << 8 | static_cast<uint32_t>(to_utype(p_object));
  }

  constexpr uint32_t
  _make_key(const operator_type p_operator, const value_type p_operand, const object_type p_object = object_type::none)
  {
    return static_cast<uint32_t>(to_utype(p_operator)) << 16 | static_cast<uint32_t>(to_utype(p_operand)) << 8 |
           static_cast<uint32_t>(to_utype(p_object));
  }

  constexpr value_error object_error_to_value_error(object_error p_error)
  {
    switch(p_error)
    {
    case object_error::undefined_operation:
      return value_error::undefined_operation;
    default:
      return value_error::undefined_operation;
    }
  }

  value_t::value_t(bool p_bool) : type(value_type::bool_val), as({.boolean = p_bool})
  {
  }

  value_t::value_t(double p_number) : type(value_type::number_val), as({.number = p_number})
  {
  }

  value_t::value_t() : type(value_type::null_val), as({.number = 0})
  {
  }

  value_t::value_t(const char* p_str, size_t p_length)
      : type(value_type::object_val), as({.obj = string_object::create(p_str)})
  {
  }

  value_t::value_t(object* p_object) : type(value_type::object_val), as({.obj = p_object})
  {
  }

  bool value_t::is(const value_type p_type) const
  {
    return p_type == type;
  }

  bool value_t::is_falsy() const
  {
    return type == value_type::null_val || (type == value_type::bool_val && !as.boolean) ||
           (type == value_type::number_val && as.number == 0);
  }

  std::expected<value_t, value_error> value_t::operator_prefix_unary(const operator_type p_operator) const
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

  std::expected<value_t, value_error> value_t::operator_infix_binary(const operator_type p_operator,
                                                                     const value_t p_other) const
  {
    auto key =
        _make_key(type, p_operator, p_other.type, type == value_type::object_val ? (as.obj->type) : object_type::none);
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
    case _make_key(value_type::object_val, operator_type::equal, value_type::object_val, object_type::obj_string):
    {
      auto ret = as.obj->equal(p_other.as.obj);
      if(ret.has_value())
        return ret.value();
      return std::unexpected(object_error_to_value_error(ret.error()));
    }
    case _make_key(value_type::object_val, operator_type::plus, value_type::object_val, object_type::obj_string):
    {
      auto ret = as.obj->plus(p_other.as.obj);
      if(ret.has_value())
        return ret.value();
      return std::unexpected(object_error_to_value_error(ret.error()));
    }
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

  void value_t::print() const
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
      break;
    case value_type::object_val:
      as.obj->print();
      break;
    default:
      std::print("{}", as.number); // TODO(Qais): print as pointer
      break;
    }
  }
} // namespace ok