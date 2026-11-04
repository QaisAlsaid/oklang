#define _POSIX_C_SOURCE 200809L // the CLI is the posix-ish default host (dlopen lane, build commands)
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ok/ok.h"
#include "ok/ok_module.h"

#include "project.h"

static void repl(ok* p_ok);
static void run_file(ok* p_ok, const char* p_path);
static char* read_file(const char* p_path);

// ---------------------------------------------------------------- the capability flags

// one cap name -> bit. unknown names refuse the run with usage (a typo'd
// --deny must never silently deny nothing).
static bool cap_of_name(const char* p_name, size_t p_length, uint32_t* p_out) {
  if (p_length == 6 && memcmp(p_name, "native", 6) == 0) {
    *p_out = OK_CAP_NATIVE_LOAD;
    return true;
  }
  if (p_length == 5 && memcmp(p_name, "build", 5) == 0) {
    *p_out = OK_CAP_NATIVE_BUILD;
    return true;
  }
  if (p_length == 4 && memcmp(p_name, "host", 4) == 0) {
    *p_out = OK_CAP_HOST;
    return true;
  }
  if (p_length == 3 && memcmp(p_name, "all", 3) == 0) {
    *p_out = OK_CAP_ALL;
    return true;
  }
  return false;
}

// parse a comma-separated cap list ("native,build"). false = unknown name.
static bool parse_caps(const char* p_list, uint32_t* p_out) {
  uint32_t caps = 0;
  const char* cursor = p_list;
  for (;;) {
    const char* comma = strchr(cursor, ',');
    const size_t length = comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
    uint32_t cap = 0;
    if (length == 0 || !cap_of_name(cursor, length, &cap)) {
      return false;
    }
    caps |= cap;
    if (comma == NULL) {
      break;
    }
    cursor = comma + 1;
  }
  *p_out = caps;
  return true;
}

static void usage(void) {
  fprintf(stderr,
          "usage: ok [run] [flags] [path]\n"
          "       ok build [--clean|--force]     run the project's native build commands\n"
          "       ok where <import> [path]       print where an import resolves from\n"
          "       ok\n"
          "flags:\n"
          "  --dump                   compile and disassemble only\n"
          "  --native <path>          force-load a native module (repeatable)\n"
          "  --restrict               drop native load, native build, and host caps\n"
          "  --sandbox=locked         drop every capability (pure compute)\n"
          "  --deny=<list>            drop caps (native, build, host, all)\n"
          "  --allow=<list>           narrow a previous deny (parse time only)\n"
          "  --yes                    grant build consent (no prompts)\n"
          "  --no-build               refuse every build, auto or explicit\n"
          "  --where <import>         resolve <import> from the entry, print, exit\n");
}

// ---------------------------------------------------------------- the project verbs (OR-4)

// `ok build [--clean|--force]`: the project above the cwd, its whole graph's
// native targets, explicit consent (the command itself).
static int cmd_build(int argc, char** argv) {
  bool clean = false;
  bool force = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--clean") == 0) {
      clean = true;
    } else if (strcmp(argv[i], "--force") == 0) {
      force = true;
    } else if (strcmp(argv[i], "--yes") == 0) {
      // accepted for symmetry: ok build's consent is the command itself
    } else {
      usage();
      return 64;
    }
  }
  bool failed = false;
  cli_graph* graph = cli_graph_discover(".", true, false, &failed);
  if (failed) {
    return 70;
  }
  if (graph == NULL) {
    fprintf(stderr, "no project.ok found above the current directory.\n");
    return 1;
  }
  const int code = cli_build_all(graph, force, clean);
  cli_graph_free(graph);
  return code;
}

// `ok where <import> [path]`: the same resolution a script at path (or the
// cwd) would use, printed, exit 0/1.
static int cmd_where(int argc, char** argv) {
  if (argc < 1 || argc > 2) {
    usage();
    return 64;
  }
  const char* import = argv[0];
  char* origin = NULL;
  if (argc == 2) {
    const char* slash = strrchr(argv[1], '/');
    if (slash == NULL) {
      origin = strdup(".");
    } else {
      origin = strndup(argv[1], (size_t)(slash - argv[1]));
    }
  } else {
    origin = getcwd(NULL, 0);
  }
  if (origin == NULL) {
    return 70;
  }

  ok_specs specs;
  memset(&specs, 0, sizeof(specs));
  ok* okay = ok_create(specs);
  if (okay == NULL) {
    free(origin);
    return 70;
  }
  bool failed = false;
  cli_graph* graph = cli_graph_discover(argc == 2 ? argv[1] : ".", true, false, &failed);
  if (failed) {
    ok_free(okay);
    free(origin);
    return 70;
  }
  if (graph != NULL) {
    cli_graph_install(okay, graph);
  }
  const char* canonical = NULL;
  const bool found = ok_module_where(okay, import, origin, &canonical);
  if (found) {
    printf("%s\n", canonical);
  }
  cli_graph_free(graph);
  ok_free(okay);
  free(origin);
  return found ? 0 : 1;
}

