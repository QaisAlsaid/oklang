#include <stdio.h>
#include <stdlib.h>

#include "okobject.h"
#include "okobject_store.h"
#include "okutils.h"

// the okmap.h core instantiated at ok_value keys/values. hashing rules:
//   - numbers (and int-tagged operands, widened to their double form like
//     perform_equal's int/number comparison): splitmix64 over the normalized
//     bits. -0.0 folds to +0.0 first, so 0.0 and -0.0 are one key. NaN keys
//     keep their bits: same-bits NaNs are one key, different NaNs are different
//     keys - hashing and equality stay in agreement, which is the only hard rule.
//   - strings: the cached FNV-1a from the string header, so probing never walks the bytes twice.
//   - bools: the tag bits themselves.
//   - the tag is mixed into the final hash so `1`, `true` and `"1"` can never
//     share a probe path (quality: PROBE_MATCHES is the real authority).

// the normalized double bits two keys must share to compare equal
static uint64_t map_key_number_bits(value p_key) {
  double d;
  if (IS_VALUE_NUMBER(p_key)) {
    d = VALUE_AS_NUMBER(p_key);
  } else {
    d = (double)VALUE_AS_INT(p_key);
  }
  if (d == 0.0) {
    d = 0.0; // -0.0 -> +0.0 one key, not two
  }
  union pun {
    uint64_t bits;
    double d;
  };
  union pun val = {.d = d};
  return val.bits;
}

hash_t map_value_hash(value p_key) {
  uint64_t raw;
  uint64_t tag_mix;
  if (IS_VALUE_NUMBER(p_key) || IS_VALUE_INT(p_key)) {
    raw = map_key_number_bits(p_key);
    tag_mix = 0; // numbers are their tag space. mixing 0 keeps splitmix64(bits)
  } else if (IS_VALUE_STRING(p_key)) {
    raw = VALUE_AS_STRING(p_key)->hash;
    tag_mix = TAG_OBJECT_MASK;
  } else {
    raw = 0; // bools: the tag bits are the whole identity
    tag_mix = VALUE_TAG(p_key);
  }
  return splitmix64(raw ^ tag_mix);
}

bool map_value_key_equals(value p_probe, value p_key) {
  if (IS_VALUE_NUMBER(p_probe) || IS_VALUE_INT(p_probe)) {
    return (IS_VALUE_NUMBER(p_key) || IS_VALUE_INT(p_key)) &&
           map_key_number_bits(p_probe) == map_key_number_bits(p_key);
  }
  if (IS_VALUE_STRING(p_probe)) {
    if (!IS_VALUE_STRING(p_key)) {
      return false;
    }
    // the cached-hash fast reject already ran in find_slot (hashes[i] == hash)
    const object_string* left = VALUE_AS_STRING(p_probe);
    const object_string* right = VALUE_AS_STRING(p_key);
    const uint64_t left_length = string_get_length(&left->string);
    return left_length == string_get_length(&right->string) &&
           memcmp(left->string.chars, right->string.chars, left_length) == 0;
  }
  return p_probe == p_key; // bools (and any future same-tag scalars): raw identity
}

// number first: a negative subnormal double can alias TAG_INT_MASK, but
// IS_VALUE_NUMBER already claimed every raw double before this check runs.
bool map_key_is_legal(value p_key) {
  return IS_VALUE_NUMBER(p_key) || IS_VALUE_INT(p_key) || IS_VALUE_STRICTLY_BOOL(p_key) || IS_VALUE_STRING(p_key);
}

#define VM_KEY_IS_EMPTY(k) IS_VALUE_STRICTLY_NULL(k)
#define VM_KEY_SET_EMPTY(k) (*(k) = NULL_AS_VALUE())
#define VM_PROBE_HASH(p) (map_value_hash(p))
#define VM_PROBE_MATCHES(p, k) (map_value_key_equals((p), (k)))
#define VM_KEY_CLONE(p, a) (p)       /* values are copied bit for bit; the gc owns the memory */
#define VM_NO_DEINIT(k, a) ((void)0) /* ditto: nothing to release per entry */

