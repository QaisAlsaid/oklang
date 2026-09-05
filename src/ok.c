#include <stdio.h>
#include <stdlib.h>

#define OK_GC_EXPOSE
#include "okgc.h"

#include "ok/ok.h"
#include "okcompiler.h"
#include "okgc.h"
#include "okobject.h"
#include "okobject_store.h"
#include "okparser.h"
#include "okspecs.h"
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
};

ok* ok_create(ok_specs p_specs) {
  patch_specs(&p_specs);
  ok* okay = p_specs.allocators.allocate(sizeof(ok));
  if (okay == NULL) {
    return NULL;
  }
  okay->raw_alloc = p_specs.allocators;
  okay->alloc = create_allocators(&okay->raw_alloc);

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
  return okay;
}

ok_result ok_run(ok* p_ok, ok_source p_source) {
  parser parser;
  parser_specs specs = {.alloc = &p_ok->alloc, .source = &p_source};
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
  compile_specs.source = &p_source;
  compile_result compile_result = compiler_compile(&p_ok->compiler, compile_specs);
  parse_result_deinit(&parse_result);
  if (compile_result.status != COMPILE_OK) {
    return OK_COMPILE_ERROR;
  }
  interpret_specs interpret_specs;
  interpret_specs.function = compile_result.function;
  interpret_specs.source = &p_source;
  interpret_result interpret_result = vm_interpret(&p_ok->vm, interpret_specs);
  if (interpret_result.status != RUNTIME_OK) {
    interpret_result_deinit(&interpret_result);
    return OK_RUNTIME_ERROR;
  }

  return OK;
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
  return IS_VALUE_NULL(p_value);
}

bool ok_value_is_bool(ok_value p_value) {
  return IS_VALUE_BOOL(p_value);
}

bool ok_value_is_number(ok_value p_value) {
  return IS_VALUE_NUMBER(p_value);
}

bool ok_value_is_object(ok_value p_value) {
  return IS_VALUE_OBJECT(p_value);
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
  uint32_t stack_top = p_ok->vm.stack.top;
  stack_push(&p_ok->vm.stack, p_callee);
  for (uint8_t i = 0; i < p_argc; ++i) {
    stack_push(&p_ok->vm.stack, p_argv[i]);
  }
  interpret_result result = vm_call(&p_ok->vm, p_callee, p_argc);
  if (result.status != RUNTIME_OK) {
    stack_resize(&p_ok->vm.stack, stack_top);
    *p_result = NULL_AS_VALUE();
    return false;
  }
  *p_result = stack_popr(&p_ok->vm.stack);
  return true;
}

bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable) {
  gc_guard_up(&p_ok->gc, p_value);
  uint32_t packed = globals_store_add(&p_ok->globals_store, p_name, p_is_mutable);
  uint32_t index = global_get_raw_index(packed);
  if (!IS_GLOBAL_VALID(packed)) {
    gc_guard_down(&p_ok->gc);
    return false;
  }
  p_ok->globals_store.global_values.data[index] = p_value;
  gc_guard_down(&p_ok->gc);
  return true;
}

bool ok_global_get(ok* p_ok, ok_string_view p_name, ok_value* p_value) {
  uint32_t packed = globals_store_get(&p_ok->globals_store, p_name);
  if (!IS_GLOBAL_VALID(packed)) {
    *p_value = NULL_AS_VALUE();
    return false;
  }
  *p_value = p_ok->globals_store.global_values.data[global_get_raw_index(packed)];
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
  if (!IS_VALUE_OBJECT(p_value)) {
    return NULL;
  }

  object* obj = VALUE_AS_OBJECT(p_value);

  if (object_get_type(obj) != OBJ_NATIVE_VALUE) {
    return NULL;
  }

  object_native_value* native = (object_native_value*)obj;
  return native->user_data;
}
