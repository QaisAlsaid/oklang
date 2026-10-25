#ifndef OK_DEF_H
#define OK_DEF_H

#include <stdio.h>

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

#define ASSERT(x)                                                                                                      \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr, "%s:%d in %s: assertion '%s' failed.", __FILE__, __LINE__, __func__, STRINGIFY(x));              \
      fflush(stderr);                                                                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

#define ASSERT_MSG(x, msg)                                                                                             \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr, "%s:%d in %s: assertion '%s' failed. %s", __FILE__, __LINE__, __func__, STRINGIFY(x), msg);      \
      fflush(stderr);                                                                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

#define CRASH(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr,                                                                                                  \
              "%s:%d in %s: (YOU ENCOUNTERED  A BUG). the assertion '%s' failed.",                                     \
              __FILE__,                                                                                                \
              __LINE__,                                                                                                \
              __func__,                                                                                                \
              STRINGIFY(x));                                                                                           \
      fflush(stderr);                                                                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

#define CRASH_MSG(x, msg)                                                                                              \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr,                                                                                                  \
              "%s:%d in %s: (YOU ENCOUNTERED A BUG). '%s' failed. %s",                                                 \
              __FILE__,                                                                                                \
              __LINE__,                                                                                                \
              __func__,                                                                                                \
              STRINGIFY(x),                                                                                            \
              msg);                                                                                                    \
      fflush(stderr);                                                                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

#endif // OK_DEF_H
