// in the *near* future oklang should be built as a library (static or shared)
// and the driver (cli) is the executable, since oklang is an embedded language.
// but for now it is fine to have both here.

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ok.h"

static void repl(ok* p_ok);
static void run_file(ok* p_ok, const char* p_path);
static char* read_file(const char* p_path);

int main(int argc, char** argv) {
  ok ok;
  ok_init(&ok);
  if (argc == 1) {
    repl(&ok);
  } else if (argc == 2) {
    run_file(&ok, argv[1]);
  } else {
    fprintf(stderr, "usage: ok [path]");
  }
  ok_free(&ok);
}

void repl(ok* p_ok) {
  char line[1024];
  for (;;) {
    printf("> ");
    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }
    ok_run(p_ok, line);
  }
}

void run_file(ok* p_ok, const char* p_path) {
  char* src = read_file(p_path);
  if (src == NULL) {
    exit(1);
  }
  run_result res = ok_run(p_ok, src);
  free(src);
  if (res == RUN_COMPILE_ERROR) {
    exit(65);
  } else if (res == RUN_RUNTIME_ERROR) {
    exit(70);
  }
}

char* read_file(const char* p_path) {
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    fprintf(stderr, "can't open file: \"%s\".\n", p_path);
    exit(74);
  }
  fseek(file, 0l, SEEK_END);
  size_t fsz = ftell(file);
  rewind(file);

  char* buff = (char*)malloc(fsz + 1);
  if (buff == NULL) {
    fprintf(stderr, "not enough memory to load file: \"%s\".\n", p_path);
    exit(74);
  }
  size_t bytes_read = fread(buff, sizeof(char), fsz, file);
  if (bytes_read < fsz) {
    fprintf(stderr, "can't read file: \"%s\".\n", p_path);
    exit(74);
  }
  buff[bytes_read] = '\n';
  fclose(file);
  return buff;
}
