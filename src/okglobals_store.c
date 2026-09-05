#include "okglobals_store.h"

#define ARE_KEYS_EQUAL(lhs, rhs)                                                                                       \
  (string_get_length(&lhs.string) == string_get_length(&rhs.string) &&                                                 \
   strncmp(lhs.string.chars, rhs.string.chars, string_get_length(&lhs.string)) == 0)
#define GET_HASH(key) (key.hash)

// TODO memory arena
ARRAY_DEFINE(global_values, value, uint32_t, UINT32_MAX, ARRAY_DEFAULT_TYPE_DEINIT)
TABLE_DEFINE(globals_table,
             uint32_t,
             uint32_t,
             UINT32_MAX,
             UINT32_MAX,
             hashed_string,
             uint32_t,
             hash_t,
             TABLE_DEFAULT_LOAD_NUM,
             TABLE_DEFAULT_LOAD_DEN,
             ARE_KEYS_EQUAL,
             hashed_string_deinit,
             ARRAY_DEFAULT_TYPE_DEINIT,
             GET_HASH)

void globals_store_init(globals_store* p_globals, allocators* p_alloc) {
  p_globals->alloc = p_alloc;
  global_values_init(&p_globals->global_values, p_alloc);
  globals_table_init(&p_globals->table, p_alloc);
}

void globals_store_deinit(globals_store* p_globals) {
  globals_table_deinit(&p_globals->table);
  global_values_deinit(&p_globals->global_values, p_globals->alloc);
}

uint32_t globals_store_add(globals_store* p_store, const ok_string_view p_identifier, bool p_is_mutable) {
  allocators* alloc = p_store->alloc;
  hashed_string hs = create_hashed_string_hash(p_identifier, alloc);
  {
    uint32_t* index = globals_table_get(&p_store->table, hs);
    if (index != NULL) {
      hashed_string_deinit(&hs, alloc);
      return *index;
    }
  }

  if (p_store->global_values.count >= GLOBAL_MAX) {
    hashed_string_deinit(&hs, alloc);
    return GLOBAL_OVERFLOW;
  }
  if (!global_values_append(&p_store->global_values, NULL_AS_VALUE())) {
    hashed_string_deinit(&hs, alloc);
    return GLOBAL_ALLOCATION_FAILED;
  }
  uint32_t raw_index = p_store->global_values.count - 1;
  uint32_t index = raw_index;
  if (p_is_mutable) {
    index = (raw_index & 0x00ffffff) | UINT32_C(1) << (24 + GLOBAL_FLAG_MUTABLE);
  }
  if (!globals_table_set(&p_store->table, hs, index)) {
    hashed_string_deinit(&hs, alloc);
    global_values_remove(&p_store->global_values, raw_index, raw_index); // TODO pop
    return GLOBAL_ALLOCATION_FAILED;
  }
  return index;
}

uint32_t globals_store_get(globals_store* p_store, const ok_string_view p_identifier) {
  allocators* alloc = p_store->alloc;
  hashed_string hs = create_hashed_string_hash(p_identifier, alloc);
  uint32_t* index = globals_table_get(&p_store->table, hs);
  if (index != NULL) {
    hashed_string_deinit(&hs, alloc);
    return *index;
  }
  hashed_string_deinit(&hs, alloc);
  return GLOBAL_NOT_FOUND;
}
