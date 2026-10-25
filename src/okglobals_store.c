#include "okglobals_store.h"

#define ARE_KEYS_EQUAL(lhs, rhs)                                                                                       \
  (string_get_length(&lhs.string) == string_get_length(&rhs.string) &&                                                 \
   strncmp(lhs.string.chars, rhs.string.chars, string_get_length(&lhs.string)) == 0)
#define GET_HASH(key) (key.hash)

ARRAY_DEFINE(global_values, value, uint16_t, UINT16_MAX, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE(global_descriptors, global, uint16_t, UINT16_MAX, ARRAY_DEFAULT_TYPE_DEINIT)
TABLE_DEFINE(globals_table,
             uint16_t,
             uint16_t,
             UINT16_MAX,
             UINT16_MAX,
             hashed_string,
             global_id,
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
  global_descriptors_init(&p_globals->global_descriptors, p_alloc);
  globals_table_init(&p_globals->table, p_alloc);
}

void globals_store_deinit(globals_store* p_globals) {
  globals_table_deinit(&p_globals->table);
  global_descriptors_deinit(&p_globals->global_descriptors, p_globals->alloc);
  global_values_deinit(&p_globals->global_values, p_globals->alloc);
}

global_id globals_store_add(globals_store* p_store, const ok_string_view p_identifier, global_flags p_flags) {
  allocators* alloc = p_store->alloc;
  global_id glob_id = {.index = 0, .errors = GLOBAL_OK, .flags = GLOBAL_FLAG_NONE};
  hashed_string hs = create_hashed_string_hash(p_identifier, alloc);

  {
    global_id* glob = globals_table_get(&p_store->table, hs);
    if (glob != NULL) {
      hashed_string_deinit(&hs, alloc);
      return *glob;
    }
  }

  if (p_store->global_values.count >= GLOBAL_MAX) {
    hashed_string_deinit(&hs, alloc);
    glob_id.errors = GLOBAL_OVERFLOW;
    return glob_id;
  }
  if (!global_values_append(&p_store->global_values, NULL_AS_VALUE())) {
    hashed_string_deinit(&hs, alloc);
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  // the value slot is the position just appended (count - 1): count was already incremented
  global global = {p_store->global_values.count - 1, p_flags, GLOBAL_OK};
  if (!global_descriptors_append(&p_store->global_descriptors, global)) {
    hashed_string_deinit(&hs, alloc);
    global_values_pop(&p_store->global_values);
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  glob_id.index = p_store->global_descriptors.count - 1;
  if (!globals_table_set(&p_store->table, hs, glob_id)) {
    hashed_string_deinit(&hs, alloc);
    global_values_pop(&p_store->global_values);
    global_descriptors_pop(&p_store->global_descriptors);
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  return glob_id;
}

global_id globals_store_get(globals_store* p_store, const ok_string_view p_identifier) {
  allocators* alloc = p_store->alloc;
  hashed_string hs = create_hashed_string_hash(p_identifier, alloc);
  {
    global_id* glob = globals_table_get(&p_store->table, hs);
    if (glob != NULL) {
      hashed_string_deinit(&hs, alloc);
      return *glob;
    }
  }
  hashed_string_deinit(&hs, alloc);
  global_id glob = {.index = 0, .flags = GLOBAL_FLAG_NONE, .errors = GLOBAL_UNDEFINED};
  return glob;
}
