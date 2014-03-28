
/*
 * DPATH.C	- Return the path to specific diablo files.  Typically used
 *		  by shell scripts.
 *
 * dpath [-C config] [-v] [-q] [-c] idname
 *
 *	(see top of diablo.config for id names)
 */

#include "defs.h"

void PrintDiabloPath(const char *ptr);

int VerboseOpt = 0;
int QuietOpt = 0;
int CshStyleOpt = 0;

int
main(int ac, char **av)
{
    int i;
    int loop;
    int pcount = 0;

    LoadDiabloConfig(ac, av);

    for (loop = 0; loop < 2; ++loop) {
	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-') {
		if (loop == 1) {
		    PrintDiabloPath(ptr);
		    ++pcount;
		}
		continue;
	    }
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'C':
		/*
		 * -C configfile	(handled by LoadDiabloConfig)
		 */
		if (*ptr == 0)
		    ++i;
		break;
	    case 'c':
		CshStyleOpt = 1;
		VerboseOpt = 1;
		break;
	    case 'q':
		QuietOpt = 1;
		break;
	    case 's':
		CshStyleOpt = 0;
		VerboseOpt = 1;
		break;
	    case 'V':
		PrintVersion();
		break;
	    case 'v':
		VerboseOpt = 1;
		break;
	    default:
		fprintf(stderr, "Bad option: %s\n", ptr - 2);
		exit(1);
	    }
	}
    }
    if (pcount == 0) 
	PrintDiabloPath(NULL);
    return(0);
}

typedef const char *ccptr;

typedef struct PathList {
    const char *id;
    const char **pglob;
    const char *(*func)(const char *pat);
} PathList;

PathList PList[] = {
    { "home",	&NewsHome, NULL },
    { "spool",	&SpoolHomePat, PatExpand },
    { "dqueue",	&DQueueHomePat, PatExpand },
    { "group",	&GroupHomePat, PatExpand },
    { "cache",	&CacheHomePat, PatExpand },
    { "feeds",	&FeedsHomePat, PatExpand },
    { "log",	&LogHomePat, PatExpand },
    { "lib",	&LibHomePat, PatExpand },
    { "db",	&DbHomePat, PatExpand },
    { "run",	&RunHomePat, PatExpand },
    { "diablo_socket",	&DiabloSocketPat, PatRunExpand },
    { "dreader_socket",	&DReaderSocketPat, PatRunExpand },
    { "feednotify",	&DFeedNotifySocketPat, PatRunExpand },


    { "dexpire",	&DExpireCtlPat, PatLibExpand },
    { "dcontrol",	&DControlCtlPat, PatLibExpand },
    { "diablo_hosts",	&DiabloHostsPat, PatLibExpand },
    { "dserver_hosts",	&DServerHostsPat, PatLibExpand },
    { "moderators",	&ModeratorsPat, PatLibExpand },
    { "dnewsfeeds",	&DNewsfeedsPat, PatLibExpand },
    { "dnntpspool",	&DNNTPSpoolCtlPat, PatLibExpand },
    { "distrib_pats",	&DistribDotPatsPat, PatLibExpand },
    { "distributions",	&DistributionsPat, PatLibExpand },

    { "server_dactive",	&ServerDActivePat, PatDbExpand },
    { "reader_dactive",	&ReaderDActivePat, PatDbExpand },
    { "dhistory",	&DHistoryPat, PatDbExpand },
    { "spam.body.cache",	&SpamBodyCachePat, PatDbExpand },
    { "spam.nph.cache",	&SpamNphCachePat, PatDbExpand },
    { "pcommit_cache",	&PCommitCachePat, PatDbExpand }
};

void
PrintDiabloPath(const char *ptr) 
{
    int i;
    int c = 0;

    for (i = 0; i < arysize(PList); ++i) {
	PathList *pl = &PList[i];

	if (ptr == NULL || strcmp(ptr, pl->id) == 0) {
	    if (VerboseOpt) {
		switch(CshStyleOpt) {
		case 0:
		    printf("%s=", pl->id);
		    break;
		case 1:
		    printf("set %s = \"", pl->id);
		    break;
		}
	    }

	    if (pl->func)
		printf("%s", pl->func(*pl->pglob));
	    else
		printf("%s", *pl->pglob);

	    if (CshStyleOpt == 1)
		printf("\"");
	    printf("\n");
	    ++c;
	}
    }
    if (c == 0) {
	if (QuietOpt == 0)
	    fprintf(stderr, "dpath: illegal path id '%s'\n", ptr);
    }
}

