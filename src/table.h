#ifndef OK_TABLE_H
#define OK_TABLE_H

#include "array.h"

/* non-standard implemnintation of closed adressing hash table, we don't require keys to be compareable,
 * so the only requirement for key are to be hashable.
 * and the requirement for hashes is to have a value considered as null hash.
 */

#define TABLE_DECLARE(name, table_size_type, bucket_size_type, key_type, value_type, hash_type)                        \
  typedef struct name name;                                                                                            \
  typedef struct name##_entry name##_entry;                                                                            \
                                                                                                                       \
  struct name##_entry {                                                                                                \
    key_type key;                                                                                                      \
    value_type value;                                                                                                  \
  };                                                                                                                   \
                                                                                                                       \
  ARRAY_DECLARE(name##_bucket, name##_entry, bucket_size_type)                                                         \
  ARRAY_DECLARE(name##_buckets, name##_bucket, table_size_type)                                                        \
  struct name {                                                                                                        \
    name##_buckets buckets;                                                                                            \
    table_size_type count;                                                                                             \
  };                                                                                                                   \
                                                                                                                       \
  void name##_init(name* p_table);                                                                                     \
  void name##_deinit(name* p_table);                                                                                   \
  bool name##_set(name* p_table, key_type p_key, value_type p_value);                                                  \
  value_type* name##_get(name* p_table, key_type p_key);

#define TABLE_DEFINE(name,                                                                                             \
                     table_size_type,                                                                                  \
                     bucket_size_type,                                                                                 \
                     key_type,                                                                                         \
                     value_type,                                                                                       \
                     hash_type,                                                                                        \
                     load_max,                                                                                         \
                     is_hash_null,                                                                                     \
                     get_hash /* called in hot paths, preferably the actuall hash is stroed in the key_type,*/         \
                     /* and this *call* only returns cached value. */)                                                 \
                                                                                                                       \
  ARRAY_DEFINE(name##_bucket, name##_entry, bucket_size_type)                                                          \
  ARRAY_DEFINE(name##_buckets, name##_bucket, table_size_type)                                                         \
                                                                                                                       \
  static name##_entry* name##_bucket_get(name##_bucket* p_bucket, hash_type p_hash, bucket_size_type* p_index) {       \
    if (p_bucket->count == 1) {                                                                                        \
      *p_index = 0;                                                                                                    \
      return &p_bucket->data[0];                                                                                       \
    }                                                                                                                  \
    if (p_bucket->count == 0) {                                                                                        \
      return NULL;                                                                                                     \
    }                                                                                                                  \
    for (bucket_size_type i = 0; i < p_bucket->count; ++i) {                                                           \
      if (get_hash(p_bucket->data[i].key) == p_hash) {                                                                 \
        *p_index = i;                                                                                                  \
        return &p_bucket->data[i];                                                                                     \
      }                                                                                                                \
    }                                                                                                                  \
    return NULL;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  void name##_init(name* p_table) {                                                                                    \
    name##_buckets_init(&p_table->buckets);                                                                            \
  }                                                                                                                    \
                                                                                                                       \
  void name##_deinit(name* p_table) {                                                                                  \
    for (table_size_type i = 0; i < p_table->buckets.count; ++i) {                                                     \
      name##_bucket_deinit(&p_table->buckets.data[i]);                                                                 \
    }                                                                                                                  \
    name##_buckets_deinit(&p_table->buckets);                                                                          \
  }                                                                                                                    \
                                                                                                                       \
  static table_size_type name##_find_entry(name##_buckets* p_buckets,                                                  \
                                           table_size_type p_capacity,                                                 \
                                           hash_type p_hash,                                                           \
                                           name##_entry** p_out,                                                       \
                                           bucket_size_type* p_index_in_bucket) {                                      \
    table_size_type bucket_index = p_hash % p_capacity;                                                                \
    name##_entry* ptr = name##_bucket_get(&p_buckets->data[bucket_index], p_hash, p_index_in_bucket);                  \
    *p_out = ptr;                                                                                                      \
    return bucket_index;                                                                                               \
  }                                                                                                                    \
                                                                                                                       \
  static bool name##_adjust_to_capacity(name* p_table, table_size_type p_capacity) {                                   \
    name##_buckets buckets;                                                                                            \
    name##_buckets_init(&buckets);                                                                                     \
    if (!name##_buckets_grow(&buckets, p_capacity)) {                                                                  \
      name##_buckets_deinit(&buckets);                                                                                 \
      return false;                                                                                                    \
    }                                                                                                                  \
    for (table_size_type i = 0; i < p_capacity; ++i) {                                                                 \
      name##_bucket_init(&buckets.data[i]);                                                                            \
    }                                                                                                                  \
    for (table_size_type i = 0; i < p_table->buckets.capacity; ++i) {                                                  \
      name##_bucket* bucket = &p_table->buckets.data[i];                                                               \
      for (bucket_size_type i = 0; i < bucket->count; ++i) {                                                           \
        const name##_entry* entry = &bucket->data[i];                                                                  \
        if (is_hash_null(get_hash(entry->key))) {                                                                      \
          continue;                                                                                                    \
        }                                                                                                              \
        name##_entry* dest = NULL;                                                                                     \
        bucket_size_type _index = 0;                                                                                   \
        const table_size_type bucket_index =                                                                           \
            name##_find_entry(&buckets, p_capacity, get_hash(entry->key), &dest, &_index);                             \
        if (dest == NULL) {                                                                                            \
          name##_bucket* new_bucket = &buckets.data[bucket_index];                                                     \
          name##_bucket_init(new_bucket);                                                                              \
          name##_bucket_append(new_bucket, *entry);                                                                    \
          buckets.count++;                                                                                             \
        } else {                                                                                                       \
          *dest = *entry;                                                                                              \
        }                                                                                                              \
      }                                                                                                                \
    }                                                                                                                  \
    name##_buckets_deinit(&p_table->buckets);                                                                          \
    p_table->buckets = buckets;                                                                                        \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_set(name* p_table, key_type p_key, value_type p_value) {                                                 \
    if (p_table->buckets.count + 1 > p_table->buckets.capacity * load_max) {                                           \
      table_size_type capacity =                                                                                       \
          array_grow_capacity(p_table->buckets.capacity,                                                               \
                              0); /* it's mandatory to not use the min parameter of the array_grow_capacity function*/ \
                                  /*so we can safely replace the % operator with fast bitwise opperation. */           \
      name##_adjust_to_capacity(p_table, capacity);                                                                    \
    }                                                                                                                  \
    const hash_type hash = get_hash(p_key);                                                                            \
    name##_entry* entry = NULL;                                                                                        \
    bucket_size_type _index = 0;                                                                                       \
    const table_size_type index =                                                                                      \
        name##_find_entry(&p_table->buckets, p_table->buckets.capacity, hash, &entry, &_index);                        \
    const bool new_bucket = entry == NULL;                                                                             \
    name##_bucket* bucket = &p_table->buckets.data[index];                                                             \
    if (new_bucket) {                                                                                                  \
      name##_bucket_init(bucket);                                                                                      \
      p_table->buckets.count++;                                                                                        \
    }                                                                                                                  \
    name##_entry new_entry = {.key = p_key, .value = p_value};                                                         \
    name##_bucket_append(bucket, new_entry);                                                                           \
    p_table->count++;                                                                                                  \
    return new_bucket;                                                                                                 \
  }                                                                                                                    \
                                                                                                                       \
  value_type* name##_get(name* p_table, key_type p_key) {                                                              \
    if (p_table->buckets.count == 0) {                                                                                 \
      return NULL;                                                                                                     \
    }                                                                                                                  \
    name##_entry* entry = NULL;                                                                                        \
    bucket_size_type _index = 0;                                                                                       \
    name##_find_entry(&p_table->buckets, p_table->buckets.capacity, get_hash(p_key), &entry, &_index);                 \
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
        name##_find_entry(&p_table->buckets, p_table->buckets.capacity, get_hash(p_key), &entry, &index_in_bucket);    \
    if (entry == NULL) {                                                                                               \
      return false;                                                                                                    \
    }                                                                                                                  \
    name##_bucket_remove(&p_table->buckets.data[bucket_index], index_in_bucket, index_in_bucket);                      \
    return true;                                                                                                       \
  }

#endif // OK_TABLE_H
