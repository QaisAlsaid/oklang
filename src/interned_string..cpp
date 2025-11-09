#include "interned_string.hpp"
#include "object.hpp"
#include "vm.hpp"

namespace ok
{
  interned_string::interned_string()
  {
  }

  interned_string::~interned_string()
  {
  }

  string_object* interned_string::set(const std::string_view p_str)
  {
    auto hs = hash(p_str.data());
    auto it = interned_strings.find(hs);
    if(interned_strings.end() != it)
      return nullptr;
    auto* vm_ = get_g_vm();
    auto* real = new string_object{p_str,
                                   vm_->get_builtin_class(object_type::obj_string),
                                   vm_->get_objects_list()}; // exception to the creation rule
    // get_vm_gc().increment_used_memory(sizeof(string_object));
    assert(real->hash_code == hs);
    interned_strings[hs] = real;
    return real;
  }

  string_object* interned_string::get(const std::string_view p_str)
  {
    auto it = interned_strings.find(hash(p_str.data()));
    if(interned_strings.end() == it)
      return nullptr;
    return it->second;
  }
} // namespace ok