
/*
 * DFEEDINFO.C	Dump the feed stats cache
 *
 * (c)Copyright 2000, Russell Vincent, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

void
Usage(int ac, char **av)
{
    fprintf(stderr, "Show some newsfeed statistics\n");
    fprintf(stderr, "Usage: %s [-a] [-f file] [-I] [-i] [-o] [-P[ext] ] [-r] [-S] [-s] [-t] [-z] [hostname]\n", av[0]);
    fprintf(stderr, "	-a		dump all statcs\n");
    fprintf(stderr, "	-f file		specify the file to use\n");
    fprintf(stderr, "	-I		dump detailed incoming stats\n");
    fprintf(stderr, "	-i		dump incoming stats\n");
    fprintf(stderr, "	-o		dump outgoing stats\n");
    fprintf(stderr, "	-P[ext]		create a snapshot file of the stats\n");
    fprintf(stderr, "	-r		show raw stats (no byte formatting)\n");
    fprintf(stderr, "	-S		dump detailed spool stats\n");
    fprintf(stderr, "	-s		dump spool stats\n");
    fprintf(stderr, "	-t		only print total stats\n");
    fprintf(stderr, "	-z		zero stats\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    char *hostname = NULL;
    int outstats = 0;
    int instats = 0;
    int indetstats = 0;
    int spoolstats = 0;
    int spooldetstats = 0;
    int zerostats = 0;
    int raw = 0;
    int totalonly = 0;
    int snapshot = 0;
    char *snapext = NULL;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    hostname = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'C':
	    break;
	case 'a':
	    instats = 1;
	    outstats = 1;
	    spoolstats = 1;
	    break;
	case 'f':
	    DFeedStatsPat = (*ptr ? ptr : av[++i]);
	    break;
	case 'I':
	    indetstats = 1;
	    break;
	case 'i':
	    instats = 1;
	    break;
	case 'o':
	    outstats = 1;
	    break;
	case 'P':
	    snapshot = 1;
	    if (*ptr)
		snapext = ptr;
	    break;
	case 'r':
	    raw = 1;
	    break;
	case 'S':
	    spooldetstats = 1;
	    break;
	case 's':
	    spoolstats = 1;
	    break;
	case 't':
	    totalonly = 1;
	    break;
	case 'z':
	    zerostats = 1;
	    break;
	default:
	    Usage(ac, av);
	}
    }
    if (!instats && !indetstats && !outstats &&
				!spoolstats && !spooldetstats &&
				!zerostats && !snapshot)
	Usage(ac, av);
    if (instats) {
	if (totalonly)
	    FeedStatsDump(stdout, "TOTAL", raw, FSTATS_IN);
	else
	    FeedStatsDump(stdout, hostname, raw, FSTATS_IN);
    }
    if (indetstats) {
	if (totalonly)
	    FeedStatsDump(stdout, "TOTAL", raw, FSTATS_INDETAIL);
	else
	    FeedStatsDump(stdout, hostname, raw, FSTATS_INDETAIL);
    }
    if (outstats) {
	if (totalonly)
	    FeedStatsDump(stdout, "TOTAL", raw, FSTATS_OUT);
	else
	    FeedStatsDump(stdout, hostname, raw, FSTATS_OUT);
    }
    if (spoolstats) {
	if (totalonly)
	    FeedStatsDump(stdout, "TOTAL", raw, FSTATS_SPOOL);
	else
	    FeedStatsDump(stdout, hostname, raw, FSTATS_SPOOL);
    }
    if (spooldetstats) {
	if (totalonly)
	    FeedStatsDump(stdout, "TOTAL", raw, FSTATS_SPOOLDETAIL);
	else
	    FeedStatsDump(stdout, hostname, raw, FSTATS_SPOOLDETAIL);
    }
    if (snapshot)
	FeedStatsSnapShot(stdout, hostname, snapext);
    if (zerostats && (instats || indetstats))
	FeedStatsClear(stdout, hostname, FSTATS_IN);
    if (zerostats && outstats)
	FeedStatsClear(stdout, hostname, FSTATS_OUT);
    if (zerostats && (spoolstats || spooldetstats))
	FeedStatsClear(stdout, hostname, FSTATS_SPOOL);
    return(0);
}

