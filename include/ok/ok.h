#ifndef OK_H
#define OK_H

#include "ok/ok_source.h"
#include "ok/ok_specs.h"

typedef struct ok ok;

typedef enum {
  OK,
  OK_PARSE_ERROR,
  OK_COMPILE_ERROR,
  OK_RUNTIME_ERROR,
} ok_result;

// creates parser + compiler + vm, sets up pipline
ok* ok_create(ok_specs p_specs);
void ok_free(ok* p_ok);

// runs the pipline with the source.
// takes ownership of code and path strings.
ok_result ok_run(ok* p_ok, ok_source p_source);
#endif // OK_H
