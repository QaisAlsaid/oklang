// the OR-3 sample native module: one function, one class, one constant.
// the build is a raw gcc command on purpose (M12: any toolchain, no
// coupling): gcc -shared -fPIC -I<repo>/include -o hello.so hello.c
//
// the shape to copy for real modules: OK_API on the one export, the api
// stashed at export time, every reach into the vm through it.

#include <stdio.h>
#include <string.h>

#include "ok/ok_module.h"

static const ok_api* api = NULL;
static const char* g_prefix = "unloaded"; // on_load proves it ran

// greet(name): "hello, <name>!"
static ok_value hello_greet(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc != 1 || api->value_type(p_argv[0]) != OK_TYPE_STR) {
    api->native_error(p_ok, api->view("greet expects one string.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  // the read side's views are NUL-terminated vm strings: strlen is the
  // native-side length (ok.h's ok_cstring_view_get_length is a library
  // symbol, and a native links against none of those)
  ok_cstring_view name = api->value_as_string(p_argv[0]);
  char buffer[256];
  size_t room = strlen(name.chars);
  if (room > 200) {
    room = 200;
  }
  snprintf(buffer, sizeof(buffer), "%s, %.*s!", g_prefix, (int)room, name.chars);
  return api->value_string(p_ok, api->view(buffer, OK_STRING_VIEW_CALCULATE_LENGTH));
}

// Greeter.greet(name): the class-method lane (the receiver rides argv[0])
static ok_value greeter_greet(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc != 2 || api->value_type(p_argv[1]) != OK_TYPE_STR) {
    api->native_error(p_ok, api->view("Greeter.greet expects one string.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  ok_cstring_view name = api->value_as_string(p_argv[1]);
  char buffer[256];
  size_t room = strlen(name.chars);
  if (room > 200) {
    room = 200;
  }
  snprintf(buffer, sizeof(buffer), "hi, %.*s!", (int)room, name.chars);
  return api->value_string(p_ok, api->view(buffer, OK_STRING_VIEW_CALCULATE_LENGTH));
}

// the non-function surface: a class and a constant, exported through
// api->module_export inside the mount callback.
static bool hello_mount(ok* p_ok) {
  ok_value greeter = api->class_new(p_ok, api->view("Greeter", 7), api->value_null());
  if (api->value_type(greeter) != OK_TYPE_CLASS) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, greeter, api->view("greet", 5), 2, greeter_greet,
                                       true)) {
    return false;
  }
  if (!api->class_freeze(p_ok, greeter)) {
    return false;
  }
  if (!api->module_export(p_ok, api->view("Greeter", 7), greeter)) {
    return false;
  }
  return api->module_export(p_ok, api->view("magic", 5), api->value_number(42));
}

static bool hello_on_load(ok* p_ok) {
  (void)p_ok;
  g_prefix = "hello";
  return true;
}

static const ok_native_fn_def hello_fns[] = {
    {"greet", 1, hello_greet},
};

static const ok_native_module hello_module = {
    OK_NATIVE_ABI,
    "hello",
    "1.0.0",
    1,
    hello_fns,
    hello_mount,
    hello_on_load,
    NULL, // on_unload: the sample holds no C resources
};

// the one symbol. the module checks the host back: the both-directions
// discipline (we may refuse a host we do not know).
OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  return &hello_module;
}
