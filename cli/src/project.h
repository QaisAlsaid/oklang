#ifndef OK_CLI_PROJECT_H
#define OK_CLI_PROJECT_H

// OR-4 "O Resolution" - the CLI's project machinery: project.ok discovery,
// the manifest evaluator's harvest, the dependency graph, the .okbuild
// staleness stamps, and the consent-gated build lane. the library carries
// the resolution semantics (ok_set_projects); this file is the host that
// knows what a file system and a shell are.

#include "ok/ok.h"

#include <stdbool.h>
#include <stdint.h>

// one declared native target (the manifest's "natives" table entry)
typedef struct {
  char* name;     // the import name
  char* dir;      // the declared source dir, display only (may be NULL)
  char* build;    // the declared build command
  char* artifact; // what the name resolves to, absolute
} cli_native;

// one path dependency (the manifest's "deps" table entry)
typedef struct {
  char* name;
  char* path; // absolute
  bool is_project;
} cli_dep;

// one evaluated project
typedef struct {
  char* root; // absolute canonical
  char** module_roots;
  uint32_t module_roots_count;
  cli_native* natives;
  uint32_t natives_count;
  cli_dep* deps;
  uint32_t deps_count;
} cli_project;

typedef struct {
  cli_project* projects;
  uint32_t count;
  // the borrowed views handed to ok_set_projects (all pointing into the
  // owned strings above)
  ok_project* view;
  const char** view_roots;
  ok_project_native* view_natives;
  ok_project_dep* view_deps;
  // the build policy (the run path's flags)
  bool yes;
  bool no_build;
} cli_graph;

// discover + evaluate the project graph above the entry path (walking up,
// never cwd). NULL without failure = no project.ok up there (the standalone
// lane, not an error). NULL with *p_failed = the diagnostics printed.
cli_graph* cli_graph_discover(const char* p_entry_path, bool p_yes, bool p_no_build, bool* p_failed);

void cli_graph_free(cli_graph* p_graph);

// install the borrowed views + the ensure hook (the whole graph).
void cli_graph_install(ok* p_ok, const cli_graph* p_graph);

// ok build: ensure every native target in the graph, explicitly (no
// consent prompts: the command itself is the consent). prints one line per
// target. returns 0 on success, 1 on any failure (diagnostics printed).
int cli_build_all(const cli_graph* p_graph, bool p_force, bool p_clean);

#endif // OK_CLI_PROJECT_H
