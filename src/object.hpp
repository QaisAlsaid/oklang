#ifndef OK_OBJECT_HPP
#define OK_OBJECT_HPP

// #include "object_store.hpp"
#include "value.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <new>
#include <print>
#include <span>
#include <string_view>
#include <vector> // for hash

namespace ok
{
  enum class object_type : uint8_t
  {
    none, // compatiblity with _make_key for non object type
    obj_string,
  };

  enum class object_error
  {
    undefined_operation,
  };

  // TODO(Qais) no need for virtual table for operations such as print and equal etc.. since we are gonna roll our own
  // anyways, so this is wasteful having to do 2 indirections instead of one!
  // also rethink the c++ inheritance and use c style object composition instead, because we are gonna use this
  // anyways for class objects that are defined at runtime
  struct object
  {
    object_type type;
    object* next = nullptr;

    object(object_type p_type, uint32_t p_vm_id);

    virtual ~object() = default;

    virtual void print() const
    {
      std::print("{:p}", (void*)this);
    };

    virtual std::expected<value_t, object_error> equal(object* p_other, uint32_t p_vm_id) const
    {
      return value_t{this == p_other};
    }

    virtual std::expected<value_t, object_error> plus(object* p_other, uint32_t p_vm_id) const
    {
      return std::unexpected(object_error::undefined_operation);
    }
  };

  struct string_object : public object
  {
    std::string* string = nullptr;
    bool is_dummy = false; // TODO(Qais): this is shit i dont like this i dont like wasting memory on this fix it

    string_object(const std::string_view p_src, uint32_t p_vm_id);

    string_object(const std::span<std::string_view> p_sources, uint32_t p_vm_id);

    void print() const override
    {
      std::print("{}", *string);
    }

    virtual std::expected<value_t, object_error> equal(object* p_other, uint32_t p_vm_id) const override
    {
      if(type == p_other->type)
      {
        auto other_string = (string_object*)p_other;
        return value_t{string == other_string->string}; // pointer comparision on strings because its interned
      }
      return std::unexpected(object_error::undefined_operation);
    }

    virtual std::expected<value_t, object_error> plus(object* p_other, uint32_t p_vm_id) const override
    {
      if(type == p_other->type)
      {
        auto other_string = (string_object*)p_other;
        std::string_view temp_arr[2] = {*string, *other_string->string};
        return value_t{create(temp_arr, p_vm_id)};
      }
      return std::unexpected(object_error::undefined_operation);
    }

    static string_object* create(const std::string_view p_src, uint32_t p_vm_id);

    // TODO(Qais): fix this one, (too lazy to do anything rn)
    static string_object* create(const std::span<std::string_view> p_sources, uint32_t p_vm_id)
    {
      return new string_object(p_sources, p_vm_id);
    }

    // fake(shallow copying) constructor for hash table hashing DO NOT USE
    explicit string_object(std::string* p_str) : object(object_type::obj_string, 0), is_dummy(true)
    {
      string = p_str;
    }
  };

  // // TODO(Qais): cleanup + remove unused + support resize
  // struct string_object : public object
  // {
  //   // create a string object (allocates char buffer on heap)
  //   string_object(size_t p_length, const char* p_src, uint32_t p_vm_id)
  //       : object(object_type::obj_string, p_vm_id), length(p_length)
  //   {
  //     chars = new char[p_length + 1];
  //     strncpy(chars, p_src, p_length);
  //     chars[p_length] = '\0';
  //   }

  //   // // create a string object from pre allocated char buffer
  //   // string_object(size_t p_length, const char* p_src, void* p_mem, uint32_t p_vm_id)
  //   //     : object(object_type::obj_string, p_vm_id), length(p_length)
  //   // {
  //   //   chars = (char*)p_mem;
  //   //   strncpy(chars, p_src, p_length);
  //   // }

  //   // // create a string object from pre allocated char buffer, from an array of sources
  //   // string_object(size_t p_length, std::vector<std::pair<size_t, const char*>> p_srcs, void* p_mem, uint32_t
  //   p_vm_id)
  //   //     : object(object_type::obj_string, p_vm_id), length(p_length)
  //   // {
  //   //   chars = (char*)p_mem;
  //   //   auto cursor = chars;
  //   //   for(auto i = 0; i < p_srcs.size(); ++i)
  //   //   {
  //   //     auto src = p_srcs[i].second;
  //   //     auto len = p_srcs[i].first;
  //   //     strncpy(cursor, src, len);
  //   //     cursor = cursor + len;
  //   //   }
  //   // }

  //   ~string_object()
  //   {
  //     // std::println("will delete chars: {}", chars);
  //     delete[] chars;
  //     chars = nullptr;
  //   }

  //   // create string object with char buffer right after the object
  //   // no, bad idea resize and destroy will be broken, also its how all strings are implemented so dont play smart!
  //   static string_object* create(size_t p_length, const char* p_src, uint32_t p_vm_id)
  //   {
  //     // auto mem = malloc(sizeof(string_object) + p_length);
  //     // new(mem) string_object(p_length, p_src, (uint8_t*)mem + sizeof(string_object), p_vm_id);
  //     // return (string_object*)mem;
  //     return new string_object(p_length, p_src, p_vm_id);
  //   }

  //   static string_object* create(std::vector<std::pair<size_t, const char*>> p_srcs, uint32_t p_vm_id)
  //   {
  //     auto len = std::accumulate(
  //         p_srcs.begin(), p_srcs.end(), size_t{0}, [](const auto acc, const auto& entry) { return acc + entry.first;
  //         });

  //     auto mem = (char*)malloc(len);
  //     auto curr = mem;
  //     for(auto src : p_srcs)
  //     {
  //       strncpy(curr, src.second, src.first);
  //       curr += src.first;
  //     }
  //     // auto mem = malloc(sizeof(string_object) + len);
  //     // new(mem) string_object(len, p_srcs, (uint8_t*)mem + sizeof(string_object), p_vm_id);
  //     // return (string_object*)mem;
  //     return create(len, mem, p_vm_id);
  //   }

  //   void print() const override
  //   {
  //     std::print("{}", std::string_view{chars, length});
  //   }

  //   virtual std::expected<value_t, object_error> equal(object* p_other, uint32_t p_vm_id) const override
  //   {
  //     if(type == p_other->type)
  //     {
  //       auto other_string = (string_object*)p_other;
  //       return value_t{length == other_string->length && strncmp(chars, other_string->chars, length)};
  //     }
  //     return std::unexpected(object_error::undefined_operation);
  //   }

  //   virtual std::expected<value_t, object_error> plus(object* p_other, uint32_t p_vm_id) const override
  //   {
  //     if(type == p_other->type)
  //     {
  //       auto other_string = (string_object*)p_other;
  //       return value_t{create({{length, chars}, {other_string->length, other_string->chars}}, p_vm_id)};
  //     }
  //     return std::unexpected(object_error::undefined_operation);
  //   }

  //   size_t length;
  //   char* chars;
  // };

} // namespace ok

namespace std
{
  template <>
  struct hash<ok::string_object>
  {
    size_t operator()(const ok::string_object& str) const
    {
      return hash<std::string>{}(*str.string);
    }
  };
  template <>
  struct hash<ok::string_object*>
  {
    size_t operator()(const ok::string_object* str) const
    {
      return hash<std::string>{}(*str->string);
    }
  };
} // namespace std

#endif // OK_OBJECT_HPP