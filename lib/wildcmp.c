
/*
 * WILDCMP.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int WildCmp(const char *w, const char *s);
Prototype int WildCaseCmp(const char *w, const char *s);

/*
 * WildCmp() - compare wild string to sane string
 *
 */

int
WildCmp(const char *w, const char *s)
{
    /*
     * skip fixed portion
     */
  
    for (;;) {
	switch(*w) {
	case '*':
	    if (w[1] == 0)	/* optimize wild* case */
		return(0);
	    {
		int i;
		int l = strlen(s);

		for (i = 0; i <= l; ++i) {
		    if (WildCmp(w + 1, s + i) == 0)
			return(0);
		}
	    }
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s)
		return(-1);
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}


/*
 * WildCaseCmp() - compare wild string to sane string, case insensitive
 */

int
WildCaseCmp(const char *w, const char *s)
{
    /*
     * skip fixed portion
     */
  
    for (;;) {
	switch(*w) {
	case '*':
	    if (w[1] == 0)	/* optimize wild* case */
		return(0);
	    {
		int i;
		int l = strlen(s);

		for (i = 0; i <= l; ++i) {
		    if (WildCaseCmp(w + 1, s + i) == 0)
			return(0);
		}
	    }
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s) {
		if (tolower(*w) != tolower(*s))
		    return(-1);
	    }
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}

