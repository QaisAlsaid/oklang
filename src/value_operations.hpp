#ifndef OK_VALUE_OPERATIONS_HPP
#define OK_VALUE_OPERATIONS_HPP

#include <expected>
#include <functional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ok
{
  template <typename T>
  concept is_hashable =
      requires(const T& t) { std::hash<T>{}(t) == std::is_convertible_v<decltype(std::hash<T>{}(t)), size_t>; };

  template <typename KeyType, typename ValueType, typename ReturnType, typename... Args>
    requires is_hashable<KeyType> && std::is_invocable_r_v<ReturnType, ValueType, Args...>
  class value_operations_base
  {
  public:
    using storage_type = std::unordered_map<KeyType, ValueType>;
    struct collision_policy_skip
    {
      using return_type = bool;
      return_type operator()(KeyType p_kt, ValueType p_vt, storage_type& p_store)
      {
        const auto& it = p_store.find(p_kt);
        if(p_store.end() != it)
          return false;
        p_store[p_kt] = p_vt;
        return true;
      }
    };
    struct collision_policy_overwrite
    {
      using return_type = void;
      return_type operator()(KeyType p_kt, ValueType p_vt, storage_type& p_store)
      {
        p_store[p_kt] = p_vt;
      }
    };

  public:
    void register_operations(std::span<std::pair<KeyType, ValueType>> span)
    {
      m_map.insert_range(span);
    }

    template <typename CollisionPolicy = collision_policy_skip>
    CollisionPolicy::return_type register_operation(KeyType p_kt, ValueType p_vt)
    {
      return CollisionPolicy{}(p_kt, p_vt, m_map);
    }

    std::expected<ReturnType, bool> call_operation(KeyType p_kt, Args&&... args)
    {
      const auto& it = m_map.find(p_kt);
      if(m_map.end() == it)
        return std::unexpected(false);
      return it->second(std::forward<Args>(args)...);
    }

  private:
    storage_type m_map;
  };
} // namespace ok

#endif // OK_VALUE_OPERATIONS_HPP