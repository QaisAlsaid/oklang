#include "ok.h"
#include "chunk.h"
#include <stdlib.h>
#include <stdio.h>

void ok_init(ok* p_ok) {
  p_ok->compiler = malloc(sizeof(compiler));
  p_ok->vm = malloc(sizeof(vm));
  compiler_init(p_ok->compiler);
  vm_init(p_ok->vm);
}

run_result ok_run(ok* p_ok, source p_source) {
  parser parser;
  parser_init(&parser, &p_source);
  parse_result parse_result = parser_parse(&parser);
  if (parse_result.status != PARSE_OK) {
    parse_result_deinit(&parse_result);
    return RUN_PARSE_ERROR;
  }

  ast_root_statements_list_node* node = parse_result.root->statements.head;
  
  while(node != NULL) {
    printf("%i\n", (int)node->statement->node.node_type);
    node = node->next;
  }

  compiler_specs compiler_specs;
  compiler_specs.root = parse_result.root;
  compiler_specs.source = &p_source;
  compile_result compile_result = compiler_compile(p_ok->compiler, compiler_specs);
  parse_result_deinit(&parse_result);
  if (compile_result.status != COMPILE_OK) {
    compile_result_deinit(&compile_result);
    return RUN_COMPILE_ERROR;
  }
  return RUN_OK;
  interpret_specs interpret_specs;
  interpret_specs.chunk = compile_result.chunk;
  interpret_specs.source = &p_source;

  interpret_result interpret_result = vm_interpret(p_ok->vm, interpret_specs);
  compile_result_deinit(&compile_result);

  if (interpret_result.status != RUNTIME_OK) {
    interpret_result_deinit(&interpret_result);
    return RUN_RUNTIME_ERROR;
  }

  return RUN_OK;
}

void ok_free(ok* p_ok) 
{
  vm_deinit(p_ok->vm);
  compiler_deinit(p_ok->compiler);
  free(p_ok->vm);
  free(p_ok->compiler);
}
