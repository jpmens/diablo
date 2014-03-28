
/*
 * LIB/FATAL.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void fatal(const char *ctl, ...);

void
fatal(const char *ctl, ...)
{
    va_list va;
    char buf[1024];

    va_start(va, ctl);
    vsnprintf(buf, sizeof(buf), ctl, va);
    va_end(va);
    fprintf(stderr, "%s\n", buf);
    logit(LOG_ERR, "%s", buf);

    exit(1);
}

