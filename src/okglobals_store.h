#ifndef OK_GLOBALS_STORE_H
#define OK_GLOBALS_STORE_H

#include "okarray.h"
#include "oktable.h"
#include "okutils.h"
#include "okvalue.h"

#define GLOBAL_ALLOCATION_FAILED UINT32_MAX
#define GLOBAL_OVERFLOW (UINT32_MAX - 1)
#define GLOBAL_NOT_FOUND (UINT32_MAX - 404)
#define GLOBAL_MAX UINT24_MAX
#define GLOBAL_INVALID (GLOBAL_MAX + 1)
#define IS_GLOBAL_VALID(i) ((i) < GLOBAL_INVALID)

typedef struct chunk chunk;

ARRAY_DECLARE(global_values, value, uint32_t)
TABLE_DECLARE(globals_table, uint32_t, uint32_t, hashed_string, uint32_t, hash_t)

#if defined(OK_DEBUG) // TODO debug meta data to get a name from an index
TABLE_DECLARE(debug_table, uint32_t, uint32_t, uint32_t, hashed_string, hash_t)
#endif

typedef struct {
  global_values global_values;
  globals_table table;
} globals_store;

void globals_store_init(globals_store* p_store);
void globals_store_deinit(globals_store* p_store);
uint32_t globals_store_add(globals_store* p_store, const string_view p_identifier);
uint32_t globals_store_get(globals_store* p_store, const string_view p_identifier);

#endif // OK_GLOBALS_STORE_H
