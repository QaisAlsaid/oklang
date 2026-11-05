#include "ok_posix.h" // FIRST: the deliberate POSIX opt-in (see its header)

#include "ok_time.h"

#include <time.h>

void ok_time_wall(double* p_out) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    *p_out = 0.0; // unreachable on a conforming POSIX 2008 host; defensive
    return;
  }
  *p_out = (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

void ok_time_monotonic(double* p_out) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    *p_out = 0.0; // same defensive stance
    return;
  }
  *p_out = (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}
