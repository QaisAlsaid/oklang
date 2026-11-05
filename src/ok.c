#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK_GC_EXPOSE
#include "ok/ok.h"
#include "ok/ok_module.h"
#include "okcompiler.h"
#include "okdebug.h"
#include "ok_env.h" // P4: std.sys's environment door
#include "ok_fs.h"  // P4: std.fs's file door
#include "okgc.h"
#include "okmodules.h"
#include "okobject.h"
#include "okobject_store.h"
#include "okparser.h"
#include "okspecs.h"
#include "okstate.h"
#include "ok_time.h" // P4: std.sys's clock door
#include "okutf8.h"
#include "okutils.h"
#include "okvalue.h"
#include "okvm.h"

// the vm's natives receive the ok* per the native ABI and reach the vm through
// here (struct ok lives in okstate.h since OR-1 - the module driver shares it)
vm* ok_vm(ok* p_ok) {
  return &p_ok->vm;
}

// the core builtins are receiver-first native functions: `len(a)` and
// `a.len()` are one C entry, mounted twice (free function + method on the
// array/map class singletons). the P3 str toolkit below is methods-only (the
// class-canonical ruling), and std.math's members are module-only. errors go
// through the supported path ok_native_error -> vm_native_error.

static ok_value native_len(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (IS_VALUE_ARRAY(p_argv[0])) {
    // f64, never an int, numbers are the only public numeric type
    return NUMBER_AS_VALUE((double)VALUE_AS_ARRAY(p_argv[0])->values.count);
  }
  if (IS_VALUE_MAP(p_argv[0])) {
    return NUMBER_AS_VALUE((double)VALUE_AS_MAP(p_argv[0])->entries.count);
  }
  if (IS_VALUE_STRING(p_argv[0])) {
    // the P2 ruling: len answers CODEPOINTS, not bytes - both counts live
    // cached on the interned header; the byte length keeps its own door
    const object_string* str = (object_string*)VALUE_AS_OBJECT(p_argv[0]);
    return NUMBER_AS_VALUE((double)str->codepoints);
  }
  ok_native_error(p_ok,
                  ok_create_string_view("invalid argument: 'len' expects an array, a map, or a string.",
                                        OK_STRING_VIEW_CALCULATE_LENGTH,
                                        true));
  return NULL_AS_VALUE();
}

// classof(v) (C12): the one class every value is an instance of. never
// fails - the answer is total over the value space.
static ok_value native_classof(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return OBJECT_AS_VALUE(vm_classof(&p_ok->vm, p_argv[0]));
}

// std.io's print (OR-2): the function face of the print statement, one
// argument, the same formatting and the same hook. the statement stays
// until the stdlib version that deprecates it; this native is a module
// member only, never a seeded flat global.
static ok_value native_print_fn(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (p_ok->vm.print_cb != NULL) {
    p_ok->vm.print_cb(p_ok->vm.print_ctx, p_argv[0]);
  } else {
    value_debug_print(p_argv[0]);
    puts("");
  }
  return NULL_AS_VALUE();
}

// isinstance(v, C) (C12): the chain walk - true when C is classof(v) or one
// of its ancestors (object at the root matches everything, including null
// through nil). the exact-class check is `classof(v) == C` or the `is`
// operator; this is the family question.
static ok_value native_isinstance(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_CLASS(p_argv[1])) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "invalid argument: 'isinstance' expects a class.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return BOOL_AS_VALUE(vm_is_instance(&p_ok->vm, p_argv[0], VALUE_AS_CLASS(p_argv[1])));
}

