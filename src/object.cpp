#include "object.hpp"
#include "object_store.hpp"
#include <numeric>
#include <string>

namespace ok
{
  object::object(object_type p_type, uint32_t p_vm_id) : type(p_type)
  {
    auto ret = object_store::insert(p_vm_id, this);
    if(!ret)
    {
      std::println("vm id {}, object store is invalid", p_vm_id);
    }
  }

  // string_object::string_object(const std::string_view p_src, uint32_t p_vm_id)
  //     : object(object_type::obj_string, p_vm_id), string(new std::string{p_src})
  // {
  // }

  // // TODO(Qais): fix
  // string_object::string_object(const std::span<std::string_view> p_sources, uint32_t p_vm_id)
  //     : object(object_type::obj_string, p_vm_id), string(new std::string())
  // {
  //   auto len = std::accumulate(
  //       p_sources.begin(), p_sources.end(), size_t{0}, [](const auto sum, const auto src) { return sum + src.size();
  //       });
  //   string->reserve(len);
  //   // string.resize(len);
  //   for(const auto src : p_sources)
  //   {
  //     *string += src;
  //   }
  // }

  // string_object* string_object::create(const std::string_view p_src, uint32_t p_vm_id)
  // {
  //   auto vm_store = interned_strings_store::get_vm_interned(p_vm_id);
  //   if(vm_store == nullptr)
  //     return nullptr;
  //   // TODO(Qais): fix: bad call will create a string out of the view
  //   auto interned = vm_store->get(std::string{p_src});
  //   if(interned == nullptr)
  //     return vm_store->set(std::string{p_src}, p_vm_id);
  //   return interned;
  // }

  string_object::string_object(const std::string_view p_src, uint32_t p_vm_id)
      : object(object_type::obj_string, p_vm_id), length(p_src.size())
  {
    chars = new char[p_src.size() + 1];
    strncpy(chars, p_src.data(), length);
    chars[length] = '\0';
    hash_code = hash(chars);
  }

  string_object::string_object(std::span<std::string_view> p_srcs, uint32_t p_vm_id)
      : object(object_type::obj_string, p_vm_id)
  {

    hash_code = hash(chars);
  }

  string_object* string_object::create(const std::string_view p_src, uint32_t p_vm_id)
  {
    // auto mem = malloc(sizeof(string_object) + p_length);
    // new(mem) string_object(p_length, p_src, (uint8_t*)mem + sizeof(string_object), p_vm_id);
    // return (string_object*)mem;
    auto vm_store = interned_strings_store::get_vm_interned(p_vm_id);
    if(!vm_store)
      return nullptr;
    auto interned = vm_store->get(p_src);
    if(interned != nullptr)
      return interned;

    return vm_store->set(p_src, p_vm_id);
  }

  string_object* string_object::create(const std::span<std::string_view> p_srcs, uint32_t p_vm_id)
  {
    // TODO(Qais): double copy new ctor and method in interned store will mitigate that
    auto length = std::accumulate(
        p_srcs.begin(), p_srcs.end(), size_t{0}, [](const auto acc, const auto& entry) { return acc + entry.size(); });

    auto chars = new char[length + 1];
    auto curr = chars;
    for(auto src : p_srcs)
    {
      strncpy(curr, src.data(), src.size());
      curr += src.size();
    }
    chars[length] = '\0';
    auto vm_store = interned_strings_store::get_vm_interned(p_vm_id);
    if(!vm_store)
      return nullptr;
    auto interned = vm_store->get({chars, length});
    if(interned != nullptr)
      return interned;
    return vm_store->set({chars, length}, p_vm_id);
  }

} // namespace ok