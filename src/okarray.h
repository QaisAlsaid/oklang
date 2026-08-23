#ifndef OK_ARRAY_HPP
#define OK_ARRAY_HPP

#include <stdbool.h>
#include <string.h> // for memcpy

#include "okmm.h"

#define ARRAY_DEFAULT_TYPE_DEINIT(x)

#define ARRAY_DECLARE(name, type, size_type)                                                                           \
  typedef struct name name;                                                                                            \
  struct name {                                                                                                        \
    size_type count;                                                                                                   \
    size_type capacity;                                                                                                \
    type* data;                                                                                                        \
  };                                                                                                                   \
                                                                                                                       \
  void name##_init(name* p_array);                                                                                     \
  void name##_deinit(name* p_array);                                                                                   \
  bool name##_grow(name* p_array, const size_type p_new_capacity);                                                     \
  bool name##_append(name* p_array, type p_element);                                                                   \
  bool name##_append_n(name* p_array, const type* p_elements, const size_type p_elements_count);                       \
  bool name##_remove(name* p_array, const size_type p_first, const size_type p_last);

#define ARRAY_DEFINE(name, type, size_type, size_type_max, deinit_type)                                                \
  void name##_init(name* p_array) {                                                                                    \
    p_array->count = 0;                                                                                                \
    p_array->capacity = 0;                                                                                             \
    p_array->data = NULL;                                                                                              \
  }                                                                                                                    \
                                                                                                                       \
  void name##_deinit(name* p_array) {                                                                                  \
    for (size_type i = 0; i < p_array->count; ++i) {                                                                   \
      deinit_type(&p_array->data[i]);                                                                                  \
    }                                                                                                                  \
    reallocate(p_array->data, sizeof(type) * p_array->capacity, 0);                                                    \
    name##_init(p_array);                                                                                              \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_grow(name* p_array, const size_type p_new_capacity) {                                                    \
    if (p_new_capacity == p_array->capacity) {                                                                         \
      if (p_array->capacity == p_array->count) {                                                                       \
        return false;                                                                                                  \
      }                                                                                                                \
      return true;                                                                                                     \
    }                                                                                                                  \
    type* ptr = (type*)reallocate(p_array->data, sizeof(type) * p_array->capacity, sizeof(type) * p_new_capacity);     \
    if (ptr == NULL) {                                                                                                 \
      return false;                                                                                                    \
    }                                                                                                                  \
    p_array->data = ptr;                                                                                               \
    p_array->capacity = p_new_capacity;                                                                                \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_append(name* p_array, type p_element) {                                                                  \
    if (p_array->capacity < p_array->count + 1) {                                                                      \
      const size_t new_capacity = array_grow_capacity(p_array->capacity, 0, size_type_max);                            \
      if (!name##_grow(p_array, (size_type)new_capacity)) {                                                            \
        return false;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
    p_array->data[p_array->count] = p_element;                                                                         \
    p_array->count++;                                                                                                  \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_append_n(name* p_array, const type* p_elements, const size_type p_elements_count) {                      \
    if (p_array->capacity < p_array->count + p_elements_count) {                                                       \
      const size_t new_capacity = array_grow_capacity(p_array->capacity, p_elements_count, size_type_max);             \
      if (!name##_grow(p_array, (size_type)new_capacity)) {                                                            \
        return false;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
    memcpy(p_array->data + p_array->count,                                                                             \
           p_elements,                                                                                                 \
           p_elements_count > size_type_max ? size_type_max : sizeof(type) * p_elements_count);                        \
    p_array->count += p_elements_count;                                                                                \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_remove(name* p_array, const size_type p_first, const size_type p_last) {                                 \
    if (p_first >= p_array->count || p_last >= p_array->count || p_last < p_first) {                                   \
      return false;                                                                                                    \
    }                                                                                                                  \
    const size_type rmcount = p_last - p_first + 1;                                                                    \
    const size_type diff = p_array->count - p_last - 1;                                                                \
    memmove(p_array->data + p_first, p_array->data + p_last + 1, sizeof(type) * diff);                                 \
    p_array->count -= rmcount;                                                                                         \
    for (size_type i = p_array->count + p_first; i < p_array->count + p_last + 1; i++) {                               \
      deinit_type(&p_array->data[i]);                                                                                  \
    }                                                                                                                  \
    return true;                                                                                                       \
  }

static inline size_t array_grow_capacity(size_t p_capacity, size_t p_min, size_t p_size_max) {
  size_t calc_cap = p_capacity < 8 ? 8 : p_capacity * 2;
  calc_cap = calc_cap <= p_capacity + p_min ? calc_cap + p_min + 8 : calc_cap;
  return calc_cap <= p_size_max ? calc_cap : p_size_max;
}

#endif // OK_ARRAY_HPP