// get(m, k) / get(m, k, default)
// absent yields null (or the given default), where [] hard-errors.
// the key rules are the map's: null and unhashable keys error, only absence is lax.
static ok_value native_get(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc < 1) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'get': expected 2 arguments (map, key) or 3 (map, key, default).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_MAP(p_argv[0])) {
    ok_native_error(
        p_ok, ok_create_string_view("invalid argument: 'get' expects a map.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (p_argc != 2 && p_argc != 3) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'get': expected 2 arguments (map, key) or 3 (map, key, default).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (IS_VALUE_STRICTLY_NULL(p_argv[1])) {
    ok_native_error(p_ok, ok_create_string_view("map key cannot be null.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (!map_key_is_legal(p_argv[1])) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid map key: only strings, numbers and booleans can be keys.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  value* slot = value_map_get(&VALUE_AS_MAP(p_argv[0])->entries, p_argv[1]);
  if (slot != NULL) {
    return *slot;
  }
  return p_argc == 3 ? p_argv[2] : NULL_AS_VALUE();
}

static ok_value native_push(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'push' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  // no gc guard needed here: the array is rooted through the caller's argument
  // window [0, top), and value_array_append only reallocs the array's
  // own buffer. it never triggers a collection. do not "fix" this in the future.
  if (!value_array_append(&arr->values, p_argv[1])) {
    ok_native_error(
        p_ok, ok_create_string_view("out of memory: failed to grow array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return NULL_AS_VALUE(); // inert
}

static ok_value native_pop(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'pop' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  if (arr->values.count == 0) {
    return NULL_AS_VALUE(); // pop on empty is null.
  }
  const value popped = arr->values.data[arr->values.count - 1];
  value_array_pop(&arr->values); // does not shrink capacity.
  return popped;
}

// remove: arrays take the half open range remove(a, m, n); maps take the key remove(m, k).
// removing an absent map key is a legal no-op (the array form's empty range is legal for the same reason).
static ok_value native_remove(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc < 1) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'remove' expects an array or a map.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  if (IS_VALUE_MAP(p_argv[0])) {
    if (p_argc != 2) {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid call to 'remove' on a map: expected 2 arguments (map, key).",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
    if (IS_VALUE_STRICTLY_NULL(p_argv[1])) {
      ok_native_error(p_ok, ok_create_string_view("map key cannot be null.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL_AS_VALUE();
    }
    if (!map_key_is_legal(p_argv[1])) {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid map key: only strings, numbers and booleans can be keys.",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
    object_map_remove(VALUE_AS_MAP(p_argv[0]), p_argv[1]); // absent = no-op
    return NULL_AS_VALUE();
  }
  if (p_argc != 3) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'remove' on an array: expected 3 arguments (array, start, end).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'remove' expects an array or a map.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  uint32_t range[2] = {0, 0};
  for (uint8_t i = 1; i < 3; ++i) {
    const value v = p_argv[i];
    if (IS_VALUE_NUMBER(v)) {
      const double d = VALUE_AS_NUMBER(v);
      // cast based integrality. the range guard runs first
      if (!(d >= MIN_SAFE_INTEGER && d <= MAX_SAFE_INTEGER) || d != (double)(int64_t)d) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove': bounds must be whole numbers.",
                                              OK_STRING_VIEW_CALCULATE_LENGTH,
                                              true));
        return NULL_AS_VALUE();
      }
      if (d < 0 || d > (double)UINT32_MAX) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL_AS_VALUE();
      }
      range[i - 1] = (uint32_t)d;
    } else if (IS_VALUE_INT(v)) {
      const int64_t iv = VALUE_AS_INT(v);
      if (iv < 0 || iv > (int64_t)UINT32_MAX) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL_AS_VALUE();
      }
      range[i - 1] = (uint32_t)iv;
    } else {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid range for 'remove': bounds must be numbers.",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  if (range[0] > range[1] || range[1] > arr->values.count) {
    ok_native_error(p_ok, ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (range[0] < range[1]) {                                  // empty range is a legal
    value_array_remove(&arr->values, range[0], range[1] - 1); // inclusive end underlying impl
  }
  return NULL_AS_VALUE();
}

// ---------------------------------------------------------------------------
// the P3 str method toolkit. methods on the str class only (the ruling: the
// class surface is the canonical surface, std.str is killed) - no free
// spellings, no hidden globals, the seeding table is untouched. every method
// is receiver-first like the rest of the class surface.
//
// matching is byte-exact over the utf-8 payload: an occurrence of a
// well-formed needle always lands on codepoint boundaries, so no decode is
// needed to find, and no piece can split a codepoint. index_of answers in
// CODEPOINTS (the s[i] unit); upper/lower fold the ASCII pairs only, every
// other byte rides through - full case mapping wants tables the core will
// not carry.

static bool str_is_ascii_space(char p_c) {
  return p_c == ' ' || p_c == '\t' || p_c == '\n' || p_c == '\r' || p_c == '\f' || p_c == '\v';
}

// naive substring search: the toolkit's needles are short, memmem is GNU.
static const char* str_find_bytes(const char* p_hay, uint64_t p_hay_len, const char* p_needle, uint64_t p_needle_len) {
  if (p_needle_len == 0) {
    return p_hay;
  }
  if (p_needle_len > p_hay_len) {
    return NULL;
  }
  const uint64_t last = p_hay_len - p_needle_len;
  for (uint64_t i = 0; i <= last; ++i) {
    if (p_hay[i] == p_needle[0] && memcmp(p_hay + i, p_needle, (size_t)p_needle_len) == 0) {
      return p_hay + i;
    }
  }
  return NULL;
}

// the receiver/argument unpack: bytes + length of a string value.
typedef struct {
  const char* chars;
  uint64_t length;
} str_bytes;

static bool str_unpack(ok_value p_value, str_bytes* p_out) {
  if (!IS_VALUE_STRING(p_value)) {
    return false;
  }
  const object_string* str = VALUE_AS_STRING(p_value);
  p_out->chars = str->string.chars;
  p_out->length = string_get_length(&str->string);
  return true;
}

// one fresh string from a byte range. the "final allocation last" contract:
// callers build everything in C memory first, this is the only GC object,
// and call_native guards it the moment the native returns.
static ok_value str_from_range(ok* p_ok, const char* p_chars, uint64_t p_length) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_string* str = create_object_string(ok_create_string_view(p_chars, p_length, false), &specs);
  if (str == NULL) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate string object.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(str);
}

// s.trim(): both ends, the six ASCII whitespaces, a new string.
static ok_value native_str_trim(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  if (!str_unpack(p_argv[0], &s)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'trim' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  uint64_t first = 0;
  uint64_t last = s.length; // one past the last byte
  while (first < last && str_is_ascii_space(s.chars[first])) {
    ++first;
  }
  while (last > first && str_is_ascii_space(s.chars[last - 1])) {
    --last;
  }
  return str_from_range(p_ok, s.chars + first, last - first);
}

// s.split(sep): non-empty separator required, no default, no magic trimming.
// returns n+1 pieces for n occurrences (python's shape, minus its implicit
// whitespace lane).
static ok_value native_str_split(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  str_bytes sep;
  if (!str_unpack(p_argv[0], &s) || !str_unpack(p_argv[1], &sep)) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'split' expects a string separator.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  if (sep.length == 0) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'split' expects a non-empty separator.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_array* arr = create_object_array(&specs);
  if (arr == NULL || !gc_guard_up(p_ok->alloc.gc, OBJECT_AS_VALUE(arr))) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate array object.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  uint64_t pos = 0;
  for (;;) {
    const char* hit = str_find_bytes(s.chars + pos, s.length - pos, sep.chars, sep.length);
    if (hit == NULL) {
      break;
    }
    const uint64_t piece_length = (uint64_t)(hit - (s.chars + pos));
    object_string* piece = create_object_string(ok_create_string_view(s.chars + pos, piece_length, false), &specs);
    if (piece == NULL || !gc_guard_up(p_ok->alloc.gc, OBJECT_AS_VALUE(piece))) {
      gc_guard_down(p_ok->alloc.gc); // the array
      ok_native_error(p_ok,
                      ok_create_string_view(
                          "out of memory: failed to allocate string object.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL_AS_VALUE();
    }
    const bool appended = value_array_append(&arr->values, OBJECT_AS_VALUE(piece));
    gc_guard_down(p_ok->alloc.gc); // the piece: the array owns it now
    if (!appended) {
      gc_guard_down(p_ok->alloc.gc); // the array
      ok_native_error(
          p_ok, ok_create_string_view("out of memory: failed to grow array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL_AS_VALUE();
    }
    pos += piece_length + sep.length;
  }
  const ok_value tail = str_from_range(p_ok, s.chars + pos, s.length - pos);
  if (IS_VALUE_STRICTLY_NULL(tail)) {
    gc_guard_down(p_ok->alloc.gc); // the array
    return NULL_AS_VALUE();        // str_from_range reported the oom
  }
  const bool appended = value_array_append(&arr->values, tail);
  gc_guard_down(p_ok->alloc.gc); // the array
  if (!appended) {
    ok_native_error(
        p_ok, ok_create_string_view("out of memory: failed to grow array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(arr);
}

// sep.join(array): the receiver is the separator. every member must already
// be a string - the explicit `as str` lane exists, join does not ride it
// implicitly (the conversion ruling, same shape as binary +).
static ok_value native_str_join(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes sep;
  if (!str_unpack(p_argv[0], &sep)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'join' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_ARRAY(p_argv[1])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'join' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const object_array* arr = VALUE_AS_ARRAY(p_argv[1]);
  uint64_t total = 0;
  for (uint32_t i = 0; i < arr->values.count; ++i) {
    str_bytes piece;
    if (!str_unpack(arr->values.data[i], &piece)) {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid argument: 'join' expects an array of strings.",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
    if (i > 0) {
      if (total > UINT64_MAX - sep.length || total + sep.length > UINT64_MAX - piece.length) {
        ok_native_error(
            p_ok,
            ok_create_string_view("out of memory: 'join' result is too large.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL_AS_VALUE();
      }
      total += sep.length;
    }
    if (total > UINT64_MAX - piece.length) {
      ok_native_error(
          p_ok,
          ok_create_string_view("out of memory: 'join' result is too large.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL_AS_VALUE();
    }
    total += piece.length;
  }
  char* scratch = (char*)p_ok->alloc.allocate(&p_ok->alloc, total);
  if (scratch == NULL) {
    ok_native_error(
        p_ok,
        ok_create_string_view("out of memory: failed to concatenate strings.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  uint64_t written = 0;
  for (uint32_t i = 0; i < arr->values.count; ++i) {
    const object_string* piece = VALUE_AS_STRING(arr->values.data[i]);
    const uint64_t piece_length = string_get_length(&piece->string);
    if (i > 0 && sep.length > 0) {
      memcpy(scratch + written, sep.chars, (size_t)sep.length);
      written += sep.length;
    }
    memcpy(scratch + written, piece->string.chars, (size_t)piece_length);
    written += piece_length;
  }
  const ok_value joined = str_from_range(p_ok, scratch, written);
  p_ok->alloc.release(&p_ok->alloc, scratch);
  return joined;
}

static ok_value native_str_contains(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  str_bytes sub;
  if (!str_unpack(p_argv[0], &s) || !str_unpack(p_argv[1], &sub)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'contains' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return BOOL_AS_VALUE(str_find_bytes(s.chars, s.length, sub.chars, sub.length) != NULL);
}

static ok_value native_str_starts_with(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  str_bytes prefix;
  if (!str_unpack(p_argv[0], &s) || !str_unpack(p_argv[1], &prefix)) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "invalid argument: 'starts_with' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return BOOL_AS_VALUE(prefix.length <= s.length && memcmp(s.chars, prefix.chars, (size_t)prefix.length) == 0);
}

static ok_value native_str_ends_with(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  str_bytes suffix;
  if (!str_unpack(p_argv[0], &s) || !str_unpack(p_argv[1], &suffix)) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "invalid argument: 'ends_with' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return BOOL_AS_VALUE(suffix.length <= s.length &&
                       memcmp(s.chars + (s.length - suffix.length), suffix.chars, (size_t)suffix.length) == 0);
}

// s.index_of(sub): the first occurrence's CODEPOINT index (the s[i] unit),
// null when absent - the get(m, k) convention for absence, not a -1 that
// arithmetic would silently eat.
static ok_value native_str_index_of(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  str_bytes sub;
  if (!str_unpack(p_argv[0], &s) || !str_unpack(p_argv[1], &sub)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'index_of' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const char* hit = str_find_bytes(s.chars, s.length, sub.chars, sub.length);
  if (hit == NULL) {
    return NULL_AS_VALUE();
  }
  return NUMBER_AS_VALUE((double)ok_utf8_count_lossy(s.chars, (uint64_t)(hit - s.chars)));
}

// the ASCII case fold: one table-less pass, every other byte rides through.
static ok_value str_ascii_case(ok* p_ok, ok_value p_value, bool p_to_upper) {
  str_bytes s;
  if (!str_unpack(p_value, &s)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'upper' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  char* scratch = (char*)p_ok->alloc.allocate(&p_ok->alloc, s.length == 0 ? 1 : s.length);
  if (scratch == NULL) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate string buffer.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  for (uint64_t i = 0; i < s.length; ++i) {
    const char c = s.chars[i];
    if (p_to_upper && c >= 'a' && c <= 'z') {
      scratch[i] = (char)(c - 'a' + 'A');
    } else if (!p_to_upper && c >= 'A' && c <= 'Z') {
      scratch[i] = (char)(c - 'A' + 'a');
    } else {
      scratch[i] = c;
    }
  }
  const ok_value folded = str_from_range(p_ok, scratch, s.length);
  p_ok->alloc.release(&p_ok->alloc, scratch);
  return folded;
}

static ok_value native_str_upper(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return str_ascii_case(p_ok, p_argv[0], true);
}

static ok_value native_str_lower(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return str_ascii_case(p_ok, p_argv[0], false);
}

// s.repeat(n): a whole, non-negative count.
static ok_value native_str_repeat(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes s;
  if (!str_unpack(p_argv[0], &s)) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'repeat' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_NUMBER(p_argv[1])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("the count for 'repeat' must be a whole number.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const double d = VALUE_AS_NUMBER(p_argv[1]);
  if (isnan(d) || d != (double)(int64_t)d || d < MIN_SAFE_INTEGER || d > MAX_SAFE_INTEGER) {
    ok_native_error(
        p_ok,
        ok_create_string_view("the count for 'repeat' must be a whole number.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (d < 0) {
    ok_native_error(
        p_ok,
        ok_create_string_view("the count for 'repeat' must not be negative.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const uint64_t count = (uint64_t)d;
  if (s.length != 0 && count > 0 && count > (uint64_t)(SIZE_MAX / 2) / s.length) {
    ok_native_error(
        p_ok,
        ok_create_string_view("out of memory: 'repeat' result is too large.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const uint64_t total = s.length * count;
  char* scratch = (char*)p_ok->alloc.allocate(&p_ok->alloc, total == 0 ? 1 : total);
  if (scratch == NULL) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate string buffer.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  for (uint64_t i = 0; i < count; ++i) {
    memcpy(scratch + i * s.length, s.chars, (size_t)s.length);
  }
  const ok_value repeated = str_from_range(p_ok, scratch, total);
  p_ok->alloc.release(&p_ok->alloc, scratch);
  return repeated;
}

// the byte/codepoint pair: len (and codepoints()) answer codepoints, bytes()
// is the embedder's byte door surfaced at last. two views, one string.
static ok_value native_str_bytes(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_STRING(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'bytes' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const object_string* str = VALUE_AS_STRING(p_argv[0]);
  return NUMBER_AS_VALUE((double)string_get_length(&str->string));
}

static ok_value native_str_codepoints(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_STRING(p_argv[0])) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "invalid argument: 'codepoints' expects a string.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  const object_string* str = VALUE_AS_STRING(p_argv[0]);
  return NUMBER_AS_VALUE((double)str->codepoints);
}

static bool ok_define_core_natives(ok* p_ok) {
  bool status = true;
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" len", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_len);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" push", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_push);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" pop", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_pop);
  // ' remove' and ' get' validate argc themselves.
  // remove is 3 for arrays / 2 for maps, get is 2 or 3
  status &= ok_global_define_native_function(p_ok,
                                             ok_create_string_view(" remove", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                             OK_NATIVE_ARITY_ANY,
                                             native_remove);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" get", OK_STRING_VIEW_CALCULATE_LENGTH, true), OK_NATIVE_ARITY_ANY, native_get);
  // the class-surface builtins (C12): classof/isinstance are plain native
  // functions like the rest, rebindable like the rest. the `is` operator and
  // `===` are the primitive lanes that never consult bindings.
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" classof", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_classof);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" isinstance", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_isinstance);
  // hidden like the rest, but deliberately NOT seeded flat: the function
  // spelling of print is std.io's member (OR-2), the statement stays the
  // flat way.
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" print", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_print_fn);
  return status;
}

// ---------------------------------------------------------------------------
// std.math (P3): the deterministic cmath surface. "deterministic" is the
// ruling's word and it means: finite in, finite out, every domain violation
// a hard located error - no NaN or infinity ever crosses back into script,
// where it would coerce silently through the arithmetic lanes. module
// members only (hidden globals, never flat seeds): math is not any class's
// surface, the way len was the containers'. the error text composes the
// function name at the call site; math_one's view spans one message, the
// name is spliced where the * sits by the callers below.

typedef enum {
  MATH_TOTAL, // defined for every finite number
  MATH_NON_NEGATIVE,
  MATH_POSITIVE,
  MATH_UNIT, // the inverse trig domains, [-1, 1]
} math_domain;

typedef struct {
  const char* name;
  double (*fn)(double);
  math_domain domain;
} math_def;

// the splice: one static message per site, the name replaces the '*'.
static void math_error(ok* p_ok, const char* p_pattern, const char* p_name) {
  char message[128];
  const int written = snprintf(message, sizeof(message), p_pattern, p_name);
  if (written > 0 && (size_t)written < sizeof(message)) {
    ok_native_error(p_ok, ok_create_string_view(message, (uint64_t)written, true));
  } else {
    ok_native_error(p_ok, ok_create_string_view("invalid argument.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  }
}

static ok_value math_one(ok* p_ok, ok_value p_arg, const math_def* p_def) {
  if (!IS_VALUE_NUMBER(p_arg)) {
    math_error(p_ok, "invalid argument: 'math.%s' expects numbers.", p_def->name);
    return NULL_AS_VALUE();
  }
  const double x = VALUE_AS_NUMBER(p_arg);
  if (isnan(x) || isinf(x)) {
    math_error(p_ok, "invalid argument: 'math.%s' expects finite numbers.", p_def->name);
    return NULL_AS_VALUE();
  }
  switch (p_def->domain) {
  case MATH_NON_NEGATIVE:
    if (x < 0.0) {
      math_error(p_ok, "math.%s expects a non-negative number.", p_def->name);
      return NULL_AS_VALUE();
    }
    break;
  case MATH_POSITIVE:
    if (x <= 0.0) {
      math_error(p_ok, "math.%s expects a positive number.", p_def->name);
      return NULL_AS_VALUE();
    }
    break;
  case MATH_UNIT:
    if (x < -1.0 || x > 1.0) {
      math_error(p_ok, "math.%s expects a number between -1 and 1.", p_def->name);
      return NULL_AS_VALUE();
    }
    break;
  case MATH_TOTAL:
    break;
  }
  const double r = p_def->fn(x);
  if (isnan(r) || isinf(r)) {
    math_error(p_ok, "the result of 'math.%s' is out of range.", p_def->name);
    return NULL_AS_VALUE();
  }
  return NUMBER_AS_VALUE(r);
}

#define MATH_UNARY_X(X)                                                                                                \
  X(abs, fabs, MATH_TOTAL)                                                                                             \
  X(floor, floor, MATH_TOTAL)                                                                                          \
  X(ceil, ceil, MATH_TOTAL)                                                                                            \
  X(sqrt, sqrt, MATH_NON_NEGATIVE)                                                                                     \
  X(exp, exp, MATH_TOTAL)                                                                                              \
  X(log, log, MATH_POSITIVE)                                                                                           \
  X(log2, log2, MATH_POSITIVE)                                                                                         \
  X(log10, log10, MATH_POSITIVE)                                                                                       \
  X(sin, sin, MATH_TOTAL)                                                                                              \
  X(cos, cos, MATH_TOTAL)                                                                                              \
  X(tan, tan, MATH_TOTAL)                                                                                              \
  X(asin, asin, MATH_UNIT)                                                                                             \
  X(acos, acos, MATH_UNIT)                                                                                             \
  X(atan, atan, MATH_TOTAL)                                                                                            \
  X(sinh, sinh, MATH_TOTAL)                                                                                            \
  X(cosh, cosh, MATH_TOTAL)                                                                                            \
  X(tanh, tanh, MATH_TOTAL)

// one body, seventeen front doors: the X table is the single source.
#define MATH_UNARY_DEF(id, sym, dom)                                                                                   \
  static ok_value native_math_##id(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {                                 \
    (void)p_argc;                                                                                                      \
    static const math_def def = {#id, sym, dom};                                                                       \
    return math_one(p_ok, p_argv[0], &def);                                                                            \
  }
MATH_UNARY_X(MATH_UNARY_DEF)

static bool math_arg(ok* p_ok, ok_value p_arg, double* p_out, const char* p_name) {
  if (!IS_VALUE_NUMBER(p_arg)) {
    math_error(p_ok, "invalid argument: 'math.%s' expects numbers.", p_name);
    return false;
  }
  const double d = VALUE_AS_NUMBER(p_arg);
  if (isnan(d) || isinf(d)) {
    math_error(p_ok, "invalid argument: 'math.%s' expects finite numbers.", p_name);
    return false;
  }
  *p_out = d;
  return true;
}

// math.pow(x, y): total over the finite pairs whose result is finite.
// pow(-8, 0.5) style NaN lanes and the overflow lanes both die at the
// result check - the error names the range, not C's errno folklore.
static ok_value native_math_pow(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  double x;
  double y;
  if (!math_arg(p_ok, p_argv[0], &x, "pow") || !math_arg(p_ok, p_argv[1], &y, "pow")) {
    return NULL_AS_VALUE();
  }
  const double r = pow(x, y);
  if (isnan(r) || isinf(r)) {
    math_error(p_ok, "the result of 'math.%s' is out of range.", "pow");
    return NULL_AS_VALUE();
  }
  return NUMBER_AS_VALUE(r);
}

// math.atan2(y, x): total over the finite pairs (the axis conventions are
// C's, which are IEEE's).
static ok_value native_math_atan2(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  double y;
  double x;
  if (!math_arg(p_ok, p_argv[0], &y, "atan2") || !math_arg(p_ok, p_argv[1], &x, "atan2")) {
    return NULL_AS_VALUE();
  }
  return NUMBER_AS_VALUE(atan2(y, x));
}

// math.idiv(a, b) (RULED): integer division, truncation toward zero, a hard
// error on the zero divisor. whole operands inside the safe range divide
// through int64 exactly; anything else (huge or fractional) falls to
// trunc(a / b) and the f64 rounding that carries - documented, not secret.
static ok_value native_math_idiv(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  double a;
  double b;
  if (!math_arg(p_ok, p_argv[0], &a, "idiv") || !math_arg(p_ok, p_argv[1], &b, "idiv")) {
    return NULL_AS_VALUE();
  }
  if (b == 0.0) {
    math_error(p_ok, "math.%s expects a non-zero divisor.", "idiv");
    return NULL_AS_VALUE();
  }
  if (a == (double)(int64_t)a && b == (double)(int64_t)b && a >= MIN_SAFE_INTEGER && a <= MAX_SAFE_INTEGER &&
      b >= MIN_SAFE_INTEGER && b <= MAX_SAFE_INTEGER) {
    return NUMBER_AS_VALUE((double)((int64_t)a / (int64_t)b));
  }
  return NUMBER_AS_VALUE(trunc(a / b));
}

// the constants (the registry's non-native lane, Q-approved for v1):
// std.math's pi and e. spelled to full precision - the C library's M_PI
// family is POSIX, not c99, and the strict gate hides it - and both
// literals round to the correctly-rounded doubles (verified against the
// library's own values at patch time).
#define OK_MATH_PI 3.14159265358979323846
#define OK_MATH_E 2.71828182845904523536

// the hidden globals std.math's registry rows harvest - the functions,
// and since the constants lane the numbers too (the harvest never cared
// what a hidden global holds; only the table rows say). never seeded
// flat: the module is the only door (the print_fn precedent).
static bool ok_define_math_members(ok* p_ok) {
  bool status = true;
#define MATH_GLOBAL(id, ...)                                                                                           \
  status &= ok_global_define_native_function(                                                                          \
      p_ok, ok_create_string_view(" " #id, OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_math_##id);
  MATH_UNARY_X(MATH_GLOBAL)
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" pow", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_math_pow);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" atan2", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_math_atan2);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" idiv", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_math_idiv);
  // the constants ride the same hidden lane: plain immutable numbers,
  // unreachable flat (the leading space), harvested by the std table.
  status &= ok_global_define(p_ok,
                             ok_create_string_view(" pi", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                             NUMBER_AS_VALUE(OK_MATH_PI),
                             false);
  status &= ok_global_define(p_ok,
                             ok_create_string_view(" e", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                             NUMBER_AS_VALUE(OK_MATH_E),
                             false);
  return status;
}

// ============================ std.fs + std.sys (P4) ============================
//
// the host door natives. every host call routes through the ok_fs /
// ok_time / ok_env layers (the audit's one-door rule) - nothing here
// touches a syscall directly, so an embedder reading those three files
// knows every byte ok can move across the host boundary. the modules
// themselves are HOST-gated at the import site (okmodules.c's std lane);
// these natives can therefore assume the cap held when the module loaded.

// the splice: one static message per site, the '*' is the member name.
static void host_error(ok* p_ok, const char* p_pattern, const char* p_name) {
  char message[192];
  const int written = snprintf(message, sizeof(message), p_pattern, p_name);
  if (written > 0 && (size_t)written < sizeof(message)) {
    ok_native_error(p_ok, ok_create_string_view(message, (uint64_t)written, true));
  } else {
    ok_native_error(p_ok, ok_create_string_view("invalid argument.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  }
}

// a message carrying one path: 512 (the defer lane's buffer precedent -
// a path longer than this truncates the MESSAGE, never the operation).
static void host_path_error(ok* p_ok, const char* p_pattern, const char* p_path) {
  char message[512];
  const int written = snprintf(message, sizeof(message), p_pattern, p_path);
  if (written > 0 && (size_t)written < sizeof(message)) {
    ok_native_error(p_ok, ok_create_string_view(message, (uint64_t)written, true));
  } else {
    ok_native_error(p_ok, ok_create_string_view("the host operation failed.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  }
}

// one NUL-terminated copy of a path/name argument. the C host APIs want
// C strings; ok strings are length-carried and may contain anything, and
// an embedded NUL would silently truncate - which for a path is a WRONG
// path, so it refuses instead. the caller releases the copy.
static bool host_zstring(ok* p_ok, ok_value p_arg, char** p_out, const char* p_who) {
  str_bytes s;
  if (!str_unpack(p_arg, &s)) {
    host_error(p_ok, "invalid argument: '%s' expects a string.", p_who);
    return false;
  }
  if (memchr(s.chars, '\0', s.length) != NULL) {
    host_error(p_ok, "invalid argument: the string passed to '%s' contains a NUL.", p_who);
    return false;
  }
  char* z = (char*)p_ok->alloc.allocate(&p_ok->alloc, s.length + 1);
  if (z == NULL) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate a path buffer.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return false;
  }
  memcpy(z, s.chars, (size_t)s.length);
  z[s.length] = '\0';
  *p_out = z;
  return true;
}

// one ok array of strings from a C name list (ok_fs_list / ok_env_list's
// shape). the C list is released here - the ok array is the caller's
// (call_native guards it the moment the native returns).
static ok_value host_names_array(ok* p_ok, char** p_names, uint32_t p_count) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_array* arr = create_object_array(&specs);
  if (arr == NULL || !gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(arr))) {
    goto fail;
  }
  for (uint32_t i = 0; i < p_count; ++i) {
    object_string* piece =
        create_object_string(ok_create_string_view(p_names[i], (uint32_t)strlen(p_names[i]), false), &specs);
    if (piece == NULL || !gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(piece))) {
      gc_guard_down(&p_ok->gc); // the array
      goto fail;
    }
    const bool appended = value_array_append(&arr->values, OBJECT_AS_VALUE(piece));
    gc_guard_down(&p_ok->gc); // the piece: the array owns it now
    if (!appended) {
      goto fail_with_array;
    }
  }
  gc_guard_down(&p_ok->gc); // the array: the caller owns it now
  for (uint32_t i = 0; i < p_count; ++i) {
    p_ok->raw_alloc.release(p_names[i]);
  }
  p_ok->raw_alloc.release(p_names);
  return OBJECT_AS_VALUE(arr);

fail_with_array:
  gc_guard_down(&p_ok->gc); // the array (half-built, unreferenced)
fail:
  for (uint32_t i = 0; i < p_count; ++i) {
    p_ok->raw_alloc.release(p_names[i]);
  }
  p_ok->raw_alloc.release(p_names);
  ok_native_error(p_ok,
                  ok_create_string_view(
                      "out of memory: failed to build the names array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  return NULL_AS_VALUE();
}

// fs.read(path): the whole file, one string. the 1 GiB cap and the
// regular-files-only rule are ok_fs's own (the loader's contract, shared).
static ok_value native_fs_read(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.read")) {
    return NULL_AS_VALUE();
  }
  ok_fs_buffer buffer;
  const bool read = ok_fs_read_file(path, &p_ok->raw_alloc, &buffer);
  if (!read) {
    host_path_error(p_ok, "fs.read failed: '%s' is not a readable regular file.", path);
    p_ok->alloc.release(&p_ok->alloc, path);
    return NULL_AS_VALUE();
  }
  p_ok->alloc.release(&p_ok->alloc, path);
  const ok_value contents = str_from_range(p_ok, buffer.chars, buffer.length);
  p_ok->raw_alloc.release(buffer.chars);
  return contents;
}

// fs.write / fs.append: whole-buffer, one fwrite, checked. the DATA may
// carry NULs - only paths refuse them.
static ok_value native_fs_write_impl(ok* p_ok, ok_value p_path_arg, ok_value p_data_arg, bool p_append,
                                     const char* p_who) {
  char* path = NULL;
  if (!host_zstring(p_ok, p_path_arg, &path, p_who)) {
    return NULL_AS_VALUE();
  }
  str_bytes data;
  if (!str_unpack(p_data_arg, &data)) {
    host_error(p_ok, "invalid argument: '%s' expects a string of data.", p_who);
    p_ok->alloc.release(&p_ok->alloc, path);
    return NULL_AS_VALUE();
  }
  const bool written = ok_fs_write_file(path, data.chars, data.length, p_append);
  if (!written) {
    // two-step splice: the member name rides the pattern, host_path_error
    // carries the path (one format arg each, never a dangling conversion)
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s failed: cannot write '%%s'.", p_who);
    host_path_error(p_ok, pattern, path);
    p_ok->alloc.release(&p_ok->alloc, path);
    return NULL_AS_VALUE();
  }
  p_ok->alloc.release(&p_ok->alloc, path);
  return NULL_AS_VALUE();
}

static ok_value native_fs_write(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return native_fs_write_impl(p_ok, p_argv[0], p_argv[1], false, "fs.write");
}

static ok_value native_fs_append(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return native_fs_write_impl(p_ok, p_argv[0], p_argv[1], true, "fs.append");
}

// the probes: three shapes of "what is at this path".
static ok_value native_fs_exists(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.exists")) {
    return NULL_AS_VALUE();
  }
  const bool exists = ok_fs_exists(path);
  p_ok->alloc.release(&p_ok->alloc, path);
  return BOOL_AS_VALUE(exists);
}

static ok_value native_fs_is_file(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.is_file")) {
    return NULL_AS_VALUE();
  }
  const bool is_file = ok_fs_is_file(path);
  p_ok->alloc.release(&p_ok->alloc, path);
  return BOOL_AS_VALUE(is_file);
}

static ok_value native_fs_is_dir(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.is_dir")) {
    return NULL_AS_VALUE();
  }
  const bool is_dir = ok_fs_is_dir(path);
  p_ok->alloc.release(&p_ok->alloc, path);
  return BOOL_AS_VALUE(is_dir);
}

// fs.remove: a file or an EMPTY directory (the layer has no recursion,
// ever). fs.mkdir: one level, no parents.
static ok_value native_fs_remove(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.remove")) {
    return NULL_AS_VALUE();
  }
  const bool removed = ok_fs_remove(path);
  if (!removed) {
    host_path_error(p_ok, "fs.remove failed: '%s' is not an existing file or empty directory.", path);
  }
  p_ok->alloc.release(&p_ok->alloc, path);
  return NULL_AS_VALUE();
}

static ok_value native_fs_mkdir(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.mkdir")) {
    return NULL_AS_VALUE();
  }
  const bool made = ok_fs_mkdir(path);
  if (!made) {
    host_path_error(p_ok, "fs.mkdir failed: cannot create '%s' (it may exist, or a parent is missing).", path);
  }
  p_ok->alloc.release(&p_ok->alloc, path);
  return NULL_AS_VALUE();
}

// fs.list: the entry names, sorted - the determinism rule. "." and ".."
// are structure, not entries.
static ok_value native_fs_list(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* path = NULL;
  if (!host_zstring(p_ok, p_argv[0], &path, "fs.list")) {
    return NULL_AS_VALUE();
  }
  char** names = NULL;
  uint32_t count = 0;
  const bool listed = ok_fs_list(path, &p_ok->raw_alloc, &names, &count);
  if (!listed) {
    host_path_error(p_ok, "fs.list failed: '%s' is not a readable directory.", path);
    p_ok->alloc.release(&p_ok->alloc, path);
    return NULL_AS_VALUE();
  }
  p_ok->alloc.release(&p_ok->alloc, path);
  return host_names_array(p_ok, names, count);
}

// fs.cwd: the process working directory (its CONTENT is host state -
// never a fixture's output).
static ok_value native_fs_cwd(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  (void)p_argv;
  char buffer[4096];
  if (!ok_fs_cwd(buffer, sizeof(buffer))) {
    ok_native_error(p_ok,
                    ok_create_string_view("fs.cwd failed: the working directory is unreadable or too long.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return str_from_range(p_ok, buffer, (uint64_t)strlen(buffer));
}

// fs.join(a, b): pure path joining, no host contact at all - it rides
// the module because that is where path shape lives. empty sides pass
// through; a trailing separator does not double.
static ok_value native_fs_join(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  str_bytes a;
  str_bytes b;
  if (!str_unpack(p_argv[0], &a) || !str_unpack(p_argv[1], &b)) {
    host_error(p_ok, "invalid argument: '%s' expects strings.", "fs.join");
    return NULL_AS_VALUE();
  }
  if (a.length == 0) {
    return p_argv[1];
  }
  if (b.length == 0) {
    return p_argv[0];
  }
  const bool sep = a.chars[a.length - 1] == '/';
  const uint64_t total = a.length + (sep ? 0 : 1) + b.length;
  char* scratch = (char*)p_ok->alloc.allocate(&p_ok->alloc, total);
  if (scratch == NULL) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to allocate the joined path.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  memcpy(scratch, a.chars, (size_t)a.length);
  if (!sep) {
    scratch[a.length] = '/';
  }
  memcpy(scratch + a.length + (sep ? 0 : 1), b.chars, (size_t)b.length);
  const ok_value joined = str_from_range(p_ok, scratch, total);
  p_ok->alloc.release(&p_ok->alloc, scratch);
  return joined;
}

// sys.env_get: the entry's value, or null when unset (null is a legal
// value in ok - the sentinel question never arises).
static ok_value native_sys_env_get(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* name = NULL;
  if (!host_zstring(p_ok, p_argv[0], &name, "sys.env_get")) {
    return NULL_AS_VALUE();
  }
  const char* value = ok_env_get(name);
  p_ok->alloc.release(&p_ok->alloc, name);
  if (value == NULL) {
    return NULL_AS_VALUE();
  }
  return str_from_range(p_ok, value, (uint64_t)strlen(value));
}

// sys.env_set / sys.env_unset: setenv's EINVAL lanes (an '=' or NUL in
// the name) arrive as the invalid-name error; unset is idempotent.
static ok_value native_sys_env_set(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* name = NULL;
  if (!host_zstring(p_ok, p_argv[0], &name, "sys.env_set")) {
    return NULL_AS_VALUE();
  }
  char* value = NULL;
  if (!host_zstring(p_ok, p_argv[1], &value, "sys.env_set")) {
    p_ok->alloc.release(&p_ok->alloc, name);
    return NULL_AS_VALUE();
  }
  const bool set = ok_env_set(name, value);
  if (!set) {
    host_path_error(p_ok, "sys.env_set failed: '%s' is not a valid environment name.", name);
  }
  p_ok->alloc.release(&p_ok->alloc, name);
  p_ok->alloc.release(&p_ok->alloc, value);
  return NULL_AS_VALUE();
}

static ok_value native_sys_env_unset(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  char* name = NULL;
  if (!host_zstring(p_ok, p_argv[0], &name, "sys.env_unset")) {
    return NULL_AS_VALUE();
  }
  const bool unset = ok_env_unset(name);
  if (!unset) {
    host_path_error(p_ok, "sys.env_unset failed: '%s' is not a valid environment name.", name);
  }
  p_ok->alloc.release(&p_ok->alloc, name);
  return NULL_AS_VALUE();
}

// sys.env_keys: the names, sorted and deduped - the determinism rule.
static ok_value native_sys_env_keys(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  (void)p_argv;
  char** names = NULL;
  uint32_t count = 0;
  if (!ok_env_list(&p_ok->raw_alloc, &names, &count)) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "out of memory: failed to list the environment.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return host_names_array(p_ok, names, count);
}

// sys.clock / sys.monotonic: seconds as doubles. fixtures assert
// relations (>=, later >= earlier), never values - the host's clock is
// not the suite's to know.
static ok_value native_sys_clock(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_ok;
  (void)p_argc;
  (void)p_argv;
  double now = 0.0;
  ok_time_wall(&now);
  return NUMBER_AS_VALUE(now);
}

static ok_value native_sys_monotonic(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_ok;
  (void)p_argc;
  (void)p_argv;
  double now = 0.0;
  ok_time_monotonic(&now);
  return NUMBER_AS_VALUE(now);
}

// sys.exit(code): the host request. sets the code, then rides the error
// lane (a native's only control-flow escape) as OK_EK_EXIT - the unwind
// closes frames and runs defers, no catch may take it, and ok_run maps
// the end to OK_EXITED. a failing defer on the way out replaces the
// request (a real error outranks a mere ask - first-failure-wins).
static ok_value native_sys_exit(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_NUMBER(p_argv[0])) {
    host_error(p_ok, "invalid argument: '%s' expects a number (the exit code).", "sys.exit");
    return NULL_AS_VALUE();
  }
  const double code = VALUE_AS_NUMBER(p_argv[0]);
  if (isnan(code) || isinf(code) || code != floor(code) || code < -2147483648.0 || code > 2147483647.0) {
    host_error(p_ok, "invalid argument: '%s' expects a whole number between -2147483648 and 2147483647.", "sys.exit");
    return NULL_AS_VALUE();
  }
  p_ok->exit_requested = true;
  p_ok->exit_code = (int32_t)code;
  vm_raise_exit(ok_vm(p_ok));
  return NULL_AS_VALUE();
}

// the hidden globals the std tables harvest - the same lane math uses.
// never seeded flat: the module is the only door.
static bool ok_define_fs_members(ok* p_ok) {
  bool status = true;
#define FS_GLOBAL(id, arity)                                                                                             \
  status &= ok_global_define_native_function(                                                                            \
      p_ok, ok_create_string_view(" " #id, OK_STRING_VIEW_CALCULATE_LENGTH, true), arity, native_##id);
  FS_GLOBAL(fs_read, 1)
  FS_GLOBAL(fs_write, 2)
  FS_GLOBAL(fs_append, 2)
  FS_GLOBAL(fs_exists, 1)
  FS_GLOBAL(fs_is_file, 1)
  FS_GLOBAL(fs_is_dir, 1)
  FS_GLOBAL(fs_remove, 1)
  FS_GLOBAL(fs_mkdir, 1)
  FS_GLOBAL(fs_list, 1)
  FS_GLOBAL(fs_cwd, 0)
  FS_GLOBAL(fs_join, 2)
#undef FS_GLOBAL
  return status;
}

static bool ok_define_sys_members(ok* p_ok) {
  bool status = true;
#define SYS_GLOBAL(id, arity)                                                                                            \
  status &= ok_global_define_native_function(                                                                            \
      p_ok, ok_create_string_view(" " #id, OK_STRING_VIEW_CALCULATE_LENGTH, true), arity, native_##id);
  SYS_GLOBAL(sys_env_get, 1)
  SYS_GLOBAL(sys_env_set, 2)
  SYS_GLOBAL(sys_env_unset, 1)
  SYS_GLOBAL(sys_env_keys, 0)
  SYS_GLOBAL(sys_clock, 0)
  SYS_GLOBAL(sys_monotonic, 0)
  SYS_GLOBAL(sys_exit, 1)
#undef SYS_GLOBAL
  return status;
}

// the natives-as-classes step: mount the method surfaces on the builtin
// singletons, then seal them. the container C handlers are the SAME
// functions the free-function spellings use - they were receiver-first all
// along (`len(x)` always took its receiver as argv[0]) - so `len(a)` and
// `a.len()` are one entry, and the "no new stdlib" ruling held by
// construction through v0.8.0 (array: len/push/pop/remove; map:
// len/remove/get). the P3 str toolkit breaks that mirror ON PURPOSE, the
// ruled direction: new surface lands as methods only, the free spellings
// retire through the 0.9.0 deprecation window. the sealing happens here,
// after the tables fill - which is why vm_init_singletons leaves the
// builtins unfrozen.
static bool ok_define_builtin_surfaces(ok* p_ok) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  struct {
    builtin_class_id klass;
    const char* name;
    uint16_t arity; // counts the receiver
    ok_native_fn fn;
  } const methods[] = {
      {OK_BUILTIN_ARRAY, "len", 1, native_len},
      {OK_BUILTIN_ARRAY, "push", 2, native_push},
      {OK_BUILTIN_ARRAY, "pop", 1, native_pop},
      {OK_BUILTIN_ARRAY, "remove", OK_NATIVE_ARITY_ANY, native_remove},
      {OK_BUILTIN_MAP, "len", 1, native_len},
      {OK_BUILTIN_MAP, "remove", OK_NATIVE_ARITY_ANY, native_remove},
      {OK_BUILTIN_STR, "len", 1, native_len}, // P2: the codepoint count - str's first method
      {OK_BUILTIN_MAP, "get", OK_NATIVE_ARITY_ANY, native_get},
      // the P3 str toolkit (the ruling: methods on the class, std.str killed).
      // these have no free spellings and no hidden globals - the class is the
      // only door, the retier direction the deprecation windows document.
      {OK_BUILTIN_STR, "trim", 1, native_str_trim},
      {OK_BUILTIN_STR, "split", 2, native_str_split},
      {OK_BUILTIN_STR, "join", 2, native_str_join},
      {OK_BUILTIN_STR, "contains", 2, native_str_contains},
      {OK_BUILTIN_STR, "starts_with", 2, native_str_starts_with},
      {OK_BUILTIN_STR, "ends_with", 2, native_str_ends_with},
      {OK_BUILTIN_STR, "index_of", 2, native_str_index_of},
      {OK_BUILTIN_STR, "upper", 1, native_str_upper},
      {OK_BUILTIN_STR, "lower", 1, native_str_lower},
      {OK_BUILTIN_STR, "repeat", 2, native_str_repeat},
      {OK_BUILTIN_STR, "bytes", 1, native_str_bytes},
      {OK_BUILTIN_STR, "codepoints", 1, native_str_codepoints},
  };
  bool status = true;
  for (uint32_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
    status &=
        object_class_define_native_method(p_ok->vm.builtin_classes[methods[i].klass],
                                          ok_create_string_view(methods[i].name, OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                          methods[i].arity,
                                          methods[i].fn,
                                          /*p_pub*/ true,
                                          &specs);
  }
  // the coro surface (R20-R24): the native static `operator ()`
  // (construction), `resume`, and `status`, mounted before the seal like
  // every other builtin's methods
  status &= vm_define_coro_surface(&p_ok->vm);
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    // the seal (C15): from here the builtin tables are frozen like every
    // script class's, which is what the fast lanes depend on.
    status &= object_class_freeze(p_ok->vm.builtin_classes[i]);
  }
  return status;
}

// the seeding table: ONE list both sides walk, so the visible-name array and
// the value array can never drift apart (the old two hand-built arrays were
// position-paired by discipline alone). natives pull their hidden " name"
// global (the space prefix is what hides them); builtin classes pull the vm
// singletons; object and type ride the same walk.
typedef enum {
  SEED_NATIVE,        // the value is hidden-global " <name>" (a native fn)
  SEED_BUILTIN_CLASS, // the value is vm->builtin_classes[class_id]
  SEED_OBJECT,        // the root singleton
  SEED_CLASS,         // the metaclass singleton
} seed_kind;

typedef struct {
  const char* name; // the visible top-level binding
  seed_kind kind;
  builtin_class_id class_id; // SEED_BUILTIN_CLASS only, else OK_BUILTIN_COUNT
} seed_entry;

static const seed_entry top_level_seeds[] = {
    {"len", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"push", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"pop", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"remove", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"get", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"isinstance", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"classof", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"str", SEED_BUILTIN_CLASS, OK_BUILTIN_STR},
    {"num", SEED_BUILTIN_CLASS, OK_BUILTIN_NUM},
    {"bool", SEED_BUILTIN_CLASS, OK_BUILTIN_BOOL},
    {"array", SEED_BUILTIN_CLASS, OK_BUILTIN_ARRAY},
    {"map", SEED_BUILTIN_CLASS, OK_BUILTIN_MAP},
    {"function", SEED_BUILTIN_CLASS, OK_BUILTIN_FUNCTION},
    {"nil", SEED_BUILTIN_CLASS, OK_BUILTIN_NIL},
    {"coro", SEED_BUILTIN_CLASS, OK_BUILTIN_CORO},
    {"object", SEED_OBJECT, OK_BUILTIN_COUNT},
    {"type", SEED_CLASS, OK_BUILTIN_COUNT},
};

top_level_declarations ok_get_top_level_declarations(ok* p_ok, bool* p_error) {
  top_level_declarations decls;
  top_level_declarations_init(&decls, &p_ok->alloc);
  for (uint32_t i = 0; i < sizeof(top_level_seeds) / sizeof(top_level_seeds[0]); ++i) {
    variable_declaration decl = {
        .identifier = ok_create_string_view(top_level_seeds[i].name, OK_STRING_VIEW_CALCULATE_LENGTH, true)};
    if (!top_level_declarations_append(&decls, decl)) {
      *p_error = true;
      break;
    }
  }
  return decls;
}

value_array ok_get_top_level_values(ok* p_ok, bool* p_error) {
  value_array vals;
  value_array_init(&vals, &p_ok->alloc);

  for (uint32_t i = 0; i < sizeof(top_level_seeds) / sizeof(top_level_seeds[0]) && !*p_error; ++i) {
    const seed_entry* seed = &top_level_seeds[i];
    value temp;
    switch (seed->kind) {
    case SEED_NATIVE: {
      // the hidden global is " " + the visible name; the names are short
      // static strings, so a bounded stack buffer is the whole story
      char hidden[32];
      const int written = snprintf(hidden, sizeof(hidden), " %s", seed->name);
      if (written <= 0 || (size_t)written >= sizeof(hidden) ||
          !ok_global_get(p_ok, ok_create_string_view(hidden, (uint32_t)written, true), &temp)) {
        *p_error = true;
      }
      break;
    }
    case SEED_BUILTIN_CLASS:
      temp = OBJECT_AS_VALUE(p_ok->vm.builtin_classes[seed->class_id]);
      break;
    case SEED_OBJECT:
      // object and type have no global storage; the vm singletons are
      // the values (seeded fresh every run, rooted by mark_vm in between)
      temp = OBJECT_AS_VALUE(p_ok->vm.object_singleton);
      break;
    case SEED_CLASS:
      temp = OBJECT_AS_VALUE(p_ok->vm.class_singleton);
      break;
    default:
      // the sweep's -O2 find: an unknown seed kind must refuse, never
      // append an uninitialized value (the enum is closed in this file,
      // but the analyzer is right to ask)
      *p_error = true;
      break;
    }
    if (*p_error || !value_array_append(&vals, temp)) {
      *p_error = true;
      break;
    }
  }
  return vals;
}

ok* ok_create(ok_specs p_specs) {
  patch_specs(&p_specs);
  ok* okay = p_specs.allocators.allocate(sizeof(ok));
  if (okay == NULL) {
    return NULL;
  }
  okay->raw_alloc = p_specs.allocators;
  okay->alloc = create_allocators(&okay->raw_alloc);
  okay->dump_bytecode = false;
  okay->dump_skip_run = false;
  // OR-3: the capability ratchet's birth state and the resolved dylib
  // table (the host's hooks, else the platform wrapper, else NULL: the
  // no-loading stance). set before anything can allocate: the loader and
  // the caps gates read them.
  okay->caps_dropped = p_specs.caps_dropped;
  okay->dylib = p_specs.dylib != NULL ? p_specs.dylib : ok_dylib_platform();

  object_store_init(&okay->objects_store, &okay->alloc);

  gc_init(&okay->gc, &okay->objects_store, &okay->alloc);
  okay->alloc.gc = &okay->gc;

  globals_store_init(&okay->globals_store, &okay->alloc);
  gc_watch_globals(&okay->gc, &okay->globals_store);
  // the interner precedes both consumers (compiler + vm): they only store
  // the pointer, but the gc watch must be live before either interns.
  string_interner_init(&okay->interner, &okay->alloc);
  gc_watch_interner(&okay->gc, &okay->interner);
  compiler_specs compiler_specs = {.alloc = &okay->alloc,
                                   .objects_store = &okay->objects_store,
                                   .globals_store = &okay->globals_store,
                                   .interner = &okay->interner};
  compiler_init(&okay->compiler, compiler_specs);
  gc_watch_compiler(&okay->gc, &okay->compiler);
  // OR-1: the module system - registry, loader (the fs default), and the
  // gc watch that roots loaded modules' namespaces and the pending roots.
  // initialized before the vm: vm_init allocates, and under aggressive gc
  // a collection there must mark a VALID (empty) module system - the
  // singleton failures' ok_free paths see one too.
  ok_modules_init(&okay->modules, okay, &okay->alloc);
  gc_watch_modules(&okay->gc, &okay->modules);
  vm_specs vm_specs = {.alloc = &okay->alloc,
                       .globals_store = &okay->globals_store,
                       .objects_store = &okay->objects_store,
                       .ok = okay,
                       .interner = &okay->interner};
  vm_init(&okay->vm, vm_specs);
  gc_watch_vm(&okay->gc, &okay->vm);
  // the singletons need mark_vm live before they exist: their only root is
  // the vm itself, and the in-flight pieces ride the guard stack.
  if (!vm_init_singletons(&okay->vm)) {
    return NULL;
  }
  if (!ok_define_core_natives(okay)) { // beware we not compiling yet
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_math_members(okay)) { // std.math's hidden globals: the natives (P3), the constants (57)
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_fs_members(okay)) { // std.fs's hidden globals: the host door (P4)
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_sys_members(okay)) { // std.sys's hidden globals: the host door (P4)
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_builtin_surfaces(okay)) { // mount the method tables, then seal
    ok_free(okay);
    return NULL;
  }
  return okay;
}

static ok_result ok_run_inner(ok* p_ok, uint32_t p_source_id, parse_result* p_parsed, ok_import_bindings p_imports);

// stamp the freshly compiled tree with its source id: the main function and
// every nested proto (function protos live in the constants arrays).
// proto trees grow downward only (a proto's constants are built before it is finished),
// so the walk terminates. shared protos stamp idempotently.
void ok_stamp_source_ids(object_function* p_function, uint32_t p_source_id) {
  p_function->chunk.source_id = p_source_id;
  for (uint32_t i = 0; i < p_function->chunk.constants.count; ++i) {
    const value v = p_function->chunk.constants.data[i];
    if (IS_VALUE_FUNCTION(v)) {
      ok_stamp_source_ids(VALUE_AS_FUNCTION(v), p_source_id);
    }
  }
}

ok_result ok_run(ok* p_ok, ok_source p_source) {
  // a fresh run starts with a clean error lane: ok_last_error reads the
  // most recent failed run, and this run hasn't failed yet.
  vm_clear_error(&p_ok->vm);
  // and a clean exit lane: a previous run's half-spent request (or its
  // code) never leaks into this one.
  p_ok->exit_requested = false;
  p_ok->exit_code = 0;
  const uint32_t source_id = vm_begin_source(&p_ok->vm, p_source.code, p_source.path);
  if (source_id == VM_SOURCE_UNSTAMPED) {
    return OK_INIT_ERROR;
  }
  p_ok->source = p_source;

  // OR-1: the module graph phase. the TU parses once here (ok_run_inner
  // reuses the tree); if it has imports, the module system resolves and
  // initializes the whole graph before the compile - the bindings arrive
  // as constants, and the compiler resolves nothing at run time.
  parser parser;
  parser_specs parser_specs = {.alloc = &p_ok->alloc, .source = &p_ok->source};
  parser_init(&parser, parser_specs);
  parse_result parsed = parser_parse(&parser);
  ok_result result = OK;
  ok_import_binding_array bindings;
  ok_import_binding_array_init(&bindings, &p_ok->alloc);
  if (parsed.status != PARSE_OK) {
    result = OK_PARSE_ERROR;
  } else {
#if defined(OK_TRACE_AST)
    gc_pause(&p_ok->gc);
    printf("--- ast ---\n");
    ast_dispatch_print((ast_node*)parsed.root);
    printf("--- ast ---");
    fflush(stdout);
    gc_resume(&p_ok->gc);
#endif // defined(OK_TRACE_AST)
    bool has_imports = false;
    for (uint32_t i = 0; i < parsed.root->statements.count; ++i) {
      if (parsed.root->statements.data[i]->node.node_type == AST_IMPORT_STATEMENT) {
        has_imports = true;
        break;
      }
    }
    if (has_imports) {
      // the per-run windows reset: the previous run's roots and names are
      // adopted by its compiled constants, and its bindings are spent.
      p_ok->modules.pending_roots.count = 0;
      for (uint32_t i = 0; i < p_ok->modules.name_arena_count; ++i) {
        p_ok->raw_alloc.release(p_ok->modules.name_arena[i]);
      }
      p_ok->modules.name_arena_count = 0;
      char** entry_imports = NULL;
      uint32_t entry_imports_count = 0;
      result = ok_modules_prepare(
          &p_ok->modules, parsed.root, &p_ok->source, &bindings, &entry_imports, &entry_imports_count);
      // the entry TU is not a module: its edge list is information for the
      // host (the registry reads it per module), not state we keep.
      for (uint32_t i = 0; i < entry_imports_count; ++i) {
        p_ok->raw_alloc.release(entry_imports[i]);
      }
      if (entry_imports != NULL) {
        p_ok->raw_alloc.release(entry_imports);
      }
    }
  }
  if (result == OK) {
    result = ok_run_inner(p_ok, source_id, &parsed, (ok_import_bindings){bindings.data, bindings.count});
  }
  ok_import_binding_array_deinit(&bindings, &p_ok->alloc);
  parse_result_deinit(&parsed);
  vm_end_source(&p_ok->vm, source_id);
  return result;
}

void ok_retain_source(ok* p_ok, bool p_retain) {
  p_ok->vm.retain_source = p_retain;
}

/* capabilities (OR-3) */

// the ratchet's read side: the granted set, always a subset of what the
// instance was born with. unknown bits are not invented.
uint32_t ok_caps_get(ok* p_ok) {
  return OK_CAP_ALL & ~p_ok->caps_dropped;
}

// the ratchet's only write: DROP. grants are impossible after ok_create by
// construction (nothing can clear these bits), which is the security
// property: not a native, not a script, not a manifest can escalate.
void ok_caps_drop(ok* p_ok, uint32_t p_caps) {
  p_ok->caps_dropped |= p_caps;
}

// P4: the code sys.exit requested. meaningful after ok_run answered
// OK_EXITED; 0 otherwise (the guard makes a stale code unreadable).
int32_t ok_exit_code(ok* p_ok) {
  return p_ok->exit_requested ? p_ok->exit_code : 0;
}

/* the loader (OR-3, the public ok_module.h door) */

// install the loader before ok_run. replaces the default (fs reads + the
// dylib native lane) wholesale: a host that only wants custom sources but
// keeps the native lane wraps ok_load_native_default (ok_module.h).
void ok_set_loader(ok* p_ok, ok_loader p_loader) {
  ok_modules_set_loader(&p_ok->modules, p_loader);
}

/* projects (OR-4) */

void ok_set_projects(ok* p_ok, const ok_project* p_projects, uint32_t p_count, ok_project_build_hooks p_hooks) {
  ok_modules_set_projects(&p_ok->modules, p_projects, p_count, p_hooks);
}

bool ok_module_where(ok* p_ok, const char* p_import, const char* p_from_dir, const char** p_out_canonical) {
  return ok_modules_where(&p_ok->modules, p_import, p_from_dir, p_out_canonical);
}

ok_value ok_module_evaluate(ok* p_ok, ok_source p_source) {
  return ok_modules_evaluate(&p_ok->modules, &p_source);
}

/* modules (embedder side, OR-3) */

// the registry read side: what got loaded, in load order. file modules key
// by their canonical absolute path, std modules by "std/<stem>", force-
// loaded natives by their declared name (the ok_module.h lane). this is the
// surface the CLI (and OR-4's manifest harvest) reads a module's exports
// through, and the embedder's window into what a run pulled in.
uint32_t ok_module_count(ok* p_ok) {
  return p_ok->modules.loaded.count;
}

const char* ok_module_path(ok* p_ok, uint32_t p_index) {
  if (p_index >= p_ok->modules.loaded.count) {
    return NULL;
  }
  return p_ok->modules.loaded.data[p_index].canonical;
}

ok_value ok_module_value(ok* p_ok, uint32_t p_index) {
  if (p_index >= p_ok->modules.loaded.count) {
    return ok_value_null();
  }
  return p_ok->modules.loaded.data[p_index].namespace;
}

ok_value ok_module_find(ok* p_ok, const char* p_canonical) {
  if (p_canonical == NULL) {
    return ok_value_null();
  }
  for (uint32_t i = 0; i < p_ok->modules.loaded.count; ++i) {
    if (strcmp(p_ok->modules.loaded.data[i].canonical, p_canonical) == 0) {
      return p_ok->modules.loaded.data[i].namespace;
    }
  }
  return ok_value_null();
}

uint32_t ok_module_import_count(ok* p_ok, uint32_t p_index) {
  if (p_index >= p_ok->modules.loaded.count) {
    return 0;
  }
  return p_ok->modules.loaded.data[p_index].imports_count;
}

const char* ok_module_import_path(ok* p_ok, uint32_t p_index, uint32_t p_import) {
  if (p_index >= p_ok->modules.loaded.count || p_import >= p_ok->modules.loaded.data[p_index].imports_count) {
    return NULL;
  }
  return p_ok->modules.loaded.data[p_index].imports[p_import];
}

// a module namespace's class, or NULL for anything else (the read API's
// gate: only sealed namespace instances answer the export enumeration).
static const object_class* module_namespace_class_of(ok_value p_module) {
  if (!IS_VALUE_INSTANCE(p_module)) {
    return NULL;
  }
  const object_class* klass = VALUE_AS_INSTANCE(p_module)->klass;
  if (!klass->module_namespace) {
    return NULL;
  }
  return klass;
}

// one module's pub exports, declaration order. p_module is any module value
// (an ok_module_value/ok_module_find result, or a module binding passed back
// by script through a native). 0 for anything that is not a module namespace.
uint32_t ok_module_export_count(ok* p_ok, ok_value p_module) {
  (void)p_ok;
  const object_class* klass = module_namespace_class_of(p_module);
  if (klass == NULL) {
    return 0;
  }
  return klass->own_names.count;
}

const char* ok_module_export_name(ok* p_ok, ok_value p_module, uint32_t p_index) {
  (void)p_ok;
  const object_class* klass = module_namespace_class_of(p_module);
  if (klass == NULL || p_index >= klass->own_names.count) {
    return NULL;
  }
  return klass->own_names.data[p_index]->string.chars;
}

ok_value ok_module_export_get(ok* p_ok, ok_value p_module, uint32_t p_index) {
  (void)p_ok;
  const object_class* klass = module_namespace_class_of(p_module);
  if (klass == NULL || p_index >= klass->own_names.count) {
    return ok_value_null();
  }
  object_instance* instance = VALUE_AS_INSTANCE(p_module);
  const object_string* name = klass->own_names.data[p_index];
  const field_entry* field = object_class_find_field(klass, name);
  if (field == NULL) {
    return ok_value_null(); // unreachable on a frozen namespace
  }
  return instance->fields[field->slot];
}

/* classes (embedder side) */

bool ok_value_is_class(ok_value p_value) {
  return IS_VALUE_CLASS(p_value);
}

// a C-defined class: created unfrozen (define its native methods, then
// ok_class_freeze). p_parent is a class value, or a null value for object.
// returns a null value on failure. the returned class is NOT rooted - the
// definition-sequence calls below guard it across their own allocations,
// but if you allocate anything else in between (or keep it past the
// sequence), root it with ok_root first, exactly like an ok_array_new result.
ok_value ok_class_new(ok* p_ok, ok_string_view p_name, ok_value p_parent) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_class* parent = p_ok->vm.object_singleton;
  if (!IS_VALUE_STRICTLY_NULL(p_parent)) {
    if (!IS_VALUE_CLASS(p_parent)) {
      return NULL_AS_VALUE();
    }
    parent = VALUE_AS_CLASS(p_parent);
  }
  object_string* name = create_object_string(p_name, &specs);
  if (name == NULL) {
    return NULL_AS_VALUE();
  }
  if (!gc_guard_up(p_ok->alloc.gc, OBJECT_AS_VALUE(name))) {
    return NULL_AS_VALUE();
  }
  object_class* klass = create_object_class(name, parent, &specs);
  gc_guard_down(p_ok->alloc.gc); // the class (and its trace) owns the name
  if (klass == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(klass);
}

// define one native method on an unfrozen class. the arity COUNTS the
// receiver (`fu m(x)` on the script side is arity 2 here). OK_NATIVE_ARITY_ANY
// defers the check to the callback. false = p_class is not a class value, the
// class is sealed, a same-name redeclaration, a private override of a pub
// ancestor, an arity disagreement with an ancestor, or OOM.
bool ok_class_define_native_method(ok* p_ok,
                                   ok_value p_class,
                                   ok_string_view p_name,
                                   uint16_t p_arity_including_receiver,
                                   ok_native_fn p_cb,
                                   bool p_pub) {
  if (!IS_VALUE_CLASS(p_class)) {
    return false;
  }
  // the class may be unrooted C state: guard it across the name allocation
  // and the map insert (both can collect under aggressive gc).
  if (!gc_guard_up(p_ok->alloc.gc, p_class)) {
    return false;
  }
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  const bool defined = object_class_define_native_method(
      VALUE_AS_CLASS(p_class), p_name, p_arity_including_receiver, p_cb, p_pub, &specs);
  gc_guard_down(p_ok->alloc.gc);
  return defined;
}

// seal the class (C15): builds the flat field map and freezes both tables.
// after this, ok_class_define_native_method is refused. script sees the class
// like any other: store it in a global and construct with C(args).
bool ok_class_freeze(ok* p_ok, ok_value p_class) {
  if (!IS_VALUE_CLASS(p_class)) {
    return false;
  }
  if (!gc_guard_up(p_ok->alloc.gc, p_class)) {
    return false;
  }
  const bool frozen = object_class_freeze(VALUE_AS_CLASS(p_class));
  gc_guard_down(p_ok->alloc.gc);
  return frozen;
}

// patch 49: the field-access trio. a native class grows per-instance state:
// declare fields before the freeze, fill them from a native init method,
// read them from any native method. the machinery is the VM's own (flat
// slots, the frozen field map), this is only the door.
bool ok_class_add_field(ok* p_ok, ok_value p_class, ok_string_view p_name, bool p_mut, ok_value p_initial) {
  return ok_class_add_field_ex(p_ok, p_class, p_name, p_mut, false, p_initial);
}

// the pub form (patch 51): the private default stays the patch-49 behavior,
// p_pub opens the member ring from script. the C lane is trusted either way.
bool ok_class_add_field_ex(
    ok* p_ok, ok_value p_class, ok_string_view p_name, bool p_mut, bool p_pub, ok_value p_initial) {
  if (!IS_VALUE_CLASS(p_class) || p_name.chars == NULL || ok_string_view_get_length(&p_name) == 0) {
    return false;
  }
  object_class* klass = VALUE_AS_CLASS(p_class);
  if (klass->module_namespace) {
    // the loader's territory: a namespace class is built and frozen by the
    // module system, never handed out unfrozen. the frozen check below
    // covers it in practice; this stays explicit for the reader.
    return false;
  }
  // the class and the initial value are caller-held (the define sequence's
  // rooting contract): guard both across the interner's allocation. the
  // interner owns the name from here on, the class owns the default. each
  // failed guard pops exactly what it pushed, the up/down pairs stay honest.
  if (!gc_guard_up(p_ok->alloc.gc, p_class)) {
    return false;
  }
  if (!gc_guard_up(p_ok->alloc.gc, p_initial)) {
    gc_guard_down(p_ok->alloc.gc);
    return false;
  }
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_string* name = string_interner_intern(&p_ok->interner, p_name, &specs);
  if (name == NULL) {
    gc_guard_down(p_ok->alloc.gc);
    gc_guard_down(p_ok->alloc.gc);
    return false;
  }
  // the interned name rides unrooted from here until the class's own_names
  // adopts it: the appends inside object_class_add_field allocate, and
  // under aggressive gc that collection must not sweep it (the interner
  // does not root it for you - build_namespace guards the same window)
  if (!gc_guard_up(p_ok->alloc.gc, OBJECT_AS_VALUE(name))) {
    gc_guard_down(p_ok->alloc.gc);
    gc_guard_down(p_ok->alloc.gc);
    return false;
  }
  const bool added = object_class_add_field(klass, name, p_mut, p_pub, NULL, p_initial, NULL);
  gc_guard_down(p_ok->alloc.gc);
  gc_guard_down(p_ok->alloc.gc);
  gc_guard_down(p_ok->alloc.gc);
  return added;
}

bool ok_value_get_field(ok* p_ok, ok_value p_value, ok_string_view p_name, ok_value* p_out) {
  if (!IS_VALUE_INSTANCE(p_value) || p_name.chars == NULL) {
    return false;
  }
  // an unseen spelling cannot be a field (universal interning, D20): the
  // probe allocates nothing, so the read is allocation-free end to end
  const object_string* name = string_interner_find(&p_ok->interner, p_name);
  if (name == NULL) {
    return false;
  }
  object_instance* instance = VALUE_AS_INSTANCE(p_value);
  const field_entry* field = object_class_find_field(instance->klass, name);
  if (field == NULL) {
    return false;
  }
  *p_out = instance->fields[field->slot];
  return true;
}

bool ok_value_set_field(ok* p_ok, ok_value p_value, ok_string_view p_name, ok_value p_new_value) {
  if (!IS_VALUE_INSTANCE(p_value) || p_name.chars == NULL) {
    return false;
  }
  const object_string* name = string_interner_find(&p_ok->interner, p_name);
  if (name == NULL) {
    return false;
  }
  object_instance* instance = VALUE_AS_INSTANCE(p_value);
  if (instance->klass->module_namespace) {
    return false; // a module's exports are sealed, same doctrine as script
  }
  const field_entry* field = object_class_find_field(instance->klass, name);
  if (field == NULL || !field->mut) {
    return false;
  }
  instance->fields[field->slot] = p_new_value;
  return true;
}

ok_value ok_last_error(ok* p_ok) {
  return vm_last_error(&p_ok->vm);
}

void ok_clear_error(ok* p_ok) {
  vm_clear_error(&p_ok->vm);
}

static ok_result ok_run_inner(ok* p_ok, uint32_t p_source_id, parse_result* p_parsed, ok_import_bindings p_imports) {
  // the entry compile: a plain TU (module mode is the loader's lane). the
  // parse came in from ok_run's graph phase - one parse per run, ever.
  compile_specs compile_specs;
  compile_specs.root = p_parsed->root;
  compile_specs.source = &p_ok->source;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(p_ok, &err);
  if (err) {
    return OK_INIT_ERROR;
  }
  compile_specs.imports = p_imports;
  compile_specs.is_module = false;
  compile_specs.module_path = ok_create_string_view(NULL, 0, false);
  compile_result compile_result = compiler_compile(&p_ok->compiler, compile_specs);
  top_level_declarations_deinit(&compile_specs.top_level_declarations, &p_ok->alloc);
  if (compile_result.status != COMPILE_OK) {
    module_exports_deinit(&compile_result.exports, &p_ok->alloc);
    return OK_COMPILE_ERROR;
  }
  module_exports_deinit(&compile_result.exports, &p_ok->alloc); // the entry exports nothing
  // the freshly compiled tree remembers its source:
  // main + every nested proto. runtime reports read it back per frame
  ok_stamp_source_ids(compile_result.function, p_source_id);
  if (p_ok->dump_bytecode) {
    gc_pause(&p_ok->gc);
    disassembler disassembler;
    disassembler_specs disassembler_specs = {.chunk = &compile_result.function->chunk,
                                             .globals_store = &p_ok->globals_store};
    disassembler_init(&disassembler, disassembler_specs);
    debug_disassemble_function(&disassembler, compile_result.function, "<main>");
    disassembler_deinit(&disassembler);
    gc_resume(&p_ok->gc);
    if (p_ok->dump_skip_run) {
      return OK;
    }
  }
  interpret_specs interpret_specs;
  interpret_specs.function = compile_result.function;
  interpret_specs.source_id = p_source_id;
  if (!gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(compile_result.function))) {
    return OK_INIT_ERROR;
  }
  err = false;
  interpret_specs.top_level_values = ok_get_top_level_values(p_ok, &err);
  if (err) {
    gc_guard_down(&p_ok->gc);
    return OK_INIT_ERROR;
  }
  interpret_result interpret_result = vm_interpret(&p_ok->vm, interpret_specs);
  gc_guard_down(&p_ok->gc);
  value_array_deinit(&interpret_specs.top_level_values, &p_ok->alloc);
  if (interpret_result.status != RUNTIME_OK) {
    interpret_result_deinit(&interpret_result);
    // P4: sys.exit unwound to the top as an uncatchable error - the run
    // ENDED, it did not fail. the code reads back through ok_exit_code.
    return p_ok->exit_requested ? OK_EXITED : OK_RUNTIME_ERROR;
  }

  return OK;
}

void ok_debug_dump_bytecode(ok* p_ok, bool p_enabled, bool p_skip_run) {
  p_ok->dump_bytecode = p_enabled;
  p_ok->dump_skip_run = p_skip_run;
}

void ok_set_print(ok* p_ok, ok_print_fn p_cb, void* p_ctx) {
  p_ok->vm.print_cb = p_cb;
  p_ok->vm.print_ctx = p_ctx;
}

void ok_free(ok* p_ok) {
  ok_modules_deinit(&p_ok->modules);
  vm_deinit(&p_ok->vm);
  compiler_deinit(&p_ok->compiler);
  string_interner_deinit(&p_ok->interner);
  globals_store_deinit(&p_ok->globals_store);
  object_store_deinit(&p_ok->objects_store);
  gc_deinit(&p_ok->gc);
  p_ok->alloc.release(&p_ok->alloc, p_ok);
}

ok_value ok_value_null(void) {
  return NULL_AS_VALUE();
}

ok_value ok_value_bool(bool p_value) {
  return BOOL_AS_VALUE(p_value);
}

ok_value ok_value_number(double p_value) {
  return NUMBER_AS_VALUE(p_value);
}

ok_value ok_value_string(ok* p_ok, ok_string_view p_string) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_string* str = create_object_string(p_string, &specs);
  if (str == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(str);
}

bool ok_value_is_null(ok_value p_value) {
  return IS_VALUE_STRICTLY_NULL(p_value);
}

bool ok_value_is_bool(ok_value p_value) {
  return IS_VALUE_STRICTLY_BOOL(p_value);
}

bool ok_value_is_number(ok_value p_value) {
  return IS_VALUE_NUMBER(p_value);
}

bool ok_value_is_object(ok_value p_value) {
  return IS_VALUE_STRICTLY_OBJECT(p_value);
}

bool ok_value_is_string(ok_value p_value) {
  return IS_VALUE_STRING(p_value);
}

bool ok_value_is_coro(ok_value p_value) {
  return IS_VALUE_CORO(p_value);
}

bool ok_value_as_bool(ok_value p_value) {
  return VALUE_AS_BOOL(p_value);
}

double ok_value_as_number(ok_value p_value) {
  return VALUE_AS_NUMBER(p_value);
}

ok_cstring_view ok_value_as_string(ok_value p_value) {
  return ok_create_cstring_view_from_string(VALUE_AS_STRING(p_value)->string);
}

bool ok_call(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_result) {
  execution_state* exe = p_ok->vm.current_execution_state;
  register_file* reg = &exe->registers;
  const uint32_t base = reg->top;

  // stage the call window at the top of the register file: [base] = callee, [base+1..] = args.
  // the vm can grow the file later when the callee frame needs more registers.
  if ((uint32_t)(base + p_argc + 1) > reg->array.capacity) {
    if (!register_array_grow(&reg->array, base + p_argc + 1)) {
      *p_result = NULL_AS_VALUE();
      return false;
    }
  }
  reg->array.data[base] = p_callee;
  memcpy(&reg->array.data[base + 1], p_argv, p_argc * sizeof(*p_argv));
  reg->top = base + p_argc + 1;

  interpret_result result = vm_call(&p_ok->vm, p_callee, p_argc);
  if (result.status != RUNTIME_OK) {
    // runtime_error already reset the execution state.
    *p_result = NULL_AS_VALUE();
    return false;
  }
  *p_result = reg->array.data[base];
  return true;
}

bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable) {
  gc_guard_up(&p_ok->gc, p_value);
  global_id id = globals_store_add(&p_ok->globals_store, p_name, p_is_mutable ? GLOBAL_FLAG_MUTABLE : GLOBAL_FLAG_NONE);
  if (!IS_GLOBAL_VALID(id)) {
    gc_guard_down(&p_ok->gc);
    return false;
  }
  p_ok->globals_store.global_values.data[id.index] = p_value;
  gc_guard_down(&p_ok->gc);
  return true;
}

bool ok_global_get(ok* p_ok, ok_string_view p_name, ok_value* p_value) {
  global_id id = globals_store_get(&p_ok->globals_store, p_name);
  if (!IS_GLOBAL_VALID(id)) {
    *p_value = NULL_AS_VALUE();
    return false;
  }
  *p_value = p_ok->globals_store.global_values.data[id.index];
  return true;
}

bool ok_global_define_native_function(ok* p_ok, ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_native_function* native = create_object_native_function(p_name, p_arity, p_cb, &s);
  if (native == NULL) {
    return false;
  }
  return ok_global_define(p_ok, p_name, OBJECT_AS_VALUE(native), false);
}

bool ok_native_error(ok* p_ok, ok_string_view p_message) {
  return vm_native_error(&p_ok->vm, p_message);
}

bool ok_native_throw(ok* p_ok, ok_value p_value) {
  return vm_native_throw(&p_ok->vm, p_value);
}

ok_value
ok_native_value_new(ok* p_ok, void* p_user_data, ok_native_destructor p_destructor, ok_native_tracer p_tracer) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_native_value* native = create_object_native_value(p_user_data, p_destructor, p_tracer, &s);
  if (native == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(native);
}

void* ok_value_as_native_value(ok_value p_value) {
  if (!IS_VALUE_STRICTLY_OBJECT(p_value)) {
    return NULL;
  }

  object* obj = VALUE_AS_OBJECT(p_value);

  if (object_get_type(obj) != OBJ_NATIVE_VALUE) {
    return NULL;
  }

  object_native_value* native = (object_native_value*)obj;
  return native->user_data;
}

bool ok_value_is_native_value(ok_value p_value) {
  return IS_VALUE_NATIVE_VALUE(p_value);
}

bool ok_gc_mark_value(ok* p_ok, ok_value p_value) {
  return gc_mark_value_user(&p_ok->gc, p_value);
}

uint32_t ok_root(ok* p_ok, ok_value p_value) {
  // null is the roots' free-slot marker, so rooting it cannot be supported.
  if (IS_VALUE_STRICTLY_NULL(p_value)) {
    return OK_ROOT_INVALID;
  }
  return gc_root(&p_ok->gc, p_value);
}

bool ok_unroot(ok* p_ok, uint32_t p_root) {
  return gc_unroot(&p_ok->gc, p_root);
}

ok_value ok_array_new(ok* p_ok, const ok_value* p_items, uint32_t p_count) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_array* arr = create_object_array(&s);
  if (arr == NULL) {
    return NULL_AS_VALUE();
  }
  // mirror OP_ARRAY_NEW: between creation and publication the array is linked
  // in the store yet unreachable, and every append grows through the gc-aware
  // allocator (collects). the items are host locals, equally invisible: guard
  // the array for the whole loop and each item across its own append. guard
  // depth peaks at 2 (capacity 16), mirroring the VM's unchecked pattern.
  gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(arr));
  for (uint32_t i = 0; i < p_count; ++i) {
    gc_guard_up(&p_ok->gc, p_items[i]);
    const bool appended = value_array_append(&arr->values, p_items[i]);
    gc_guard_down(&p_ok->gc);
    if (!appended) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE();
    }
  }
  gc_guard_down(&p_ok->gc);
  return OBJECT_AS_VALUE(arr);
}

bool ok_value_is_array(ok_value p_value) {
  return IS_VALUE_ARRAY(p_value);
}

uint32_t ok_array_len(ok_value p_value) {
  if (!IS_VALUE_ARRAY(p_value)) {
    return 0;
  }
  return VALUE_AS_ARRAY(p_value)->values.count;
}

ok_value ok_array_get(ok_value p_value, uint32_t p_index) {
  if (!IS_VALUE_ARRAY(p_value) || p_index >= VALUE_AS_ARRAY(p_value)->values.count) {
    return NULL_AS_VALUE();
  }
  return VALUE_AS_ARRAY(p_value)->values.data[p_index];
}

bool ok_array_set(ok_value p_value, uint32_t p_index, ok_value p_new_value) {
  if (!IS_VALUE_ARRAY(p_value) || p_index >= VALUE_AS_ARRAY(p_value)->values.count) {
    return false;
  }
  VALUE_AS_ARRAY(p_value)->values.data[p_index] = p_new_value;
  return true;
}

bool ok_array_push(ok* p_ok, ok_value p_array, ok_value p_value) {
  if (!IS_VALUE_ARRAY(p_array)) {
    return false;
  }
  // unlike the ' push' core native (whose arguments are rooted through the caller's register window),
  // a host-held array and value are invisible to the collector,
  // thus both need the guard while the append may grow (collect).
  gc_guard_up(&p_ok->gc, p_array);
  gc_guard_up(&p_ok->gc, p_value);
  const bool appended = value_array_append(&VALUE_AS_ARRAY(p_array)->values, p_value);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  return appended;
}

/* maps */

// builds a new map from p_pair_count (key, value) pairs laid out flat {k0, v0, k1, v1, ...}.
// p_pairs may be NULL when p_pair_count is 0.
// duplicate keys: the later pair wins, same as the literal.
// returns a null value on allocation failure or an illegal key (null / unhashable).
ok_value ok_map_new(ok* p_ok, const ok_value* p_pairs, uint32_t p_pair_count) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_map* map = create_object_map(&s);
  if (map == NULL) {
    return NULL_AS_VALUE();
  }
  // the constructor guard pattern, mirroring ok_array_new: the map is linked
  // in the store yet unreachable, and every set may rehash (grow) under the
  // collector. so it stays rooted across the whole fill and each pair across
  // its own set. guard depth peaks at 3 (capacity 16).
  gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(map));
  for (uint32_t i = 0; i < p_pair_count; ++i) {
    const ok_value key = p_pairs[2 * i];
    const ok_value val = p_pairs[2 * i + 1];
    if (IS_VALUE_STRICTLY_NULL(key) || !map_key_is_legal(key)) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE(); // null return: only strings, numbers, bools are valid keys
    }
    gc_guard_up(&p_ok->gc, key);
    gc_guard_up(&p_ok->gc, val);
    const bool set = object_map_set(map, key, val);
    gc_guard_down(&p_ok->gc);
    gc_guard_down(&p_ok->gc);
    if (!set) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE();
    }
  }
  gc_guard_down(&p_ok->gc);
  return OBJECT_AS_VALUE(map);
}

bool ok_value_is_map(ok_value p_value) {
  return IS_VALUE_MAP(p_value);
}

uint32_t ok_map_len(ok_value p_value) {
  if (!IS_VALUE_MAP(p_value)) {
    return 0;
  }
  return VALUE_AS_MAP(p_value)->entries.count;
}

// read only and allocation free (probes never allocate).
// return false when type is not a map, or illegal key, or absent.
// the stored value (even a stored null) lands in *p_out only on success (true).
bool ok_map_get(ok_value p_map, ok_value p_key, ok_value* p_out) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  value* slot = value_map_get(&VALUE_AS_MAP(p_map)->entries, p_key);
  if (slot == NULL) {
    return false;
  }
  *p_out = *slot;
  return true;
}

// insert or overwrite. unlike the ' get'/' remove' core natives
// (whose arguments are rooted through the caller's register window),
// a host-held map, key and value are invisible to the collector
// thus all three need the guard while the set may grow (collect).
bool ok_map_set(ok* p_ok, ok_value p_map, ok_value p_key, ok_value p_value) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  gc_guard_up(&p_ok->gc, p_map);
  gc_guard_up(&p_ok->gc, p_key);
  gc_guard_up(&p_ok->gc, p_value);
  const bool set = object_map_set(VALUE_AS_MAP(p_map), p_key, p_value);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  return set;
}

// backward-shift removal, no allocation. false = not a map, illegal key, or absent (in which case nothing changed).
bool ok_map_remove(ok_value p_map, ok_value p_key) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  return object_map_remove(VALUE_AS_MAP(p_map), p_key);
}

bool ok_map_pair_at(ok_value p_map, uint32_t p_index, ok_value* p_out_key, ok_value* p_out_value) {
  if (!IS_VALUE_MAP(p_map)) {
    return false;
  }
  // the keys array is the occupancy authority: walk occupied slots only
  // (empty slots hold the strict-null sentinel, their value slots are
  // stale garbage - the gc trace's comment, the same discipline here)
  const object_map* map = VALUE_AS_MAP(p_map);
  uint32_t seen = 0;
  for (uint32_t i = 0; i < map->entries.capacity; ++i) {
    if (IS_VALUE_STRICTLY_NULL(map->entries.keys[i])) {
      continue;
    }
    if (seen == p_index) {
      *p_out_key = map->entries.keys[i];
      *p_out_value = map->entries.values[i];
      return true;
    }
    ++seen;
  }
  return false;
}
