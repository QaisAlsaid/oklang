#ifndef OK_MODULE_H
#define OK_MODULE_H

// OR-3 "O Resolution" - the native module ABI.
//
// a native module is a shared object exporting exactly ONE symbol:
//
//   OK_API const ok_native_module* ok_native_export(const ok_api* p_api);
//
// it never links against oklang symbols (no -rdynamic, no collisions,
// several vms coexist in one process): every reach into the vm goes through
// the ok_api table the loader hands to ok_native_export. static embedders
// keep using ok.h directly; this header is the dynamic lane.
//
// the api table is STATIC and stateless: every entry routes through its ok*
// (or value) argument, so a module may stash the pointer at export time and
// it stays valid for the process lifetime. the same table serves every ok
// instance.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ok/ok.h"
#include "ok/ok_fwd.h"
#include "ok/ok_source.h"
#include "ok/ok_specs.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// the ABI number: a single integer, bumped on ANY change to the vtable, the
// descriptor layouts, or the tag enum. every module carries the number it
// was built against; the loader checks at load (and a module may check the
// api's number the other way, refusing a host it does not know). mismatch
// refuses with the rebuild message. no semver, no compat promises pre-1.0:
// "rebuild your natives" is the honest stance. the one discipline kept
// anyway: vtable entries are APPEND-ONLY, never repurposed, never reordered.
#define OK_NATIVE_ABI 1u

// the one symbol a native shared object exports. msvc needs dllexport on
// the building side and dllimport on the consuming side; everywhere else
// default visibility is fine (and -fvisibility=hidden builds export only
// this). the wrapper is compile-time, no runtime dispatch.
#if defined(_WIN32)
#  if defined(OK_MODULE_BUILDING)
#    define OK_API __declspec(dllexport)
#  else
#    define OK_API __declspec(dllimport)
#  endif
#else
#  define OK_API __attribute__((visibility("default")))
#endif

typedef struct ok_native_fn_def {
  const char* name;
  uint16_t arity; // counts the receiver where applicable; OK_NATIVE_ARITY_ANY allowed
  ok_native_fn call;
} ok_native_fn_def;

typedef struct ok_native_module {
  uint32_t abi;        // must equal OK_NATIVE_ABI, checked at load
  const char* name;    // self-declared; must match the import stem for graph loads
  const char* version; // the module's own version string, display only
  uint32_t fn_count;
  const ok_native_fn_def* fns;
  // the non-function surface (classes, constants): called after the fn
  // members are staged and before the namespace freezes. define classes
  // (api->class_new, api->class_define_native_method, api->class_freeze)
  // and export them, plus any constant values, with api->module_export.
  // false fails the load.
  bool (*mount_classes)(ok* p_ok);
  // lifecycle. on_load runs at load, before the namespace is built; on_unload
  // at ok_free, never mid-run (the lua lesson), and after a failed on_load.
  // on_unload is for releasing the module's own C resources: do not allocate
  // or call into the api beyond simple teardown.
  bool (*on_load)(ok* p_ok);
  void (*on_unload)(ok* p_ok);
} ok_native_module;

typedef const ok_native_module* (*ok_native_export_fn)(const ok_api* p_api);

// the type tag every value answers, the read side's discriminator. identity
// itself stays word equality (===): free, no entry needed.
typedef enum {
  OK_TYPE_NIL = 0,
  OK_TYPE_BOOL,
  OK_TYPE_NUM,
  OK_TYPE_STR,
  OK_TYPE_ARRAY,
  OK_TYPE_MAP,
  OK_TYPE_FN,
  OK_TYPE_CLASS,
  OK_TYPE_INSTANCE,
  OK_TYPE_CORO,
  OK_TYPE_NATIVE,
} ok_type_tag;