MAP_DEFINE(value_map,
           value,
           value,
           value,
           VM_KEY_IS_EMPTY,
           VM_KEY_SET_EMPTY,
           VM_PROBE_HASH,
           VM_PROBE_MATCHES,
           VM_KEY_CLONE,
           VM_NO_DEINIT,
           VM_NO_DEINIT)

bool object_map_set(object_map* p_map, value p_key, value p_value) {
  const uint32_t before = p_map->entries.count;
  if (!value_map_set(&p_map->entries, p_key, p_value)) {
    return false;
  }
  if (p_map->entries.count != before) {
    p_map->version++; // a size change: active for-ins must fail, not mis-walk
  }
  return true;
}

bool object_map_remove(object_map* p_map, value p_key) {
  if (!value_map_remove(&p_map->entries, p_key)) {
    return false;
  }
  p_map->version++;
  return true;
}

static void object_init(object* p_object, uint32_t p_type) {
  p_object->info = p_type & 0x00ffffff;
  p_object->next = NULL;
}

static void object_deinit(object* p_object) {
}

uint32_t object_get_type(object* p_object) {
  return p_object->info & 0x00ffffff;
}

typedef enum {
  OBJECT_FLAG_MARKED = 0,
} object_flags;

bool object_is_marked(object* p_object) {
  return (p_object->info & (UINT32_C(1) << (24 + OBJECT_FLAG_MARKED))) != 0;
}

void object_set_marked(object* p_object, bool p_marked) {
  const uint32_t mask = UINT32_C(1) << (24 + OBJECT_FLAG_MARKED);
  p_object->info = p_marked ? p_object->info | mask : p_object->info & ~mask;
}

static void object_string_init(object_string* p_object_string, const ok_string_view p_string, object_specs* p_specs) {
  object_init(&p_object_string->object, OBJ_STRING);
  p_object_string->string = create_string_from_string_view(p_string, p_specs->alloc);
  p_object_string->hash = default_hash_str(p_string);
}

static void object_string_deinit(object_string* p_object_string, object_specs* p_specs) {
  string_deinit(&p_object_string->string, p_specs->alloc);
  object_deinit(&p_object_string->object);
}

object_string* create_object_string(const ok_string_view p_string, object_specs* p_specs) {
  object_string* object_str = (object_string*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_string));
  if (object_str == NULL) {
    return NULL;
  }
  object_string_init(object_str, p_string, p_specs);
  object_store_append(p_specs->store, (object*)object_str);
  return object_str;
}

static void object_function_init(object_function* p_object_function,
                                 object_string* p_name,
                                 uint16_t p_arity,
                                 object_specs* p_specs) {
  object_init(&p_object_function->object, OBJ_FUNCTION);
  p_object_function->name = p_name;
  p_object_function->arity = p_arity;
  p_object_function->ndefaults = 0;       // compiler sets this when the function is complete
  p_object_function->registers = 0;       // compiler sets this when the function is complete
  p_object_function->results = 1;         // compiler raises this when every return path agrees on n
  p_object_function->has_varargs = false; // compiler sets this when the last parameter is `rest...`
  upvalue_descriptor_array_init(&p_object_function->upvalues, p_specs->alloc);
  chunk_init(&p_object_function->chunk, p_specs->alloc);
  p_object_function->patched_by = NULL;
  p_object_function->calls_super_init = false;
}

static void object_function_deinit(object_function* p_object_function, object_specs* p_specs) {
  chunk_deinit(&p_object_function->chunk);
  upvalue_descriptor_array_deinit(&p_object_function->upvalues, p_specs->alloc);
  p_object_function->arity = 0;
  p_object_function->name = NULL;
  object_deinit(&p_object_function->object);
}

object_function* create_object_function(object_string* p_name, uint16_t p_arity, object_specs* p_specs) {
  object_function* object_fu = (object_function*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_function));
  if (object_fu == NULL) {
    return NULL;
  }
  object_function_init(object_fu, p_name, p_arity, p_specs);
  object_store_append(p_specs->store, (object*)object_fu);
  return object_fu;
}

