#ifndef OK_H
#define OK_H

#include <stdbool.h>

#include "ok/ok_fwd.h"
#include "ok/ok_source.h"
#include "ok/ok_specs.h"
#include "ok/ok_string.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef enum {
  OK,
  OK_PARSE_ERROR,
  OK_COMPILE_ERROR,
  OK_RUNTIME_ERROR,
} ok_result;

/* runtime */

// creates parser + compiler + vm, sets up pipline
ok* ok_create(ok_specs p_specs);
void ok_free(ok* p_ok);
// runs the pipline with the source.
// takes ownership of code and path strings.
ok_result ok_run(ok* p_ok, ok_source p_source);

/* values */
typedef uint64_t ok_value;

ok_value ok_value_null(void);
ok_value ok_value_bool(bool p_value);
ok_value ok_value_number(double p_value);
ok_value ok_value_string(ok* p_ok, ok_string_view p_string);

bool ok_value_is_null(ok_value p_value);
bool ok_value_is_bool(ok_value p_value);
bool ok_value_is_number(ok_value p_value);
bool ok_value_is_object(ok_value p_value);
bool ok_value_is_string(ok_value p_value);

bool ok_value_as_bool(ok_value p_value);
double ok_value_as_number(ok_value p_value);
ok_cstring_view ok_value_as_string(ok_value p_value);

/* globals */
bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable);

/* native */
typedef ok_value (*ok_native_fn)(ok* p_ok, uint8_t p_argc, const ok_value* p_argv);
bool ok_global_define_native_function(ok* p_ok, ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb);

bool ok_native_error(ok* p_ok, ok_string_view p_message);

typedef void (*ok_native_destructor)(void* p_data);
ok_value ok_native_value_new(ok* p_ok, void* p_user_data, ok_native_destructor p_destructor);

void* ok_value_as_native_value(ok_value p_value);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_H
