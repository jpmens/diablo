
/*
 * DSPAMINFO.C	Dump the spam cache (for debugging)
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

void
Usage(char *progname)
{
    printf("Usage: %s [-B] [-C file] [-c n] [-D[n]] [-l] [-N] [-r] [-V]\n", progname);
    printf("    where:\n");
    printf("\t-B\tenable working on body filter\n");
    printf("\t-C file\tset different diablo.config location\n");
    printf("\t-c n\tclear a specific table entry (-1 clears all)\n");
    printf("\t-D[n]\tenable and set debug level\n");
    printf("\t-l\tdump contents of spam cache\n");
    printf("\t-N\tenable working on nph filter\n");
    printf("\t-r\talso show expired entries\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    int dumpCache = 0;
    char *ptr;
    int bodyCache = 1;
    int postHostCache = 1;
    int clearEntry = 0;
    int clearVal = -1;
    int raw = 0;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	ptr = av[i];
	if (*ptr != '-')
	    Usage(av[0]);
	ptr += 2;
	switch(ptr[-1]) {
	case 'B':
	    bodyCache = 1;
	    break;
	case 'C':           /* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'c':
	    if (*ptr == 0)
		ptr = av[++i];
	    if (ptr == NULL)
		Usage(av[0]);
	    clearEntry = 1;
	    clearVal = strtol(ptr, NULL, 0);
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'l':
	    dumpCache = 1;
	    break;
	case 'N':
	    postHostCache = 1;
	    break;
	case 'r':
	    raw = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    Usage(av[0]);
	}
    }
    if (!dumpCache && !clearEntry)
	Usage(av[0]);

    if (dumpCache) {
	if (bodyCache == 0 && postHostCache == 0)
	    SetSpamFilterTrip(1, 1);
	else
	    SetSpamFilterTrip(bodyCache, postHostCache);
	InitSpamFilter();
	DumpSpamFilterCache(stdout, raw);
    }
    if (clearEntry) {
	if (!bodyCache && !postHostCache) {
	    fprintf(stderr, "Must specify which cache to clear (-B or -N)\n");
	    Usage(av[0]);
	}
	SetSpamFilterTrip(bodyCache, postHostCache);
	InitSpamFilter();
	if (bodyCache)
	    ClearSpamFilterEntry(1, clearVal);
	if (postHostCache)
	    ClearSpamFilterEntry(2, clearVal);
	TermSpamFilter();
    }
    return(0);
}