ARRAY_DEFINE_DEFAULT(upvalue_array, object_upvalue*, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE(upvalue_descriptor_array, upvalue_descriptor, uint16_t, UINT16_MAX, ARRAY_DEFAULT_TYPE_DEINIT)

static void object_closure_init(object_closure* p_closure, object_function* p_function, object_specs* p_specs) {
  object_init(&p_closure->object, OBJ_CLOSURE);
  p_closure->function = p_function;
  upvalue_array_init(&p_closure->upvalues, p_specs->alloc);
  upvalue_array_grow(&p_closure->upvalues, p_function->upvalues.count);
  p_closure->owner = NULL;
}

static void object_closure_deinit(object_closure* p_closure, object_specs* p_specs) {
  upvalue_array_deinit(&p_closure->upvalues, p_specs->alloc);
  p_closure->function = NULL;
  object_deinit(&p_closure->object);
}

object_closure* create_object_closure(object_function* p_function, object_specs* p_specs) {
  object_closure* closure = (object_closure*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_closure));
  if (closure == NULL) {
    return NULL;
  }
  object_closure_init(closure, p_function, p_specs);
  object_store_append(p_specs->store, (object*)closure);
  return closure;
}

static void object_upvalue_init(object_upvalue* p_object_upvalue, uint32_t p_location, object_specs* p_specs) {
  object_init(&p_object_upvalue->object, OBJ_UPVALUE);
  p_object_upvalue->info = p_location & 0x00ffffff;
  p_object_upvalue->next = NULL;
  p_object_upvalue->closed = NULL_AS_VALUE();
}

static void object_upvalue_deinit(object_upvalue* p_object_upvalue, object_specs* p_specs) {
  p_object_upvalue->closed = NULL_AS_VALUE();
  p_object_upvalue->next = NULL;
  p_object_upvalue->info = 0;
  object_deinit(&p_object_upvalue->object);
}

void object_upvalue_set_location(object_upvalue* p_upvalue, uint32_t p_location) {
  p_upvalue->info = (p_upvalue->info & 0xff000000) | (p_location & 0x00ffffff);
}

void object_upvalue_set_closed(object_upvalue* p_upvalue) {
  p_upvalue->info |= UINT32_C(1) << 24;
}

uint32_t object_upvalue_get_location(object_upvalue* p_upvalue) {
  return p_upvalue->info & 0x00ffffff;
}

bool object_upvalue_is_closed(object_upvalue* p_upvalue) {
  return (p_upvalue->info & (UINT32_C(1) << 24)) != 0;
}

value* object_upvalue_get_value(object_upvalue* p_upvalue, value* p_frame_window) {
  if (object_upvalue_is_closed(p_upvalue)) {
    return &p_upvalue->closed;
  }
  return &p_frame_window[object_upvalue_get_location(p_upvalue)];
}

object_upvalue* create_object_upvalue(uint32_t p_location, object_specs* p_specs) {
  object_upvalue* object_up = (object_upvalue*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_upvalue));
  if (object_up == NULL) {
    return NULL;
  }
  object_upvalue_init(object_up, p_location, p_specs);
  object_store_append(p_specs->store, (object*)object_up);
  return object_up;
}

void object_native_value_init(object_native_value* p_native_value,
                              void* p_user_data,
                              ok_native_destructor p_destructor,
                              ok_native_tracer p_tracer,
                              object_specs* p_specs) {
  object_init(&p_native_value->object, OBJ_NATIVE_VALUE);
  p_native_value->user_data = p_user_data;
  p_native_value->destructor = p_destructor;
  p_native_value->tracer = p_tracer;
}

void object_native_value_deinit(object_native_value* p_native_value, object_specs* p_specs) {
  if (p_native_value->destructor) {
    p_native_value->destructor(p_native_value->user_data);
  }
  p_native_value->user_data = NULL;
  object_deinit(&p_native_value->object);
}

object_native_value* create_object_native_value(void* p_user_data,
                                                ok_native_destructor p_destructor,
                                                ok_native_tracer p_tracer,
                                                object_specs* p_specs) {
  object_native_value* native_value =
      (object_native_value*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_native_value));
  if (native_value == NULL) {
    return NULL;
  }
  object_native_value_init(native_value, p_user_data, p_destructor, p_tracer, p_specs);
  object_store_append(p_specs->store, (object*)native_value);
  return native_value;
}

