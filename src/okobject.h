#ifndef OK_OBJECT_H
#define OK_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "okobject_store.h"
#include "okutils.h"
#include "okvalue.h"

#define OBJECT_TYPE(value) object_get_type(VALUE_AS_OBJECT(value))

#define IS_VALUE_STRING(value) value_is_object_type(value, OBJ_STRING)

#define VALUE_AS_STRING(value) ((object_string*)VALUE_AS_OBJECT(value))

typedef enum {
  OBJ_STRING,
} object_type;

typedef struct object object;
struct object {
  uint32_t info; // 24bit for type, and rest of bits for later usage
  object* next;
};

void object_init(object* p_object, uint32_t p_type);
void object_deinit(object* p_object);
uint32_t object_get_type(object* p_object);

static inline bool value_is_object_type(value p_value, object_type p_type) {
  return IS_VALUE_OBJECT(p_value) && OBJECT_TYPE(p_value) == p_type;
}

struct object_string {
  object object;
  string string;
};
typedef struct object_string object_string;

void object_string_init(object_string* p_object_string, const string_view p_string);
void object_string_deinit(object_string* p_object_string);
object_string* create_object_string(const string_view p_string, object_store* p_store);

void object_dispatch_deinit(object* p_object);
void objects_list_deinit(object* p_head);

void object_debug_print(object* p_object);
#endif // OK_OBJECT_H
