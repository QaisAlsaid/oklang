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
// runs the pipline with the source. reads p_source.code and p_source.path
// during the call, your buffers are yours. the path is interned (copied) so diagnostics can name it
// after the run. the code is copied only when source retention is on.
ok_result ok_run(ok* p_ok, ok_source p_source);

// opt-in source retention (default off).
// when on: every ok_run's source is copied into the ok state and kept alive until ok_free.
// runtime reports can reference source text for functions compiled by earlier runs
// when off: the code is borrowed for the duration of each ok_run call only.
// later reports on leftover functions print the right path and line:col,
// without referencing code.
// embedders that never reuse buffers past ok_run pay nothing.
void ok_retain_source(ok* p_ok, bool p_retain);

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

// calls a script closure or native and yields only one result.
// multi-result (span) script functions are legal callees: ok_call exposes only the first result
bool ok_call(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_result);

/* globals */
bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable);
bool ok_global_get(ok* p_ok, ok_string_view p_name, ok_value* p_value);

/* native */
typedef ok_value (*ok_native_fn)(ok* p_ok, uint8_t p_argc, const ok_value* p_argv);

// register with this arity to accept any argument count and validate inside
// the callback (used by the polymorphic core builtins: 'remove' takes 3
// arguments for arrays, 2 for maps; 'get' takes 2 or 3 for maps).
#define OK_NATIVE_ARITY_ANY UINT8_MAX

bool ok_global_define_native_function(ok* p_ok, ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb);

// the message lane: raises inside the native,
// crosses into script catch as the map {:kind: "native", :what: <message>}
// (message sugar) (use ok_native_throw to send any value whole).
bool ok_native_error(ok* p_ok, ok_string_view p_message);

// the value lane: the value crosses into the script's catch binder as-is,
// exactly like a script `throw v`. only valid inside a native callback (like ok_native_error).
// a host error that no script catches fails the ok_call/ok_run. read it with ok_last_error.
bool ok_native_throw(ok* p_ok, ok_value p_value);

// the error from the most recent failure (ok_run/ok_call)
// on this state: your thrown value as-is,
// or (for runtime-raised errors) a fresh  {:kind: "...", :what: "..."} map.
// root it (ok_root) if you need it past your next ok allocation.
// a null value means "no error". a caught error clears it. ok_clear_error clears it explicitly. the next ok_run also
// clears it. a failed ok_call prints nothing. reporting is your call.
ok_value ok_last_error(ok* p_ok);

// clears the ok_last_error lane.
void ok_clear_error(ok* p_ok);

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

/* maps */

// builds a new map from p_pair_count (key, value) pairs laid out flat {k0, v0, k1, v1, ...}.
// p_pairs may be NULL when p_pair_count is 0.
// duplicate keys: the later pair wins, same as the literal.
// returns a null value on allocation failure or an illegal key (null / unhashable).
ok_value ok_map_new(ok* p_ok, const ok_value* p_pairs, uint32_t p_pair_count);

bool ok_value_is_map(ok_value p_value);

// 0 for anything that is not a map.
uint32_t ok_map_len(ok_value p_value);

// read-only, allocation-free. false = not a map, illegal key, or absent.
// the stored value (even a stored null) lands in *p_out only on success (true).
bool ok_map_get(ok_value p_map, ok_value p_key, ok_value* p_out);

// insert or overwrite. the map, key and value are guarded against the
// collector for the duration of the call (the set may grow).
bool ok_map_set(ok* p_ok, ok_value p_map, ok_value p_key, ok_value p_value);

// removes the key. false = not a map, illegal key, or absent
// (no store happens either way). never allocates.
bool ok_map_remove(ok_value p_map, ok_value p_key);

/* classes (embedder side) */

bool ok_value_is_class(ok_value p_value);

// a C-defined class: created unfrozen (define its native methods, then
// ok_class_freeze). p_parent is a class value, or a null value for Object.
// returns a null value on failure. the result is not rooted: the define and
// freeze calls below guard it across their own allocations, but anything
// else you allocate in between (or keeping it past the sequence) is your
// rooting job - ok_root it, same contract as an ok_array_new result.
// script sees the frozen class like any other: store it in a global and
// construct instances with C(args).
ok_value ok_class_new(ok* p_ok, ok_string_view p_name, ok_value p_parent);

// define one native method on an unfrozen class. the receiver rides argv[0]
// and the arity COUNTS it (`fu m(x)` on the script side is arity 2 here);
// OK_NATIVE_ARITY_ANY defers the check to the callback. false = p_class is
// not a class value, the class is sealed, a same-name redeclaration, a
// private override of a pub ancestor, an arity disagreement with an
// ancestor's dispatch, or OOM.
bool ok_class_define_native_method(ok* p_ok,
                                    ok_value p_class,
                                    ok_string_view p_name,
                                    uint16_t p_arity_including_receiver,
                                    ok_native_fn p_cb,
                                    bool p_pub);

// seal the class (C15): builds the flat field map and freezes the tables.
// after this, definitions are refused forever.
bool ok_class_freeze(ok* p_ok, ok_value p_class);

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