int main(int argc, char** argv) {
  // the OR-4 verbs
  if (argc > 1 && strcmp(argv[1], "build") == 0) {
    return cmd_build(argc - 2, argv + 2);
  }
  if (argc > 1 && strcmp(argv[1], "where") == 0) {
    return cmd_where(argc - 2, argv + 2);
  }

  ok_specs specs = {0};
  const char* natives[16];
  uint32_t natives_count = 0;
  bool dump = false;
  bool yes = false;
  bool no_build = false;
  const char* where = NULL;
  const char* path = NULL;
  bool bad = false;

  int i = 1;
  if (i < argc && strcmp(argv[i], "run") == 0) {
    i++; // the explicit form: `ok run [flags] path` is `ok [flags] path`
  }
  for (; i < argc; ++i) {
    const char* arg = argv[i];
    if (strcmp(arg, "--dump") == 0) {
      dump = true;
    } else if (strcmp(arg, "--native") == 0) {
      if (i + 1 >= argc || natives_count == 16) {
        bad = true;
        break;
      }
      natives[natives_count++] = argv[++i];
    } else if (strcmp(arg, "--restrict") == 0) {
      specs.caps_dropped |= OK_CAP_NATIVE_LOAD | OK_CAP_NATIVE_BUILD | OK_CAP_HOST;
    } else if (strcmp(arg, "--sandbox=locked") == 0) {
      specs.caps_dropped = OK_CAP_ALL;
    } else if (strncmp(arg, "--deny=", 7) == 0) {
      uint32_t caps = 0;
      if (!parse_caps(arg + 7, &caps)) {
        bad = true;
        break;
      }
      specs.caps_dropped |= caps;
    } else if (strncmp(arg, "--allow=", 8) == 0) {
      uint32_t caps = 0;
      if (!parse_caps(arg + 8, &caps)) {
        bad = true;
        break;
      }
      // parse-time only: narrowing a deny before the instance exists. the
      // ratchet itself is untouched (nothing grants after ok_create).
      specs.caps_dropped &= ~caps;
    } else if (strcmp(arg, "--yes") == 0) {
      yes = true;
    } else if (strcmp(arg, "--no-build") == 0) {
      no_build = true;
    } else if (strcmp(arg, "--where") == 0) {
      if (i + 1 >= argc || where != NULL) {
        bad = true;
        break;
      }
      where = argv[++i];
    } else if (arg[0] == '-' && arg[1] != '\0') {
      bad = true;
      break;
    } else if (path == NULL) {
      path = arg;
    } else {
      bad = true;
      break;
    }
  }

  if (bad) {
    usage();
    return 64;
  }
  if (where != NULL && path == NULL) {
    usage(); // --where needs the entry it resolves from
    return 64;
  }

  ok* ok = ok_create(specs);
  if (dump) {
    ok_debug_dump_bytecode(ok, true, true);
  }

  // OR-4: the project graph, discovered from the ENTRY file's directory
  // (never cwd, the last implicit working-dir dependence died here). the
  // REPL discovers from the cwd instead: it has no entry file. the graph
  // lives in main's frame for the whole run: the instance borrows its
  // views, and run_file's error exits keep the frame alive with it.
  cli_graph* graph = NULL;
  {
    bool failed = false;
    graph = cli_graph_discover(path != NULL ? path : ".", yes, no_build, &failed);
    if (failed) {
      ok_free(ok);
      return 70;
    }
    if (graph != NULL) {
      cli_graph_install(ok, graph);
    }
  }

  // force-loads happen before the run: scripts import the modules by their
  // declared names, the registry answers.
  for (uint32_t n = 0; n < natives_count; ++n) {
    if (!ok_native_force_load(ok, natives[n], NULL)) {
      ok_free(ok);
      return 70;
    }
  }

  // --where: the resolution question, asked of the same graph, then exit.
  // nothing loads and nothing runs (the where lane probes through reads).
  if (where != NULL) {
    const char* canonical = NULL;
    char* dir = NULL;
    const char* slash = strrchr(path, '/');
    if (slash == NULL) {
      dir = strdup(".");
    } else {
      dir = strndup(path, (size_t)(slash - path));
    }
    bool found = false;
    if (dir != NULL) {
      found = ok_module_where(ok, where, dir, &canonical);
      if (found) {
        printf("%s\n", canonical);
      }
      free(dir);
    }
    ok_free(ok);
    return found ? 0 : 1;
  }

  if (path == NULL && natives_count == 0) {
    repl(ok);
  } else if (path != NULL) {
    run_file(ok, path);
  } else {
    usage();
    ok_free(ok);
    cli_graph_free(graph);
    return 64;
  }
  ok_free(ok);
  cli_graph_free(graph);
}

void repl(ok* p_ok) {
  char line[1024];
  ok_retain_source(p_ok, true);
  for (;;) {
    printf("> ");
    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }
    ok_source source;
    source.code = line;
    source.path = "stdin";
    ok_result result = ok_run(p_ok, source);
    if (result == OK_PARSE_ERROR) {
      fprintf(stderr, "parse error occurred.\n");
    } else if (result == OK_COMPILE_ERROR) {
      fprintf(stderr, "compiler error occurred.\n");
    } else if (result == OK_RUNTIME_ERROR) {
      fprintf(stderr, "runtime error occurred.\n");
    } else if (result == OK_INIT_ERROR) {
      fprintf(stderr, "init error occurred.\n");
    }
  }
}

void run_file(ok* p_ok, const char* p_path) {
  char* src = read_file(p_path);
  if (src == NULL) {
    exit(1);
  }
  ok_source source;
  source.code = src;
  source.path = p_path;
  ok_result result = ok_run(p_ok, source);
  free(src);
  if (result == OK_PARSE_ERROR || result == OK_COMPILE_ERROR) {
    exit(65);
  } else if (result == OK_RUNTIME_ERROR || result == OK_INIT_ERROR) {
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
  buff[bytes_read] = '\0';
  fclose(file);
  return buff;
}
