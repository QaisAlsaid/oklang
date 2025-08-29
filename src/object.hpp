#ifndef OK_OBJECT_HPP
#define OK_OBJECT_HPP

#include "utility.hpp"
#include "value.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <new>
#include <numeric>
#include <print>
#include <span>
#include <string_view>
#include <vector> // for hash

namespace ok
{
  enum class object_type : uint8_t // TODO(Qais): for now its uint8_t and enum class, you should do this refactor asap
                                   // before its too hard
  {
    none, // compatiblity with _make_object_key
    obj_string,
  };

  // TODO(Qais) no need for virtual table for operations such as print and equal etc.. since we are gonna roll our own
  // anyways, so this is wasteful having to do 2 indirections instead of one!
  // also rethink the c++ inheritance and use c style object composition instead, because we are gonna use this
  // anyways for class objects that are defined at runtime
  struct object
  {
    object_type type; // TODO(Qais): maybe store as 32 bit integer, and also steal the 1 bit boolean form here, so it
                      // will be 24bit integer which is exactly what the _make_object_key function caps the limit to
    bool is_registered = false; // to minimize lookups, we just check a flag => faster
    object* next = nullptr;
    object(object_type p_type);
  };

  //   //TODO(Qais): kill all of those
  //   virtual void print() const
  //   {
  //     std::print("{:p}", (void*)this);
  //   };
  //   virtual std::expected<value_t, object_error> equal(object* p_other) const
  //   {
  //     return value_t{this == p_other};
  //   }

  //   virtual std::expected<value_t, object_error> plus(object* p_other) const
  //   {
  //     return std::unexpected(object_error::undefined_operation);
  //   }
  // };

  // struct string_object : public object
  // {
  //   std::string* string = nullptr;
  //   bool is_dummy = false; // TODO(Qais): this is shit i dont like this i dont like wasting memory on this fix it

  //   string_object(const std::string_view p_src, uint32_t p_vm_id);

  //   string_object(const std::span<std::string_view> p_sources, uint32_t p_vm_id);

  //   void print() const override
  //   {
  //     std::print("{}", *string);
  //   }

  //   virtual std::expected<value_t, object_error> equal(object* p_other, uint32_t p_vm_id) const override
  //   {
  //     if(type == p_other->type)
  //     {
  //       auto other_string = (string_object*)p_other;
  //       return value_t{string == other_string->string}; // pointer comparision on strings because its interned
  //     }
  //     return std::unexpected(object_error::undefined_operation);
  //   }

  //   virtual std::expected<value_t, object_error> plus(object* p_other, uint32_t p_vm_id) const override
  //   {
  //     if(type == p_other->type)
  //     {
  //       auto other_string = (string_object*)p_other;
  //       std::string_view temp_arr[2] = {*string, *other_string->string};
  //       return value_t{create(temp_arr, p_vm_id)};
  //     }
  //     return std::unexpected(object_error::undefined_operation);
  //   }

  //   static string_object* create(const std::string_view p_src, uint32_t p_vm_id);

  //   // TODO(Qais): fix this one, (too lazy to do anything rn)
  //   static string_object* create(const std::span<std::string_view> p_sources, uint32_t p_vm_id)
  //   {
  //     return new string_object(p_sources, p_vm_id);
  //   }

  //   // fake(shallow copying) constructor for hash table hashing DO NOT USE
  //   explicit string_object(std::string* p_str) : object(object_type::obj_string, 0), is_dummy(true)
  //   {
  //     string = p_str;
  //   }
  // };

  // TODO(Qais): cleanup + remove unused + support resize
  struct string_object
  {
    // create a string object (allocates char buffer on heap)
    string_object(const std::string_view p_src);

    // // create a string object from pre allocated char buffer
    // string_object(size_t p_length, const char* p_src, void* p_mem, uint32_t p_vm_id)
    //     : object(object_type::obj_string, p_vm_id), length(p_length)
    // {
    //   chars = (char*)p_mem;
    //   strncpy(chars, p_src, p_length);
    // }

    // // create a string object from pre allocated char buffer, from an array of sources
    // string_object(size_t p_length, std::vector<std::pair<size_t, const char*>> p_srcs, void* p_mem, uint32_t
    // p_vm_id)
    //     : object(object_type::obj_string, p_vm_id), length(p_length)
    // {
    //   chars = (char*)p_mem;
    //   auto cursor = chars;
    //   for(auto i = 0; i < p_srcs.size(); ++i)
    //   {
    //     auto src = p_srcs[i].second;
    //     auto len = p_srcs[i].first;
    //     strncpy(cursor, src, len);
    //     cursor = cursor + len;
    //   }
    // }

    string_object(std::span<std::string_view> p_srcs);

    ~string_object()
    {
      // std::println("will delete chars: {}", chars);
      delete[] chars;
      chars = nullptr;
    }

    // create string object with char buffer right after the object
    // no, bad idea resize and destroy will be broken, also its how all strings are implemented so dont play smart!
    static object* create(const std::string_view p_src);
    static object* create(const std::span<std::string_view> p_srcs);

    // void print() const override
    // {
    //   std::print("{}", std::string_view{chars, length});
    // }

    // virtual std::expected<value_t, object_error> equal(object* p_other) const override
    // {
    //   if(type == p_other->type)
    //   {
    //     auto other_string = (string_object*)p_other;
    //     return value_t{this == p_other}; // pointer compare, interning deals with this
    //   }
    //   return std::unexpected(object_error::undefined_operation);
    // }

    // virtual std::expected<value_t, object_error> plus(object* p_other) const override
    // {
    //   if(type == p_other->type)
    //   {
    //     auto other_string = (string_object*)p_other;
    //     std::array<std::string_view, 2> arr = {std::string_view{chars, length},
    //                                            {other_string->chars, other_string->length}};
    //     return value_t{create(arr)};
    //   }
    //   return std::unexpected(object_error::undefined_operation);
    // }

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

  // // TODO(Qais): this is shit, you cant have more than 256 objects
  // constexpr uint32_t _make_object_key(object_type p_lhs,
  //                                     const operator_type p_operator,
  //                                     const value_type p_rhs,
  //                                     const object_type p_other_object_type = object_type::none)
  // {
  //   // ill try a -maybe stupid- idea to make object cap at least of uint16_t size which is plenty of objects
  //   // TODO(Qais): validate the sanity of this thing, also object_type shouldnt be an enum because its runtime
  //   dependant
  //   // for most cases!
  //   if(p_other_object_type == object_type::none)
  //   {
  //     return static_cast<uint32_t>(to_utype(p_lhs)) << 24 | static_cast<uint32_t>(to_utype(p_operator)) << 16 |
  //            static_cast<uint32_t>(to_utype(p_rhs)) | 8;
  //   }
  //   else
  //   {
  //     return static_cast<uint32_t>(to_utype(p_lhs)) << 24 | static_cast<uint32_t>(to_utype(p_operator)) << 16 |
  //            static_cast<uint32_t>(to_utype(p_other_object_type));
  //   }
  // }

  // this is much better an leaves a 24bit integer room for objects which is more than we ever will need
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