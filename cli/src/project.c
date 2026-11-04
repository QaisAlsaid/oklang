#define _XOPEN_SOURCE 700 // the CLI is the posix-ish default host (stamps, realpath, popen)
// OR-4 "O Resolution" - the CLI's project machinery. see project.h.

#include "project.h"

#include "ok/ok_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------- small plumbing

static char* dup_str(const char* p_text) {
  const size_t n = strlen(p_text);
  char* copy = (char*)malloc(n + 1);
  if (copy != NULL) {
    memcpy(copy, p_text, n + 1);
  }
  return copy;
}

static char* dup_view(const ok_cstring_view* p_view) {
  const size_t n = p_view->chars != NULL ? strlen(p_view->chars) : 0;
  char* copy = (char*)malloc(n + 1);
  if (copy != NULL) {
    if (n > 0) {
      memcpy(copy, p_view->chars, n);
    }
    copy[n] = '\0';
  }
  return copy;
}

static char* join_path(const char* p_dir, const char* p_rel) {
  if (p_rel[0] == '/') {
    return dup_str(p_rel);
  }
  const size_t a = strlen(p_dir);
  const size_t b = strlen(p_rel);
  char* out = (char*)malloc(a + b + 2);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, p_dir, a);
  out[a] = '/';
  memcpy(out + a + 1, p_rel, b + 1);
  return out;
}

// the directory part of a path (a copy): "a/b/c.ok" -> "a/b", "x.ok" -> "."
static char* dir_name(const char* p_path) {
  const char* slash = strrchr(p_path, '/');
  if (slash == NULL) {
    return dup_str(".");
  }
  if (slash == p_path) {
    return dup_str("/");
  }
  const size_t n = (size_t)(slash - p_path);
  char* out = (char*)malloc(n + 1);
  if (out != NULL) {
    memcpy(out, p_path, n);
    out[n] = '\0';
  }
  return out;
}

// absolute-if-possible: realpath when the path exists, the plain join
// otherwise (an unbuilt artifact's tail does not exist yet, and the
// library canonicalizes again at load). frees the input either way.
static char* canonical_or_free(char* p_path) {
  char* resolved = realpath(p_path, NULL);
  if (resolved != NULL) {
    free(p_path);
    return resolved;
  }
  return p_path;
}

static bool file_readable(const char* p_path) {
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return false;
  }
  fclose(file);
  return true;
}

static bool is_directory(const char* p_path) {
  struct stat info;
  return stat(p_path, &info) == 0 && S_ISDIR(info.st_mode);
}

static char* read_file_or_null(const char* p_path) {
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return NULL;
  }
  fseek(file, 0L, SEEK_END);
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
  const size_t read = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read] = '\0';
  return buffer;
}

// ---------------------------------------------------------------- discovery

// walk up from p_dir looking for project.ok. NULL = none up there.
static char* find_project_root(const char* p_dir) {
  char* current = canonical_or_free(dup_str(p_dir));
  if (current == NULL) {
    return NULL;
  }
  for (;;) {
    char* manifest = join_path(current, "project.ok");
    if (manifest == NULL) {
      free(current);
      return NULL;
    }
    const bool found = file_readable(manifest);
    free(manifest);
    if (found) {
      return current;
    }
    // up one (stop at the filesystem root: "/" has no parent)
    const size_t length = strlen(current);
    if (length == 1 && current[0] == '/') {
      free(current);
      return NULL;
    }
    char* slash = strrchr(current, '/');
    if (slash == current) {
      current[1] = '\0'; // "/x" -> "/"
    } else if (slash != NULL) {
      *slash = '\0';
    } else {
      free(current);
      current = dup_str(".");
      if (current == NULL) {
        return NULL;
      }
    }
  }
}

// ---------------------------------------------------------------- the manifest harvest

// one manifest table field read: the value for a string key
static bool map_field(ok* p_ok, const ok_value p_map, const char* p_key, ok_value* p_out) {
  ok_value key = ok_value_string(p_ok, ok_create_string_view(p_key, (uint32_t)strlen(p_key), true));
  return ok_map_get(p_map, key, p_out);
}

static bool string_field(ok* p_ok, const ok_value p_map, const char* p_key, char** p_out) {
  ok_value value;
  if (!map_field(p_ok, p_map, p_key, &value) || !ok_value_is_string(value)) {
    return false;
  }
  const ok_cstring_view text = ok_value_as_string(value);
  *p_out = dup_view(&text);
  return *p_out != NULL;
}

