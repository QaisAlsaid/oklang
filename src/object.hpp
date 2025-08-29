#ifndef OK_OBJECT_HPP
#define OK_OBJECT_HPP

#include "utility.hpp"
#include "value.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

namespace ok
{
  enum class object_type : uint8_t // TODO(Qais): for now its uint8_t and enum class, you should do this refactor asap
                                   // before its too hard
  {
    none, // compatiblity with _make_object_key
    obj_string,
  };

  struct object
  {
    object_type type; // TODO(Qais): maybe store as 32 bit integer, and also steal the 1 bit boolean form here, so it
                      // will be 24bit integer which is exactly what the _make_object_key function caps the limit to
    bool is_registered = false; // to minimize lookups, we just check a flag => faster
    object* next = nullptr;
    object(object_type p_type);
  };

  struct string_object
  {
    // create a string object (allocates char buffer on heap)
    string_object(const std::string_view p_src);
    // not implemented!
    string_object(std::span<std::string_view> p_srcs);

    ~string_object();

    static object* create(const std::string_view p_src);
    static object* create(const std::span<std::string_view> p_srcs);

    object up;
    hashed_string hash_code;
    size_t length;
    char* chars;

  private:
    string_object();

  private:
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_string);
    static std::expected<value_t, value_error> plus(object* p_this, value_t p_sure_is_string);
    static void print(object* p_this);
  };

  // leaves a 24bit integer room for objects which is more than we ever will need
  constexpr uint32_t _make_object_key(const operator_type p_operator,
                                      const value_type p_rhs,
                                      const object_type p_other_object_type = object_type::none)
  {
    // TODO(Qais): validate the sanity of this thing, also object_type shouldnt be an enum because its runtime dependant
    if(p_other_object_type == object_type::none)
    {
      return static_cast<uint32_t>(to_utype(p_operator)) << 24 | static_cast<uint32_t>(to_utype(p_rhs)) | 16;
    }
    else
    {
      return static_cast<uint32_t>(to_utype(p_operator)) << 24 | static_cast<uint32_t>(to_utype(p_other_object_type));
    }
  }
} // namespace ok

namespace std
{
  template <>
  struct hash<ok::string_object>
  {
    size_t operator()(const ok::string_object& str) const
    {
      return hash<std::string_view>{}({str.chars, str.length});
    }
  };
  template <>
  struct hash<ok::string_object*>
  {
    size_t operator()(const ok::string_object* str) const
    {
      return hash<std::string_view>{}({str->chars, str->length});
    }
  };
} // namespace std

#endif // OK_OBJECT_HPP