void object_native_function_init(object_native_function* p_native_function,
                                 ok_string_view p_name,
                                 uint16_t p_arity,
                                 ok_native_fn p_cb,
                                 object_specs* p_specs) {
  object_init(&p_native_function->object, OBJ_NATIVE_FUNCTION);
  p_native_function->name = create_string_from_string_view(p_name, p_specs->alloc);
  p_native_function->arity = p_arity;
  p_native_function->function = p_cb;
}

void object_native_function_deinit(object_native_function* p_native_function, object_specs* p_specs) {
  string_deinit(&p_native_function->name, p_specs->alloc);
  p_native_function->arity = 0;
  p_native_function->function = NULL;
  object_deinit(&p_native_function->object);
}

object_native_function*
create_object_native_function(ok_string_view p_name, uint16_t p_arity, ok_native_fn p_cb, object_specs* p_specs) {
  object_native_function* native_function =
      (object_native_function*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_native_function));
  if (native_function == NULL) {
    return NULL;
  }
  object_native_function_init(native_function, p_name, p_arity, p_cb, p_specs);
  object_store_append(p_specs->store, (object*)native_function);
  return native_function;
}

static void object_array_init(object_array* p_object_array, object_specs* p_specs) {
  object_init(&p_object_array->object, OBJ_ARRAY);
  value_array_init(&p_object_array->values, p_specs->alloc);
}

static void object_array_deinit(object_array* p_object_array, object_specs* p_specs) {
  // element values are gc owned (value_array's element deinit is a no-op), this only
  // releases the backing buffer.
  value_array_deinit(&p_object_array->values, p_specs->alloc);
  object_deinit(&p_object_array->object);
}

object_array* create_object_array(object_specs* p_specs) {
  object_array* object_arr = (object_array*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_array));
  if (object_arr == NULL) {
    return NULL;
  }
  object_array_init(object_arr, p_specs);
  object_store_append(p_specs->store, (object*)object_arr);
  return object_arr;
}

static void object_map_init(object_map* p_object_map, object_specs* p_specs) {
  object_init(&p_object_map->object, OBJ_MAP);
  value_map_init(&p_object_map->entries, p_specs->alloc);
  p_object_map->version = 0;
}

static void object_map_deinit(object_map* p_object_map, object_specs* p_specs) {
  // entry keys and values are gc owned (the instantiation's deinits are no-ops),
  // this only releases the parallel backing buffers.
  value_map_deinit(&p_object_map->entries);
  object_deinit(&p_object_map->object);
}

object_map* create_object_map(object_specs* p_specs) {
  object_map* object_m = (object_map*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_map));
  if (object_m == NULL) {
    return NULL;
  }
  object_map_init(object_m, p_specs);
  object_store_append(p_specs->store, (object*)object_m);
  return object_m;
}

// method + field tables: object_string* keys, content-compared probes over
// the cached FNV-1a. keys are shared, not owned: they are the class body
// chunk's string constants, kept alive by the method protos the class itself
// traces, so the per-entry deinits are no-ops.
#define SM_KEY_IS_EMPTY(k) ((k) == NULL)
#define SM_KEY_SET_EMPTY(k) (*(k) = NULL)
#define SM_PROBE_HASH(p) ((p)->hash)
#define SM_PROBE_MATCHES(p, k)                                                                                \
  (string_get_length(&(p)->string) == string_get_length(&(k)->string) &&                                     \
   memcmp((p)->string.chars, (k)->string.chars, string_get_length(&(p)->string)) == 0)
#define SM_KEY_CLONE(p, a) (p)
#define SM_NO_DEINIT(k, a) ((void)0)

MAP_DEFINE(method_map,
           object_string*,
           method_entry,
           const object_string*,
           SM_KEY_IS_EMPTY,
           SM_KEY_SET_EMPTY,
           SM_PROBE_HASH,
           SM_PROBE_MATCHES,
           SM_KEY_CLONE,
           SM_NO_DEINIT,
           SM_NO_DEINIT)

