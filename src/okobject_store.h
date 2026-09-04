#ifndef OK_OBJECT_STORE_H
#define OK_OBJECT_STORE_H

#include "okfwd.h"
#include <stdbool.h>

struct object_store {
  object* objects;
  allocators* alloc;
};

void object_store_init(object_store* p_store, allocators* p_alloc);
void object_store_deinit(object_store* p_store);
void object_store_append(object_store* p_store, object* p_object);

#endif // OK_OBJECT_STORE_H
