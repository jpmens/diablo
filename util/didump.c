
/*
 * DIDUMP.C	Dump or trace a dhistory file
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

/*
 * OldHistory - versions <= 1.07
 *
 */

typedef struct OldHistory {
    uint32	next;   /* next link            */
    uint32	gmt;    /* gmt time in minutes  */
    hash_t      hv;     /* hash value           */
    uint16	iter;   /* file id              */
    uint16	exp;    /* hours relative to gmt minutes */
} OldHistory;

int TraceMode = 0;
int DNPOpt = 0;
int FOpt = 0;
int FCount = 0;
int OldOpt = 0;
int StartOff = 0;
int LineModeOpt = 0;
int VerboseOpt = 0;
int QuietOpt = 0;
int VerifyHistory = 0;
int NextOpt = 0;
int EntriesOpt = 0;
int EmptyOpt = 0;
int ShowProgress = 0;
int DumpHashOnly = 0;
char *FindHash = NULL;
time_t MaxAge = -1;
int HistoryVersion = 0;
uint32 HOffset = 0;

uint32 ExpireDropCount = 0;
uint32 ExpireKeepCount = 0;
uint32 MaxAgeCount = 0;
uint32 ZeroGmtCount = 0;
uint32 LookupCount = 0;
uint32 DumpedCount = 0;

void DumpTrace(int fd, int hsize, int rsize);
void DumpQuick(int fd, int hsize, int rsize);
void DumpChain(int fd, int hsize, int rsize, hash_t *hv);

