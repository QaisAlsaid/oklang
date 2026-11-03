// tests/capi/src/main.c - the coro C API harness (patch 42).
//
// drives ok_coro_new / ok_coro_resume / ok_coro_status / ok_coro_current
// and ok_native_suspend through real scripts: the two-way resume channel,
// error death (the R22 defer sweep from the C lane), the native park at
// every call shape (plain call, spread, method, defer exit), nested resume
// from inside a native, and the barrier refusals.
//
// pure C99 on purpose: the public header must stay C-clean. the aggressive
// gc build runs this binary too - every host-held value that crosses an
// allocation is rooted or consumed first, that discipline is part of what
// is under test.

#include <ok/ok.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;

#define CHECK(p_cond)                                                                                                 \
  do {                                                                                                                \
    ++g_checks;                                                                                                       \
    if (!(p_cond)) {                                                                                                  \
      fprintf(stderr, "FAILED: %s (%s:%d)\n", #p_cond, __FILE__, __LINE__);                                           \
      return 1;                                                                                                       \
    }                                                                                                                 \
  } while (0)

// the print hook captures script output so defer/sweep lanes are observable
// without stdout comparison.
static char g_log[4096];
static size_t g_log_len = 0;

static void log_reset(void) {
  g_log[0] = '\0';
  g_log_len = 0;
}

static bool log_has(const char* p_needle) {
  return strstr(g_log, p_needle) != NULL;
}

static void log_print(void* p_ctx, ok_value p_value) {
  (void)p_ctx;
  if (!ok_value_is_string(p_value)) {
    return;
  }
  ok_cstring_view s = ok_value_as_string(p_value);
  if (s.chars != NULL && g_log_len + 1 < sizeof(g_log)) {
    const size_t n = strlen(s.chars);
    const size_t room = sizeof(g_log) - g_log_len - 1;
    memcpy(g_log + g_log_len, s.chars, n < room ? n : room);
    g_log_len += n < room ? n : room;
    g_log[g_log_len++] = '\n';
    g_log[g_log_len] = '\0';
  }
  ok_cstring_view_deinit(&s);
}

// the suspend-requesting native: parks its caller at the return boundary,
// answers its argument doubled. serves both spellings - the plain function
// park(x) (argc 1) and the Cap method tick (argc 2, receiver first) - so
// every call shape rides one suspension path. also records the running
// coro as seen from inside a native, and whether the current boundary
// allowed the request.
static ok_value g_seen_current;
static bool g_last_suspend_granted;

static ok_value native_park(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  CHECK(p_argc == 1 || p_argc == 2);
  const ok_value payload = p_argv[p_argc - 1];
  CHECK(ok_value_is_number(payload));
  g_seen_current = ok_coro_current(p_ok);
  g_last_suspend_granted = ok_native_suspend(p_ok);
  if (!g_last_suspend_granted) {
    return ok_value_null(); // the error lane carries the refusal
  }
  return ok_value_number(ok_value_as_number(payload) * 2.0);
}

// a native that drives another coro from C (the parked-resumer chain from
// the host lane): one resume per call. captures the coro it runs ON, so a
// test can prove the native executed inside the caller's state.
static ok_value native_drive(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  CHECK(p_argc == 1);
  CHECK(ok_value_is_coro(p_argv[0]));
  g_seen_current = ok_coro_current(p_ok);
  ok_value out = ok_value_null();
  CHECK(ok_coro_resume(p_ok, p_argv[0], ok_value_null(), false, &out));
  return out;
}

// a native that tries to resume the RUNNING coro (itself): must fail and
// fail its own call through the pending error lane.
static ok_value native_self_resume(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  (void)p_argv;
  ok_value self = ok_coro_current(p_ok);
  CHECK(ok_value_is_coro(self));
  ok_value out = ok_value_null();
  CHECK(!ok_coro_resume(p_ok, self, ok_value_null(), false, &out));
  return ok_value_null(); // never reached by the script: the lane is pending
}

static ok_string_view sv(const char* p_chars) {
  return ok_create_string_view(p_chars, OK_STRING_VIEW_CALCULATE_LENGTH, true);
}

// reads :what: out of a materialized {:kind:, :what:} error map.
static bool error_what_contains(ok* p_ok, const char* p_needle) {
  ok_value error = ok_last_error(p_ok);
  if (!ok_value_is_map(error)) {
    return false;
  }
  ok_value key = ok_value_string(p_ok, sv("what"));
  ok_value what = ok_value_null();
  if (!ok_map_get(error, key, &what)) {
    return false;
  }
  if (!ok_value_is_string(what)) {
    return false;
  }
  ok_cstring_view text = ok_value_as_string(what);
  const bool found = text.chars != NULL && strstr(text.chars, p_needle) != NULL;
  ok_cstring_view_deinit(&text);
  return found;
}

static bool run(ok* p_ok, const char* p_code) {
  log_reset();
  ok_source source = {p_code, "capi.ok"};
  return ok_run(p_ok, source) == OK;
}

int main(void) {
  ok_specs specs = {};
  ok* p_ok = ok_create(specs);
  CHECK(p_ok != NULL);

  ok_set_print(p_ok, log_print, NULL);

  CHECK(ok_global_define_native_function(p_ok, sv("park"), 1, native_park));
  CHECK(ok_global_define_native_function(p_ok, sv("drive"), 1, native_drive));
  CHECK(ok_global_define_native_function(p_ok, sv("self_resume"), 0, native_self_resume));

  // a C-defined class with a native method: the OP_INVOKE park lane.
  ok_value cap = ok_class_new(p_ok, sv("Cap"), ok_value_null());
  CHECK(ok_value_is_class(cap));
  CHECK(ok_class_define_native_method(p_ok, cap, sv("tick"), 2, native_park, true));
  CHECK(ok_class_freeze(p_ok, cap));
  CHECK(ok_global_define(p_ok, sv("Cap"), cap, false));

  // ---------- 1. construct, drive, the two-way channel ----------
  CHECK(run(p_ok,
            "glob fu worker(n) {\n"
            "  print(\"start \" + n as str);\n"
            "  let got = yield n;\n"
            "  print(\"got \" + got as str);\n"
            "  return \"done \" + got as str;\n"
            "}\n"));

  ok_value body = ok_value_null();
  CHECK(ok_global_get(p_ok, sv("worker"), &body));
  CHECK(!ok_value_is_coro(body));

  // a non-closure body is refused with null
  CHECK(ok_value_is_null(ok_coro_new(p_ok, ok_value_number(1))));

  ok_value coro = ok_coro_new(p_ok, body);
  CHECK(ok_value_is_coro(coro));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_READY);

  // on the main state, between runs: no running coro
  CHECK(ok_value_is_null(ok_coro_current(p_ok)));

  const uint32_t root = ok_root(p_ok, coro);
  CHECK(root != OK_ROOT_INVALID);

  ok_value out = ok_value_null();
  CHECK(ok_coro_resume(p_ok, coro, ok_value_number(7), true, &out));
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 7.0); // yield n
  CHECK(log_has("start 7"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_SUSPENDED);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_string(p_ok, sv("hi")), true, &out));
  CHECK(ok_value_is_string(out));
  ok_cstring_view done = ok_value_as_string(out);
  CHECK(done.chars != NULL && strcmp(done.chars, "done hi") == 0);
  ok_cstring_view_deinit(&done);
  CHECK(log_has("got hi"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);

  // R21 from the C lane: a dead coro answers null, not an error
  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(ok_value_is_null(out));

  CHECK(ok_unroot(p_ok, root));

  // ---------- 2. error death: the lane, the sweep, the status ----------
  CHECK(run(p_ok,
            "fu close(tag) {\n"
            "  print(\"closing \" + tag);\n"
            "  return true;\n"
            "}\n"
            "glob fu dying() {\n"
            "  defer close(\"a\");\n"
            "  defer close(\"b\");\n"
            "  yield 1;\n"
            "  let boom = 1 + \"x\";\n"
            "  return 0;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("dying"), &body));
  coro = ok_coro_new(p_ok, body);
  CHECK(ok_value_is_coro(coro));
  const uint32_t root2 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 1.0);

  ok_clear_error(p_ok);
  CHECK(!ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(ok_value_is_null(out));
  CHECK(error_what_contains(p_ok, "invalid operands")); // the transplanted lane
  CHECK(log_has("closing b") && log_has("closing a"));  // the R22 death sweep ran, lifo
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  ok_clear_error(p_ok);

  CHECK(ok_unroot(p_ok, root2));

  // ---------- 3. the native park, plain call shape ----------
  CHECK(run(p_ok,
            "glob fu plain_worker() {\n"
            "  let got = park(21);\n"
            "  print(\"plain woke \" + got as str);\n"
            "  return got;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("plain_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root3 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(g_last_suspend_granted);
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 42.0); // the native's result goes out
  CHECK(g_seen_current == coro);                                     // word identity: the running coro
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_SUSPENDED);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_string(p_ok, sv("late")), true, &out));
  CHECK(ok_value_is_string(out));
  ok_cstring_view woke = ok_value_as_string(out);
  CHECK(woke.chars != NULL && strcmp(woke.chars, "late") == 0); // the wake value landed in `got`
  ok_cstring_view_deinit(&woke);
  CHECK(log_has("plain woke late"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  CHECK(ok_unroot(p_ok, root3));

  // ---------- 4. the native park, spread shape ----------
  CHECK(run(p_ok,
            "glob fu spread_worker() {\n"
            "  let args = [4];\n"
            "  let got = park(args...);\n"
            "  print(\"spread woke \" + got as str);\n"
            "  return got;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("spread_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root4 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(g_last_suspend_granted);
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 8.0);
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_SUSPENDED);
  CHECK(ok_coro_resume(p_ok, coro, ok_value_number(9), true, &out));
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 9.0);
  CHECK(log_has("spread woke 9"));
  CHECK(ok_unroot(p_ok, root4));

  // ---------- 5. the native park, method shape (OP_INVOKE) ----------
  CHECK(run(p_ok,
            "glob fu method_worker() {\n"
            "  let c = Cap();\n"
            "  let got = c.tick(5);\n"
            "  print(\"method woke \" + got as str);\n"
            "  return \"fin\";\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("method_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root5 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(g_last_suspend_granted);
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 10.0);
  CHECK(g_seen_current == coro);
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_SUSPENDED);
  CHECK(ok_coro_resume(p_ok, coro, ok_value_string(p_ok, sv("go")), true, &out));
  CHECK(ok_value_is_string(out));
  ok_cstring_view fin = ok_value_as_string(out);
  CHECK(fin.chars != NULL && strcmp(fin.chars, "fin") == 0);
  ok_cstring_view_deinit(&fin);
  CHECK(log_has("method woke go"));
  CHECK(ok_unroot(p_ok, root5));

  // ---------- 6. the native park on the normal defer exit ----------
  CHECK(run(p_ok,
            "glob fu defer_worker() {\n"
            "  defer park(3);\n"
            "  print(\"before\");\n"
            "  return 2;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("defer_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root6 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(g_last_suspend_granted);
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 6.0); // parked mid-cleanup
  CHECK(log_has("before"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_SUSPENDED);
  CHECK(ok_coro_resume(p_ok, coro, ok_value_number(5), true, &out));
  CHECK(ok_value_is_number(out) && ok_value_as_number(out) == 2.0); // the body's return, after the chain
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  CHECK(ok_unroot(p_ok, root6));

  // ---------- 7. nested resume from inside a native ----------
  // drive() resumes the inner coro once from C: inner suspends (the outer
  // state parks on the resume chain while it runs), the native answers the
  // yielded value, and the outer body keeps going - the suspension does NOT
  // propagate through the native. the outer body then hands the still-
  // suspended inner coro OUT as its return value; the host drives it the
  // rest of the way after its creator is dead.
  CHECK(run(p_ok,
            "glob fu outer() {\n"
            "  let inner = coro(fu() { yield \"inner-out\"; return \"inner-done\"; });\n"
            "  let got = drive(inner);\n"
            "  print(\"inner gave \" + got as str);\n"
            "  return inner;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("outer"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root7 = ok_root(p_ok, coro);

  CHECK(ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(g_seen_current == coro); // the native ran on the outer coro's state
  CHECK(log_has("inner gave inner-out"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  CHECK(ok_unroot(p_ok, root7));

  // the escaped inner coro: still suspended, resumable from C
  CHECK(ok_value_is_coro(out));
  const uint32_t root7b = ok_root(p_ok, out);
  CHECK(ok_coro_status(p_ok, out) == OK_CORO_SUSPENDED);
  ok_value inner_final = ok_value_null();
  CHECK(ok_coro_resume(p_ok, out, ok_value_null(), false, &inner_final));
  CHECK(ok_value_is_string(inner_final));
  ok_cstring_view inner2 = ok_value_as_string(inner_final);
  CHECK(inner2.chars != NULL && strcmp(inner2.chars, "inner-done") == 0);
  ok_cstring_view_deinit(&inner2);
  CHECK(ok_coro_status(p_ok, out) == OK_CORO_DEAD);
  CHECK(ok_unroot(p_ok, root7b));

  // ---------- 8. resume of a running coro, from inside a native ----------
  CHECK(run(p_ok,
            "glob fu selfish() {\n"
            "  let got = self_resume();\n"
            "  return got;\n"
            "}\n"));

  CHECK(ok_global_get(p_ok, sv("selfish"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root8 = ok_root(p_ok, coro);

  ok_clear_error(p_ok);
  CHECK(!ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(error_what_contains(p_ok, "resume of a running coroutine"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  ok_clear_error(p_ok);
  CHECK(ok_unroot(p_ok, root8));

  // ---------- 9. the refusals (the yield barrier doctrine) ----------
  ok_clear_error(p_ok);
  CHECK(!run(p_ok, "park(1);\n"));
  CHECK(error_what_contains(p_ok, "cannot suspend outside a coroutine"));
  ok_clear_error(p_ok);

  // the barrier lanes need a coro around them (otherwise "outside a
  // coroutine" fires first): the operator and constructor refusals
  CHECK(run(p_ok,
            "glob fu op_worker() {\n"
            "  class Box {\n"
            "    pub fu operator +(other) {\n"
            "      return park(1);\n"
            "    }\n"
            "  }\n"
            "  let b = Box() + Box();\n"
            "  return 0;\n"
            "}\n"));
  CHECK(ok_global_get(p_ok, sv("op_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root9a = ok_root(p_ok, coro);
  ok_clear_error(p_ok);
  CHECK(!ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(error_what_contains(p_ok, "cannot suspend from inside an operator method"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  ok_clear_error(p_ok);
  CHECK(ok_unroot(p_ok, root9a));

  CHECK(run(p_ok,
            "glob fu init_worker() {\n"
            "  class Bot {\n"
            "    fu init() {\n"
            "      park(1);\n"
            "    }\n"
            "  }\n"
            "  let b = Bot();\n"
            "  return 0;\n"
            "}\n"));
  CHECK(ok_global_get(p_ok, sv("init_worker"), &body));
  coro = ok_coro_new(p_ok, body);
  const uint32_t root9b = ok_root(p_ok, coro);
  ok_clear_error(p_ok);
  CHECK(!ok_coro_resume(p_ok, coro, ok_value_null(), false, &out));
  CHECK(error_what_contains(p_ok, "cannot suspend from inside a constructor"));
  CHECK(ok_coro_status(p_ok, coro) == OK_CORO_DEAD);
  ok_clear_error(p_ok);
  CHECK(ok_unroot(p_ok, root9b));

  // ---------- 10. status of a non-coro, teardown ----------
  CHECK(ok_coro_status(p_ok, ok_value_number(1)) == UINT32_MAX);
  CHECK(!ok_value_is_coro(ok_value_string(p_ok, sv("x"))));

  ok_free(p_ok);

  printf("capi: %d checks OK\n", g_checks);
  return 0;
}
