#ifndef OK_ARRAY_HPP
#define OK_ARRAY_HPP

#define OK_ARRAY_GROW_CAPACITY(capacity, min) \
  ((capacity) < 8 ? 8 : (capacity) * 2 > (capacity) + (min) ? (capacity) * 2 : (min))

#define OK_ARRAY_GROW(type, data, old_capacity, new_capacity) \
  (type*)reallocate(data, sizeof(type) * (old_capacity), sizeof(type) * (new_capacity))

#define OK_ARRAY_FREE(type, capacity, data) reallocate(data, sizeof(type) * (capacity), 0)

#define  OK_ARRAY_INIT(count, capacity, data) \
  do { \
    count = 0; \
    capacity = 0; \
    data = NULL; \
  } while(0)

#define OK_ARRAY_APPEND(type, size_type, count, capacity, data, element) \
  do { \
    if ((capacity) < (count) + 1) { \
      size_type old_capacity = (capacity); \
      (capacity) = OK_ARRAY_GROW_CAPACITY(old_capacity, 0); \
      (data) = OK_ARRAY_GROW(type, data, old_capacity, capacity); \
    } \
    (data)[count++] = (element); \
  } while(0)

#define OK_ARRAY_APPEND_N(type, size_type, count, capacity, data, elements, elements_count) \
  do { \
    if ((capacity) < (count) + elements_count) { \
	size_type old_capacity = (capacity); \
	(capacity) = OK_ARRAY_GROW_CAPACITY(old_capacity, (elements_count)); \
	(data) = OK_ARRAY_GROW(type, data, old_capacity, capacity); \
    } \
    memcpy((void*)(uintptr_t)(data)[count], (const void*)(elements), elements_count); \
    count += (elements_count); \
  } while(0)
#endif // OK_ARRAY_HPP