MAP_DEFINE(field_map,
           object_string*,
           field_entry,
           const object_string*,
           SM_KEY_IS_EMPTY,
           SM_KEY_SET_EMPTY,
           SM_PROBE_HASH,
           SM_PROBE_MATCHES,
           SM_KEY_CLONE,
           SM_NO_DEINIT,
           SM_NO_DEINIT)

ARRAY_DEFINE_DEFAULT(object_string_array, object_string*, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(uint8_array, uint8_t, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(object_closure_array, object_closure*, ARRAY_DEFAULT_TYPE_DEINIT)

static void object_class_init(object_class* p_class,
                              object_string* p_name,
                              object_class* p_parent,
                              object_specs* p_specs) {
  object_init(&p_class->object, OBJ_CLASS);
  p_class->name = p_name;
  p_class->parent = p_parent;
  p_class->nfields = 0;
  p_class->nown = 0;
  object_string_array_init(&p_class->own_names, p_specs->alloc);
  uint8_array_init(&p_class->own_muts, p_specs->alloc);
  object_closure_array_init(&p_class->own_inits, p_specs->alloc);
  field_map_init(&p_class->fields, p_specs->alloc);
  method_map_init(&p_class->methods, p_specs->alloc);
  method_map_init(&p_class->statics, p_specs->alloc);
  object_string_array_init(&p_class->own_static_names, p_specs->alloc);
  uint8_array_init(&p_class->own_static_muts, p_specs->alloc);
  value_array_init(&p_class->static_values, p_specs->alloc);
  field_map_init(&p_class->static_fields, p_specs->alloc);
  p_class->frozen = false;
}

static void object_class_deinit(object_class* p_class, object_specs* p_specs) {
  // keys and method entries are shared, not owned (constants, closures):
  // only the backing buffers die here.
  object_string_array_deinit(&p_class->own_names, p_specs->alloc);
  uint8_array_deinit(&p_class->own_muts, p_specs->alloc);
  object_closure_array_deinit(&p_class->own_inits, p_specs->alloc);
  field_map_deinit(&p_class->fields);
  method_map_deinit(&p_class->methods);
  method_map_deinit(&p_class->statics);
  object_string_array_deinit(&p_class->own_static_names, p_specs->alloc);
  uint8_array_deinit(&p_class->own_static_muts, p_specs->alloc);
  value_array_deinit(&p_class->static_values, p_specs->alloc);
  field_map_deinit(&p_class->static_fields);
  object_deinit(&p_class->object);
}

object_class* create_object_class(object_string* p_name, object_class* p_parent, object_specs* p_specs) {
  object_class* klass = (object_class*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_class));
  if (klass == NULL) {
    return NULL;
  }
  object_class_init(klass, p_name, p_parent, p_specs);
  object_store_append(p_specs->store, (object*)klass);
  return klass;
}

bool object_class_add_field(object_class* p_class,
                            const object_string* p_name,
                            bool p_mut,
                            object_closure* p_init_thunk,
                            const object_string** p_previous_owner) {
  if (p_class->frozen) {
    return false;
  }
  // redeclaration check against the frozen ancestors first: their maps are
  // complete, so one probe covers the whole inherited layout.
  for (const object_class* c = p_class->parent; c != NULL; c = c->parent) {
    if (field_map_get(&c->fields, p_name) != NULL) {
      if (p_previous_owner != NULL) {
        *p_previous_owner = c->name;
      }
      return false;
    }
  }
  // then own declarations so far (this class's map is only built at freeze).
  for (uint32_t i = 0; i < p_class->own_names.count; ++i) {
    if (p_class->own_names.data[i] == p_name ||
        (string_get_length(&p_class->own_names.data[i]->string) == string_get_length(&p_name->string) &&
         memcmp(p_class->own_names.data[i]->string.chars, p_name->string.chars, string_get_length(&p_name->string)) ==
             0)) {
      if (p_previous_owner != NULL) {
        *p_previous_owner = p_class->name;
      }
      return false;
    }
  }
  if (!object_string_array_append(&p_class->own_names, (object_string*)p_name) ||
      !uint8_array_append(&p_class->own_muts, p_mut ? 1u : 0u) ||
      !object_closure_array_append(&p_class->own_inits, p_init_thunk)) {
    return false;
  }
  p_class->nown++;
  return true;
}

bool object_class_freeze(object_class* p_class) {
  if (p_class->frozen) {
    return true;
  }
  if (p_class->parent != NULL) {
    p_class->nfields = (uint16_t)(p_class->parent->nfields + p_class->nown);
  } else {
    p_class->nfields = p_class->nown;
  }
  if (p_class->nfields > aprime_MAX) {
    // the field ops address slots with 12-bit operands.
    return false;
  }
  // flat map, parent segments first so a subclass's own entries land after
  // (redeclaration was already rejected at add_field time).
  for (const object_class* c = p_class->parent; c != NULL; c = c->parent) {
    for (uint32_t i = 0; i < c->own_names.count; ++i) {
      const field_entry* inherited = field_map_get(&c->fields, c->own_names.data[i]);
      if (inherited == NULL || !field_map_set(&p_class->fields, c->own_names.data[i], *inherited)) {
        return false;
      }
    }
  }
  const uint16_t base = p_class->parent != NULL ? p_class->parent->nfields : 0;
  for (uint32_t i = 0; i < p_class->own_names.count; ++i) {
    const field_entry entry = {(uint16_t)(base + i), p_class->own_muts.data[i]};
    if (!field_map_set(&p_class->fields, p_class->own_names.data[i], entry)) {
      return false;
    }
  }
  p_class->frozen = true;
  return true;
}

const field_entry* object_class_find_field(const object_class* p_class, const object_string* p_name) {
  return field_map_get(&p_class->fields, p_name);
}

const method_entry* object_class_find_method(const object_class* p_class, const object_string* p_name) {
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    const method_entry* entry = method_map_get(&c->methods, p_name);
    if (entry != NULL) {
      return entry;
    }
  }
  return NULL;
}

