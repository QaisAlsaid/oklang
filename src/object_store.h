#ifndef OK_OBJECT_STORE_H
#define OK_OBJECT_STORE_H

#include <stdbool.h>

typedef struct {
  struct object* objects;
} object_store;

void object_store_init(object_store* p_store);
void object_store_deinit(object_store* p_store);
void object_store_append(object_store* p_store, struct object* p_object);

#endif // OK_OBJECT_STORE_H
