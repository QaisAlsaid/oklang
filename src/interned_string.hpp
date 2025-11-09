#ifndef OK_INTERNED_STRING_HPP
#define OK_INTERNED_STRING_HPP

#include "object.hpp"
#include "vm_stack.hpp"
#include <cassert>

namespace ok
{
  class interned_string
  {
  public:
    interned_string();
    ~interned_string();

    // the whole idea is not to construct new string_object just for the check, thus we hash and compare against raw
    // string hashes, not sure if there is a flaw here but lets test it ig
    string_object* set(const std::string_view p_str);

    // TODO(Qais): hash compare, thus take a string_object instead of std::string_view,
    // or support both [slow_path, hot_path]
    string_object* get(const std::string_view p_str);

    inline std::unordered_map<hashed_string, string_object*>& underlying()
    {
      return interned_strings;
    }

  private:
    std::unordered_map<hashed_string, string_object*> interned_strings;
  };

} // namespace ok

#endif // OK_INTERNED_STRING_HPP