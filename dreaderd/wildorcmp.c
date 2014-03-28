
/*
 * DREADERD/WILDORCMP.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int WildOrCmp(const char *wild, const char *str);

int
WildOrCmp(const char *wild, const char *str)
{
    const char *s;
    int r = 1;

    while (r != 0 && (s = strchr(wild, '|')) != NULL) {
	char *t = zalloc(&SysMemPool, s - wild + 1);
	memcpy(t, wild, s - wild);
	t[s-wild] = 0;
	r = WildCmp(t, str);
	zfree(&SysMemPool, t, s - wild + 1);
	wild = s + 1;
    }
    if (r != 0)
	r = WildCmp(wild, str);
    return(r);
}

