// the zero-dep fuzzer (P5, the front-door patch). the stance, per the
// roadmap: corpus-driven, deterministic seed loop, byte mutations over the
// fixture corpus + synthesized inputs, feeding the lexer/parser/compiler.
// the vm is deliberately NOT fed - hostile bytes must never become a running
// program - so the bar is: no crash, no sanitizer trip, and every diagnostic
// carries a location (validated through the okdiag sink, not by scanning
// stderr). the corpus is committed; the seed loop is deterministic; every
// CI-style run is reproducible to the byte.
//
// usage:
//   okfuzz [iterations] [seed]      the fuzz loop (defaults 20000, 0xC0FFEE)
//   okfuzz --crosscheck <root>      manifest of parse+compile outcomes for
//                                   every .ok/.bin file under root (recursive,
//                                   sorted) - the native side of the
//                                   tree-sitter cross-check; the grammar sweep
//                                   diffs against this.
//   okfuzz --corpus <dir> ...       where the seeds live (default: the
//                                   compiled-in corpus prefix + /corpus)
//
// exit 0 = the bar held. anything else is a finding, and the input that
// tripped it is printed with its seed.

#define _POSIX_C_SOURCE 200809L // dirent, harness-only (never library code)

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ok/ok.h"
#include "ok/ok_string.h"
#include "okcompiler.h"
#include "okdiag.h"
#include "okparser.h"
#include "okstate.h"

#ifndef OK_FUZZ_CORPUS_PREFIX
#define OK_FUZZ_CORPUS_PREFIX ""
#endif

#define FUZZ_MAX_INPUT 4096u
#define FUZZ_INSTANCE_LIFETIME 256u // inputs per ok instance (bounded state bleed)

// ---------------------------------------------------------------- the rng

// splitmix64: deterministic, one word of state, good enough mixing for byte
// mutations. the whole point is reproducibility: seed + iterations == the
// same run, forever.
typedef struct {
  uint64_t state;
} rng;

static uint64_t rng_next(rng* p_rng) {
  uint64_t z = (p_rng->state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

static uint32_t rng_below(rng* p_rng, uint32_t p_bound) {
  return p_bound == 0 ? 0 : (uint32_t)(rng_next(p_rng) % p_bound);
}

// ---------------------------------------------------------------- the corpus

typedef struct {
  char** files;
  uint32_t count;
  uint32_t capacity;
} file_list;

static void file_list_push(file_list* p_list, char* p_path) {
  if (p_list->count == p_list->capacity) {
    p_list->capacity = p_list->capacity == 0 ? 16 : p_list->capacity * 2;
    p_list->files = (char**)realloc(p_list->files, p_list->capacity * sizeof(char*));
    if (p_list->files == NULL) {
      fprintf(stderr, "fuzz: out of memory growing the file list.\n");
      exit(70);
    }
  }
  p_list->files[p_list->count++] = p_path;
}

static int path_cmp(const void* p_a, const void* p_b) {
  return strcmp(*(const char**)p_a, *(const char**)p_b);
}

// recursive, sorted walk: every .ok (and .bin - the invalid-utf-8 seeds)
// under p_root lands in p_out, path-joined into a malloc'd string.
static void walk(file_list* p_out, const char* p_root) {
  DIR* dir = opendir(p_root);
  if (dir == NULL) {
    return;
  }
  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    const size_t root_len = strlen(p_root);
    const size_t name_len = strlen(entry->d_name);
    char* path = (char*)malloc(root_len + 1 + name_len + 1);
    if (path == NULL) {
      closedir(dir);
      fprintf(stderr, "fuzz: out of memory walking the corpus.\n");
      exit(70);
    }
    memcpy(path, p_root, root_len);
    path[root_len] = '/';
    memcpy(path + root_len + 1, entry->d_name, name_len + 1);
    struct stat st;
    if (stat(path, &st) != 0) {
      free(path);
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      walk(p_out, path);
      free(path);
    } else if (S_ISREG(st.st_mode)) {
      const size_t path_len = root_len + 1 + name_len;
      if (path_len >= 4 &&
          (memcmp(path + path_len - 3, ".ok", 3) == 0 || memcmp(path + path_len - 4, ".bin", 4) == 0)) {
        file_list_push(p_out, path);
      } else {
        free(path);
      }
    } else {
      free(path);
    }
  }
  closedir(dir);
  qsort(p_out->files, p_out->count, sizeof(char*), path_cmp);
}

static char* read_whole_file(const char* p_path, size_t* p_length) {
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  rewind(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }
  char* buffer = (char*)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }
  if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
    free(buffer);
    fclose(file);
    return NULL;
  }
  buffer[size] = '\0';
  fclose(file);
  *p_length = (size_t)size;
  return buffer;
}