static void project_deinit(cli_project* p_project) {
  free(p_project->root);
  for (uint32_t i = 0; i < p_project->module_roots_count; ++i) {
    free(p_project->module_roots[i]);
  }
  free(p_project->module_roots);
  for (uint32_t i = 0; i < p_project->natives_count; ++i) {
    free(p_project->natives[i].name);
    free(p_project->natives[i].dir);
    free(p_project->natives[i].build);
    free(p_project->natives[i].artifact);
  }
  free(p_project->natives);
  for (uint32_t i = 0; i < p_project->deps_count; ++i) {
    free(p_project->deps[i].name);
    free(p_project->deps[i].path);
  }
  free(p_project->deps);
  memset(p_project, 0, sizeof(*p_project));
}

// evaluate one project.ok into a cli_project. prints its own diagnostics
// (the manifest's own path, the teaching errors). false = failure.
static bool eval_manifest(const char* p_root, cli_project* p_out) {
  memset(p_out, 0, sizeof(*p_out));
  char* manifest_path = join_path(p_root, "project.ok");
  if (manifest_path == NULL) {
    return false;
  }
  char* code = read_file_or_null(manifest_path);
  if (code == NULL) {
    fprintf(stderr, "can't read the manifest: \"%s\".\n", manifest_path);
    free(manifest_path);
    return false;
  }

  // the manifest instance: full stdlib, both native caps refused, always
  // (M10: "not limited, but it can't load native")
  ok_specs specs;
  memset(&specs, 0, sizeof(specs));
  specs.caps_dropped = OK_CAP_NATIVE_LOAD | OK_CAP_NATIVE_BUILD;
  ok* eval_ok = ok_create(specs);
  if (eval_ok == NULL) {
    free(code);
    free(manifest_path);
    return false;
  }

  const ok_source source = {code, manifest_path};
  ok_value manifest = ok_module_evaluate(eval_ok, source);
  free(code);
  if (ok_value_is_null(manifest)) {
    // the evaluation's diagnostics printed with the manifest's own path
    ok_free(eval_ok);
    free(manifest_path);
    return false;
  }
  const uint32_t manifest_root = ok_root(eval_ok, manifest);
  if (manifest_root == OK_ROOT_INVALID) {
    ok_free(eval_ok);
    free(manifest_path);
    return false;
  }

  // harvest: the pub 'project' table
  ok_value table = ok_value_null();
  bool found = false;
  const uint32_t exports = ok_module_export_count(eval_ok, manifest);
  for (uint32_t i = 0; i < exports; ++i) {
    const char* name = ok_module_export_name(eval_ok, manifest, i);
    if (name != NULL && strcmp(name, "project") == 0) {
      table = ok_module_export_get(eval_ok, manifest, i);
      found = true;
      break;
    }
  }
  bool ok = false;
  if (!found || !ok_value_is_map(table)) {
    fprintf(stderr,
            "%s: the manifest exports no 'project' table: a pub let named 'project', holding the project's "
            "modules, deps and natives.\n",
            manifest_path);
    goto done;
  }

  p_out->root = dup_str(p_root);

  // "modules": an array of strings, default ["src"]
  {
    ok_value modules;
    if (map_field(eval_ok, table, "modules", &modules)) {
      if (!ok_value_is_array(modules)) {
        fprintf(stderr, "%s: the 'modules' field must be an array of strings.\n", manifest_path);
        goto done;
      }
      const uint32_t count = ok_array_len(modules);
      p_out->module_roots = (char**)calloc(count > 0 ? count : 1, sizeof(char*));
      if (p_out->module_roots == NULL) {
        goto oom;
      }
      for (uint32_t i = 0; i < count; ++i) {
        const ok_value item = ok_array_get(modules, i);
        if (!ok_value_is_string(item)) {
          fprintf(stderr, "%s: the 'modules' field must be an array of strings.\n", manifest_path);
          goto done;
        }
        const ok_cstring_view text = ok_value_as_string(item);
        p_out->module_roots[i] = dup_view(&text);
        if (p_out->module_roots[i] == NULL) {
          goto oom;
        }
        p_out->module_roots_count++;
      }
    }
  }
  if (p_out->module_roots_count == 0) {
    p_out->module_roots = (char**)calloc(1, sizeof(char*));
    if (p_out->module_roots == NULL) {
      goto oom;
    }
    p_out->module_roots[0] = dup_str("src");
    if (p_out->module_roots[0] == NULL) {
      goto oom;
    }
    p_out->module_roots_count = 1;
  }

  // "deps": a map of names to tables with a string "path"
  {
    ok_value deps;
    if (map_field(eval_ok, table, "deps", &deps)) {
      if (!ok_value_is_map(deps)) {
        fprintf(stderr, "%s: the 'deps' field must be a map of names to tables.\n", manifest_path);
        goto done;
      }
      const uint32_t count = ok_map_len(deps);
      p_out->deps = (cli_dep*)calloc(count > 0 ? count : 1, sizeof(cli_dep));
      if (p_out->deps == NULL) {
        goto oom;
      }
      for (uint32_t i = 0; i < count; ++i) {
        ok_value key;
        ok_value entry;
        if (!ok_map_pair_at(deps, i, &key, &entry) || !ok_value_is_string(key)) {
          fprintf(stderr, "%s: the 'deps' field must be a map of names to tables.\n", manifest_path);
          goto done;
        }
        const ok_cstring_view name = ok_value_as_string(key);
        if (name.chars == NULL || name.chars[0] == '\0') {
          fprintf(stderr, "%s: a project dep needs a name.\n", manifest_path);
          goto done;
        }
        cli_dep* dep = &p_out->deps[p_out->deps_count];
        dep->name = dup_view(&name);
        if (dep->name == NULL) {
          goto oom;
        }
        if (strcmp(dep->name, "std") == 0) {
          fprintf(stderr, "%s: the dep name 'std' is reserved.\n", manifest_path);
          goto done;
        }
        if (!ok_value_is_map(entry)) {
          fprintf(stderr, "%s: project dep '%s' needs a string 'path' field.\n", manifest_path, dep->name);
          goto done;
        }
        char* rel = NULL;
        if (!string_field(eval_ok, entry, "path", &rel) || rel == NULL) {
          fprintf(stderr, "%s: project dep '%s' needs a string 'path' field.\n", manifest_path, dep->name);
          goto done;
        }
        char* joined = canonical_or_free(join_path(p_root, rel));
        free(rel);
        dep->path = joined;
        if (dep->path == NULL || !is_directory(dep->path)) {
          fprintf(stderr,
                  "%s: project dep '%s' does not exist: %s.\n",
                  manifest_path,
                  dep->name,
                  joined != NULL ? joined : "(unresolvable path)");
          goto done;
        }
        char* dep_manifest = join_path(dep->path, "project.ok");
        if (dep_manifest == NULL) {
          goto oom;
        }
        dep->is_project = file_readable(dep_manifest);
        free(dep_manifest);
        p_out->deps_count++;
      }
    }
  }

  // "natives": a map of names to tables with string "build" and "artifact"
  {
    ok_value natives;
    if (map_field(eval_ok, table, "natives", &natives)) {
      if (!ok_value_is_map(natives)) {
        fprintf(stderr, "%s: the 'natives' field must be a map of names to tables.\n", manifest_path);
        goto done;
      }
      const uint32_t count = ok_map_len(natives);
      p_out->natives = (cli_native*)calloc(count > 0 ? count : 1, sizeof(cli_native));
      if (p_out->natives == NULL) {
        goto oom;
      }
      for (uint32_t i = 0; i < count; ++i) {
        ok_value key;
        ok_value entry;
        if (!ok_map_pair_at(natives, i, &key, &entry) || !ok_value_is_string(key)) {
          fprintf(stderr, "%s: the 'natives' field must be a map of names to tables.\n", manifest_path);
          goto done;
        }
        const ok_cstring_view name = ok_value_as_string(key);
        if (name.chars == NULL || name.chars[0] == '\0') {
          fprintf(stderr, "%s: a native target needs a name.\n", manifest_path);
          goto done;
        }
        cli_native* native = &p_out->natives[p_out->natives_count];
        native->name = dup_view(&name);
        if (native->name == NULL) {
          goto oom;
        }
        if (strcmp(native->name, "std") == 0) {
          fprintf(stderr, "%s: the native target name 'std' is reserved.\n", manifest_path);
          goto done;
        }
        if (!ok_value_is_map(entry) || !string_field(eval_ok, entry, "build", &native->build) ||
            native->build == NULL) {
          fprintf(stderr,
                  "%s: native target '%s' needs string 'build' and 'artifact' fields.\n",
                  manifest_path,
                  native->name);
          goto done;
        }
        char* artifact_rel = NULL;
        if (!string_field(eval_ok, entry, "artifact", &artifact_rel) || artifact_rel == NULL) {
          fprintf(stderr,
                  "%s: native target '%s' needs string 'build' and 'artifact' fields.\n",
                  manifest_path,
                  native->name);
          goto done;
        }
        native->artifact = canonical_or_free(join_path(p_root, artifact_rel));
        free(artifact_rel);
        if (native->artifact == NULL) {
          goto oom;
        }
        string_field(eval_ok, entry, "dir", &native->dir); // optional, display only
        p_out->natives_count++;
      }
    }
  }

  // the ambiguity check (M13): one name, one meaning. a dep and a native
  // target sharing a name would answer one import two ways.
  for (uint32_t d = 0; d < p_out->deps_count; ++d) {
    for (uint32_t n = 0; n < p_out->natives_count; ++n) {
      if (strcmp(p_out->deps[d].name, p_out->natives[n].name) == 0) {
        fprintf(stderr,
                "%s: the name '%s' is both a dep (%s) and a native target (%s): rename one of them.\n",
                manifest_path,
                p_out->deps[d].name,
                p_out->deps[d].path,
                p_out->natives[n].artifact);
        goto done;
      }
    }
  }

  ok = true;
  goto done;

oom:
  fprintf(stderr, "out of memory while reading the manifest.\n");
done:
  ok_unroot(eval_ok, manifest_root);
  ok_free(eval_ok);
  free(manifest_path);
  if (!ok) {
    project_deinit(p_out);
  }
  return ok;
}