void
Usage(void)
{
    printf("Dump the history file entries to stdout.\n\n");
    printf("didump [-e] [-f] [-H msgid|hash] [-h] [-l] [-n] [-o] [-p] [-r remember] [-t]\n");
    printf("\t[-TN] [-v] [-x] [-C diablo.config] [-d[n]] [-V] [@offset] dhistory-file\n");
    printf("  where:\n");
    printf("\tdefault\t- quick dump\n");
    printf("\t-e\t- also dump an ENTRIES line - useful for diload progress\n");
    printf("\t-f\t- 'tail -f' the history file\n");
    printf("\t-H\t- dump hash trace for specified Message-ID or hash\n");
    printf("\t-h\t- display entries not found with a HistoryLookup\n");
    printf("\t-l\t- line mode - flush output after every line\n");
    printf("\t-m\t- dump msgid hashes only\n");
    printf("\t-n\t- also dump the value of the 'next' pointer\n");
    printf("\t-o\t- dump old-style (diablo V <= 1.07) history file\n");
    printf("\t-p\t- show progress on stderr\n");
    printf("\t-q\t- quiet - don't show stats\n");
    printf("\t-rN\t- set rememberdays\n");
    printf("\t-t\t- hash table trace (slow)\n");
    printf("\t-TN\t- don't dump articles older than N seconds\n");
    printf("\t-v\t- Include additional (synthesized) info\n");
    printf("\t-x\t- do not dump records older than rememberdays old\n");
    printf("\t-z\t- include entries with a gmt of zero in the dump\n");
    printf("\t-Cfile\t- specify diablo.config to use\n");
    printf("\t-d[n]\t- set debug [with optional level]\n");
    printf("\t-V\t- print version and exit\n");
    printf("\t@\t- specify the starting offset (in bytes)\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int fd;
    int i;
    int hsize = 1024 * 1024;
    int rsize = sizeof(History);
    struct stat st;
    char *fileName = NULL;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    if (*ptr == '@') {
		StartOff = strtol(ptr + 1, NULL, 0);
		continue;
	    }
	    if (fileName) {
		fprintf(stderr, "unexpected argument\n");
		exit(1);
	    }
	    fileName = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'e':
	    EntriesOpt = 1;
	    break;
	case 'f':
	    FOpt = 1;
	    if (*ptr)
		FCount = strtol(ptr, NULL, 0);
	    break;
	case 'H':
	    FindHash = (*ptr ? ptr : av[++i]);
	    break;
	case 'h':
	    VerifyHistory = 1;
	    break;
	case 'l':
	    LineModeOpt = 1;
	    break;
	case 'm':
	    DumpHashOnly = 1;
	    break;
	case 'n':
	    NextOpt = 1;
	    break;
	case 'o':
	    OldOpt = 1;
	    break;
	case 'p':
	    ShowProgress = 1;
	    break;
	case 'q':
	    QuietOpt = 1;
	    break;
	case 'r':
	    if (!*ptr)
		ptr = av[++i];
	    DOpts.RememberSecs = TimeSpec(ptr, "d");
	    if (DOpts.RememberSecs == -1)
		Usage();
	    break;
	case 'T':
	    MaxAge = btimetol(*ptr ? ptr : av[++i]);
	    break;
	case 't':
	    TraceMode = 1;
	    break;
	case 'v':
	    if (*ptr)
		VerboseOpt = strtol(ptr, NULL, 0);
	    else
		++VerboseOpt;
	    break;
	case 'x':
	    DNPOpt = 1;
	    break;
	case 'z':
	    EmptyOpt = 1;
	    break;
	/* Common options */
	case 'C':		/* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = 1;
	    if (*ptr)
		DebugOpt = strtol(ptr, NULL, 0);
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    if (isdigit((int)ptr[-1])) {
		FCount = strtol(ptr - 1, NULL, 0);
	    } else {
		fprintf(stderr, "illegal option: %s\n", ptr - 2);
		Usage();
	    }
	}
    }

    if (fileName == NULL) {
	Usage();
    }

    if (VerifyHistory || FindHash != NULL)
	HistoryOpen(fileName, HGF_READONLY);

    if (VerboseOpt)
	LoadSpoolCtl(0, 1);


    if (OldOpt) {
	char *paramName = malloc(strlen(fileName) + 32);
	FILE *fi;

	sprintf(paramName, "%s.param", fileName);

	rsize = sizeof(OldHistory);

	if ((fi = fopen(paramName, "r")) != NULL) {
	    char buf[256];

	    hsize = 0;

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		if (buf[0] == 'H')
		    hsize = strtol(buf + 1, NULL, 0);
	    }
	    fclose(fi);

	    if (hsize == 0) {
		fprintf(stderr, "dhistory parameter file error\n");
		exit(1);
	    }
	}
    }

    if (DNPOpt && TraceMode) {
	fprintf(stderr, "-x only works for quick dumps\n");
	exit(1);
    }

    if ((fd = open(fileName, O_RDONLY)) >= 0 && fstat(fd, &st) == 0) {
	/*
	 * new style history file has a header
	 */

	if (OldOpt == 0) {
	    HistHead hh;

	    if (read(fd, &hh, sizeof(hh)) != sizeof(hh)) {
		fprintf(stderr, "corrupted history file\n");
		exit(1);
	    }
	    if (hh.hmagic != HMAGIC) {
		fprintf(stderr, "corrupted history file\n");
		exit(1);
	    }
	    if (hh.version > HVERSION) {
		fprintf(stderr, "WARNING! Version mismatch file V%d, expecting V%d\n", hh.version, HVERSION);
		fprintf(stderr, "dump may be invalid\n");
	    }
	    HistoryVersion = hh.version;
	    rsize = hh.henSize;
	    hsize = hh.hashSize;
	    HOffset = hh.headSize + hsize * sizeof(HistIndex);

	    lseek(fd, hh.headSize, 0);
	}

	if (!QuietOpt)
	    fprintf(stderr, "Dumping, hash table %d entries, record size %d\n",
							hsize, rsize);

	if (TraceMode) {
	    DumpTrace(fd, hsize, rsize);
	} else if (FindHash != NULL) {
	    hash_t hv;
	    if (*FindHash == '<')
		hv = hhash(FindHash);
	    else
		sscanf(FindHash, "%x.%x", &hv.h1, &hv.h2);
	    DumpChain(fd, hsize, rsize, &hv);
	} else {
	    DumpQuick(fd, hsize, rsize);
	}
	close(fd);
    } else {
	fprintf(stderr, "open failed\n");
	exit(1);
    }
    return(0);
}

