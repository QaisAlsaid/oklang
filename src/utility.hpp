#ifndef OK_UTILITY_HPP
#define OK_UTILITY_HPP

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>
namespace ok
{
  template <typename scoped_enum>
    requires std::is_scoped_enum_v<scoped_enum>
  constexpr auto to_utype(scoped_enum e)
  {
    return static_cast<std::underlying_type_t<scoped_enum>>(e);
  }

  template <typename T, size_t N>
    requires std::is_integral_v<T> && (N <= sizeof(T))
  constexpr auto encode_int(T value)
  {
    std::vector<uint8_t> bytes(N);
    for(auto i = 0; i < N; ++i)
    {
      bytes[i] = ((value >> (8 * i)) & 0xff);
    }
    return bytes;
  }

  template <typename T, size_t N>
    requires std::is_integral_v<T> && (N <= sizeof(T))
  constexpr auto decode_int(const std::span<const uint8_t> data, size_t offset)
  {
    T result = 0;
    for(auto i = 0; i < N; ++i)
    {
      result |= static_cast<T>(data[offset + i]) << (8 * i);
    }
    return result;
  }
} // namespace ok

#endif // OK_UTILITY_HPP