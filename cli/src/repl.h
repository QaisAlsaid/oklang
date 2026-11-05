#ifndef OK_REPL_H
#define OK_REPL_H

// the REPL engine (P5): cli/src/repl.c. semantics only - the posix tty
// editor lives in term.c. the session's exit code lands in *p_exit_code
// (std.sys's exit request, or 0 for a clean leave).

#include <stdint.h>

struct ok;

void repl_run(struct ok* p_ok, int32_t* p_exit_code);

#endif // OK_REPL_H
