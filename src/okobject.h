#ifndef OK_OBJECT_H
#define OK_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okindex.h"
#include "okmap.h"
#include "okobject_store.h"
#include "okstring.h"
#include "okvalue.h"

#define OBJECT_TYPE(value) object_get_type(VALUE_AS_OBJECT(value))

#define IS_VALUE_STRING(value) value_is_object_type(value, OBJ_STRING)
#define IS_VALUE_FUNCTION(value) value_is_object_type(value, OBJ_FUNCTION)
#define IS_VALUE_CLOSURE(value) value_is_object_type(value, OBJ_CLOSURE)
#define IS_VALUE_UPVALUE(value) value_is_object_type(value, OBJ_UPVALUE)
#define IS_VALUE_NATIVE_VALUE(value) value_is_object_type(value, OBJ_NATIVE_VALUE)
#define IS_VALUE_NATIVE_FUNCTION(value) value_is_object_type(value, OBJ_NATIVE_FUNCTION)
#define IS_VALUE_ARRAY(value) value_is_object_type(value, OBJ_ARRAY)
#define IS_VALUE_MAP(value) value_is_object_type(value, OBJ_MAP)

#define VALUE_AS_STRING(value) ((object_string*)VALUE_AS_OBJECT(value))
#define VALUE_AS_FUNCTION(value) ((object_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CLOSURE(value) ((object_closure*)VALUE_AS_OBJECT(value))
#define VALUE_AS_UPVALUE(value) ((object_upvalue*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_VALUE(value) ((object_native_type*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_FUNCTION(value) ((object_native_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_ARRAY(value) ((object_array*)VALUE_AS_OBJECT(value))
#define VALUE_AS_MAP(value) ((object_map*)VALUE_AS_OBJECT(value))

typedef struct {
  allocators* alloc;
  object_store* store;
} object_specs;

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
  OBJ_NATIVE_VALUE,
  OBJ_NATIVE_FUNCTION,
  OBJ_ARRAY,
  OBJ_MAP,
} object_type;

struct object {
  uint32_t info; // 24bit for type + 1 bit for is marked and rest of bits for later usage
  object* next;
};

uint32_t object_get_type(object* p_object);
bool object_is_marked(object* p_object);
void object_set_marked(object* p_object, bool p_marked);

static inline bool value_is_object_type(value p_value, object_type p_type) {
  // strict check: raw doubles alias the non strict tag bits
  return IS_VALUE_STRICTLY_OBJECT(p_value) && OBJECT_TYPE(p_value) == p_type;
}

typedef struct {
  object object;
  string string;
  hash_t hash; // FNV-1a over the bytes, cached at creation. strings are immutable,
               // so map keys (and any future hashed user) never re-walk the bytes.
} object_string;

object_string* create_object_string(const ok_string_view p_string, object_specs* p_specs);

typedef struct {
  ok_index index;
  bool is_local;
} upvalue_descriptor;

ARRAY_DECLARE(upvalue_descriptor_array, upvalue_descriptor, uint16_t)

typedef struct {
  object object;
  object_string* name;
  chunk chunk;
  uint16_t arity;
  uint16_t ndefaults; // trailing defaulted params: floor = arity - ndefaults
  uint16_t registers;
  uint16_t results; // the function's result span width (SPANS R2, inferred by the compiler).
                     // 1 for every single-result function (the default); n >= 2 when every
                     // return path agrees on n. call() compares the caller's requested retc
                     // against this — the strictness doctrine (R6) lives there.
  bool has_varargs;  // the last parameter is `rest...` (SPANS R3): argc has no ceiling, the
                     // floor drops it, and the runtime frame widens past `registers` to cover
                     // the staged arguments. set by the compiler at the prologue.
  upvalue_descriptor_array upvalues;
} object_function;

object_function* create_object_function(object_string* p_name, uint16_t p_arity, object_specs* p_specs);

typedef struct object_upvalue {
  object object;
  uint32_t info; // 24 bit index into the stack + 1 bit for is_closed
  value closed;
  struct object_upvalue* next;
} object_upvalue;

void object_upvalue_set_location(object_upvalue* p_upvalue, uint32_t p_location);
void object_upvalue_set_closed(object_upvalue* p_upvalue);
uint32_t object_upvalue_get_location(object_upvalue* p_upvalue);
bool object_upvalue_is_closed(object_upvalue* p_upvalue);
value* object_upvalue_get_value(object_upvalue* p_upvalue, value* p_frame_window);

object_upvalue* create_object_upvalue(uint32_t p_location, object_specs* p_specs);

ARRAY_DECLARE_DEFAULT(upvalue_array, object_upvalue*)

// the ok_value-keyed instantiation of the okmap core (defined in okobject.c):
// strict-null key = empty slot, cached hashes, backward-shift deletion.
MAP_DECLARE(value_map, value, value, value)

typedef struct {
  object object;
  object_function* function;
  upvalue_array upvalues;
} object_closure;

object_closure* create_object_closure(object_function* p_function, object_specs* p_specs);

typedef struct {
  object object;
  void* user_data;
  ok_native_destructor destructor;
  ok_native_tracer tracer; // NULL: opaque to the collector (no ok_values inside)
} object_native_value;

object_native_value* create_object_native_value(void* p_user_data,
                                                ok_native_destructor p_destructor,
                                                ok_native_tracer p_tracer,
                                                object_specs* p_specs);

typedef struct {
  object object;
  string name;
  uint16_t arity;
  ok_native_fn function;
} object_native_function;

object_native_function*
create_object_native_function(ok_string_view p_name, uint16_t p_arity, ok_native_fn p_cb, object_specs* p_specs);

typedef struct {
  object object;
  value_array values; // ARRAY_DECLARE(value_array, value, uint32_t) in okvalue.h
} object_array;

object_array* create_object_array(object_specs* p_specs);

// the surface map: the same linear-probing core the compiler's tables use
// (okmap.h), instantiated at ok_value keys AND values. the strict-null key is
// the empty-slot sentinel, which is exactly why null KEYS are illegal on the
// surface (the encoding spends them on "empty"). keys are stored as the values
// they arrived as; probes hash through map_value_hash and compare through
// map_value_key_equals, both of which must stay in agreement with perform_equal
// for the types that are legal keys (strings, numbers, bools).
//
// version: bumped by every size-changing operation (insert-new, remove;
// overwrites do NOT bump). the for-in ops snapshot it at loop entry and hard
// error on a mid-iteration size change - overwriting values while iterating is
// fine, inserting/removing is not.
typedef struct {
  object object;
  value_map entries;
  uint32_t version;
} object_map;

object_map* create_object_map(object_specs* p_specs);

hash_t map_value_hash(value p_key);
bool map_value_key_equals(value p_probe, value p_key);

// the v1 key whitelist: strings, numbers (int-tagged included) and booleans.
// null is the empty-slot encoding (never a legal key), arrays/maps/functions
// are identity-shaped and wait for classes (docs/drafts/MAPS.md §1).
bool map_key_is_legal(value p_key);

// insert-or-overwrite + version bookkeeping. returns false only on OOM (the
// okmap core's contract); a size change bumps p_map->version so active
// iterations can detect it.
bool object_map_set(object_map* p_map, value p_key, value p_value);

// false = absent (no version change).
bool object_map_remove(object_map* p_map, value p_key);

void object_dispatch_deinit(object* p_object, object_specs* p_specs);
void objects_list_deinit(object* p_head, object_specs* p_specs);

void object_debug_print(object* p_object);
#endif // OK_OBJECT_H
