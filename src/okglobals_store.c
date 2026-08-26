#include "okglobals_store.h"
#include "okchunk.h"

#define ARE_KEYS_EQUAL(lhs, rhs) (strncmp(lhs.string.chars, rhs.string.chars, string_get_length(&lhs.string)) == 0)
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
void globals_store_init(globals_store* p_globals) {
  global_values_init(&p_globals->global_values);
  globals_table_init(&p_globals->table);
}

void globals_store_deinit(globals_store* p_globals) {
  globals_table_deinit(&p_globals->table);
  global_values_deinit(&p_globals->global_values);
}

uint32_t globals_store_add(globals_store* p_store, const string_view p_identifier, uint8_t p_flags) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  {
    uint32_t* index = globals_table_get(&p_store->table, hs);
    if (index != NULL) {
      hashed_string_deinit(&hs);
      return *index;
    }
  }

  if (p_store->global_values.count >= GLOBAL_MAX) {
    hashed_string_deinit(&hs);
    return GLOBAL_OVERFLOW;
  }
  if (!global_values_append(&p_store->global_values, NULL_AS_VALUE())) {
    hashed_string_deinit(&hs);
    return GLOBAL_ALLOCATION_FAILED;
  }
  uint32_t index = p_store->global_values.count - 1;
  if (!globals_table_set(&p_store->table, hs, index)) {
    hashed_string_deinit(&hs);
    global_values_remove(&p_store->global_values, index, index); // TODO pop
    return GLOBAL_ALLOCATION_FAILED;
  }
  return index;
}

uint32_t globals_store_get(globals_store* p_store, const string_view p_identifier) {
  hashed_string hs = create_hashed_string_hash(p_identifier);
  uint32_t* index = globals_table_get(&p_store->table, hs);
  if (index != NULL) {
    hashed_string_deinit(&hs);
    return *index;
  }
  hashed_string_deinit(&hs);
  return GLOBAL_NOT_FOUND;
}