// the api vtable (M14: "the vm only cares about its vtable"). carries the
// whole ok.h surface a module needs, both directions, per Q's ruling:
// "support everything we have and expose to the abi". deliberately OUT of
// v1: method enumeration, dynamic member get/set, subclass creation from
// native, global define/get (a native's registration is its module
// membership, the global namespace stays the host's).
struct ok_api {
  uint32_t abi; // OK_NATIVE_ABI
  // view construction: the module builds the string views every other
  // entry takes (value_string, native_error, class_new, module_export,
  // ...). p_chars is borrowed for the call only, NUL-termination not
  // required. [NEW with OR-3's landing: a native cannot even spell its
  // arguments without it, the sample module found the gap]
  ok_string_view (*view)(const char* p_chars, uint64_t p_length);
  // values: the write side (construct)
  ok_value (*value_null)(void);
  ok_value (*value_string)(ok* p_ok, ok_string_view p_string);
  ok_value (*value_number)(double p_value);
  ok_value (*value_bool)(bool p_value);
  ok_value (*array_new)(ok* p_ok, const ok_value* p_items, uint32_t p_count);
  ok_value (*map_new)(ok* p_ok, const ok_value* p_pairs, uint32_t p_pair_count);
  bool (*map_set)(ok* p_ok, ok_value p_map, ok_value p_key, ok_value p_value);
  ok_value (*native_value_new)(ok* p_ok, void* p_data, ok_native_destructor p_destructor, ok_native_tracer p_tracer);
  // values: the read side (inspect + unwrap)
  ok_type_tag (*value_type)(ok_value p_value);
  double (*value_as_number)(ok_value p_value);
  bool (*value_as_bool)(ok_value p_value);
  ok_cstring_view (*value_as_string)(ok_value p_value); // owned by the value, never freed by the native;
                                                        // the chars are a NUL-terminated vm string, strlen
                                                        // is the native-side length (ok.h's
                                                        // ok_cstring_view_get_length is a library symbol)
  uint32_t (*array_len)(ok_value p_value);
  ok_value (*array_get)(ok_value p_value, uint32_t p_index); // out of bounds: null
  uint32_t (*map_len)(ok_value p_value);
  bool (*map_get)(ok_value p_map, ok_value p_key, ok_value* p_out); // false: absent/illegal/not-a-map
  void* (*value_as_native_value)(ok_value p_value);
  // type + class identity: a native type-checks its arguments constantly,
  // and a native class must recognize instances of itself (and of user
  // subclasses)
  ok_value (*classof)(ok* p_ok, ok_value p_value);
  bool (*is_instance)(ok* p_ok, ok_value p_value, ok_value p_class);
  ok_cstring_view (*class_name)(ok* p_ok, ok_value p_class); // owned by the class, never freed by the native;
                                                             // NUL-terminated, same as value_as_string
  // calls: a native invoking a script callback (sort comparators, event
  // hooks, the future asio), ok_call's exact shape
  bool (*call)(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_out);
  // classes (the ok.h door)
  ok_value (*class_new)(ok* p_ok, ok_string_view p_name, ok_value p_parent);
  bool (*class_define_native_method)(ok* p_ok,
                                     ok_value p_class,
                                     ok_string_view p_name,
                                     uint16_t p_arity_including_receiver,
                                     ok_native_fn p_cb,
                                     bool p_pub);
  bool (*class_freeze)(ok* p_ok, ok_value p_class);
  // module building: export one value under a pub name in the module being
  // loaded. ONLY valid inside mount_classes (the namespace freezes when
  // mount returns); anywhere else it fails with false.
  bool (*module_export)(ok* p_ok, ok_string_view p_name, ok_value p_value);
  // errors + roots
  bool (*native_error)(ok* p_ok, ok_string_view p_message);
  bool (*native_throw)(ok* p_ok, ok_value p_value);
  uint32_t (*root)(ok* p_ok, ok_value p_value);
  bool (*unroot)(ok* p_ok, uint32_t p_root);
  // coro primitives (the asio door)
  ok_value (*coro_new)(ok* p_ok, ok_value p_body);
  bool (*coro_resume)(ok* p_ok, ok_value p_coro, ok_value p_arg, bool p_has_arg, ok_value* p_out);
  uint32_t (*coro_status)(ok* p_ok, ok_value p_coro);
  ok_value (*coro_current)(ok* p_ok);
  bool (*native_suspend)(ok* p_ok);
};

