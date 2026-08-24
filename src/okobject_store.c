#include "okobject_store.h"
#include "okobject.h"

void object_store_init(object_store* p_store) {
  p_store->objects = NULL;
}

void object_store_deinit(object_store* p_store) {
  objects_list_deinit(p_store->objects);
}

void object_store_append(object_store* p_store, struct object* p_object) {
  p_object->next = p_store->objects;
  p_store->objects = p_object;
}
