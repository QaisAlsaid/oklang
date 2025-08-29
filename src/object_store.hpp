#ifndef OK_OBJECT_STORE_HPP
#define OK_OBJECT_STORE_HPP

#include "object.hpp"
#include "utility.hpp"
#include <cassert>
#include <expected>
#include <memory>
#include <new>
#include <print>
#include <string>
#include <unordered_map>
#include <unordered_set>

// TODO(Qais): you might want to change this file's name since its got repurposed to store all tables

// TODO(Qais): dont use hash maps, or use them it but dont use mutexes, instead make it thread local
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
    string_object* set(const std::string_view p_str, uint32_t p_vm_id)
    {
      auto hs = hash(p_str.data());
      auto it = interned_strings.find(hs);
      if(interned_strings.end() != it)
        return nullptr;
      auto* real = new string_object{p_str, p_vm_id};
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

  class interned_strings_store
  {
  public:
    static interned_string* create_vm_interned(uint32_t p_vm_id)
    {
      auto ptr = new interned_string();
      s_store[p_vm_id] = std::unique_ptr<interned_string>(ptr);
      return ptr;
    }

    static interned_string* get_vm_interned(const uint32_t p_vm_id)
    {
      const auto& it = s_store.find(p_vm_id);
      if(s_store.end() == it)

        return nullptr;
      return it->second.get();
    }

    static bool destroy_vm_interned(uint32_t p_vm_id)
    {
      auto it = s_store.find(p_vm_id);
      if(s_store.end() == it)
        return false;
      s_store.erase(it);
      return true;
    }

  private:
    static std::unordered_map<uint32_t, std::unique_ptr<interned_string>> s_store;
  };
} // namespace ok
#endif // OK_OBJECT_STORE_HPP