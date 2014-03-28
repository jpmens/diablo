#include "defs.h"

#define	COUNT	1000
#define	FCOUNT	1

typedef struct ForkMap {
    int id;
    int type;
    int count;
    struct timeval tstart;
    struct timeval tend;
} ForkMap;

void DoIt(int Action, int count, ForkMap *smap);

ForkMap *StatsMap;
int AddForks = 0;
int AddCount = COUNT;
int LookupForks = 0;
int LookupCount = COUNT;
char StatsPath[PATH_MAX];

void
Usage(void)
{
    fprintf(stderr, "A simple history performance tester\n\n");
    fprintf(stderr, "Usage: dhisbench [-ac n] [-af n] [-F] [-f historyfile] [-l]\n");
    fprintf(stderr, "                 [-lc n] [-lf n] [-m map_file]\n");
    fprintf(stderr, "  where:\n");
    fprintf(stderr, "\t-ac\tspecify number if history additions (default:%d)\n",
							AddCount);
    fprintf(stderr, "\t-af\tspecify number if addition forks (default: %d)\n",
							AddForks);
    fprintf(stderr, "\t-F\tfast mode - no locking or duplicate checking for add\n");
    fprintf(stderr, "\t-Fl\tdon't perform locking on history adds\n");
    fprintf(stderr, "\t-Fs\tdon't check for duplicates on history adds\n");
    fprintf(stderr, "\t-f FILE\tuse FILE has the history file (default: %s)\n",
					PatDbExpand(DHistoryPat));
    fprintf(stderr, "\t-h n\tspecify history hash size\n");
    fprintf(stderr, "\t-lc\tspecify number of history lookups (default: %d)\n",
							LookupCount);
    fprintf(stderr, "\t-lf\tspecify number of lookup forks (default: %d)\n",
							LookupForks);
    fprintf(stderr, "\t-m FILE\tuse FILE for the temp stats mmap (default: %s)\n",
					StatsPath);
    fprintf(stderr, "\nWARNING: This program writes garbage entries to the history file\n\n");
    exit(1);
}

const char *HistoryFile = NULL;
int HOFlags = 0;