// ---------------------------------------------------------------- the pipeline

// the counters the summary line is made of.
static uint64_t stat_inputs;
static uint64_t stat_parsed;
static uint64_t stat_compiled;
static uint64_t stat_parse_errors;
static uint64_t stat_compile_errors;
static uint64_t stat_diagnostics;
static uint64_t stat_bare; // the bar: this stays zero or the run fails

// the sink (P5): every rendered diagnostic passes here. the location
// contract: a real span with line >= 1, column >= 1, and an offset that
// actually sits inside the source it names.
static report_status fuzz_sink(bool* p_panic,
                               bool* p_had_error,
                               const char* p_path,
                               const char* p_source,
                               const ok_span* p_span,
                               bool p_is_eof,
                               report_severity p_severity,
                               const ok_string_view p_message,
                               const ok_note* p_note) {
  (void)p_path;
  (void)p_is_eof;
  (void)p_severity;
  (void)p_note;
  *p_panic = true;
  *p_had_error = true;
  stat_diagnostics++;
  const bool located = p_span != NULL && p_span->line >= 1 && p_span->column >= 1;
  bool in_bounds = true;
  if (located && p_source != NULL) {
    const size_t length = strlen(p_source);
    if ((size_t)p_span->offset > length) {
      in_bounds = false;
    }
  }
  if (!located || !in_bounds) {
    stat_bare++;
    fprintf(stdout,
            "FUZZ FINDING (unlocated diagnostic): span=%s line=%u column=%u offset=%u message=%.*s\n",
            p_span == NULL ? "none" : "present",
            p_span == NULL ? 0u : p_span->line,
            p_span == NULL ? 0u : p_span->column,
            p_span == NULL ? 0u : p_span->offset,
            (int)ok_string_view_get_length(&p_message),
            p_message.chars);
  }
  return REPORT_STATUS_OK;
}

// one input through lexer + parser + compiler. never the vm.
static void feed(ok* p_ok, const char* p_code) {
  ok_source source;
  source.code = (char*)p_code;
  source.path = "fuzz";
  parser parser;
  parser_specs specs;
  specs.alloc = &p_ok->alloc;
  specs.source = &source;
  parser_init(&parser, specs);
  parse_result parsed = parser_parse(&parser);
  if (parsed.status != PARSE_OK) {
    stat_parse_errors++;
  } else {
    stat_parsed++;
    compile_specs compile;
    memset(&compile, 0, sizeof(compile));
    compile.root = parsed.root;
    compile.source = &source;
    bool seed_error = false;
    compile.top_level_declarations = ok_get_top_level_declarations(p_ok, &seed_error);
    compile.imports = (ok_import_bindings){NULL, 0};
    compile.is_module = false;
    compile.module_path = ok_create_string_view(NULL, 0, false);
    compile_result compiled = compiler_compile(&p_ok->compiler, compile);
    if (compiled.status == COMPILE_OK) {
      stat_compiled++;
    } else {
      stat_compile_errors++;
    }
    module_exports_deinit(&compiled.exports, &p_ok->alloc);
    top_level_declarations_deinit(&compile.top_level_declarations, &p_ok->alloc);
  }
  parse_result_deinit(&parsed);
}

