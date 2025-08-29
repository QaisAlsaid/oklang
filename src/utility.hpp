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

  using hashed_string = size_t;

  inline constexpr hashed_string fnv1a_hash(const char* str, size_t hash = 14695981039346656037ULL)
  {
    return (*str == 0) ? hash : fnv1a_hash(str + 1, (hash ^ static_cast<size_t>(*str)) * 1099511628211ULL);
  }

  inline constexpr hashed_string hash(const char* str)
  {
    return fnv1a_hash(str);
  }

  namespace stringliterals
  {
    inline constexpr hashed_string operator""_fnv1a_hs(const char* str, size_t size)
    {
      return fnv1a_hash(str);
    }

    inline constexpr hashed_string operator""_hs(const char* str, size_t size)
    {
      return hash(str);
    }
  } // namespace stringliterals
} // namespace ok

#endif // OK_UTILITY_HPP