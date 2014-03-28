#include "defs.h"

#if NEED_TERMIOS
#include <sys/termios.h>
#endif

typedef struct Feed {
    struct Feed *fe_Next;
    char        *fe_Label;
    int         fe_Fd;
    char        *fe_Buf;
    int         fe_BufIdx;
    int         fe_BufMax;
    int         fe_Failed;
} Feed;

Feed    *FeBase;
MemPool *ParProcMemPool;
char    *MySpoolHome;

int fwCallBack(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize);
void writeFeedOut(const char *label, const char *file, const char *msgid, const char *offSize, int rt, int headOnly, const char *cSize);
void flushFeeds(int justClose);
void flushFeedOut(Feed *fe);

void
Usage(void)
{
    fprintf(stderr, "Requeue articles for a feed or all feeds from DUMP lines (usually written\n");
    fprintf(stderr, "from diloadfromspool) on stdin.\n\n");
    fprintf(stderr, "Usage: drequeue [-a] [-C file] [-d[n]] [-f file] [-s] [-V] [label]\n");
    fprintf(stderr, "  where:\n");
    fprintf(stderr, "\t-a\t- requeue for all labels in dnewsfeeds\n");
    fprintf(stderr, "\t-C file\t- specify diablo.config to use\n");
    fprintf(stderr, "\t-d[n]\t- set debug [with optional level]\n");
    fprintf(stderr, "\t-f file\t- specify the dnewsfeeds filename\n");
    fprintf(stderr, "\t-s\t- print each line to stdout and to labels\n");
    fprintf(stderr, "\t-V\t- print version and exit\n");
    fprintf(stderr, "\tlabel\t- only requeue entries for specified dnewsfeeds label\n");
    exit(1);
}

int
main(int ac, char **av)
{
    char buf[8192];
    char *s1;
    char *hlabel = NULL;
    int i;
    int sendStdout = 0;
    int doAll = 0;

    LoadDiabloConfig(ac, av);

     /*
     * Extract the spool home pattern into a real path and place
     * in MySpoolHome
     */

    {
        int l = strlen(PatExpand(SpoolHomePat)) + 1;
        MySpoolHome = malloc(l);
        snprintf(MySpoolHome, l, PatExpand(SpoolHomePat));
    }

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	
	if (*ptr != '-') {
	    hlabel = ptr;
	} else {
	    ptr++;
	    switch(*ptr++) {
		case 'a':
		    doAll = 1;
		    break;
		case 'C':
		    break;
		case 'd':
		    DebugOpt = atoi(*ptr ? ptr : av[++i]);
		    break;
		case 'f':
		    DNewsfeedsPat = *ptr ? ptr : av[++i];
		    break;
		case 's':
		    sendStdout = 1;
		    break;
		case 'V':
		    PrintVersion();
		    break;
		default:
		    Usage();
	    }
	}
    }

    if (hlabel == NULL && doAll == 0)
	Usage();

    if (hlabel != NULL)
	printf("Requeueing for label: %s\n", hlabel);

    LoadNewsFeed(0, 1, hlabel);

    while (!feof(stdin)) {
	fgets(buf, sizeof(buf), stdin);
        s1 = strtok(buf, " \t\n");
        if (s1 == NULL)
            continue;

        if (strncmp(s1, "SOUT", 4) == 0) {
            char *path = strtok(NULL, "\t\n");
            char *offsize = strtok(NULL, "\t\n");
            const char *msgid = MsgId(strtok(NULL, "\t\n"), NULL);
            char *nglist = strtok(NULL, "\t\n");
            char *dist = strtok(NULL, "\t\n");
            char *npath = strtok(NULL, "\t\n");
            char *headOnly = strtok(NULL, "\t\n");
            const char *artType = strtok(NULL, "\t\n");
            const char *cSize = strtok(NULL, "\t\n");
            if (path && offsize && msgid && nglist) {
                int spamArt = 0;
		if (npath == NULL) npath = "1";
		if (headOnly == NULL) headOnly = "0";
		if (artType == NULL) artType = "000000";
		if (cSize != NULL && strcmp(cSize, "0") == 0) cSize = NULL;
		if (sendStdout)
		    printf(
			"SOUTLINE: %s %s %s %s %s HO=%s AT=%s %s\n",
			path, 
			offsize,
			msgid,
			nglist,
			npath, 
			headOnly,
			artType,
			cSize != NULL ? cSize : ""
		    );

    		FeedWrite(0, fwCallBack, msgid, path, offsize, nglist, npath, dist, headOnly, artType, spamArt, cSize);
	    }
	}
    }
    flushFeeds(0);
    exit(0);
}

