
/*
 * GROUPFIND.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int GroupFind(const char *g, struct GroupList *gl);

/*
 * GroupFind() - Find a specified group in a wildcard list of groups
 *
 */

int
GroupFind(const char *g, struct GroupList *gl)
{
    int found = 0;
    char *p;

    for (; gl != NULL; gl = gl->next) {
	p = gl->group;
	if (*p == '!')
	    p++;
	if (WildCmp(p, g) == 0) {
	    if (*gl->group == '!')
		found = 0;
	    else
		found = 1;
	}
	/* logit(LOG_INFO, "Matched %s to %s = %d", g, gl->group, found); */
    }
    return(found);
}


