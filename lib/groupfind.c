
/*
 * GROUPFIND.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int GroupFindWild(const char *g, struct GroupList *gl);
Prototype int WildGroupFind(const char *g, struct GroupList *gl);
Prototype void DumpGroupList(struct GroupList *gl);

/*
 * GroupFindWild() - Find a specified group in a wildcard list of groups
 *
 *	Returns: 1 = Found
 *		 0 = Not Found
 */

int
GroupFindWild(const char *g, struct GroupList *gl)
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
    }
    return(found);
}

/*
 * WildGroupFind() - Match a wildcard group to a list of groups
 *
 * Returns:	0 = no match
 *		1 = match
 *		2 = match negative (!)
 */

int
WildGroupFind(const char *g, struct GroupList *gl)
{
    static char *gn;

    if (*g == '!')
	return(0);
    for (; gl != NULL; gl = gl->next) {
	gn = gl->group;
	if (*gn == '!')
	    gn++;
	if (WildCmp(g, gn) == 0) {
	    if (*gl->group == '!')
		return(2);
	    else
		return(1);
	}
    }
    return(0);
}

/*
 * DumpGroupList() - dump the contents of a GroupList structure
 *	Useful for debugging
 */
void
DumpGroupList(struct GroupList *gl)
{
    for (; gl != NULL; gl = gl->next)
	printf("GL:%s\n", gl->group);

}

