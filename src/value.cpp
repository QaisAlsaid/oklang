#include "value.hpp"
#include "object.hpp"
#include <print>

namespace ok
{
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
      : type(value_type::object_val), as({.obj = string_object::create({p_str, p_length})})
  {
  }

  value_t::value_t(std::string_view p_str) : type(value_type::object_val), as({.obj = string_object::create(p_str)})

  {
  }

  value_t::value_t(function_object* p_function) : type(value_type::object_val), as({.obj = (object*)p_function})
  {
  }

  value_t::value_t(native_function p_native_function)
      : type(value_type::object_val), as({.obj = native_function_object::create(p_native_function)})
  {
  }

  value_t::value_t(object* p_object) : type(value_type::object_val), as({.obj = p_object})
  {
  }
} // namespace ok