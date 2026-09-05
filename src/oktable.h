#ifndef OK_TABLE_H
#define OK_TABLE_H

#include "okarray.h"

#define TABLE_DEFAULT_LOAD_NUM 3
#define TABLE_DEFAULT_LOAD_DEN 4

#define TABLE_DECLARE_DEFAULT(name, key_type, value_type, hash_type)                                                   \
  TABLE_DECLARE(name, uint32_t, uint32_t, key_type, value_type, hash_type)
#define TABLE_DEFINE_DEFAULT(                                                                                          \
    name, key_type, value_type, hash_type, are_keys_equal, key_deinit, value_deinit, get_hash)                         \
  TABLE_DEFINE(name,                                                                                                   \
               uint32_t,                                                                                               \
               uint32_t,                                                                                               \
               UINT32_MAX,                                                                                             \
               UINT32_MAX,                                                                                             \
               key_type,                                                                                               \
               value_type,                                                                                             \
               hash_type,                                                                                              \
               TABLE_DEFAULT_LOAD_NUM,                                                                                 \
               TABLE_DEFAULT_LOAD_DEN,                                                                                 \
               are_keys_equal,                                                                                         \
               key_deinit,                                                                                             \
               value_deinit,                                                                                           \
               get_hash)

#define TABLE_DECLARE(name, table_size_type, bucket_size_type, key_type, value_type, hash_type)                        \
  typedef struct name name;                                                                                            \
  typedef struct name##_entry name##_entry;                                                                            \
                                                                                                                       \
  struct name##_entry {                                                                                                \
    key_type key;                                                                                                      \
    value_type value;                                                                                                  \
  };                                                                                                                   \
                                                                                                                       \
  void name##_entry_init(name##_entry* p_entry, key_type p_key, value_type p_value);                                   \
  void name##_entry_deinit(name##_entry* p_entry, allocators* p_alloc);                                                \
                                                                                                                       \
  ARRAY_DECLARE(name##_bucket, name##_entry, bucket_size_type)                                                         \
  ARRAY_DECLARE(name##_buckets, name##_bucket, table_size_type)                                                        \
  struct name {                                                                                                        \
    name##_buckets buckets;                                                                                            \
    table_size_type count;                                                                                             \
    allocators* alloc;                                                                                                 \
  };                                                                                                                   \
                                                                                                                       \
  void name##_init(name* p_table, allocators* p_alloc);                                                                \
  void name##_deinit(name* p_table);                                                                                   \
  bool name##_set(name* p_table, key_type p_key, value_type p_value);                                                  \
  value_type* name##_get(name* p_table, key_type p_key);                                                               \
  bool name##_remove(name* p_table, key_type p_key);

#define TABLE_DEFINE(name,                                                                                             \
                     table_size_type,                                                                                  \
                     bucket_size_type,                                                                                 \
                     table_size_type_max,                                                                              \
                     bucket_size_type_max,                                                                             \
                     key_type,                                                                                         \
                     value_type,                                                                                       \
                     hash_type,                                                                                        \
                     load_num,                                                                                         \
                     load_den,                                                                                         \
                     are_keys_equal,                                                                                   \
                     key_deinit,                                                                                       \
                     value_deinit,                                                                                     \
                     get_hash /* called in hot paths, preferably the actuall hash is stroed in the key_type,*/         \
                     /* and this *call* only returns cached value. */)                                                 \
                                                                                                                       \
  ARRAY_DEFINE(name##_bucket, name##_entry, bucket_size_type, bucket_size_type_max, name##_entry_deinit)               \
  ARRAY_DEFINE(name##_buckets, name##_bucket, table_size_type, table_size_type_max, name##_bucket_deinit)              \
                                                                                                                       \
  void name##_entry_init(name##_entry* p_entry, key_type p_key, value_type p_value) {                                  \
    p_entry->key = p_key;                                                                                              \
    p_entry->value = p_value;                                                                                          \
  }                                                                                                                    \
                                                                                                                       \
  void name##_entry_deinit(name##_entry* p_entry, allocators* p_alloc) {                                               \
    value_deinit(&p_entry->value, p_alloc);                                                                            \
    key_deinit(&p_entry->key, p_alloc);                                                                                \
  }                                                                                                                    \
  static name##_entry* name##_bucket_get(name##_bucket* p_bucket, const key_type p_key, bucket_size_type* p_index) {   \
    if (p_bucket->count == 1) {                                                                                        \
      if (get_hash(p_bucket->data[0].key) == get_hash(p_key) && are_keys_equal(p_bucket->data[0].key, p_key)) {        \
        *p_index = 0;                                                                                                  \
        return &p_bucket->data[0];                                                                                     \
      }                                                                                                                \
    }                                                                                                                  \
    if (p_bucket->count == 0) {                                                                                        \
      return NULL;                                                                                                     \
    }                                                                                                                  \
    for (bucket_size_type i = 0; i < p_bucket->count; ++i) {                                                           \
      if (get_hash(p_bucket->data[i].key) == get_hash(p_key) && are_keys_equal(p_bucket->data[i].key, p_key)) {        \
        *p_index = i;                                                                                                  \
        return &p_bucket->data[i];                                                                                     \
      }                                                                                                                \
    }                                                                                                                  \
    return NULL;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  static void name##_free_buckets_storage(name##_buckets* p_buckets) {                                                 \
    for (uint32_t i = 0; i < p_buckets->capacity; ++i) {                                                               \
      name##_bucket_free_storage(&p_buckets->data[i]);                                                                 \
    }                                                                                                                  \
    name##_buckets_free_storage(p_buckets);                                                                            \
  }                                                                                                                    \
                                                                                                                       \
  void name##_init(name* p_table, allocators* p_alloc) {                                                               \
    p_table->alloc = p_alloc;                                                                                          \
    p_table->count = 0;                                                                                                \
    name##_buckets_init(&p_table->buckets, p_table->alloc);                                                            \
  }                                                                                                                    \
                                                                                                                       \
  void name##_deinit(name* p_table) {                                                                                  \
    for (table_size_type i = 0; i < p_table->buckets.capacity; ++i) {                                                  \
      name##_bucket_deinit(&p_table->buckets.data[i], p_table->alloc);                                                 \
    }                                                                                                                  \
    name##_buckets_deinit(&p_table->buckets, p_table->alloc);                                                          \
  }                                                                                                                    \
                                                                                                                       \
  static table_size_type name##_find_entry(name##_buckets* p_buckets,                                                  \
                                           const table_size_type p_capacity,                                           \
                                           key_type p_key,                                                             \
                                           name##_entry** p_out,                                                       \
                                           bucket_size_type* p_index_in_bucket) {                                      \
    table_size_type bucket_index = get_hash(p_key) & (p_capacity - 1);                                                 \
    name##_entry* ptr = name##_bucket_get(&p_buckets->data[bucket_index], p_key, p_index_in_bucket);                   \
    *p_out = ptr;                                                                                                      \
    return bucket_index;                                                                                               \
  }                                                                                                                    \
                                                                                                                       \
  static bool name##_adjust_to_capacity(name* p_table, table_size_type p_capacity) {                                   \
    name##_buckets buckets;                                                                                            \
    name##_buckets_init(&buckets, p_table->alloc);                                                                     \
    if (!name##_buckets_grow(&buckets, p_capacity)) {                                                                  \
      name##_buckets_free_storage(&buckets);                                                                           \
      return false;                                                                                                    \
    }                                                                                                                  \
    for (table_size_type i = 0; i < p_capacity; ++i) {                                                                 \
      name##_bucket_init(&buckets.data[i], p_table->alloc);                                                            \
    }                                                                                                                  \
    for (table_size_type i = 0; i < p_table->buckets.capacity; ++i) {                                                  \
      name##_bucket* bucket = &p_table->buckets.data[i];                                                               \
      for (bucket_size_type i = 0; i < bucket->count; ++i) {                                                           \
        const name##_entry* entry = &bucket->data[i];                                                                  \
        name##_entry* dest = NULL;                                                                                     \
        bucket_size_type _index = 0;                                                                                   \
        const table_size_type bucket_index = name##_find_entry(&buckets, p_capacity, entry->key, &dest, &_index);      \
        name##_bucket* bucket = &buckets.data[bucket_index];                                                           \
        if (bucket->count == 0) {                                                                                      \
          buckets.count++;                                                                                             \
        }                                                                                                              \
        if (!name##_bucket_append(bucket, *entry)) {                                                                   \
        fail_add:                                                                                                      \
          name##_free_buckets_storage(&buckets);                                                                       \
          return false;                                                                                                \
        }                                                                                                              \
      }                                                                                                                \
    }                                                                                                                  \
    name##_free_buckets_storage(&p_table->buckets);                                                                    \
    p_table->buckets = buckets;                                                                                        \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_set(name* p_table, key_type p_key, value_type p_value) {                                                 \
    if ((p_table->buckets.count + 1) * (load_den) > (p_table->buckets.capacity * (load_num))) {                        \
      table_size_type capacity = array_grow_capacity(                                                                  \
          p_table->buckets.capacity,                                                                                   \
          0,                                                                                                           \
          table_size_type_max); /* it's mandatory to not use the min parameter of the array_grow_capacity function */  \
                                /* so we can safely replace the % operator with fast bitwise opperation. */            \
      if (!name##_adjust_to_capacity(p_table, capacity)) {                                                             \
        return false;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
    name##_entry* entry = NULL;                                                                                        \
    bucket_size_type _index = 0;                                                                                       \
    const table_size_type index =                                                                                      \
        name##_find_entry(&p_table->buckets, p_table->buckets.capacity, p_key, &entry, &_index);                       \
    if (entry != NULL) {                                                                                               \
      value_deinit(&entry->value, p_table->alloc);                                                                     \
      key_deinit(&p_key, p_table->alloc);                                                                              \
      entry->value = p_value;                                                                                          \
      return true;                                                                                                     \
    }                                                                                                                  \
    name##_bucket* bucket = &p_table->buckets.data[index];                                                             \
    if (bucket->count == 0) {                                                                                          \
      name##_bucket_init(bucket, p_table->alloc);                                                                      \
      p_table->buckets.count++;                                                                                        \
    }                                                                                                                  \
    name##_entry new_entry = {.key = p_key, .value = p_value};                                                         \
    if (!name##_bucket_append(bucket, new_entry)) {                                                                    \
      return false;                                                                                                    \
    }                                                                                                                  \
    p_table->count++;                                                                                                  \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  value_type* name##_get(name* p_table, key_type p_key) {                                                              \
    if (p_table->count == 0) {                                                                                         \
      return NULL;                                                                                                     \
    }                                                                                                                  \
    name##_entry* entry = NULL;                                                                                        \
    bucket_size_type _index = 0;                                                                                       \
    name##_find_entry(&p_table->buckets, p_table->buckets.capacity, p_key, &entry, &_index);                           \
    if (entry != NULL) {                                                                                               \
      return &entry->value;                                                                                            \
    }                                                                                                                  \
    return NULL;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_remove(name* p_table, key_type p_key) {                                                                  \
    name##_entry* entry = NULL;                                                                                        \
    bucket_size_type index_in_bucket = 0;                                                                              \
    const table_size_type bucket_index =                                                                               \
        name##_find_entry(&p_table->buckets, p_table->buckets.capacity, p_key, &entry, &index_in_bucket);              \
    if (entry == NULL) {                                                                                               \
      return false;                                                                                                    \
    }                                                                                                                  \
    name##_bucket* bucket = &p_table->buckets.data[bucket_index];                                                      \
    if (name##_bucket_remove(bucket, index_in_bucket, index_in_bucket)) {                                              \
      p_table->count--;                                                                                                \
      if (bucket->count == 0) {                                                                                        \
        name##_bucket_deinit(bucket, p_table->alloc);                                                                  \
        p_table->buckets.count--;                                                                                      \
      }                                                                                                                \
      return true;                                                                                                     \
    }                                                                                                                  \
    return false;                                                                                                      \
  }

#endif // OK_TABLE_H
