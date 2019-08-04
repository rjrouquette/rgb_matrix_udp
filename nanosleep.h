#include <time.h>

#define nanosleep(ts, rem) clock_nanosleep(CLOCK_MONOTONIC_RAW, 0, ts, rem)