static ok* make_instance(void) {
  ok_specs specs;
  memset(&specs, 0, sizeof(specs));
  specs.caps_dropped = OK_CAP_ALL; // the hardest door: pure compute, ever
  ok* fresh = ok_create(specs);
  if (fresh == NULL) {
    fprintf(stderr, "fuzz: ok_create refused.\n");
    exit(70);
  }
  return fresh;
}

// ---------------------------------------------------------------- mutation

// bytes with business being interesting: quotes and prefixes, escapes,
// braces and regions, terminators, digits, and the utf-8 edges (a stray
// continuation byte, a lead byte, an always-invalid byte).
static const char k_interesting[] = {'"',   '\'', 'f',  'm',  '{',  '}',  '(',  ')',  '[',  ']',
                                     ';',   ':',  ',',  '.',  '$',  '\\', '\0', ' ',  '\t', '\n',
                                     'u',   'x',  '0',  '7',  'a',  'b',  'e',  '=',  '<',  '>',
                                     '!',   '?',  '&',  '|',  '+',  '-',  '*',  '/',  '%',  '#',
                                     '~',   '^',  (char)0x80, (char)0xC3, (char)0xA9, (char)0xFF};

// token-ish fragments for the synthesis lane: real shapes, hostile orders.
static const char* const k_fragments[] = {
    "let ", "mut ", "pub ", "class ", "fu ", "import ", "defer ", "for ", "while ", "if ",
    "else ", "return ", "break", "continue", "print", "this", "super", "true", "false",
    "null", "new ", "(", ")", "{", "}", "[", "]", ";", ",", ":", ".", "..", "...", "=",
    "==", "===", "!=", "<", ">", "<=", ">=", "&&", "||", "!", "+", "-", "*", "/", "%",
    "123", "1.5", "0x1F", "1e10", "\"s\"", "m\"\"\"", "f\"${", "f\"${x}\"", "\\u{", "\\u{1F600}",
    "\\x", "\\x41", "\\0", "\\e", "//c\n", "${", "}", "\"", "'", "f\"", "m\"", "str", "num",
    "array", "map", "fu(){}", "=> ", "?.", "std", "io", "math", "@", "`", "\xC3\xA9", "\xF0\x9F\x98"};

static char pick_interesting(rng* p_rng) {
  return k_interesting[rng_below(p_rng, sizeof(k_interesting))];
}

static void mutate(rng* p_rng, char* p_buffer, size_t* p_length, const file_list* p_corpus) {
  size_t length = *p_length;
  for (uint32_t round = 1 + rng_below(p_rng, 8); round > 0; --round) {
    const uint32_t op = rng_below(p_rng, 7);
    if (op == 0 && length > 0) { // flip one bit in one byte
      const size_t at = rng_below(p_rng, (uint32_t)length);
      p_buffer[at] ^= (char)(1 << rng_below(p_rng, 8));
    } else if (op == 1 && length + 1 < FUZZ_MAX_INPUT) { // insert an interesting byte
      const size_t at = rng_below(p_rng, (uint32_t)length + 1);
      memmove(p_buffer + at + 1, p_buffer + at, length - at);
      p_buffer[at] = pick_interesting(p_rng);
      length++;
    } else if (op == 2 && length > 1) { // delete one byte
      const size_t at = rng_below(p_rng, (uint32_t)length);
      memmove(p_buffer + at, p_buffer + at + 1, length - at - 1);
      length--;
    } else if (op == 3 && length > 2) { // duplicate a span onto itself
      const size_t at = rng_below(p_rng, (uint32_t)length - 1);
      const size_t span = 1 + rng_below(p_rng, 16);
      if (length - at < span || length + span >= FUZZ_MAX_INPUT) {
        continue;
      }
      memmove(p_buffer + at + span, p_buffer + at, length - at);
      memcpy(p_buffer + at, p_buffer + at + span, span); // the copy now sits before itself
      length += span;
    } else if (op == 4 && p_corpus->count > 0 && length > 0) { // splice from another seed
      const uint32_t other = rng_below(p_rng, p_corpus->count);
      size_t other_length = 0;
      char* donor = read_whole_file(p_corpus->files[other], &other_length);
      if (donor == NULL) {
        continue;
      }
      const size_t take =
          other_length == 0 ? 0 : 1 + rng_below(p_rng, (uint32_t)(other_length > 64 ? 64 : other_length));
      const size_t from = other_length == 0 ? 0 : rng_below(p_rng, (uint32_t)(other_length - take + 1));
      const size_t at = rng_below(p_rng, (uint32_t)length + 1);
      if (length + take < FUZZ_MAX_INPUT) {
        memmove(p_buffer + at + take, p_buffer + at, length - at);
        memcpy(p_buffer + at, donor + from, take);
        length += take;
      }
      free(donor);
    } else if (op == 5 && length > 0) { // replace one byte with an interesting one
      const size_t at = rng_below(p_rng, (uint32_t)length);
      p_buffer[at] = pick_interesting(p_rng);
    } else if (length + 16 < FUZZ_MAX_INPUT) { // splice a fragment in
      const char* fragment = k_fragments[rng_below(p_rng, sizeof(k_fragments) / sizeof(k_fragments[0]))];
      const size_t fragment_length = strlen(fragment);
      const size_t at = rng_below(p_rng, (uint32_t)length + 1);
      memmove(p_buffer + at + fragment_length, p_buffer + at, length - at);
      memcpy(p_buffer + at, fragment, fragment_length);
      length += fragment_length;
    }
  }
  p_buffer[length] = '\0';
  *p_length = length;
}

