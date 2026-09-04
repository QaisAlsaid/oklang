#include <stdio.h>
#include <stdlib.h>

#include "okobject.h"
#include "okobject_store.h"

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

static void object_string_init(object_string* p_object_string, const string_view p_string, object_specs* p_specs) {
  object_init(&p_object_string->object, OBJ_STRING);
  p_object_string->string = create_string_from_string_view(p_string, p_specs->alloc);
}

static void object_string_deinit(object_string* p_object_string, object_specs* p_specs) {
  string_deinit(&p_object_string->string, p_specs->alloc);
  object_deinit(&p_object_string->object);
}

object_string* create_object_string(const string_view p_string, object_specs* p_specs) {
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
                                 uint8_t p_arity,
                                 object_specs* p_specs) {
  object_init(&p_object_function->object, OBJ_FUNCTION);
  p_object_function->name = p_name;
  p_object_function->arity = p_arity;
  p_object_function->upvalues = 0;
  chunk_init(&p_object_function->chunk, p_specs->alloc);
}

static void object_function_deinit(object_function* p_object_function, object_specs* p_specs) {
  chunk_deinit(&p_object_function->chunk);
  p_object_function->upvalues = 0;
  p_object_function->arity = 0;
  p_object_function->name = NULL;
  object_deinit(&p_object_function->object);
}

object_function* create_object_function(object_string* p_name, uint8_t p_arity, object_specs* p_specs) {
  object_function* object_fu = (object_function*)p_specs->alloc->allocate(p_specs->alloc, sizeof(object_function));
  if (object_fu == NULL) {
    return NULL;
  }
  object_function_init(object_fu, p_name, p_arity, p_specs);
  object_store_append(p_specs->store, (object*)object_fu);
  return object_fu;
}

ARRAY_DEFINE_DEFAULT(upvalue_array, object_upvalue*, ARRAY_DEFAULT_TYPE_DEINIT)

static void object_closure_init(object_closure* p_closure, object_function* p_function, object_specs* p_specs) {
  object_init(&p_closure->object, OBJ_CLOSURE);
  p_closure->function = p_function;
  upvalue_array_init(&p_closure->upvalues, p_specs->alloc);
  upvalue_array_grow(&p_closure->upvalues, p_function->upvalues);
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

value* object_upvalue_get_value(object_upvalue* p_upvalue, value* p_stack) {
  if (object_upvalue_is_closed(p_upvalue)) {
    return &p_upvalue->closed;
  }
  return &p_stack[object_upvalue_get_location(p_upvalue)];
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
    printf("<fu %s %d>", fu->name->string.chars, fu->arity);
    break;
  }
  case OBJ_CLOSURE: {
    object_closure* closure = (object_closure*)p_object;
    printf("<closure %s %d %d>",
           closure->function->name->string.chars,
           closure->function->arity,
           closure->function->upvalues);
    break;
  }
  case OBJ_UPVALUE: {
    printf("<upvalue %p>", p_object);
    break;
  }
  }
}
