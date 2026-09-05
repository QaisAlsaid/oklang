#ifndef OK_OBJECT_H
#define OK_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok.h"
#include "okchunk.h"
#include "okfwd.h"
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

#define VALUE_AS_STRING(value) ((object_string*)VALUE_AS_OBJECT(value))
#define VALUE_AS_FUNCTION(value) ((object_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CLOSURE(value) ((object_closure*)VALUE_AS_OBJECT(value))
#define VALUE_AS_UPVALUE(value) ((object_upvalue*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_VALUE(value) ((object_native_type*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_FUNCTION(value) ((object_native_function*)VALUE_AS_OBJECT(value))

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
} object_type;

struct object {
  uint32_t info; // 24bit for type + 1 bit for is marked and rest of bits for later usage
  object* next;
};

uint32_t object_get_type(object* p_object);
bool object_is_marked(object* p_object);
void object_set_marked(object* p_object, bool p_marked);

static inline bool value_is_object_type(value p_value, object_type p_type) {
  return IS_VALUE_OBJECT(p_value) && OBJECT_TYPE(p_value) == p_type;
}

typedef struct {
  object object;
  string string;
} object_string;

object_string* create_object_string(const ok_string_view p_string, object_specs* p_specs);

typedef struct {
  object object;
  object_string* name;
  chunk chunk;
  uint8_t arity;
  uint32_t upvalues;
} object_function;

object_function* create_object_function(object_string* p_name, uint8_t p_arity, object_specs* p_specs);

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
value* object_upvalue_get_value(object_upvalue* p_upvalue, value* p_stack);

object_upvalue* create_object_upvalue(uint32_t p_location, object_specs* p_specs);

ARRAY_DECLARE_DEFAULT(upvalue_array, object_upvalue*)
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
} object_native_value;

object_native_value*
create_object_native_value(void* p_user_data, ok_native_destructor p_destructor, object_specs* p_specs);

typedef struct {
  object object;
  string name;
  uint8_t arity;
  ok_native_fn function;
} object_native_function;

object_native_function*
create_object_native_function(ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb, object_specs* p_specs);

void object_dispatch_deinit(object* p_object, object_specs* p_specs);
void objects_list_deinit(object* p_head, object_specs* p_specs);

void object_debug_print(object* p_object);
#endif // OK_OBJECT_H