bool object_class_is_a(const object_class* p_class, const object_class* p_maybe_ancestor) {
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    if (c == p_maybe_ancestor) {
      return true;
    }
  }
  return false;
}

bool object_class_add_static_field(object_class* p_class,
                                    const object_string* p_name,
                                    bool p_mut,
                                    value p_value,
                                    const object_string** p_previous_owner) {
  if (p_class->frozen) {
    return false;
  }
  // one name, one slot, chain-wide: an inherited static may never be shadowed
  // by a redeclaration, exactly like instance fields.
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    if (field_map_get(&c->static_fields, p_name) != NULL) {
      if (p_previous_owner != NULL) {
        *p_previous_owner = c->name;
      }
      return false;
    }
  }
  const field_entry entry = {(uint16_t)p_class->static_values.count, p_mut ? 1u : 0u};
  if (!object_string_array_append(&p_class->own_static_names, (object_string*)p_name) ||
      !uint8_array_append(&p_class->own_static_muts, p_mut ? 1u : 0u) ||
      !value_array_append(&p_class->static_values, p_value) ||
      !field_map_set(&p_class->static_fields, p_name, entry)) {
    return false;
  }
  return true;
}

const field_entry* object_class_find_static(const object_class* p_class,
                                             const object_string* p_name,
                                             const object_class** p_owner) {
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    const field_entry* entry = field_map_get(&c->static_fields, p_name);
    if (entry != NULL) {
      if (p_owner != NULL) {
        *p_owner = c;
      }
      return entry;
    }
  }
  return NULL;
}

const method_entry* object_class_find_static_method(const object_class* p_class, const object_string* p_name) {
  for (const object_class* c = p_class; c != NULL; c = c->parent) {
    const method_entry* entry = method_map_get(&c->statics, p_name);
    if (entry != NULL) {
      return entry;
    }
  }
  return NULL;
}

object_instance* create_object_instance(object_class* p_class, object_specs* p_specs) {
  object_instance* instance = (object_instance*)p_specs->alloc->allocate(
      p_specs->alloc, sizeof(object_instance) + p_class->nfields * sizeof(value));
  if (instance == NULL) {
    return NULL;
  }
  object_init(&instance->object, OBJ_INSTANCE);
  instance->klass = p_class;
  for (uint32_t i = 0; i < p_class->nfields; ++i) {
    instance->fields[i] = NULL_AS_VALUE();
  }
  object_store_append(p_specs->store, (object*)instance);
  return instance;
}