// ---------------------------------------------------------------- the graph

typedef struct {
  cli_project* items;
  uint32_t count;
  uint32_t capacity;
  // the roots in flight (the cycle chain, in order)
  const char** visiting;
  uint32_t visiting_count;
  uint32_t visiting_capacity;
} graph_build;

static void graph_build_deinit(graph_build* p_build) {
  for (uint32_t i = 0; i < p_build->count; ++i) {
    project_deinit(&p_build->items[i]);
  }
  free(p_build->items);
  free(p_build->visiting);
}

static bool projects_push(graph_build* p_build, cli_project p_project) {
  if (p_build->count == p_build->capacity) {
    const uint32_t grown = p_build->capacity == 0 ? 4 : p_build->capacity * 2;
    cli_project* items = (cli_project*)realloc(p_build->items, sizeof(cli_project) * grown);
    if (items == NULL) {
      return false;
    }
    p_build->items = items;
    p_build->capacity = grown;
  }
  p_build->items[p_build->count++] = p_project;
  return true;
}

static bool visiting_push(graph_build* p_build, const char* p_root) {
  if (p_build->visiting_count == p_build->visiting_capacity) {
    const uint32_t grown = p_build->visiting_capacity == 0 ? 8 : p_build->visiting_capacity * 2;
    const char** visiting = (const char**)realloc(p_build->visiting, sizeof(const char*) * grown);
    if (visiting == NULL) {
      return false;
    }
    p_build->visiting = visiting;
    p_build->visiting_capacity = grown;
  }
  p_build->visiting[p_build->visiting_count++] = p_root;
  return true;
}