void
PrintTrace(int fd, HistIndex index, int rsize)
{
    off_t off;
    int maxChainLen = 1000;

    while (index) {
	History h = { 0 };

	if (HistoryVersion > 1)
	    off = (off_t)HOffset + (off_t)index * sizeof(History);
	else
	    off = index;
	lseek(fd, off, 0);
	if (read(fd, &h, rsize) != rsize) {
	    fprintf(stderr, "read error @ %d (%lld)", index, (long long)off);
	    break;
	} else {
	    printf(" [%u,%u %08x.%08x.%04x gm=%d ex=%d boff=%d bsize=%d F=%s part=%d]",
		index, (unsigned int)h.next,
		h.hv.h1,
		h.hv.h2,
		(int)h.iter,
		(int)h.gmt,
		(int)h.exp,
		(int)h.boffset,
		(int)h.bsize,
		((h.exp & EXPF_HEADONLY) ? "H" : ""),
		(int)H_SPOOL(h.exp)
	    );
	}
	if (VerboseOpt) {
	    char buf[1024];
	    printf("\n");
	    ArticleFileName(buf, sizeof(buf), &h, ARTFILE_FILE);
	    printf("\tfile=\"%s\"", buf);
	    {
		struct tm *tp;
		time_t t;
		char tbuf[64];

		t = h.gmt * 60;   
		tp = localtime(&t);
		strftime(tbuf, sizeof(tbuf), "%d-%b-%Y %H:%M:%S", tp);
		printf("\ttime=%s\n", tbuf);
	    }
	}
	index = h.next;
	if (--maxChainLen == 0) {
	    printf(" MAXIMUM CHAIN LENGTH EXCEEDED!");
	    break;
	}
    }
    if (index)
	printf(" offset error: %d", index);
    printf("\n");
}

void
DumpTrace(int fd, int hsize, int rsize)
{
    int i;
    HistIndex *Ary = calloc(hsize, sizeof(HistIndex));

    if (read(fd, Ary, hsize * sizeof(HistIndex)) != hsize * sizeof(HistIndex)) {
	fprintf(stderr, "Unable to read hash table array\n");
	exit(1);
    }

    for (i = 0; i < hsize; ++i) {
	if (Ary[i] != 0) {
	    printf("Index %d: ", i);
	    PrintTrace(fd, Ary[i], rsize);
	}
    }
}

void
DumpChain(int fd, int hsize, int rsize, hash_t *hv)
{
    uint32 *Ary = calloc(hsize, sizeof(uint32));
    uint32 off;

    if (read(fd, Ary, hsize * sizeof(uint32)) != hsize * sizeof(uint32)) {
	fprintf(stderr, "Unable to read hash table array\n");
	exit(1);
    }

    off = Ary[(hv->h1 ^ hv->h2) & (hsize - 1)];
    PrintTrace(fd, off, rsize);
}

