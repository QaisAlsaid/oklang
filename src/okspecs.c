#include "okspecs.h"
#include "ok/ok_specs.h"

#include <stdlib.h>

void patch_specs(ok_specs* p_specs) {
  // all or nothing.
  if (p_specs->allocators.reallocate == 0 || p_specs->allocators.allocate == 0 || p_specs->allocators.release == 0) {
    p_specs->allocators.reallocate = realloc;
    p_specs->allocators.allocate = malloc;
    p_specs->allocators.release = free;
  }
}
