#ifndef OK_H
#define OK_H

#include "compiler.h"
#include "object_store.h"
#include "source.h"
#include "vm.h"

typedef enum {
  OK,
  OK_PARSE_ERROR,
  OK_COMPILE_ERROR,
  OK_RUNTIME_ERROR,
} ok_result;

typedef struct {
  object_store objects_store;
  compiler compiler;
  vm vm;
  source source;
} ok;

// creates parser + compiler + vm, sets up pipline
void ok_init(ok* p_ok);
void ok_free(ok* p_ok);
// runs the pipline with the source.
// takes ownership of code and path strings.
ok_result ok_run(ok* p_ok, source p_source);
#endif // OK_H