// evaluate the project at p_root and recurse into its project deps. roots
// are evaluated once (dedup by path), cycles name their chain.
static bool add_project(graph_build* p_build, const char* p_root) {
  // already in the graph: nothing to do (the library matches by root)
  for (uint32_t i = 0; i < p_build->count; ++i) {
    if (strcmp(p_build->items[i].root, p_root) == 0) {
      return true;
    }
  }
  // the cycle chain check
  for (uint32_t i = 0; i < p_build->visiting_count; ++i) {
    if (strcmp(p_build->visiting[i], p_root) == 0) {
      fprintf(stderr, "project dependency cycle:");
      for (uint32_t j = i; j < p_build->visiting_count; ++j) {
        fprintf(stderr, " %s ->", p_build->visiting[j]);
      }
      fprintf(stderr, " %s.\n", p_root);
      return false;
    }
  }

  cli_project project;
  if (!eval_manifest(p_root, &project)) {
    return false;
  }
  if (!visiting_push(p_build, p_root)) {
    project_deinit(&project);
    fprintf(stderr, "out of memory while walking the project graph.\n");
    return false;
  }
  for (uint32_t d = 0; d < project.deps_count; ++d) {
    if (project.deps[d].is_project && !add_project(p_build, project.deps[d].path)) {
      p_build->visiting_count--;
      project_deinit(&project);
      return false;
    }
  }
  p_build->visiting_count--;
  if (!projects_push(p_build, project)) {
    project_deinit(&project);
    fprintf(stderr, "out of memory while walking the project graph.\n");
    return false;
  }
  return true;
}

