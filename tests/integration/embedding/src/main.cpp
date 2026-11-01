#include <ok/ok.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// regression:
// a native that calls back into the vm while the script's call stack is live.
// prefix this crashed two ways: vm_call double-based the callee slot (read
// data[base + base] instead of data[base] -> "value is not callable" whenever
// ok_call ran with top > 0), and when the inner call errored, call_native
// returned success and the interpreter resumed on a cleared call stack
// (frame reload from data[count - 1] with count == 0 -> segfault right after the error report).
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

struct string_bag {
  std::vector<ok_value> items;
  int* destructor_calls;
};

static bool string_bag_trace(ok* p_ok, void* p_data) {
  auto* bag = static_cast<string_bag*>(p_data);
  for (ok_value item : bag->items) {
    if (!ok_gc_mark_value(p_ok, item)) {
      return false;
    }
  }
  return true;
}

static void string_bag_destructor(void* p_data) {
  auto* bag = static_cast<string_bag*>(p_data);
  (*bag->destructor_calls)++;
  delete bag;
}

static ok_value native_bag_item(ok* p_ok, uint8_t argc, const ok_value* argv) {
  (void)p_ok;
  assert(argc == 2);
  auto* bag = static_cast<string_bag*>(ok_value_as_native_value(argv[0]));
  assert(bag != nullptr);
  const uint32_t index = static_cast<uint32_t>(ok_value_as_number(argv[1]));
  assert(index < bag->items.size());
  return bag->items[index];
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

  ok_value native_value = ok_native_value_new(vm, userdata, native_destructor, nullptr);

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
    if a != 44 { print "bad: add"; }

    if host_number != 44 { print "bad: number"; }

    host_mutable = 99;
    if host_mutable != 99 { print "bad: mutable"; }

    let echoed = host_echo("hello");
    if echoed != "hello" { print "bad: echo"; }

    let object_value = host_get_object(host_object);
    if object_value != 44 { print "bad: object"; }

    // native -> vm callback while main's frame is on the call stack. exercises ok_call with top > 0.
    glob fu double_it(x) {
      return x * 2;
    }

    let applied = host_apply(double_it, 21);
    if applied != 42 { print "bad: apply"; }

    // SPANS: a two-result script function, destructured on the script side
    glob fu minmax(a, b) {
      if a < b { return a, b; }
      return b, a;
    }

    let (lo, hi) = minmax(3, 9);
    if lo != 3 { print "bad: lo"; }
    if hi != 9 { print "bad: hi"; }

    glob fu sum_all(nums...) {
      let mut total = 0;
      for let n : nums {
        total = total + n;
      }
      return total;
    }

    if sum_all(1, 2, 3, 4) != 10 { print "bad: sum_all"; }
    if sum_all([1, 2, 3]...) != 6 { print "bad: spread"; }
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

  // ok_call on a multi-result script function takes the first result
  // (the dynamic lane). no arity error, single-result callees unchanged.
  ok_value minmax = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("minmax", OK_STRING_VIEW_CALCULATE_LENGTH, true), &minmax));

  ok_value minmax_args[] = {
      ok_value_number(3),
      ok_value_number(9),
  };

  ok_value minmax_result = ok_value_null();

  assert(ok_call(vm, minmax, 2, minmax_args, &minmax_result));

  assert(ok_value_is_number(minmax_result));
  assert(ok_value_as_number(minmax_result) == 3); // the first of (3, 9)

  // ok_call on a varargs function. argument count is the host's
  // choice, the pack is the callee's business.
  ok_value sum_all = ok_value_null();

  assert(ok_global_get(vm, ok_create_string_view("sum_all", OK_STRING_VIEW_CALCULATE_LENGTH, true), &sum_all));

  ok_value sum_args[] = {
      ok_value_number(1),
      ok_value_number(2),
      ok_value_number(3),
      ok_value_number(4),
  };

  ok_value sum_result = ok_value_null();

  assert(ok_call(vm, sum_all, 4, sum_args, &sum_result));

  assert(ok_value_is_number(sum_result));
  assert(ok_value_as_number(sum_result) == 10);

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

  // error propagation through a native callback: the closure errors inside
  // the native's ok_call. call_native must fail the OP_CALL (not resume on a
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

  // null is the free-slot marker, so rooting it is rejected.
  assert(ok_root(vm, ok_value_null()) == OK_ROOT_INVALID);

  ok_value rooted_string = ok_value_string(vm, ok_create_string_view("rooted", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  const uint32_t root_id = ok_root(vm, rooted_string);
  assert(root_id != OK_ROOT_INVALID);
  assert(ok_unroot(vm, root_id));
  assert(!ok_unroot(vm, root_id)); // double unroot is rejected
  const uint32_t recycled_id = ok_root(vm, rooted_string);
  assert(recycled_id == root_id); // freelist recycles lifo
  assert(ok_unroot(vm, recycled_id));
  // contract demonstration: rooted_string is no longer reachable from any
  // root, so the very next allocation may sweep it. do not touch the value
  // after its unroot. negative checks use a value we know is alive.
  assert(!ok_value_is_native_value(ok_value_number(42)));

  int bag_destructor_calls = 0;
  auto* bag = new string_bag{{}, &bag_destructor_calls};

  ok_value bag_value = ok_native_value_new(vm, bag, string_bag_destructor, string_bag_trace);
  assert(ok_value_is_object(bag_value));
  assert(ok_value_is_native_value(bag_value));
  assert(ok_value_as_native_value(bag_value) == bag);

  // root before populating: from here on, every collection walks the vector's
  // current contents through string_bag_trace.
  const uint32_t bag_root = ok_root(vm, bag_value);
  assert(bag_root != OK_ROOT_INVALID);

  assert(ok_global_define_native_function(
      vm, ok_create_string_view("bag_item", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_bag_item));

  for (int i = 0; i < 8; ++i) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "bag item %d", i);
    // each creation allocates: under aggressive collection the earlier items
    // stay alive only through the rooted bag's tracer.
    bag->items.push_back(ok_value_string(vm, ok_create_string_view(buffer, OK_STRING_VIEW_CALCULATE_LENGTH, true)));
  }

  // allocation churn with the bag reachable only via the root + tracer.
  const char* churn_code = R"(
    glob fu churn(n) {
      let mut sink = "";
      for let i = 0 : n {
        sink = sink + "chunk " + i as str;
      }
      return sink;
    }

    let s = churn(300);
    if s == "" {
      print "bad: churn";
    }
  )";

  ok_source churn_source = {
      .from = OK_FROM_FILE,
      .code = churn_code,
      .path = "churn",
  };

  result = ok_run(vm, churn_source);
  assert(result == OK);

  // the strings survived the churn: read one back out through the vm.
  ok_value bag_item = ok_value_null();
  assert(ok_global_get(vm, ok_create_string_view("bag_item", OK_STRING_VIEW_CALCULATE_LENGTH, true), &bag_item));

  ok_value bag_args[] = {
      bag_value,
      ok_value_number(3),
  };

  ok_value bag_result = ok_value_null();
  assert(ok_call(vm, bag_item, 2, bag_args, &bag_result));
  assert(ok_value_is_string(bag_result));

  ok_cstring_view bag_text = ok_value_as_string(bag_result);
  assert(std::strcmp(bag_text.chars, "bag item 3") == 0);
  ok_cstring_view_deinit(&bag_text);

  assert(ok_unroot(vm, bag_root));

  ok_value empty_array = ok_array_new(vm, nullptr, 0);
  assert(ok_value_is_array(empty_array));
  assert(ok_array_len(empty_array) == 0);
  assert(ok_value_is_null(ok_array_get(empty_array, 0)));
  assert(!ok_array_set(empty_array, 0, ok_value_number(1)));

  // 16 items on purpose: the values buffer must grow mid-construction, which
  // collects under the aggressive build -> the guards must hold.
  ok_value items[16];
  for (uint32_t i = 0; i < 16; ++i) {
    items[i] = ok_value_number((double)(i * i));
  }
  ok_value squares = ok_array_new(vm, items, 16);
  assert(ok_value_is_array(squares));
  assert(ok_array_len(squares) == 16);
  assert(ok_value_as_number(ok_array_get(squares, 7)) == 49);
  assert(ok_value_is_null(ok_array_get(squares, 16))); // oob is null
  assert(!ok_value_is_array(ok_value_number(42)));
  assert(ok_array_len(ok_value_number(42)) == 0);

  assert(ok_array_set(squares, 0, ok_value_number(100)));
  assert(ok_value_as_number(ok_array_get(squares, 0)) == 100);
  assert(!ok_array_set(squares, 99, ok_value_number(1))); // oob rejected

  // hand it to the script and read back what the script did to it.
  assert(
      ok_global_define(vm, ok_create_string_view("host_array", OK_STRING_VIEW_CALCULATE_LENGTH, true), squares, false));

  assert(ok_array_push(
      vm, squares, ok_value_string(vm, ok_create_string_view("tail", OK_STRING_VIEW_CALCULATE_LENGTH, true))));
  assert(ok_array_len(squares) == 17);
  assert(ok_value_is_string(ok_array_get(squares, 16)));

  const char* array_code = R"(
    if host_array[0] != 100 {
      print "bad: host->script array read";
    }
    if len(host_array) != 17 {
      print "bad: script sees host push";
    }
    host_array[1] = 12345;
    push(host_array, 7);
  )";

  ok_source array_source = {
      .from = OK_FROM_FILE,
      .code = array_code,
      .path = "array",
  };

  result = ok_run(vm, array_source);
  assert(result == OK);

  assert(ok_value_as_number(ok_array_get(squares, 1)) == 12345); // script -> host write
  assert(ok_array_len(squares) == 18);                           // script push -> host len
  assert(ok_value_as_number(ok_array_get(squares, 17)) == 7);

  // ---- maps: the string_bag shape, table edition ----
  // host values are invisible to the collector until they land somewhere
  // traced (the contract: ok_root / a traced native / registers / a global).
  // so: build the map empty + rooted, then fill it one set at a time - each
  // key string is created immediately before its own guarded set, and every
  // stored key stays reachable through the rooted map's tracer afterwards.
  ok_value host_map = ok_map_new(vm, nullptr, 0);
  assert(ok_value_is_map(host_map));
  assert(ok_map_len(host_map) == 0);
  const uint32_t host_map_root = ok_root(vm, host_map);
  assert(host_map_root != OK_ROOT_INVALID);

  // 300 entries on purpose: the parallel arrays must grow repeatedly mid-fill,
  // which collects under the aggressive build on every single set.
  for (uint32_t i = 0; i < 300; ++i) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "key %u", i);
    const ok_value key = ok_value_string(vm, ok_create_string_view(buffer, OK_STRING_VIEW_CALCULATE_LENGTH, true));
    assert(ok_map_set(vm, host_map, key, ok_value_number((double)(i * 3))));
  }
  assert(ok_map_len(host_map) == 300);
  assert(ok_map_len(ok_value_number(42)) == 0);
  assert(!ok_value_is_map(ok_value_number(42)));

  // probe keys are held across further allocations (each ok_value_string
  // collects): an unrooted host value would dangle, and a root taken AFTER
  // later allocations would root a swept pointer - so each key is rooted the
  // moment it exists. (found the hard way: a swept "key 0" probed through
  // recycled memory and matched "key 8" - 24.0 instead of 0.)
  uint32_t key_roots[4];
  const ok_value key0 = ok_value_string(vm, ok_create_string_view("key 0", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  key_roots[0] = ok_root(vm, key0);
  const ok_value key299 = ok_value_string(vm, ok_create_string_view("key 299", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  key_roots[1] = ok_root(vm, key299);
  const ok_value key7 = ok_value_string(vm, ok_create_string_view("key 7", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  key_roots[2] = ok_root(vm, key7);
  const ok_value key8 = ok_value_string(vm, ok_create_string_view("key 8", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  key_roots[3] = ok_root(vm, key8);
  for (uint32_t i = 0; i < 4; ++i) {
    assert(key_roots[i] != OK_ROOT_INVALID);
  }

  ok_value fetched = ok_value_null();
  assert(ok_map_get(host_map, key0, &fetched));
  assert(ok_value_as_number(fetched) == 0);
  assert(ok_map_get(host_map, key299, &fetched));
  assert(ok_value_as_number(fetched) == 897);
  assert(!ok_map_get(
      host_map, ok_value_string(vm, ok_create_string_view("absent", OK_STRING_VIEW_CALCULATE_LENGTH, true)), &fetched));
  assert(!ok_map_get(host_map, ok_value_null(), &fetched)); // null key rejected
  assert(!ok_map_get(ok_value_number(42), key0, &fetched));

  // stored nulls are values, not absence: get reports found
  const ok_value null_key =
      ok_value_string(vm, ok_create_string_view("null slot", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  assert(ok_map_set(vm, host_map, null_key, ok_value_null()));
  assert(ok_map_get(host_map, null_key, &fetched));
  assert(ok_value_is_null(fetched));
  assert(ok_map_len(host_map) == 301);
  assert(ok_map_remove(host_map, null_key));
  assert(!ok_map_remove(host_map, null_key)); // absent remove: no-op, reported
  assert(ok_map_len(host_map) == 300);

  // illegal keys are rejected without storing
  assert(!ok_map_set(vm, host_map, ok_value_null(), ok_value_number(1)));
  ok_value bad_key_items[] = {ok_value_number(1)};
  const ok_value bad_key = ok_array_new(vm, bad_key_items, 1);
  assert(!ok_map_set(vm, host_map, bad_key, ok_value_number(1)));
  assert(ok_map_len(host_map) == 300);

  // ok_map_new's flat pair list with number keys (no rooting needed for
  // numbers): duplicate keys, the later pair wins, like the literal
  ok_value dupe_pairs[] = {
      ok_value_number(1),
      ok_value_number(10),
      ok_value_number(2),
      ok_value_number(20),
      ok_value_number(1),
      ok_value_number(30),
  };
  ok_value dupes = ok_map_new(vm, dupe_pairs, 3);
  assert(ok_value_is_map(dupes));
  assert(ok_map_len(dupes) == 2);
  assert(ok_map_get(dupes, ok_value_number(1), &fetched));
  assert(ok_value_as_number(fetched) == 30);
  assert(ok_map_get(dupes, ok_value_number(2), &fetched));
  assert(ok_value_as_number(fetched) == 20);
  // and the constructor rejects an illegal flat list outright
  ok_value illegal_pairs[] = {
      ok_value_number(1),
      ok_value_number(1),
      ok_value_null(),
      ok_value_number(2),
  };
  assert(ok_value_is_null(ok_map_new(vm, illegal_pairs, 2)));

  // host -> script: the script reads the host-built map and builds one for us
  assert(
      ok_global_define(vm, ok_create_string_view("host_map", OK_STRING_VIEW_CALCULATE_LENGTH, true), host_map, false));

  const char* map_code = R"(
    if host_map["key 7"] != 21 {
      print "bad: host->script map read";
    }
    if len(host_map) != 300 {
      print "bad: script sees host map len";
    }
    host_map["key 7"] = 7000;
    remove(host_map, "key 8");

    glob let script_map = {"a": 1, "b": 2};
  )";

  ok_source map_source = {
      .from = OK_FROM_FILE,
      .code = map_code,
      .path = "map",
  };

  result = ok_run(vm, map_source);
  assert(result == OK);

  // script -> host: read the script's writes back out
  assert(ok_map_get(host_map, key7, &fetched));
  assert(ok_value_as_number(fetched) == 7000); // script write
  assert(ok_map_len(host_map) == 299);         // script remove
  assert(!ok_map_get(host_map, key8, &fetched));

  ok_value script_map = ok_value_null();
  assert(ok_global_get(vm, ok_create_string_view("script_map", OK_STRING_VIEW_CALCULATE_LENGTH, true), &script_map));
  assert(ok_value_is_map(script_map));
  assert(ok_map_len(script_map) == 2);
  assert(ok_map_set(vm,
                    script_map,
                    ok_value_string(vm, ok_create_string_view("c", OK_STRING_VIEW_CALCULATE_LENGTH, true)),
                    ok_value_number(3)));
  assert(ok_map_len(script_map) == 3); // host write -> script's map
  assert(ok_map_remove(script_map,
                       ok_value_string(vm, ok_create_string_view("a", OK_STRING_VIEW_CALCULATE_LENGTH, true))));
  assert(ok_map_len(script_map) == 2);

  for (uint32_t i = 0; i < 4; ++i) {
    assert(ok_unroot(vm, key_roots[i]));
  }
  assert(ok_unroot(vm, host_map_root));

  std::vector<std::string> printed_lines;
  ok_set_print(
      vm,
      [](void* p_ctx, ok_value p_value) {
        auto* sink = static_cast<std::vector<std::string>*>(p_ctx);
        char buffer[64];
        if (ok_value_is_number(p_value)) {
          std::snprintf(buffer, sizeof(buffer), "%g", ok_value_as_number(p_value));
          sink->emplace_back(buffer);
        } else if (ok_value_is_string(p_value)) {
          ok_cstring_view text = ok_value_as_string(p_value);
          sink->emplace_back(text.chars, std::strlen(text.chars));
          ok_cstring_view_deinit(&text);
        } else if (ok_value_is_bool(p_value)) {
          sink->emplace_back(ok_value_as_bool(p_value) ? "true" : "false");
        } else if (ok_value_is_null(p_value)) {
          sink->emplace_back("null");
        } else {
          sink->emplace_back("<object>");
        }
      },
      &printed_lines);

  const char* print_code = R"(
    print 4;
    print 1.5;
    print "hello from oklang";
    print true;
    print null;
    print [1, 2, 3];
  )";

  ok_source print_source = {
      .from = OK_FROM_FILE,
      .code = print_code,
      .path = "print",
  };

  result = ok_run(vm, print_source);
  assert(result == OK);
  ok_set_print(vm, nullptr, nullptr);

  assert(printed_lines.size() == 6);
  assert(printed_lines[0] == "4");
  assert(printed_lines[1] == "1.5");
  assert(printed_lines[2] == "hello from oklang");
  assert(printed_lines[3] == "true");
  assert(printed_lines[4] == "null");
  assert(printed_lines[5] == "<object>");

  ok_free(vm);

  assert(g_destructor_calls == 1);
  assert(bag_destructor_calls == 1);

  std::puts("embedding API: OK");
  return 0;
}
