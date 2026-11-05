#ifndef OK_TIME_H
#define OK_TIME_H

// ok_time: the clock door (the P4 audit). std.sys's clock() and
// monotonic() ride these, and nothing else in libok touches a clock.
// both answer seconds as doubles (nanosecond precision inside the f64 -
// the ~104 day monotonic horizon where the fraction thins is far past
// any process lifetime that matters).

// wall-clock seconds since the unix epoch. the calendar itself (local
// time, formatting) is deliberately NOT here: it is std work, and only
// when someone needs it.
void ok_time_wall(double* p_out);

// monotonic seconds since an arbitrary fixed point. never negative, never
// steps back; the interval-measurement clock.
void ok_time_monotonic(double* p_out);

#endif // OK_TIME_H