// ---------------------------------------------------------------- the modes

static int run_fuzz(const char* p_corpus_dir, uint64_t p_iterations, uint64_t p_seed) {
  file_list corpus;
  corpus.files = NULL;
  corpus.count = 0;
  corpus.capacity = 0;
  walk(&corpus, p_corpus_dir);
  if (corpus.count == 0) {
    fprintf(stderr, "fuzz: no corpus under \"%s\" - the seeds are committed, they must be there.\n", p_corpus_dir);
    return 70;
  }

  rng prng;
  prng.state = p_seed;
  ok* instance = make_instance();
  uint32_t instance_age = 0;

  char* buffer = (char*)malloc(FUZZ_MAX_INPUT + 1);
  if (buffer == NULL) {
    fprintf(stderr, "fuzz: out of memory.\n");
    return 70;
  }

  for (uint64_t i = 0; i < p_iterations; ++i) {
    // the base: mostly a corpus seed, sometimes pure synthesis (an empty
    // base grown from fragments alone)
    size_t length = 0;
    if (rng_below(&prng, 4) != 0) {
      const uint32_t which = rng_below(&prng, corpus.count);
      char* seed = read_whole_file(corpus.files[which], &length);
      if (seed == NULL) {
        continue;
      }
      if (length > FUZZ_MAX_INPUT) {
        length = FUZZ_MAX_INPUT;
      }
      memcpy(buffer, seed, length);
      free(seed);
    }
    buffer[length] = '\0';
    mutate(&prng, buffer, &length, &corpus);

    // the triage lane: OK_FUZZ_TRACE=1 prints every input before it feeds,
    // so a crash's last line IS the reproducer.
    if (getenv("OK_FUZZ_TRACE") != NULL) {
      fprintf(stderr, "=== input %llu (len %llu) ===\n%s\n", (unsigned long long)i, (unsigned long long)length, buffer);
      fflush(stderr);
    }

    stat_inputs++;
    feed(instance, buffer);
    if (++instance_age == FUZZ_INSTANCE_LIFETIME) {
      ok_free(instance);
      instance = make_instance();
      instance_age = 0;
    }
  }

  ok_free(instance);
  free(buffer);
  for (uint32_t i = 0; i < corpus.count; ++i) {
    free(corpus.files[i]);
  }
  free(corpus.files);

  printf("fuzz: %llu inputs (%llu parsed, %llu compiled, %llu parse errors, %llu compile errors), "
         "%llu diagnostics, %llu unlocated\n",
         (unsigned long long)stat_inputs,
         (unsigned long long)stat_parsed,
         (unsigned long long)stat_compiled,
         (unsigned long long)stat_parse_errors,
         (unsigned long long)stat_compile_errors,
         (unsigned long long)stat_diagnostics,
         (unsigned long long)stat_bare);
  if (stat_bare != 0) {
    fprintf(stderr, "fuzz: THE BAR TRIPPED - %llu unlocated diagnostics.\n", (unsigned long long)stat_bare);
    return 1;
  }
  return 0;
}