int 
fwCallBack(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize)
{
    writeFeedOut(hlabel, path, msgid, offsize, ((plfo > 0) ? 1 : 0), headOnly, cSize);
    return(0);
}

void
writeFeedOut(const char *label, const char *file, const char *msgid, const char *offSize, int rt, int headOnly, const char *cSize)
{
    Feed *fe;

    /*
     * locate feed
     */

    for (fe = FeBase; fe; fe = fe->fe_Next) {
	if (strcmp(label, fe->fe_Label) == 0)
	    break;
    }

    /*
     * allocate feed if not found
     */

    if (fe == NULL) {
	fe = zalloc(&ParProcMemPool, sizeof(Feed) + strlen(label) + 1);
	fe->fe_Label = (char *)(fe + 1);
	strcpy(fe->fe_Label, label);
	fe->fe_Fd = xopen(O_APPEND|O_RDWR|O_CREAT, 0644, "%s/%s", PatExpand(DQueueHomePat), label);
	if (fe->fe_Fd >= 0) {
	    int bsize;

	    fe->fe_Buf = pagealloc(&bsize, 1);
	    fe->fe_BufMax = bsize;
	    fe->fe_BufIdx = 0;
	    fe->fe_Failed = 0;
	}
	fe->fe_Next = FeBase;
	FeBase = fe;
    }

    /*
     * write to buffered feed, flushing buffers if there is not enough
     * room.  If the line is too long, something has gone wrong and we
     * throw it away.
     *
     * note that we cannot fill the buffer to 100%, because the trailing
     * nul (which we do not write) will overrun it.  I temporarily add 4 to
     * l instead of 3 to include the trailing nul in the calculations, but
     * subtract it off after the actual copy operation.
     */

    if (fe->fe_Fd >= 0) {
	int l = strlen(file) + strlen(msgid) + strlen(offSize) + (3 + 32 + 1);

	/*
	 * line would be too long?
	 */
	if (l < fe->fe_BufMax) {
	    /*
	     * line fits in buffer with trailing nul ?
	     */
	    if (l >= fe->fe_BufMax - fe->fe_BufIdx)
		flushFeedOut(fe);

	    sprintf(fe->fe_Buf + fe->fe_BufIdx, "%s %s %s%s%s%s\n",
		file, msgid, offSize, 
		(headOnly ? " H" : ""),
		(cSize != NULL ? " C" : ""),
		(cSize != NULL ? cSize : "")
	    );
	    fe->fe_BufIdx += strlen(fe->fe_Buf + fe->fe_BufIdx);
	    if (rt)
		flushFeedOut(fe);
	}
    }
}

void
flushFeeds(int justClose)
{
    Feed *fe;

    while ((fe = FeBase) != NULL) {
	if (justClose == 0)
	    flushFeedOut(fe);
	if (fe->fe_Buf)
	    pagefree(fe->fe_Buf, 1);
	if (fe->fe_Fd >= 0)
	    close(fe->fe_Fd);
	fe->fe_Fd = -1;
	fe->fe_Buf = NULL;
	FeBase = fe->fe_Next;
	zfree(&ParProcMemPool, fe, sizeof(Feed) + strlen(fe->fe_Label) + 1);
    }
}

void
flushFeedOut(Feed *fe)
{
    if (fe->fe_BufIdx && fe->fe_Buf && fe->fe_Fd >= 0) {
	/*
	 * flush buffer.  If the write fails, we undo it to ensure
	 * that we do not get garbaged feed files.
	 */
	int n = write(fe->fe_Fd, fe->fe_Buf, fe->fe_BufIdx);
	if (n >= 0 && n != fe->fe_BufIdx) {
	    ftruncate(fe->fe_Fd, lseek(fe->fe_Fd, 0L, 1) - n);
	}
	if (n != fe->fe_BufIdx && fe->fe_Failed == 0) {
	    fe->fe_Failed = 1;
	    logit(LOG_INFO, "failure writing to feed %s", fe->fe_Label);
	}
    }
    fe->fe_BufIdx = 0;
}

