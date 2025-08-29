#ifndef OK_INTERNED_STRING_HPP
#define OK_INTERNED_STRING_HPP

#include "object.hpp"
#include <cassert>

namespace ok
{
  class interned_string
  {
  public:
    interned_string() = default;
    ~interned_string()
    {
      // no need to free since we already do so using the linked list (gc in future)
      // for(auto ptr : interned_strings)
      //   delete ptr;
    }

    // the whole idea is not to construct new string_object just for the check, thus we hash and compare against raw
    // string hashes, not sure if there is a flaw here but lets test it ig
    string_object* set(const std::string_view p_str)
    {
      auto hs = hash(p_str.data());
      auto it = interned_strings.find(hs);
      if(interned_strings.end() != it)
        return nullptr;
      auto* real = new string_object{p_str};
      assert(real->hash_code == hs);
      interned_strings[hs] = real;
      return real;
    }

    // TODO(Qais): hash compare, thus take an string_object instead of std::string_view,
    // or support both [slow_path, hot_path]
    string_object* get(const std::string_view p_str)
    {
      auto it = interned_strings.find(hash(p_str.data()));
      if(interned_strings.end() == it)
        return nullptr;
      return it->second;
    }

  private:
    std::unordered_map<hashed_string, string_object*> interned_strings;
  };

} // namespace ok

#endif // OK_INTERNED_STRING_HPP