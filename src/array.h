#ifndef OK_ARRAY_HPP
#define OK_ARRAY_HPP

#include "mm.h"
#include <string.h> // for memcpy

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
  bool name##_append(name* p_array, const type p_element);                                                             \
  bool name##_append_n(name* p_array, const type* p_elements, const size_type p_elements_count);                       \
  bool name##_remove(name* p_array, const size_type p_first, const size_type p_last);

#define ARRAY_DEFINE(name, type, size_type)                                                                            \
  void name##_init(name* p_array) {                                                                                    \
    p_array->count = 0;                                                                                                \
    p_array->capacity = 0;                                                                                             \
    p_array->data = NULL;                                                                                              \
  }                                                                                                                    \
                                                                                                                       \
  void name##_deinit(name* p_array) {                                                                                  \
    reallocate(p_array->data, sizeof(type) * p_array->capacity, 0);                                                    \
    name##_init(p_array);                                                                                              \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_grow(name* p_array, const size_type p_new_capacity) {                                                    \
    type* ptr = (type*)reallocate(p_array->data, sizeof(type) * p_array->capacity, sizeof(type) * p_new_capacity);     \
    if (ptr == NULL) {                                                                                                 \
      return false;                                                                                                    \
    }                                                                                                                  \
    p_array->data = ptr;                                                                                               \
    p_array->capacity = p_new_capacity;                                                                                \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_append(name* p_array, const type p_element) {                                                            \
    if (p_array->capacity < p_array->count + 1) {                                                                      \
      if (!name##_grow(p_array, array_grow_capacity(p_array->capacity, 0))) {                                          \
        return false;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
    p_array->data[p_array->count++] = p_element;                                                                       \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_append_n(name* p_array, const type* p_elements, const size_type p_elements_count) {                      \
    if (p_array->capacity < p_array->count + p_elements_count + 1) {                                                   \
      if (!name##_grow(p_array, array_grow_capacity(p_array->capacity, p_elements_count))) {                           \
        return false;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
    memcpy(p_array->data + p_array->count, p_elements, p_elements_count);                                              \
    p_array->count += p_elements_count;                                                                                \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_remove(name* p_array, const size_type p_first, const size_type p_last) {                                 \
    if (p_first >= p_array->count || p_last < p_first) {                                                               \
      return false;                                                                                                    \
    }                                                                                                                  \
    const size_type count = p_last - p_first + 1;                                                                      \
    const size_type new_capacity = p_array->capacity - count;                                                          \
    const size_type new_count = p_array->count - count;                                                                \
    type* ptr = NULL;                                                                                                  \
    ptr = (type*)reallocate(ptr, sizeof(type) * p_array->capacity, sizeof(type) * new_capacity);                       \
    if (ptr == NULL) {                                                                                                 \
      return false;                                                                                                    \
    }                                                                                                                  \
    memcpy(ptr, p_array->data, p_first);                                                                               \
    memcpy(ptr, p_array->data + p_last + 1, p_array->count - p_last);                                                  \
    p_array->count = new_count;                                                                                        \
    p_array->capacity = new_capacity;                                                                                  \
    return true;                                                                                                       \
  }

static inline size_t array_grow_capacity(size_t p_capacity, size_t p_min) {
  return p_capacity < 8 ? 8 : p_capacity * 2 > p_capacity + p_min ? p_capacity * 2 : p_capacity + p_min + 8;
}

#endif // OK_ARRAY_HPP
