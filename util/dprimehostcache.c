
/*
 * UTIL/DPRIMETHOSTCACHE.C
 *
 * Prime the host cache file from diablo.hosts
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

void
Usage(void)
{
    fprintf(stderr, "Usage: dprimehostcache -dump|-prime [ hostcache [ diablo.hosts ] ] \n");
    fprintf(stderr, "\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "  -dump          Dump the contents of the incoming host cache\n");
    fprintf(stderr, "  -prime         Reload the contents of the incoming host cache\n");
    fprintf(stderr, "  hostcache      Specify a file to dump/load with host cache data\n");
    fprintf(stderr, "  diablo.hosts   Specify an incoming file to use for creating a host cache\n");
    exit(1);
}

int
main(int ac, char *av[])
{
    int dodump = 0;
    int doprime = 0;
    int arg = 1;

    OpenLog("dprimehostcache", LOG_PID|LOG_NDELAY);
    
    LoadDiabloConfig(ac, av);

    while (ac > 1) {
	if (strcasecmp(av[arg], "-V") == 0)
	    PrintVersion();
	if (strcasecmp(av[arg], "-dump") == 0) {
	    arg++;
	    ac--;
	    dodump = 1;
	    continue;
	}
	if (strcasecmp(av[arg], "-prime") == 0) {
	    arg++;
	    ac--;
	    doprime = 1;
	    continue;
	}
	if (av[arg] != NULL) {
	    DHostsCachePat = av[arg];
	    arg++;
	    ac--;
	    if (av[arg] != NULL)
		DiabloHostsPat = av[arg];
	    continue;
	}
	ac--;
    }

    if (!dodump && !doprime)
	Usage();

    if (dodump) {
	printf("Dumping cache data from : %s\n",
		PatDbExpand(DHostsCachePat));
	DumpHostCache(PatDbExpand(DHostsCachePat));
	return(0);
    }
    if (doprime) {
	printf("Loading diablo hosts cache from: %s\n",
					PatLibExpand(DiabloHostsPat));
	printf("Storing cache data into : %s\n",
					PatDbExpand(DHostsCachePat));

	LoadHostAccess(0, 1, 0);

	printf("Complete.\n");
    }
    return(0);
}

