#ifndef OK_OBJECT_STORE_HPP
#define OK_OBJECT_STORE_HPP

#include "utility.hpp"
#include <expected>
#include <print>
#include <unordered_map>

namespace ok
{
  // is this the most optimal way? i dont think so
  // does it work? yessir -> keep it till it doesnt!
  struct object;
  class object_store
  {
  public:
    // initializes a store for [id]
    // do not use for insertion obviously
    // do not use without first making sure its empty or trying to destroy because it will leak previous list
    static void set_head(uint32_t id, object* head)
    {
      s_object_stores[id] = head;
    }

    static std::expected<object*, bool> get_head(uint32_t id)
    {
      auto it = s_object_stores.find(id);
      if(s_object_stores.end() != it)
        return it->second;
      return std::unexpected(false);
    }

    static bool insert(uint32_t id, object* obj);

    static bool destroy(uint32_t id);

  private:
    static std::unordered_map<uint32_t, object*> s_object_stores;
  };
} // namespace ok

#endif // OK_OBJECT_STORE_HPP