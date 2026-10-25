#include <ok/ok.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_destructor_calls = 0;

static ok_value native_add(ok* p_ok, uint8_t argc, const ok_value* argv) {
  (void)p_ok;

  assert(argc == 2);
  assert(ok_value_is_number(argv[0]));
  assert(ok_value_is_number(argv[1]));

  double a = ok_value_as_number(argv[0]);
  double b = ok_value_as_number(argv[1]);

  return ok_value_number(a + b);
}

static ok_value native_echo(ok* p_ok, uint8_t argc, const ok_value* argv) {
  (void)p_ok;

  assert(argc == 1);

  return argv[0];
}

static ok_value native_fail(ok* p_ok, uint8_t argc, const ok_value* argv) {
  (void)argc;
  (void)argv;

  ok_native_error(p_ok, ok_create_string_view("native failure", OK_STRING_VIEW_CALCULATE_LENGTH, true));

  return ok_value_null();
}

// regression (2026-10-25, dorg3 "invalid call: value is not callable" + SIGSEGV):
// a native that calls back into the vm while the script's call stack is live.
// prefix this crashed two ways: vm_call double-based the callee slot (read
// data[base + base] instead of data[base] -> "value is not callable" whenever
// ok_call ran with top > 0), and when the inner call errored, call_native
// returned success and the interpreter resumed on a cleared call stack
// (frame reload from data[count - 1] with count == 0 -> segfault right after
// the error report).
static ok_value native_apply(ok* p_ok, uint8_t argc, const ok_value* argv) {
  assert(argc == 2);
  assert(ok_value_is_object(argv[0]));
  assert(ok_value_is_number(argv[1]));

  ok_value arg = argv[1];
  ok_value result = ok_value_null();

  if (!ok_call(p_ok, argv[0], 1, &arg, &result)) {
    // inner runtime error: propagate by returning. the vm's error_pending
    // flag makes call_native fail the OP_CALL so the error reaches the host.
    return ok_value_null();
  }

  return result;
}

static void native_destructor(void* ptr) {
  assert(ptr != nullptr);

  int* value = static_cast<int*>(ptr);
  assert(*value == 44);

  delete value;
  g_destructor_calls++;
}

static ok_value native_get_opaque(ok* p_ok, uint8_t argc, const ok_value* argv) {
  (void)p_ok;

  assert(argc == 1);
  assert(ok_value_is_object(argv[0]));

  void* ptr = ok_value_as_native_value(argv[0]);
  assert(ptr != nullptr);

  int* value = static_cast<int*>(ptr);
  assert(*value == 44);

  return ok_value_number(static_cast<double>(*value));
}

static void* test_allocate(size_t size) {
  return std::malloc(size);
}

static void* test_reallocate(void* ptr, size_t new_size) {
  return std::realloc(ptr, new_size);
}

static void test_release(void* ptr) {
  std::free(ptr);
}

