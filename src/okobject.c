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

static void object_string_init(object_string* p_object_string, const string_view p_string) {
  object_init(&p_object_string->object, OBJ_STRING);
  p_object_string->string = create_string_from_string_view(p_string);
}

static void object_string_deinit(object_string* p_object_string) {
  string_deinit(&p_object_string->string);
  object_deinit(&p_object_string->object);
}

object_string* create_object_string(const string_view p_string, object_store* p_store) {
  object_string* object_str = (object_string*)malloc(sizeof(object_string));
  if (object_str == NULL) {
    return NULL;
  }
  object_string_init(object_str, p_string);
  object_store_append(p_store, (object*)object_str);
  return object_str;
}

static void object_function_init(object_function* p_object_function, object_string* p_name, uint8_t p_arity) {
  object_init(&p_object_function->object, OBJ_FUNCTION);
  p_object_function->name = p_name;
  p_object_function->arity = p_arity;
  chunk_init(&p_object_function->chunk);
}

static void object_function_deinit(object_function* p_object_function) {
  chunk_deinit(&p_object_function->chunk);
  p_object_function->arity = 0;
  p_object_function->name = NULL;
  object_deinit(&p_object_function->object);
}

object_function* create_object_function(object_string* p_name, uint8_t p_arity, object_store* p_store) {
  object_function* object_fu = (object_function*)malloc(sizeof(object_function));
  if (object_fu == NULL) {
    return NULL;
  }
  object_function_init(object_fu, p_name, p_arity);
  object_store_append(p_store, (object*)object_fu);
  return object_fu;
}

void object_dispatch_deinit(object* p_object) {
  switch (object_get_type(p_object)) {
  case OBJ_STRING:
    object_string_deinit((object_string*)p_object);
    break;
  case OBJ_FUNCTION:
    object_function_deinit((object_function*)p_object);
    break;
  }
}

void objects_list_deinit(object* p_head) {
  while (p_head != NULL) {
    object* temp = p_head->next; // copy now since object_deinit nullifies the next pointer.
    object_dispatch_deinit(p_head);
    free(p_head);
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
  }
}