// the crosscheck manifest: for every corpus file, the native PARSER's
// verdict (parse-only - the apples-to-apples with a grammar; compile-time
// resolution is not the grammar's question). the tree-sitter sweep (Q's
// side) diffs against this; drift in either direction is a bug. one fresh
// instance per file - the manifest must not depend on evaluation order.
static int run_crosscheck(const char* p_root) {
  file_list files;
  files.files = NULL;
  files.count = 0;
  files.capacity = 0;
  walk(&files, p_root);
  if (files.count == 0) {
    fprintf(stderr, "crosscheck: nothing to walk under \"%s\".\n", p_root);
    return 70;
  }

  uint64_t accepted = 0;
  uint64_t rejected = 0;
  const size_t root_len = strlen(p_root);
  for (uint32_t i = 0; i < files.count; ++i) {
    ok* instance = make_instance();
    size_t length = 0;
    char* code = read_whole_file(files.files[i], &length);
    if (code == NULL) {
      ok_free(instance);
      continue;
    }
    ok_source source;
    source.code = code;
    source.path = files.files[i];
    parser parser;
    parser_specs specs;
    specs.alloc = &instance->alloc;
    specs.source = &source;
    parser_init(&parser, specs);
    parse_result parsed = parser_parse(&parser);
    // paths print relative to the root: the manifest commits into the repo,
    // and absolute paths would drift with every checkout
    const char* relative = files.files[i];
    if (strncmp(relative, p_root, root_len) == 0 && relative[root_len] == '/') {
      relative += root_len + 1;
    }
    if (parsed.status == PARSE_OK) {
      accepted++;
      printf("accept  %s\n", relative);
    } else {
      rejected++;
      printf("reject  %s  (parse)\n", relative);
    }
    parse_result_deinit(&parsed);
    ok_free(instance);
    free(code);
  }

  for (uint32_t i = 0; i < files.count; ++i) {
    free(files.files[i]);
  }
  free(files.files);
  printf("crosscheck: %llu files, %llu accept, %llu reject, %llu diagnostics, %llu unlocated\n",
         (unsigned long long)(accepted + rejected),
         (unsigned long long)accepted,
         (unsigned long long)rejected,
         (unsigned long long)stat_diagnostics,
         (unsigned long long)stat_bare);
  if (stat_bare != 0) {
    fprintf(stderr, "crosscheck: THE BAR TRIPPED - %llu unlocated diagnostics.\n", (unsigned long long)stat_bare);
    return 1;
  }
  return 0;
}

int main(int argc, char** argv) {
  okdiag_install_sink(fuzz_sink);

  const char* corpus_dir = OK_FUZZ_CORPUS_PREFIX "/corpus";
  const char* crosscheck_root = NULL;
  uint64_t iterations = 20000;
  uint64_t seed = 0xC0FFEEULL;
  uint32_t positional = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) {
      corpus_dir = argv[++i];
    } else if (strcmp(argv[i], "--crosscheck") == 0 && i + 1 < argc) {
      crosscheck_root = argv[++i];
    } else {
      char* end = NULL;
      const unsigned long long value = strtoull(argv[i], &end, 0);
      if (end == argv[i] || *end != '\0' || positional > 1) {
        fprintf(stderr, "usage: okfuzz [--corpus <dir>] [--crosscheck <root>] [iterations] [seed]\n");
        return 64;
      }
      if (positional == 0) {
        iterations = value;
      } else {
        seed = value;
      }
      positional++;
    }
  }

  if (crosscheck_root != NULL) {
    return run_crosscheck(crosscheck_root);
  }
  return run_fuzz(corpus_dir, iterations, seed);
}
