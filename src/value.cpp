#include "value.hpp"
#include <print>

namespace ok
{
  void value_type::print() const
  {
    std::print("{}", m_value);
  }
} // namespace ok