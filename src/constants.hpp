#ifndef OK_CONSTANTS_HPP
#define OK_CONSTANTS_HPP

#include <string_view>
namespace ok::constants
{
  constexpr std::string_view init_string_literal = "ctor";
  constexpr std::string_view deinit_string_literal = "dtor";

} // namespace ok::constants

#endif // OK_CONSTANTS_HPP