cli_graph* cli_graph_discover(const char* p_entry_path, bool p_yes, bool p_no_build, bool* p_failed) {
  *p_failed = false;
  char* dir = dir_name(p_entry_path);
  if (dir == NULL) {
    *p_failed = true;
    fprintf(stderr, "out of memory.\n");
    return NULL;
  }
  char* root = find_project_root(dir);
  free(dir);
  if (root == NULL) {
    return NULL; // the standalone lane, not an error
  }

  graph_build build;
  memset(&build, 0, sizeof(build));
  if (!add_project(&build, root)) {
    free(root);
    graph_build_deinit(&build);
    *p_failed = true;
    return NULL;
  }
  free(root);
  free(build.visiting); // the cycle chain is spent; the items move to the graph

  cli_graph* graph = (cli_graph*)calloc(1, sizeof(cli_graph));
  if (graph == NULL) {
    graph_build_deinit(&build);
    *p_failed = true;
    fprintf(stderr, "out of memory.\n");
    return NULL;
  }
  graph->projects = build.items;
  graph->count = build.count;
  graph->yes = p_yes;
  graph->no_build = p_no_build;

  // the borrowed views: flat arrays, per-project slices
  uint32_t total_roots = 0;
  uint32_t total_natives = 0;
  uint32_t total_deps = 0;
  for (uint32_t i = 0; i < graph->count; ++i) {
    total_roots += graph->projects[i].module_roots_count;
    total_natives += graph->projects[i].natives_count;
    total_deps += graph->projects[i].deps_count;
  }
  graph->view = (ok_project*)calloc(graph->count > 0 ? graph->count : 1, sizeof(ok_project));
  graph->view_roots = (const char**)calloc(total_roots > 0 ? total_roots : 1, sizeof(char*));
  graph->view_natives = (ok_project_native*)calloc(total_natives > 0 ? total_natives : 1, sizeof(ok_project_native));
  graph->view_deps = (ok_project_dep*)calloc(total_deps > 0 ? total_deps : 1, sizeof(ok_project_dep));
  if (graph->view == NULL || graph->view_roots == NULL || graph->view_natives == NULL || graph->view_deps == NULL) {
    cli_graph_free(graph); // frees the moved projects array and every view
    *p_failed = true;
    fprintf(stderr, "out of memory.\n");
    return NULL;
  }
  uint32_t root_cursor = 0;
  uint32_t native_cursor = 0;
  uint32_t dep_cursor = 0;
  for (uint32_t i = 0; i < graph->count; ++i) {
    const cli_project* project = &graph->projects[i];
    ok_project* view = &graph->view[i];
    view->root = project->root;
    view->module_roots = &graph->view_roots[root_cursor];
    for (uint32_t r = 0; r < project->module_roots_count; ++r) {
      graph->view_roots[root_cursor++] = project->module_roots[r];
    }
    view->module_roots_count = project->module_roots_count;
    view->natives = &graph->view_natives[native_cursor];
    for (uint32_t n = 0; n < project->natives_count; ++n) {
      graph->view_natives[native_cursor].name = project->natives[n].name;
      graph->view_natives[native_cursor].artifact = project->natives[n].artifact;
      graph->view_natives[native_cursor].build = project->natives[n].build;
      native_cursor++;
    }
    view->natives_count = project->natives_count;
    view->deps = &graph->view_deps[dep_cursor];
    for (uint32_t d = 0; d < project->deps_count; ++d) {
      graph->view_deps[dep_cursor].name = project->deps[d].name;
      graph->view_deps[dep_cursor].path = project->deps[d].path;
      graph->view_deps[dep_cursor].is_project = project->deps[d].is_project;
      dep_cursor++;
    }
    view->deps_count = project->deps_count;
  }
  return graph;
}

