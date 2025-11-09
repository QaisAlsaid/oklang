#ifndef OK_GC_HPP
#define OK_GC_HPP

#include "macros.hpp"
#include "utility.hpp"
#include "value.hpp"
#include <cstddef>
#include <unordered_map>

namespace ok
{
  class chunk;
  class gc
  {
  public:
    void collect();

    void increment_used_memory(size_t p_by);

    size_t get_used_memory() const
    {
      return m_used_memory;
    }

    void guard_value(value_t p_value)
    {
      m_keep.push_back(p_value);
    }

    void letgo_value()
    {
      m_keep.pop_back();
    }

    void pause()
    {
      m_is_paused = true;
    }

    void resume()
    {
      m_is_paused = false;
    }

  private:
    void mark_roots();
    void mark_compiler_roots();
    void trace_references();
    void mark_value(value_t p_value);
    void mark_object(object* p_object);
    void mark_hashtable(const std::unordered_map<string_object*, value_t>& p_table);
    void trace_object_references(object* p_object);
    void mark_chunk(chunk& p_chunk);
    void mark_array(value_array& p_array);
    void remove_ghost_references(std::unordered_map<hashed_string, string_object*>& p_table);
    void sweep();

  private:
    size_t m_used_memory = 0;
    size_t m_next = 1024 * 1024; // 1mb
    bool m_is_paused = false;
    std::vector<object*> m_gray;
    std::vector<value_t> m_keep;
    static constexpr size_t s_grow_factor = 2;
  };
} // namespace ok

#endif // OK_GC_HPP