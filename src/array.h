#ifndef OK_ARRAY_HPP
#define OK_ARRAY_HPP

#define OK_ARRAY_GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity) * 2)

#define OK_ARRAY_GROW(type, data, old_capacity, new_capacity) \
  (type*)reallocate(data, sizeof(type) * (old_capacity), sizeof(type) * (new_capacity))

#define OK_ARRAY_FREE(type, capacity, data) reallocate(data, sizeof(type) * (capacity), 0)

#define  OK_ARRAY_INIT(count, capacity, data) \
  do { \
    count = 0; \
    capacity = 0; \
    data = NULL; \
  } while(0)

#define OK_ARRAY_APPEND(type, count, capacity, data, element) \
  do { \
    if ((capacity) < (count) + 1) { \
      typeof(capacity) old_capacity = (capacity); \
      (capacity) = OK_ARRAY_GROW_CAPACITY(old_capacity); \
      (data) = OK_ARRAY_GROW(type, data, old_capacity, capacity); \
    } \
    (data)[count++] = (element); \
  } while(0)

#endif // OK_ARRAY_HPP