void cli_graph_free(cli_graph* p_graph) {
  if (p_graph == NULL) {
    return;
  }
  for (uint32_t i = 0; i < p_graph->count; ++i) {
    project_deinit(&p_graph->projects[i]);
  }
  free(p_graph->projects);
  free(p_graph->view);
  free(p_graph->view_roots);
  free(p_graph->view_natives);
  free(p_graph->view_deps);
  free(p_graph);
}

// ---------------------------------------------------------------- the build lane

// the staleness stamp: <root>/.okbuild/<name>.stamp holds the host's
// native abi line and the build command of the last success. fresh =
// artifact readable AND stamp == expected. the abi line (patch 51) is the
// toolchain's fingerprint: an ok whose abi changed never trusts an older
// artifact - the bump that taught this was 2 -> 3 leaving a green stamp
// over a stale .so.
static char* stamp_content(const char* p_build) {
  const size_t room = 16 + strlen(p_build) + 2;
  char* out = (char*)malloc(room);
  if (out == NULL) {
    return NULL;
  }
  sprintf(out, "%u\n%s", (unsigned)OK_NATIVE_ABI, p_build);
  return out;
}

static char* stamp_path(const char* p_root, const char* p_name) {
  const size_t a = strlen(p_root);
  const size_t b = strlen(p_name);
  char* out = (char*)malloc(a + b + sizeof("/.okbuild/.stamp"));
  if (out == NULL) {
    return NULL;
  }
  sprintf(out, "%s/.okbuild/%s.stamp", p_root, p_name);
  return out;
}

static bool target_fresh(const char* p_root, const char* p_name, const char* p_build, const char* p_artifact) {
  if (!file_readable(p_artifact)) {
    return false;
  }
  char* stamp = stamp_path(p_root, p_name);
  if (stamp == NULL) {
    return false;
  }
  char* recorded = read_file_or_null(stamp);
  free(stamp);
  if (recorded == NULL) {
    return false;
  }
  // the abi line and the command string, unchanged since the last success
  char* expected = stamp_content(p_build);
  const bool same = expected != NULL && strcmp(recorded, expected) == 0;
  free(expected);
  free(recorded);
  return same;
}

static void write_stamp(const char* p_root, const char* p_name, const char* p_build) {
  char* dir = (char*)malloc(strlen(p_root) + sizeof("/.okbuild"));
  if (dir == NULL) {
    return;
  }
  sprintf(dir, "%s/.okbuild", p_root);
  mkdir(dir, 0777); // EEXIST is fine
  free(dir);
  char* stamp = stamp_path(p_root, p_name);
  if (stamp == NULL) {
    return;
  }
  FILE* file = fopen(stamp, "wb");
  if (file != NULL) {
    char* content = stamp_content(p_build);
    if (content != NULL) {
      fwrite(content, 1, strlen(content), file);
      free(content);
    }
    fclose(file);
  }
  free(stamp);
}

// run one declared build command from the project root, through the shell,
// with both output streams captured. quiet on success (the declared-build
// contract), the captured output prints only on failure.
static bool run_build_command(const char* p_root, const char* p_name, const char* p_build) {
  const size_t room = strlen(p_root) + strlen(p_build) + sizeof("cd \"\" && (\" \") 2>&1");
  char* command = (char*)malloc(room);
  if (command == NULL) {
    fprintf(stderr, "out of memory while building '%s'.\n", p_name);
    return false;
  }
  sprintf(command, "cd \"%s\" && (%s) 2>&1", p_root, p_build);
  FILE* pipe = popen(command, "r");
  free(command);
  if (pipe == NULL) {
    fprintf(stderr, "could not run the build command for '%s'.\n", p_name);
    return false;
  }
  char* output = NULL;
  size_t length = 0;
  for (;;) {
    char buffer[4096];
    const size_t read = fread(buffer, 1, sizeof(buffer), pipe);
    if (read == 0) {
      break;
    }
    char* grown = (char*)realloc(output, length + read + 1);
    if (grown == NULL) {
      free(output);
      pclose(pipe);
      fprintf(stderr, "out of memory while building '%s'.\n", p_name);
      return false;
    }
    output = grown;
    memcpy(output + length, buffer, read);
    length += read;
  }
  const int status = pclose(pipe);
  if (status != 0) {
    fprintf(stderr, "the build command for native target '%s' failed:\n", p_name);
    if (output != NULL) {
      fwrite(output, 1, length, stderr);
      if (length > 0 && output[length - 1] != '\n') {
        fputc('\n', stderr);
      }
    }
    free(output);
    return false;
  }
  free(output);
  return true;
}

