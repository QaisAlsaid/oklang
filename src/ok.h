#ifndef OK_H 
#define OK_H

#include "vm.h"
#include "compiler.h"

typedef struct {
  compiler* compiler;
  vm* vm;
} ok;

typedef enum {
  RUN_OK,
  RUN_PARSE_ERROR,
  RUN_COMPILE_ERROR,
  RUN_RUNTIME_ERROR,
} run_result;

// creates parser + compiler + vm, sets up pipline 
void ok_init(ok* p_ok);
void ok_free(ok* p_ok);
// runs the pipline with the source
run_result ok_run(ok* p_ok, const char* p_src);
#endif // OK_H
