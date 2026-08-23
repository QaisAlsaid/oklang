#include "ok.h"
#include "chunk.h"
#include <stdio.h>
#include <stdlib.h>

void ok_init(ok* p_ok) {
  object_store_init(&p_ok->objects_store);
  globals_store_init(&p_ok->globals_store);
  compiler_init(&p_ok->compiler);
  vm_init(&p_ok->vm);
}

ok_result ok_run(ok* p_ok, source p_source) {
  parser parser;
  parser_init(&parser, &p_source);
  parse_result parse_result = parser_parse(&parser);
  if (parse_result.status != PARSE_OK) {
    parse_result_deinit(&parse_result);
    return OK_PARSE_ERROR;
  }

#if defined(OK_TRACE_AST)
  printf("--- ast ---\n");
  ast_dispatch_print((ast_node*)parse_result.root);
  puts("");
#endif // defined(OK_TRACE_AST)

  compiler_specs compiler_specs;
  compiler_specs.root = parse_result.root;
  compiler_specs.source = &p_source;
  compiler_specs.objects_store = &p_ok->objects_store;
  compiler_specs.globals_store = &p_ok->globals_store;
  compile_result compile_result = compiler_compile(&p_ok->compiler, compiler_specs);
  parse_result_deinit(&parse_result);
  if (compile_result.status != COMPILE_OK) {
    compile_result_deinit(&compile_result);
    return OK_COMPILE_ERROR;
  }
  interpret_specs interpret_specs;
  interpret_specs.chunk = compile_result.chunk;
  interpret_specs.source = &p_source;
  interpret_specs.objects_store = &p_ok->objects_store;
  interpret_specs.globals_store = &p_ok->globals_store;
  interpret_result interpret_result = vm_interpret(&p_ok->vm, interpret_specs);
  compile_result_deinit(&compile_result);

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
}
