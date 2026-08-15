#ifndef OK_H
#define OK_H

#include "compiler.h"
#include "source.h"
#include "vm.h"

typedef enum {
  RUN_OK,
  RUN_PARSE_ERROR,
  RUN_COMPILE_ERROR,
  RUN_RUNTIME_ERROR,
} run_result;

typedef struct {
  compiler* compiler;
  vm* vm;
  source source;
} ok;

// creates parser + compiler + vm, sets up pipline
void ok_init(ok* p_ok);
void ok_free(ok* p_ok);
// runs the pipline with the source.
// takes ownership of code and path strings.
run_result ok_run(ok* p_ok, source p_source);
#endif // OK_H
