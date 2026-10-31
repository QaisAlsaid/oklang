#include "okglobals_store.h"

#include "okutils.h"

// the name -> id index is the first okmap.h user: raw string keys (identifier
// copies owned by the map), global_id values, and lookups probe with a plain
// string view. unlike the old oktable version, a lookup allocates nothing —
// the old code built and destroyed a fresh hashed_string probe per call.

#define GL_KEY_IS_EMPTY(k) ((k).chars == NULL)
#define GL_KEY_SET_EMPTY(k)                                                                                              \
  do {                                                                                                                   \
    (k)->chars = NULL;                                                                                                   \
    (k)->info = 0;                                                                                                       \
  } while (0)
#define GL_PROBE_HASH(p) (default_hash_str(p))
#define GL_PROBE_MATCHES(p, k)                                                                                           \
  (ok_string_view_get_length(&(p)) == string_get_length(&(k)) &&                                                         \
   memcmp((k).chars, (p).chars, ok_string_view_get_length(&(p))) == 0)
#define GL_KEY_CLONE(p, a) (create_string_from_string_view((p), (a)))
#define GL_KEY_DEINIT(k, a) (string_deinit((k), (a)))

MAP_DEFINE(globals_map,
           string,
           global_id,
           ok_string_view,
           GL_KEY_IS_EMPTY,
           GL_KEY_SET_EMPTY,
           GL_PROBE_HASH,
           GL_PROBE_MATCHES,
           GL_KEY_CLONE,
           GL_KEY_DEINIT,
           ARRAY_DEFAULT_TYPE_DEINIT)

ARRAY_DEFINE(global_values, value, uint16_t, UINT16_MAX, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE(global_descriptors, global, uint16_t, UINT16_MAX, ARRAY_DEFAULT_TYPE_DEINIT)

void globals_store_init(globals_store* p_globals, allocators* p_alloc) {
  p_globals->alloc = p_alloc;
  global_values_init(&p_globals->global_values, p_alloc);
  global_descriptors_init(&p_globals->global_descriptors, p_alloc);
  globals_map_init(&p_globals->map, p_alloc);
}

void globals_store_deinit(globals_store* p_globals) {
  globals_map_deinit(&p_globals->map);
  global_descriptors_deinit(&p_globals->global_descriptors, p_globals->alloc);
  global_values_deinit(&p_globals->global_values, p_globals->alloc);
}

global_id globals_store_add(globals_store* p_store, const ok_string_view p_identifier, global_flags p_flags) {
  global_id glob_id = {.index = 0, .errors = GLOBAL_OK, .flags = GLOBAL_FLAG_NONE};
  {
    global_id* glob = globals_map_get(&p_store->map, p_identifier);
    if (glob != NULL) {
      return *glob;
    }
  }

  if (p_store->global_values.count >= GLOBAL_MAX) {
    glob_id.errors = GLOBAL_OVERFLOW;
    return glob_id;
  }
  if (!global_values_append(&p_store->global_values, NULL_AS_VALUE())) {
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  // the value slot is the position just appended (count - 1): count was already incremented
  global global = {p_store->global_values.count - 1, p_flags, GLOBAL_OK};
  if (!global_descriptors_append(&p_store->global_descriptors, global)) {
    global_values_pop(&p_store->global_values);
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  glob_id.index = p_store->global_descriptors.count - 1;
  if (!globals_map_set(&p_store->map, p_identifier, glob_id)) {
    global_values_pop(&p_store->global_values);
    global_descriptors_pop(&p_store->global_descriptors);
    glob_id.errors = GLOBAL_ALLOCATION_FAILED;
    return glob_id;
  }
  return glob_id;
}

global_id globals_store_get(globals_store* p_store, const ok_string_view p_identifier) {
  global_id* glob = globals_map_get(&p_store->map, p_identifier);
  if (glob != NULL) {
    return *glob;
  }
  global_id glob_id = {.index = 0, .flags = GLOBAL_FLAG_NONE, .errors = GLOBAL_UNDEFINED};
  return glob_id;
}