// the dynamic-library hooks, host-provided like malloc: the host supplies
// open/sym/close from its own packaging (an audited loader, a whitelisted
// one, memory-backed test doubles), the platform wrapper is the DEFAULT
// (ok_specs.dylib NULL), and a platform with nothing gets the structural
// refusal. this is also the structural sandbox denial made concrete: a
// table whose open always fails denies loading by construction.
struct ok_dylib {
  // each returns NULL (open/sym) with a message in p_err on failure
  void* (*open)(void* p_ctx, const char* p_path, char* p_err, size_t p_err_capacity);
  void* (*sym)(void* p_ctx, void* p_handle, const char* p_name, char* p_err, size_t p_err_capacity);
  void (*close)(void* p_ctx, void* p_handle);
  void* p_ctx;
};

// the loader vtable: the host installs one per ok instance, the library
// never touches the filesystem for imports on its own. the default (never
// installed automatically for embedders, the CLI's shape): fs reads + the
// dylib-based native lane. a NULL read_source refuses every file module; a
// NULL load_native denies natives through this loader.
struct ok_loader {
  // reads the source at an absolute path into *p_out. the code buffer MUST
  // be allocated with the ok_allocators this instance was created with (the
  // embedder hands the same table in ok_specs): the instance frees it with
  // its release call after the module's tree is consumed. false =
  // unreadable. p_out->path is the canonical spelling (borrowed).
  bool (*read_source)(void* p_ctx, const char* p_path, ok_source* p_out);
  // loads a native module at an absolute path. on success: *p_out is the
  // descriptor (borrowed, valid while the module stays loaded), *p_handle
  // is an opaque handle the LOADER owns (a host loader manages its own;
  // the default lane closes its handles at ok_free). p_api is the table to
  // hand the module's ok_native_export. false = not a native / unreadable /
  // no export symbol, with a message in *p_err when p_err is non-NULL.
  bool (*load_native)(void* p_ctx,
                      const char* p_path,
                      const ok_api* p_api,
                      const ok_native_module** p_out,
                      void** p_handle,
                      char* p_err,
                      size_t p_err_capacity);
  void* p_ctx;
};

// install the loader BEFORE ok_run. replaces the default (the CLI's fs
// lane) wholesale: a host that only wants custom sources but keeps the
// native lane wraps ok_load_native_default (p_ctx = the ok*, see below).
void ok_set_loader(ok* p_ok, ok_loader p_loader);

// the default native loader, exposed for composition: opens through the
// dylib hooks the instance was created with (specs->dylib, else the
// platform wrapper), resolves ok_native_export, and hands it p_api. use it
// as a custom loader's load_native with p_ctx set to the ok*.
bool ok_load_native_default(ok* p_ok,
                            const char* p_path,
                            const ok_native_module** p_out,
                            void** p_handle,
                            char* p_err,
                            size_t p_err_capacity);

// the platform's dylib table (dlopen family / LoadLibrary), or NULL where
// the platform has nothing (the host-provided hooks are the answer there).
const ok_dylib* ok_dylib_platform(void);

// the shared-library extension this platform's native lane probes for
// (".so", ".dll"): the ladder's native candidate form.
const char* ok_dylib_extension(void);

// force-load a native module outside any project (M3's last resort,
// `ok run --native m.so app.ok`): loads through the instance's loader and
// registers it under p_name (or the descriptor's own declared name when
// p_name is NULL). scripts then import it by that bare name. the
// OK_CAP_NATIVE_LOAD gate applies; failures print their own diagnostic and
// return false.
bool ok_native_force_load(ok* p_ok, const char* p_path, const char* p_name);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_MODULE_H
