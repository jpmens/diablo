
/*
 * LIB/PSSTAT.C	- command line status / shows up in ps
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void SetStatusLine(char *ptr, int len);
Prototype void vstprintf(const char *ctl, va_list va);
Prototype void stprintf(const char *ctl, ...);

char	*ArgvBuf;
int	ArgvLen;

void
SetStatusLine(char *ptr, int len)
{
    ArgvBuf = ptr;
    ArgvLen = len;
    memset(ptr, 0, len);
}

/*
 * If we have proctitle(), stprintf() is macro'd to it directly, else
 * we fudge it here.
 */

#if HAS_PROC_TITLE == 0

void
stprintf(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vstprintf(ctl, va);
    va_end(va);
}

void
vstprintf(const char *ctl, va_list va)
{
    int n;
    if (ArgvBuf) {
	n = vsnprintf(ArgvBuf, ArgvLen, ctl, va);
	if (n < ArgvLen)
	    memset(ArgvBuf + n, 0, ArgvLen - n);
    }
}

#endif

