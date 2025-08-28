#include "object_store.hpp"
#include "object.hpp"

namespace ok
{
  std::unordered_map<uint32_t, object*> object_store::s_object_stores = {};

  bool object_store::insert(uint32_t id, object* obj)
  {
    auto ret = get_head(id);
    if(!ret.has_value())
      return false;

    auto head = ret.value();
    obj->next = head;
    head = obj;
    set_head(id, head);

    return true;
  }

  bool object_store::destroy(uint32_t id)
  {
    auto it = s_object_stores.find(id);
    std::println("destroy called id: {}, id state: {}", id, s_object_stores.end() == it ? "invalid" : "valid");
    if(s_object_stores.end() == it)
      return false;
    auto head = it->second;
    while(head != nullptr)
    {
      std::println("starting to destroy object of type: {}", to_utype(head->type));
      auto next = head->next;
      delete head;
      head = next;
    }
    s_object_stores.erase(it);
    return true;
  }
} // namespace ok