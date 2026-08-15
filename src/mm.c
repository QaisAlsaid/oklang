#include "mm.h"
#include <stdlib.h>

void* reallocate(void* p_ptr, size_t p_old_size, size_t p_new_size) {
  if (p_new_size == 0) {
    free(p_ptr);
    return NULL;
  }

  void* sz = realloc(p_ptr, p_new_size);
  return sz;
}
