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
  OK_INIT_ERROR,
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

/* calls */
bool ok_call(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_result);

/* globals */
bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable);
bool ok_global_get(ok* p_ok, ok_string_view p_name, ok_value* p_value);

/* native */
typedef ok_value (*ok_native_fn)(ok* p_ok, uint8_t p_argc, const ok_value* p_argv);
bool ok_global_define_native_function(ok* p_ok, ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb);

bool ok_native_error(ok* p_ok, ok_string_view p_message);

typedef void (*ok_native_destructor)(void* p_data);

// called during garbage collection while your native value is being traced:
// mark every ok_value that p_user_data still references via ok_gc_mark_value.
// must not allocate (no ok_run / ok_value_string / ok_array_push / etc...), while the
// collector is mid-walk. return false to propagate a marking failure.
typedef bool (*ok_native_tracer)(ok* p_ok, void* p_user_data);

// p_tracer may be NULL: an untraced native value holds no ok_values the
// collector needs to see.
ok_value ok_native_value_new(ok* p_ok, void* p_user_data, ok_native_destructor p_destructor, ok_native_tracer p_tracer);

void* ok_value_as_native_value(ok_value p_value);
bool ok_value_is_native_value(ok_value p_value);

/* arrays */

// builds a new array from p_count host values. p_items may be NULL when
// p_count is 0. returns a null value on allocation failure.
ok_value ok_array_new(ok* p_ok, const ok_value* p_items, uint32_t p_count);

bool ok_value_is_array(ok_value p_value);

// 0 for anything that is not an array.
uint32_t ok_array_len(ok_value p_value);

// out of bounds (or not an array): null.
ok_value ok_array_get(ok_value p_value, uint32_t p_index);

// out of bounds (or not an array): false, no store happens.
bool ok_array_set(ok_value p_value, uint32_t p_index, ok_value p_new_value);

// appends. the array and the value are guarded against the collector for the
// duration of the call. keeping either alive afterwards is the host's job
// throught either (ok_root, a traced native value, registers, or a global).
bool ok_array_push(ok* p_ok, ok_value p_array, ok_value p_value);

/* output */

// print statements route through p_cb when set. the default (NULL) keeps the
// CLI behavior: formatted stdout + newline. the callback receives the raw
// value. format with the ok_value_as_* family. the CLI's exact formatting
// is %g for numbers, true/false/null, raw string bytes, and per-type tags
// for other objects, the trailing newline is the host's responsibility.
typedef void (*ok_print_fn)(void* p_ctx, ok_value p_value);
void ok_set_print(ok* p_ok, ok_print_fn p_cb, void* p_ctx);

/* gc interop (embedder side) */

// only valid inside an ok_native_tracer callback: marks p_value (and
// everything reachable from it) as live for the collection in progress.
// any other call site fails (no-op).
bool ok_gc_mark_value(ok* p_ok, ok_value p_value);

// strong references owned by the embedder that outlive any single ok_call /
// ok_run. any value is accepted. rooting null fails. ids are recycled: an id
// is valid until its ok_unroot returns true. a stale id may address a slot
// that was handed out again and would unroot that value instead.
#define OK_ROOT_INVALID 0u
uint32_t ok_root(ok* p_ok, ok_value p_value);
bool ok_unroot(ok* p_ok, uint32_t p_root);

/* debug */

// every ok_run after this call dumps the compiled bytecode of all functions
// (main first, nested protos after) before execution. p_skip_run: compile and dump only, do not execute.
void ok_debug_dump_bytecode(ok* p_ok, bool p_enabled, bool p_skip_run);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_H