int main() {
  ok_specs specs = {};
  specs.allocators.allocate = test_allocate;
  specs.allocators.reallocate = test_reallocate;
  specs.allocators.release = test_release;

  ok* vm = ok_create(specs);
  assert(vm != nullptr);

  ok_value null_value = ok_value_null();
  ok_value true_value = ok_value_bool(true);
  ok_value false_value = ok_value_bool(false);
  ok_value number_value = ok_value_number(123.5);

  assert(ok_value_is_null(null_value));

  assert(ok_value_is_bool(true_value));
  assert(ok_value_is_bool(false_value));
  assert(ok_value_as_bool(true_value));
  assert(!ok_value_as_bool(false_value));

  assert(ok_value_is_number(number_value));
  assert(ok_value_as_number(number_value) == 123.5);

  assert(!ok_value_is_object(number_value));
  assert(!ok_value_is_string(number_value));

  ok_string_view hello = ok_create_string_view("hello", OK_STRING_VIEW_CALCULATE_LENGTH, true);

  ok_value hello_value = ok_value_string(vm, hello);

  assert(ok_value_is_object(hello_value));
  assert(ok_value_is_string(hello_value));

  ok_cstring_view hello_result = ok_value_as_string(hello_value);

  assert(hello_result.chars != nullptr);
  assert(std::strcmp(hello_result.chars, "hello") == 0);

  ok_cstring_view_deinit(&hello_result);

  assert(ok_global_define(
      vm, ok_create_string_view("host_number", OK_STRING_VIEW_CALCULATE_LENGTH, true), ok_value_number(44), false));

  assert(ok_global_define(
      vm, ok_create_string_view("host_mutable", OK_STRING_VIEW_CALCULATE_LENGTH, true), ok_value_number(10), true));

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("host_add", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_add));

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("host_echo", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_echo));

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("host_fail", OK_STRING_VIEW_CALCULATE_LENGTH, true), 0, native_fail));

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("host_apply", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_apply));

  int* userdata = new int(44);

  ok_value native_value = ok_native_value_new(vm, userdata, native_destructor);

  assert(ok_value_is_object(native_value));
  assert(ok_value_as_native_value(native_value) == userdata);

  assert(ok_global_define(
      vm, ok_create_string_view("host_object", OK_STRING_VIEW_CALCULATE_LENGTH, true), native_value, false));

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("host_get_object", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_get_opaque));

  ok_value global_value = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("host_number", OK_STRING_VIEW_CALCULATE_LENGTH, true), &global_value));

  assert(ok_value_is_number(global_value));
  assert(ok_value_as_number(global_value) == 44);

  assert(
      ok_global_get(vm, ok_create_string_view("host_mutable", OK_STRING_VIEW_CALCULATE_LENGTH, true), &global_value));

  assert(ok_value_is_number(global_value));
  assert(ok_value_as_number(global_value) == 10);

  assert(!ok_global_get(
      vm, ok_create_string_view("does_not_exist", OK_STRING_VIEW_CALCULATE_LENGTH, true), &global_value));

  assert(ok_value_is_null(global_value));

  const char* code = R"(
    glob fu multiply(a, b) {
      return a * b;
    }

    let a = host_add(22, 22);
    if a != 44? print "bad: add";

    if host_number != 44? print "bad: number";

    host_mutable = 99;
    if host_mutable != 99? print "bad: mutable";

    let echoed = host_echo("hello");
    if echoed != "hello"? print "bad: echo";

    let object_value = host_get_object(host_object);
    if object_value != 44? print "bad: object";

    // native -> vm callback while main's frame is on the call stack (the
    // array_foreach pattern from dorg3). exercises ok_call with top > 0.
    glob fu double_it(x) {
      return x * 2;
    }

    let applied = host_apply(double_it, 21);
    if applied != 42? print "bad: apply";
  )";

  ok_source source = {
      .from = OK_FROM_FILE,
      .code = code,
      .path = "embed",
  };

  ok_result result = ok_run(vm, source);

  assert(result == OK);

  ok_value multiply = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("multiply", OK_STRING_VIEW_CALCULATE_LENGTH, true), &multiply));

  assert(ok_value_is_object(multiply));

  ok_value multiply_args[] = {
      ok_value_number(6),
      ok_value_number(7),
  };

  ok_value multiply_result = ok_value_null();

  assert(ok_call(vm, multiply, 2, multiply_args, &multiply_result));

  assert(ok_value_is_number(multiply_result));
  assert(ok_value_as_number(multiply_result) == 42);

  ok_value host_add = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("host_add", OK_STRING_VIEW_CALCULATE_LENGTH, true), &host_add));

  ok_value add_args[] = {
      ok_value_number(22),
      ok_value_number(22),
  };

  ok_value add_result = ok_value_null();

  assert(ok_call(vm, host_add, 2, add_args, &add_result));

  assert(ok_value_is_number(add_result));
  assert(ok_value_as_number(add_result) == 44);

  ok_value bad_args[] = {
      ok_value_number(1),
  };

  ok_value bad_result = ok_value_number(123);

  assert(!ok_call(vm, host_add, 1, bad_args, &bad_result));
  assert(ok_value_is_null(bad_result));

  ok_value host_fail = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("host_fail", OK_STRING_VIEW_CALCULATE_LENGTH, true), &host_fail));

  ok_value fail_result = ok_value_number(123);

  assert(!ok_call(vm, host_fail, 0, nullptr, &fail_result));
  assert(ok_value_is_null(fail_result));

  ok_value echo = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("host_echo", OK_STRING_VIEW_CALCULATE_LENGTH, true), &echo));

  ok_value echo_args[] = {
      ok_value_number(123),
  };

  ok_value echo_result = ok_value_null();

  assert(ok_call(vm, echo, 1, echo_args, &echo_result));

  assert(ok_value_is_number(echo_result));
  assert(ok_value_as_number(echo_result) == 123);

  const char* error_code = "host_fail();\n";

  ok_source error_source = {
      .from = OK_FROM_FILE,
      .code = error_code,
      .path = "embed",
  };

  ok_value debug_global = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("host_add", OK_STRING_VIEW_CALCULATE_LENGTH, true), &debug_global));

  assert(ok_value_is_object(debug_global));

  result = ok_run(vm, error_source);

  assert(result == OK_RUNTIME_ERROR);

  // error propagation THROUGH a native callback: the closure errors inside
  // the native's ok_call; call_native must fail the OP_CALL (not resume on a
  // cleared call stack) so ok_run reports OK_RUNTIME_ERROR instead of dying.
  const char* callback_error_code = R"(
    glob fu boom(x) {
      return x + "not a number";
    }

    host_apply(boom, 1);
  )";

  ok_source callback_error_source = {
      .from = OK_FROM_FILE,
      .code = callback_error_code,
      .path = "embed",
  };

  result = ok_run(vm, callback_error_source);

  assert(result == OK_RUNTIME_ERROR);

  // and the VM must still be usable afterwards for a clean C-side call:
  assert(ok_global_get(vm, ok_create_string_view("host_add", OK_STRING_VIEW_CALCULATE_LENGTH, true), &debug_global));

  assert(ok_value_is_object(debug_global));

  ok_value recover_args[] = {
      ok_value_number(1),
      ok_value_number(2),
  };

  ok_value recover_result = ok_value_null();

  assert(ok_call(vm, debug_global, 2, recover_args, &recover_result));

  assert(ok_value_as_number(recover_result) == 3);

  ok_free(vm);

  assert(g_destructor_calls == 1);

  std::puts("embedding API: OK");
  return 0;
}
