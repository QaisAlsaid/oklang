#ifndef OK_GLOBALS_STORE_H
#define OK_GLOBALS_STORE_H

#include "okarray.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okindex.h"
#include "okmap.h"
#include "okstring.h"
#include "oktable.h"
#include "okvalue.h"

typedef index_wrapper global;

typedef enum {
  GLOBAL_OK = 0,
  GLOBAL_UNDEFINED,
  GLOBAL_ALLOCATION_FAILED,
  GLOBAL_OVERFLOW,
  GLOBAL_ILL_MUTATION,
} global_error;

#define GLOBAL_MAX INDEX_MAX
#define IS_GLOBAL_VALID(g) ((g).errors == GLOBAL_OK)

typedef enum {
  GLOBAL_FLAG_NONE = 0,
  GLOBAL_FLAG_MUTABLE = 1 << 0,
} global_flags;

typedef index_wrapper global_id;

ARRAY_DECLARE(global_values, value, uint16_t)
ARRAY_DECLARE(global_descriptors, global, uint16_t)
MAP_DECLARE(globals_map, string, global_id, ok_string_view)

#if defined(OK_DEBUG) // TODO debug meta data to get a name from an index
TABLE_DECLARE(debug_table, uint32_t, uint32_t, uint32_t, hashed_string, hash_t)
#endif

struct globals_store {
  global_values global_values;
  global_descriptors global_descriptors;
  globals_map map;
  allocators* alloc;
};

void globals_store_init(globals_store* p_store, allocators* p_alloc);
void globals_store_deinit(globals_store* p_store);
global_id globals_store_add(globals_store* p_store, const ok_string_view p_identifier, global_flags p_flags);
global_id globals_store_get(globals_store* p_store, const ok_string_view p_identifier);

#endif // OK_GLOBALS_STORE_H