int
main(int ac, char **av)
{
    int i;
    int n;
    int rpid;
    int mapfd;
    int mapindex = 0;
    struct timeval tstart;
    struct timeval tend;
    double elapsed;
    int LookupTotal = 0;
    double LookupTime = 0.0;
    int AddTotal = 0;
    double AddTime = 0.0;

    LoadDiabloConfig(ac, av);
 
    strncpy(StatsPath, PatRunExpand("%s/dhisbench.map"), sizeof(StatsPath) - 1);
    StatsPath[sizeof(StatsPath) - 1] = '\0';

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr == '-') {
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'a':
		if (*ptr == 'f') {
		    ptr++;
		    AddForks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		} else if (*ptr == 'c') {
		    ptr++;
		    AddCount = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		}
		break;
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'F':
		if (*ptr == 'l')
		    HOFlags = HGF_FAST;
		else if (*ptr == 's')
		    HOFlags = HGF_NOSEARCH;
		else
		    HOFlags = HGF_FAST|HGF_NOSEARCH;
		break;
	    case 'f':
		HistoryFile = (*ptr) ? ptr : av[++i];
		break;
	    case 'h':
		n = bsizetol((*ptr) ? ptr : av[++i]);
		if (n < 256 * 1024 || n > 128 * 1024 * 1024) {
		    fprintf(stderr, "Illegal Hash size: %d\n", n);
		    exit(1);
		}
		if ((n ^ (n - 1)) != (n << 1) - 1) {
		    fprintf(stderr, "Hash size not a power of 2: %d\n", n); 
		    exit(1);
		}
		DOpts.HashSize = n;
	    case 'l':
		if (*ptr == 'f') {
		    ptr++;
		    LookupForks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		} else if (*ptr == 'c') {
		    ptr++;
		    LookupCount = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		}
		break;
	    case 'm':
		if (!*ptr)
		    ptr = av[++i];
		if (ptr == NULL || !*ptr)
		    Usage();
		strncpy(StatsPath, ptr, sizeof(StatsPath) - 1);
		StatsPath[sizeof(StatsPath) - 1] = '\0';
		break;
	    default:
		Usage();
	    }
	}
    }

    if (AddForks == 0 && LookupForks == 0)
	Usage();
    if (AddForks == 0)
	HOFlags |= HGF_READONLY;

    if (HistoryFile == NULL)
	HistoryFile = PatDbExpand(DHistoryPat);

    printf("History File: %s\n", HistoryFile);
    printf("Flags       : ");
    if (HOFlags & HGF_READONLY)
	printf("RO ");
    if (HOFlags & HGF_FAST)
	printf("NOLOCK ");
    if (HOFlags & HGF_NOSEARCH)
	printf("NOSEARCH ");
    printf("\n");
    printf("Hash Size   : %d\n", DOpts.HashSize);

    /*
     * Create history if it doesn't exist
     */
    HistoryOpen(HistoryFile, HOFlags);
    HistoryClose();

    remove(StatsPath);
    mapfd = open(StatsPath, O_RDWR|O_CREAT|O_EXCL, 0600);
    if (mapfd == -1) {
	fprintf(stderr, "Cannot create %s: %s", StatsPath, strerror(errno));
	exit(1);
    }
    for (i = 0; i < AddForks + LookupForks; i++) {
	ForkMap tsmap = { 0 };
	write(mapfd, &tsmap, sizeof(tsmap));
    }
    StatsMap = xmap(NULL, sizeof(ForkMap) * (AddForks + LookupForks),
				PROT_READ|PROT_WRITE, MAP_SHARED, mapfd, 0);
    if (StatsMap == NULL) {
	perror("mmap");
	exit(1);
    }
    gettimeofday(&tstart, NULL);
    for (i = 0; i < AddForks; i++)
	if (fork() == 0) {
	    DoIt(2, AddCount, &StatsMap[mapindex]);
	} else {
	    mapindex++;
	}
    for (i = 0; i < LookupForks; i++)
	if (fork() == 0) {
	    DoIt(1, LookupCount, &StatsMap[mapindex]);
	} else {
	    mapindex++;
	}
    i = AddForks + LookupForks;
    while (i > 0)
	if (waitpid(-1, &rpid, 0) != -1)
	    i--;
    gettimeofday(&tend, NULL);

    for (i = 0; i < mapindex; i++) {
	ForkMap *smap = &StatsMap[i];
	elapsed = (smap->tend.tv_sec + smap->tend.tv_usec / 1000000.0) -
		(smap->tstart.tv_sec + smap->tstart.tv_usec / 1000000.0);
	if (smap->type == 1) {
	    printf("L ");
	    LookupTotal += smap->count;
	    if (elapsed > LookupTime)
		LookupTime = elapsed;
	} else {
	    printf("A ");
	    AddTotal += smap->count;
	    if (elapsed > AddTime)
		AddTime = elapsed;
	}
	printf("%10d %10.3f %10.0f\n", smap->count, elapsed, smap->count / elapsed);
    }
    elapsed = (tend.tv_sec + tend.tv_usec / 1000000.0) -
			(tstart.tv_sec + tstart.tv_usec / 1000000.0);
    printf("Lookups: %d\n", LookupTotal);
    printf("   Adds: %d\n", AddTotal);
    printf("Lookup Time: %.3f seconds\n", LookupTime);
    printf("   Add Time: %.3f seconds\n", AddTime);
    printf("Total  Time: %.3f seconds\n", elapsed);
    if (LookupTime == 0)
	LookupTime = 1;
    if (AddTime == 0)
	AddTime = 1;
    printf("%.0f lookups per second\n", LookupTotal / LookupTime);
    printf("%.0f adds per second\n", AddTotal / AddTime);
    close(mapfd);
    remove(StatsPath);
    exit(0);
}

void
DoIt(int Action, int count, ForkMap *smap)
{
    int i;
    char msgid[512];
    hash_t hv;
    History h = { 0 };

    HistoryOpen(HistoryFile, HOFlags);

    printf("pid=%d  action=%d count=%d\n", getpid(), Action, count);

    srandom(time(NULL) + getpid());

    smap->type = Action;
    gettimeofday(&smap->tstart, NULL);
    for (i = 0; i < count; i++) {
	sprintf(msgid,"<%d%08lx$%08lx@%08lx.%08lx.%08lx>", i,
			random(), random(), random(), random(), random());
	hv = hhash(msgid);
	if (Action == 1) {
	    HistoryLookupByHash(hv, NULL);
	} else if (Action == 2) {
	    h.hv = hv;
	    h.gmt = 1;
	    HistoryAdd((char *)msgid, &h);
	}
	smap->count++;
    }
    gettimeofday(&smap->tend, NULL);
    exit(0);
}

