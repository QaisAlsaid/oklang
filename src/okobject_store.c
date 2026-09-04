#include "okobject_store.h"
#include "okobject.h"

void object_store_init(object_store* p_store, allocators* p_alloc) {
  p_store->objects = NULL;
  p_store->alloc = p_alloc;
}

void object_store_deinit(object_store* p_store) {
  object_specs s = {p_store->alloc, p_store};
  objects_list_deinit(p_store->objects, &s);
}

void object_store_append(object_store* p_store, struct object* p_object) {
  p_object->next = p_store->objects;
  p_store->objects = p_object;
}
