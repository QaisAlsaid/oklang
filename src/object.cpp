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

  string_object::string_object(const std::string_view p_src, uint32_t p_vm_id)
      : object(object_type::obj_string, p_vm_id), string(new std::string{p_src})
  {
  }

  // TODO(Qais): fix
  string_object::string_object(const std::span<std::string_view> p_sources, uint32_t p_vm_id)
      : object(object_type::obj_string, p_vm_id), string(new std::string())
  {
    auto len = std::accumulate(
        p_sources.begin(), p_sources.end(), size_t{0}, [](const auto sum, const auto src) { return sum + src.size(); });
    string->reserve(len);
    // string.resize(len);
    for(const auto src : p_sources)
    {
      *string += src;
    }
  }

  string_object* string_object::create(const std::string_view p_src, uint32_t p_vm_id)
  {
    auto vm_store = interned_strings_store::get_vm_interned(p_vm_id);
    if(vm_store == nullptr)
      return nullptr;
    // TODO(Qais): fix: bad call will create a string out of the view
    auto interned = vm_store->get(std::string{p_src});
    if(interned == nullptr)
      return vm_store->set(std::string{p_src}, p_vm_id);
    return interned;
  }

} // namespace ok