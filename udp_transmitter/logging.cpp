//
// Created by robert on 11/30/18.
//

#include "logging.h"
#include <cstdarg>
#include <pthread.h>
#include <cstdio>

static pthread_mutex_t mutexLog = PTHREAD_MUTEX_INITIALIZER;

void log(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);

    pthread_mutex_lock(&mutexLog);
    vfprintf(stdout, format, argptr);
    fwrite("\n", 1, 1, stdout);
    fflush(stdout);
    pthread_mutex_unlock(&mutexLog);

    va_end(argptr);
}
