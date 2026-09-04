#include <stdio.h>
#include <stdlib.h>

#define OK_GC_EXPOSE
#include "okgc.h"

#include "ok/ok.h"
#include "okcompiler.h"
#include "okgc.h"
#include "okobject_store.h"
#include "okparser.h"
#include "okspecs.h"
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
      .alloc = &okay->alloc, .globals_store = &okay->globals_store, .objects_store = &okay->objects_store};
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

  gc_pause(&p_ok->gc);
#if defined(OK_TRACE_AST)
  printf("--- ast ---\n");
  ast_dispatch_print((ast_node*)parse_result.root);
  printf("--- ast ---");
  fflush(stdout);
#endif // defined(OK_TRACE_AST)
  gc_resume(&p_ok->gc);

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
