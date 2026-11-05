#include "ok_posix.h" // FIRST: the deliberate POSIX opt-in (see its header)

#include "ok_env.h"

#include <stdlib.h>
#include <string.h>

// POSIX. glibc and friends expose it; a host without it owns the layer
// rewrite (the same stance okdylib takes with its platform tables).
extern char** environ;

const char* ok_env_get(const char* p_name) {
  return getenv(p_name);
}

bool ok_env_set(const char* p_name, const char* p_value) {
  return setenv(p_name, p_value, 1) == 0;
}

bool ok_env_unset(const char* p_name) {
  return unsetenv(p_name) == 0;
}

static int ok_env_list_cmp(const void* p_a, const void* p_b) {
  const char* const* a = (const char* const*)p_a;
  const char* const* b = (const char* const*)p_b;
  return strcmp(*a, *b);
}

bool ok_env_list(const ok_allocators* p_alloc, char*** p_out_names, uint32_t* p_out_count) {
  char** names = NULL;
  uint32_t count = 0;
  uint32_t capacity = 0;
  for (char** entry = environ; entry != NULL && *entry != NULL; ++entry) {
    const char* equals = strchr(*entry, '=');
    const size_t length = equals != NULL ? (size_t)(equals - *entry) : strlen(*entry);
    if (count == capacity) {
      const uint32_t grown = capacity == 0 ? 16 : capacity * 2;
      char** moved = (char**)p_alloc->reallocate(names, (size_t)grown * sizeof(char*));
      if (moved == NULL) {
        goto fail;
      }
      names = moved;
      capacity = grown;
    }
    char* copy = (char*)p_alloc->allocate(length + 1);
    if (copy == NULL) {
      goto fail;
    }
    memcpy(copy, *entry, length);
    copy[length] = '\0';
    names[count++] = copy;
  }
  if (count > 1) {
    qsort(names, count, sizeof(char*), ok_env_list_cmp);
  }
  // dedupe adjacent twins (a repeated name is legal in environ; the
  // first-in-sort-order survives, the walk keeps one of each)
  uint32_t unique = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (unique > 0 && strcmp(names[unique - 1], names[i]) == 0) {
      p_alloc->release(names[i]);
      continue;
    }
    names[unique++] = names[i];
  }
  *p_out_names = names;
  *p_out_count = unique;
  return true;

fail:
  for (uint32_t i = 0; i < count; ++i) {
    p_alloc->release(names[i]);
  }
  p_alloc->release(names);
  return false;
}