void
DumpQuick(int fd, int hsize, int rsize)
{
    char *hbuf;
    int n;
    int hlen;
    int rememberMins = DOpts.RememberSecs / 60;
    uint32 gmt = time(NULL) / 60;
    History th;
    off_t seekpos = 0;
    uint32 totalentries = 0;
    uint32 count = 0;
    int i;
    History *h;


    hlen = rsize * 4096;;
    if ((hbuf = (char *)malloc(hlen)) == NULL) {
	fprintf(stderr, "ERROR: Unable to malloc %dB for history cache (%s)\n",
				hlen, strerror(errno));
	return;
    }
    if (StartOff)
	seekpos = lseek(fd, StartOff, 0);
    else if (FOpt || FCount)
	seekpos = lseek(fd, -FCount * rsize, 2);
    else
	if (HistoryVersion > 1)
	    seekpos = lseek(fd, (off_t)hsize * sizeof(HistIndex) + (off_t)rsize, 1);
	else
	    seekpos = lseek(fd, (off_t)hsize * sizeof(HistIndex), 1);

    {
	struct stat st;

	if (fstat(fd, &st) == -1) {
	    fprintf(stderr, "Unable to fstat history: %s\n", strerror(errno));
	    exit(1);
	}
	totalentries = (int)(((double)st.st_size - (double)seekpos) / (double)rsize);
	if (EntriesOpt)
	    printf("ENTRIES %u\n", totalentries);
    }

    if (!QuietOpt)
	fprintf(stderr, "@%lld  %u records (%d bytes per record)\n",
				(long long)lseek(fd, 0L, 1),
				totalentries, rsize);

top:
    while ((n = read(fd, hbuf, hlen)) > 0) {

	n /= rsize;
	gmt = time(NULL) / 60;

	for (i = 0; i < n; ++i) {
	    h = (History *)(hbuf + i * rsize);


	    if (ShowProgress && (count++ % 32768) == 0)
		fprintf(stderr, "%u/%u  (%d%%)    \r", count, totalentries,
			(int)((double)count * 100.0 / (double)totalentries));

	    /*
	     * A gmt of zero is an empty entry - don't print it out unless
	     * we are verifying history
	     */
	    if (h->gmt == 0 && !VerifyHistory && !EmptyOpt) {
		ZeroGmtCount++;
		continue;
	    }

	    /*
	     * If we specified a maximum age, then deal with it
	     */
	    if (MaxAge != -1) {
		int32 dgmt = (gmt - h->gmt) * 60;	/* Delta seconds */
		if (dgmt > MaxAge) {
		    MaxAgeCount++;
		    continue;
		}
	    }

	    if (DNPOpt && H_EXPIRED(h->exp)) {
		int32 dgmt = gmt - h->gmt;	/* DELTA MINUTES */
		if (dgmt < -rememberMins || dgmt > rememberMins) {
		    ExpireDropCount++;
		    continue;
		} else {
		    ExpireKeepCount++;
		}
	    }

	    if (VerifyHistory && HistoryLookupByHash(h->hv, &th) == 0) {
		LookupCount++;
		continue;
	    }

	    if (DumpHashOnly) {
		printf("%08x.%08x\n", h->hv.h1, h->hv.h2);
		continue;
	    }
	    printf("DUMP %08x.%08x.%04x gm=%d ex=%-2d",
		h->hv.h1,
		h->hv.h2,
		(int)h->iter,
		(int)h->gmt,
		(int)h->exp
	    );
	    if (!OldOpt) {
		printf(" boff=%-7d bsize=%-6d", (int)h->boffset, (int)h->bsize);
		if (VerboseOpt) {
		    char buf[1024];
		    ArticleFileName(buf, sizeof(buf), h, ARTFILE_FILE);
		    printf(" file=\"%s\"", buf);
		}
		printf(" flags=%s",
		    ((h->exp & EXPF_HEADONLY) ? "H" : "")
		);
		if (NextOpt)
		    printf(" next=%d", h->next);
	    }
	    printf("\n");
	    DumpedCount++;
	    if (LineModeOpt)
		fflush(stdout);
	}
    }
    if (n < 0)
	fprintf(stderr, "Error reading file: %s\n", strerror(errno));

    if (FOpt) {
	usleep(100000);
	goto top;
    }
    if (!QuietOpt && count > 0)
	fprintf(stderr, "%u/%u  (%d%%)    \n", count, totalentries,
			(int)((double)count * 100.0 / (double)totalentries));
    if (!QuietOpt)
	fprintf(stderr, "%u entries dumped\n", DumpedCount);
    if (VerifyHistory) {
	fprintf(stderr, "%u entries found\n", LookupCount);
    } else if (!QuietOpt) {
	fprintf(stderr, "%u expired entries kept\n", ExpireKeepCount);
	fprintf(stderr, "%u expired entries dropped\n", ExpireDropCount);
	fprintf(stderr, "%u dropped as beyond max age\n", MaxAgeCount);
	fprintf(stderr, "%u dropped with gmt=0\n", ZeroGmtCount);
    }
    if (!QuietOpt)
	printf(".\n");
    free(hbuf);
}

