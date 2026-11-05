#ifndef OK_POSIX_H
#define OK_POSIX_H

// the one place libok opts into POSIX (the P4 audit's ask: "all host stuff
// are behind abstractions"). libok compiles c99-strict; every translation
// unit that needs a POSIX surface includes this FIRST, so the opt-in is
// deliberate, per-layer, and visible in one file. anything not included
// here stays ISO C by construction - the audit greps for that.
//
// 200809L covers the whole list the layers use: stat, opendir/readdir,
// unlink/rmdir/mkdir/getcwd, environ, setenv/unsetenv, clock_gettime,
// realpath, dlopen. the CLI keeps its own lane (its ruling: strictly
// POSIX is acceptable there) and does not include this file.
#define _POSIX_C_SOURCE 200809L

#endif // OK_POSIX_H