// the one ensure: staleness, consent, the run, the verification, the stamp.
// p_explicit = ok build's lane (the command itself is the consent, no
// prompts); p_force skips the freshness check.
static bool ensure_native(const char* p_root,
                          const char* p_name,
                          const char* p_build,
                          const char* p_artifact,
                          bool p_yes,
                          bool p_no_build,
                          bool p_explicit,
                          bool p_force,
                          char* p_err,
                          size_t p_err_capacity) {
  if (!p_force && target_fresh(p_root, p_name, p_build, p_artifact)) {
    return true;
  }
  if (p_no_build && !p_explicit) {
    snprintf(p_err, p_err_capacity, "auto-build refused: --no-build. run 'ok build' first, or drop the flag.");
    return false;
  }
  if (!p_yes && !p_explicit) {
    if (!isatty(0)) {
      snprintf(p_err,
               p_err_capacity,
               "auto-build needs consent and stdin is not a terminal: pass --yes, or run 'ok build' first.");
      return false;
    }
    printf("first build of native target '%s':\n  %s\nproceed? [y/N] ", p_name, p_build);
    fflush(stdout);
    char answer[32];
    if (fgets(answer, sizeof(answer), stdin) == NULL || (answer[0] != 'y' && answer[0] != 'Y')) {
      snprintf(p_err, p_err_capacity, "consent refused: native target '%s' was not built.", p_name);
      return false;
    }
  }
  if (!run_build_command(p_root, p_name, p_build)) {
    snprintf(p_err, p_err_capacity, "the build command failed.");
    return false;
  }
  if (!file_readable(p_artifact)) {
    snprintf(p_err, p_err_capacity, "the build finished but the artifact is missing: %s.", p_artifact);
    return false;
  }
  write_stamp(p_root, p_name, p_build);
  return true;
}

// the library's hook: the graph carries the policy flags
static bool ensure_hook(void* p_ctx, const ok_project* p_project, const ok_project_native* p_target, char* p_err,
                       size_t p_err_capacity) {
  const cli_graph* graph = (const cli_graph*)p_ctx;
  return ensure_native(p_project->root, p_target->name, p_target->build != NULL ? p_target->build : "true",
                       p_target->artifact, graph->yes, graph->no_build, false, false, p_err, p_err_capacity);
}

void cli_graph_install(ok* p_ok, const cli_graph* p_graph) {
  ok_project_build_hooks hooks;
  hooks.ensure = ensure_hook;
  hooks.p_ctx = (void*)p_graph;
  ok_set_projects(p_ok, p_graph->view, p_graph->count, hooks);
}

int cli_build_all(const cli_graph* p_graph, bool p_force, bool p_clean) {
  for (uint32_t i = 0; i < p_graph->count; ++i) {
    const cli_project* project = &p_graph->projects[i];
    for (uint32_t n = 0; n < project->natives_count; ++n) {
      const cli_native* native = &project->natives[n];
      if (p_clean) {
        char* stamp = stamp_path(project->root, native->name);
        if (stamp != NULL) {
          remove(stamp);
          free(stamp);
        }
      }
      if (target_fresh(project->root, native->name, native->build, native->artifact) && !p_force && !p_clean) {
        printf("ok build: %s up to date (%s)\n", native->name, native->artifact);
        continue;
      }
      char err[512];
      err[0] = '\0';
      if (!ensure_native(project->root, native->name, native->build, native->artifact, true, false, true,
                         p_force || p_clean, err, sizeof(err))) {
        fprintf(stderr, "ok build: %s failed: %s\n", native->name, err);
        return 1;
      }
      printf("ok build: %s built (%s)\n", native->name, native->artifact);
    }
  }
  return 0;
}
