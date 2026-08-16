#include "object.h"
#include "object_store.h"
#include <stdio.h>
#include <stdlib.h>

void object_init(object* p_object, uint32_t p_type) {
  p_object->info = p_type & 0x00ffffff;
}

void object_deinit(object* p_object) {
}

uint32_t object_get_type(object* p_object) {
  return p_object->info & 0x00ffffff;
}

void object_string_init(object_string* p_object_string, string p_string) {
  object_init(&p_object_string->object, OBJ_STRING);
  p_object_string->string = p_string;
}

void object_string_deinit(object_string* p_object_string) {
  string_deinit(&p_object_string->string);
  object_deinit(&p_object_string->object);
}

object_string* create_object_string(string p_string, object_store* p_store) {
  object_string* object_str = (object_string*)malloc(sizeof(object_string));
  if (object_str == NULL) {
    return NULL;
  }
  object_string_init(object_str, p_string);

  object_store_append(p_store, (object*)object_str);
  return object_str;
}

void object_dispatch_deinit(object* p_object) {
  switch (object_get_type(p_object)) {
  case OBJ_STRING:
    object_string_deinit((object_string*)p_object);
    break;
  }
}

void objects_list_deinit(object* p_head) {
  while (p_head != NULL) {
    object* temp = p_head->next; // copy now since object_deinit nullifies the next pointer.
    object_dispatch_deinit(p_head);
    free(p_head);
    p_head = temp->next;
  }
}

void object_debug_print(object* p_object) {
  switch (object_get_type(p_object)) {
  case OBJ_STRING:
    printf("%s", ((object_string*)p_object)->string.chars);
  }
}