object_bound_method* create_object_bound_method(value p_receiver, const method_entry* p_entry, object_specs* p_specs) {
  object_bound_method* bound =
      (object_bound_method*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_bound_method));
  if (bound == NULL) {
    return NULL;
  }
  object_init(&bound->object, OBJ_BOUND_METHOD);
  bound->receiver = p_receiver;
  bound->entry = p_entry;
  object_store_append(p_specs->store, (object*)bound);
  return bound;
}

void object_dispatch_deinit(object* p_object, object_specs* p_specs) {
  switch (object_get_type(p_object)) {
  case OBJ_STRING:
    object_string_deinit((object_string*)p_object, p_specs);
    break;
  case OBJ_FUNCTION:
    object_function_deinit((object_function*)p_object, p_specs);
    break;
  case OBJ_CLOSURE:
    object_closure_deinit((object_closure*)p_object, p_specs);
    break;
  case OBJ_UPVALUE:
    object_upvalue_deinit((object_upvalue*)p_object, p_specs);
    break;
  case OBJ_NATIVE_VALUE:
    object_native_value_deinit((object_native_value*)p_object, p_specs);
    break;
  case OBJ_NATIVE_FUNCTION:
    object_native_function_deinit((object_native_function*)p_object, p_specs);
    break;
  case OBJ_ARRAY:
    object_array_deinit((object_array*)p_object, p_specs);
    break;
  case OBJ_MAP:
    object_map_deinit((object_map*)p_object, p_specs);
    break;
  case OBJ_CLASS:
    object_class_deinit((object_class*)p_object, p_specs);
    break;
  case OBJ_INSTANCE:
    // the inline fields array dies with the allocation
    object_deinit(p_object);
    break;
  case OBJ_BOUND_METHOD:
    object_deinit(p_object);
    break;
  }
}

void objects_list_deinit(object* p_head, object_specs* p_specs) {
  while (p_head != NULL) {
    object* temp = p_head->next; // copy now since object_deinit nullifies the next pointer.
    object_dispatch_deinit(p_head, p_specs);
    p_specs->alloc->release(p_specs->alloc, p_head);
    p_head = temp;
  }
}

void object_debug_print(object* p_object) {
  switch (object_get_type(p_object)) {
  case OBJ_STRING: {
    printf("%s", ((object_string*)p_object)->string.chars);
    break;
  }
  case OBJ_FUNCTION: {
    object_function* fu = (object_function*)p_object;
    printf("<fu %s a: %d>", fu->name->string.chars, fu->arity);
    break;
  }
  case OBJ_CLOSURE: {
    object_closure* closure = (object_closure*)p_object;
    printf("<closure %s a: %d u: %d>",
           closure->function->name->string.chars,
           closure->function->arity,
           closure->function->upvalues.count);
    break;
  }
  case OBJ_UPVALUE: {
    printf("<upvalue %p>", p_object);
    break;
  }
  case OBJ_NATIVE_VALUE: {
    printf("<native value %p>", ((object_native_value*)p_object)->user_data);
    break;
  }
  case OBJ_NATIVE_FUNCTION: {
    object_native_function* fu = (object_native_function*)p_object;
    printf("<native function %s a: %d %p>", fu->name.chars, fu->arity, fu->function);
    break;
  }
  case OBJ_ARRAY: {
    printf("<array %u>", ((object_array*)p_object)->values.count);
    break;
  }
  case OBJ_MAP: {
    printf("<map %u>", ((object_map*)p_object)->entries.count);
    break;
  }
  case OBJ_CLASS: {
    printf("<class %s>", ((object_class*)p_object)->name->string.chars);
    break;
  }
  case OBJ_INSTANCE: {
    printf("<instance %s>", ((object_instance*)p_object)->klass->name->string.chars);
    break;
  }
  case OBJ_BOUND_METHOD: {
    const object_bound_method* bound = (const object_bound_method*)p_object;
    printf("<method %s>", bound->entry->name->string.chars);
    break;
  }
  }
}
