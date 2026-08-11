#include "ok.h"
#include "stdlib.h"

void ok_init(ok* p_ok) {
  p_ok->compiler = malloc(sizeof(compiler));
  p_ok->vm = malloc(sizeof(vm));
  compiler_init(p_ok->compiler);
  vm_init(p_ok->vm);
}

run_result ok_run(ok* p_ok, const char* p_src) {
  compiler_compile(p_ok->compiler, p_src);
  return RUN_OK;
}

void ok_free(ok* p_ok) {
  free(p_ok->compiler);
  free(p_ok->vm);
}
