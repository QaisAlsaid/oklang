#ifndef OK_OBJECT_H
#define OK_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "okchunk.h"
#include "okobject_store.h"
#include "okstring.h"
#include "okvalue.h"

#define OBJECT_TYPE(value) object_get_type(VALUE_AS_OBJECT(value))

#define IS_VALUE_STRING(value) value_is_object_type(value, OBJ_STRING)
#define IS_VALUE_FUNCTION(value) value_is_object_type(value, OBJ_FUNCTION)
#define IS_VALUE_CLOSURE(value) value_is_object_type(value, OBJ_CLOSURE)
#define IS_VALUE_UPVALUE(value) value_is_object_type(value, OBJ_UPVALUE)

#define VALUE_AS_STRING(value) ((object_string*)VALUE_AS_OBJECT(value))
#define VALUE_AS_FUNCTION(value) ((object_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CLOSURE(value) ((object_closure*)VALUE_AS_OBJECT(value))
#define VALUE_AS_UPVALUE(value) ((object_upvalue*)VALUE_AS_OBJECT(value))

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
} object_type;

typedef struct object object;
struct object {
  uint32_t info; // 24bit for type, and rest of bits for later usage
  object* next;
};

uint32_t object_get_type(object* p_object);

static inline bool value_is_object_type(value p_value, object_type p_type) {
  return IS_VALUE_OBJECT(p_value) && OBJECT_TYPE(p_value) == p_type;
}

typedef struct {
  object object;
  string string;
} object_string;

object_string* create_object_string(const string_view p_string, object_store* p_store);

typedef struct {
  object object;
  object_string* name;
  chunk chunk;
  uint8_t arity;
  uint32_t upvalues;
} object_function;

object_function* create_object_function(object_string* p_name, uint8_t p_arity, object_store* p_store);

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

object_upvalue* create_object_upvalue(uint32_t p_location, object_store* p_store);

ARRAY_DECLARE_DEFAULT(upvalue_array, object_upvalue*)
typedef struct {
  object object;
  object_function* function;
  upvalue_array upvalues;
} object_closure;

object_closure* create_object_closure(object_function* p_function, object_store* p_store);

void object_dispatch_deinit(object* p_object);
void objects_list_deinit(object* p_head);

void object_debug_print(object* p_object);
#endif // OK_OBJECT_H
