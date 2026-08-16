#include "object_store.h"
#include "object.h"

void object_store_init(object_store* p_store) {
  p_store->objects = NULL;
}

void object_store_deinit(object_store* p_store) {
  objects_list_deinit(p_store->objects);
}

void object_store_append(object_store* p_store, struct object* p_object) {
  if (p_store->objects == NULL) {
    p_store->objects = p_object;
  } else {
    p_store->objects->next = p_store->objects;
    p_store->objects = p_object;
  }
}
