#include <stdio.h>
#include <stdlib.h>

#define OK_GC_EXPOSE
#include "ok/ok.h"
#include "okcompiler.h"
#include "okdebug.h"
#include "okgc.h"
#include "okobject.h"
#include "okobject_store.h"
#include "okparser.h"
#include "okspecs.h"
#include "okutils.h"
#include "okvalue.h"
#include "okvm.h"

struct ok {
  object_store objects_store;
  globals_store globals_store;
  compiler compiler;
  vm vm;
  gc gc;
  ok_source source;
  allocators alloc;
  ok_allocators raw_alloc;
  bool dump_bytecode;
  bool dump_skip_run;
};

// when classes + methods are implemented, push/pop/remove become methods and these globals
// either retire or stay as free functions (len likely stays, go style).
// errors go through the supported path ok_native_error -> vm_native_error.

static ok_value native_len(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'len' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  // f64, never an int, numbers are the only public numeric type
  return NUMBER_AS_VALUE((double)VALUE_AS_ARRAY(p_argv[0])->values.count);
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

// remove(a, [m, n)): in place removal of the half open range [m, n). the language
// surface is half open, the underlying OK_ARRAY remove is
// inclusive end, so translation happen here.
static ok_value native_remove(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'remove' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
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

static bool ok_define_core_natives(ok* p_ok) {
  bool status = true;
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" len", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_len);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" push", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_push);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" pop", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_pop);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" remove", OK_STRING_VIEW_CALCULATE_LENGTH, true), 3, native_remove);
  return status;
}

static bool ok_define_temporary_types(ok* p_ok) {
  bool status = true;
  status &= ok_global_define(
      p_ok, ok_create_string_view(" str", OK_STRING_VIEW_CALCULATE_LENGTH, true), RESERVED_1_AS_VALUE(TT_STR), false);
  status &= ok_global_define(
      p_ok, ok_create_string_view(" num", OK_STRING_VIEW_CALCULATE_LENGTH, true), RESERVED_1_AS_VALUE(TT_NUM), false);
  status &= ok_global_define(
      p_ok, ok_create_string_view(" bool", OK_STRING_VIEW_CALCULATE_LENGTH, true), RESERVED_1_AS_VALUE(TT_BOOL), false);
  return status;
}

static top_level_declarations ok_get_top_level_declarations(ok* p_ok, bool* p_error) {
  top_level_declarations decls;
  top_level_declarations_init(&decls, &p_ok->alloc);
  variable_declaration raw_decls[] = {{ok_create_string_view("len", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("push", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("pop", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("remove", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("str", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("num", OK_STRING_VIEW_CALCULATE_LENGTH, true)},
                                      {ok_create_string_view("bool", OK_STRING_VIEW_CALCULATE_LENGTH, true)}};

  *p_error = !top_level_declarations_append_n(&decls, raw_decls, sizeof(raw_decls) / sizeof(variable_declaration));
  return decls;
}

static value_array ok_get_top_level_values(ok* p_ok, bool* p_error) {
  ok_string_view raw_idents[] = {ok_create_string_view(" len", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" push", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" pop", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" remove", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" str", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" num", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                 ok_create_string_view(" bool", OK_STRING_VIEW_CALCULATE_LENGTH, true)};

  value_array vals;
  value_array_init(&vals, &p_ok->alloc);

  for (uint32_t i = 0; i < sizeof(raw_idents) / sizeof(ok_string_view); ++i) {
    value temp;
    if (!ok_global_get(p_ok, raw_idents[i], &temp)) {
      *p_error = true;
      break;
    }
    if (!value_array_append(&vals, temp)) {
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

  object_store_init(&okay->objects_store, &okay->alloc);

  gc_init(&okay->gc, &okay->objects_store, &okay->alloc);
  okay->alloc.gc = &okay->gc;

  globals_store_init(&okay->globals_store, &okay->alloc);
  gc_watch_globals(&okay->gc, &okay->globals_store);
  compiler_specs compiler_specs = {
      .alloc = &okay->alloc, .objects_store = &okay->objects_store, .globals_store = &okay->globals_store};
  compiler_init(&okay->compiler, compiler_specs);
  gc_watch_compiler(&okay->gc, &okay->compiler);
  vm_specs vm_specs = {
      .alloc = &okay->alloc, .globals_store = &okay->globals_store, .objects_store = &okay->objects_store, .ok = okay};
  vm_init(&okay->vm, vm_specs);
  gc_watch_vm(&okay->gc, &okay->vm);
  if (!ok_define_core_natives(okay)) { // beware we not compiling yet
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_temporary_types(okay)) {
    ok_free(okay);
    return NULL;
  }
  return okay;
}

ok_result ok_run(ok* p_ok, ok_source p_source) {
  p_ok->source = p_source;
  parser parser;
  parser_specs specs = {.alloc = &p_ok->alloc, .source = &p_ok->source};
  parser_init(&parser, specs);
  parse_result parse_result = parser_parse(&parser);
  if (parse_result.status != PARSE_OK) {
    parse_result_deinit(&parse_result);
    return OK_PARSE_ERROR;
  }

#if defined(OK_TRACE_AST)
  gc_pause(&p_ok->gc);
  printf("--- ast ---\n");
  ast_dispatch_print((ast_node*)parse_result.root);
  printf("--- ast ---");
  fflush(stdout);
  gc_resume(&p_ok->gc);
#endif // defined(OK_TRACE_AST)

  compile_specs compile_specs;
  compile_specs.root = parse_result.root;
  compile_specs.source = &p_ok->source;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(p_ok, &err);
  if (err) {
    return OK_INIT_ERROR;
  }
  compile_result compile_result = compiler_compile(&p_ok->compiler, compile_specs);
  top_level_declarations_deinit(&compile_specs.top_level_declarations, &p_ok->alloc);
  parse_result_deinit(&parse_result);
  if (compile_result.status != COMPILE_OK) {
    return OK_COMPILE_ERROR;
  }
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
  interpret_specs.source = &p_ok->source;
  err = false;
  interpret_specs.top_level_values = ok_get_top_level_values(p_ok, &err);
  if (err) {
    return OK_INIT_ERROR;
  }
  interpret_result interpret_result = vm_interpret(&p_ok->vm, interpret_specs);
  value_array_deinit(&interpret_specs.top_level_values, &p_ok->alloc);
  if (interpret_result.status != RUNTIME_OK) {
    interpret_result_deinit(&interpret_result);
    return OK_RUNTIME_ERROR;
  }

  return OK;
}

void ok_debug_dump_bytecode(ok* p_ok, bool p_enabled, bool p_skip_run) {
  p_ok->dump_bytecode = p_enabled;
  p_ok->dump_skip_run = p_skip_run;
}

void ok_free(ok* p_ok) {
  vm_deinit(&p_ok->vm);
  compiler_deinit(&p_ok->compiler);
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

ok_value ok_native_value_new(ok* p_ok, void* p_user_data, ok_native_destructor p_destructor) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_native_value* native = create_object_native_value(p_user_data, p_destructor, &s);
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
