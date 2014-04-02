
/*
 * DNEWSLINK.C
 *
 * dnewslink -b batchfile -B srcipaddress -h host -t timeout -l log-after 
 * 	-c close-reopen-after -pipe ...other options
 *
 * This program will attempt to run one or more batch files containing
 * article references of the form:
 *
 * Relative-Path <message-id>
 * Relative-Path <message-id>
 *
 * For example:
 *
 * de/comm/chatsystems/911 <4mle9s$mef@nz12.rz.uni-karlsruhe.de>
 * alt/binaries/multimedia/d/3643 <4mme9r$plc@nntp1.best.com>
 *
 * This program also supports multi-article batch files.  Each article in the
 * multi-article batch file must be terminated with a \0 and batch file lines
 * must include and offset and byte count (non-inclusive of the \0):
 *
 * de/comm/chatsystems/911 <4mle9s$mef@nz12.rz.uni-karlsruhe.de> off,size
 * alt/binaries/multimedia/d/3643 <4mme9r$plc@nntp1.best.com> off,size
 *
 * The primary methodology of using this program is to have another
 * program, SPOOLOUT, generate and maintain numerically indexed spool files.
 * That program forks and runs this program to actually process the spool
 * files.
 *
 * This program's job is to process one or more spool files (performing locking
 * and skipping locked spool files), spool the articles in question to the
 * remote hosts, then delete the spool files as process is completed, or
 * rewrite the spool files if a failure occurs.
 *
 * (c)Copyright 1997-1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Modified 12/4/1997 to include support for compressed data streams.
 * Modifications (c) 1997, Christopher M Sedore, All Rights Reserved.
 * Modifications may be distributed freely as long as this copyright
 * notice is included without modification.
 */

#include "defs.h"
#include <sys/uio.h>

#define NDEBUG
#include <assert.h>

#define T_ACCEPTED	1
#define T_REFUSED	2
#define T_REJECTED	3
#define T_FAILED	4
#define T_STREAMING	5
#define T_FAILEDEXIT	6
#define T_DEFERIT	7

#define INF_HELP        100     /* Help text on way */
#define INF_AUTH        180     /* Authorization capabilities */
#define INF_DEBUG       199     /* Debug output */

#define MAXCLINE	8192
#define MAXFILEDES	32

#define OUR_DELAY	1
#define THEIR_DELAY	2
#define DDTIME		30
#define CACHEFLUSHTIME	300

#define OK_CANPOST      200     /* Hello; you can post */
#define OK_NOPOST       201     /* Hello; you can't post */
#define OK_SLAVE        202     /* Slave status noted */
#define OK_STREAMOK	203	/* Can-do streaming	*/
#define OK_GOODBYE      205     /* Closing connection */
#define OK_COMPRESSOK	207	/* Can-do compression */
#define OK_GROUP        211     /* Group selected */
#define OK_GROUPS       215     /* Newsgroups follow */
#define OK_ARTICLE      220     /* Article (head & body) follows */
#define OK_HEAD         221     /* Head follows */
#define OK_BODY         222     /* Body follows */
#define OK_NOTEXT       223     /* No text sent -- stat, next, last */
#define OK_NEWNEWS      230     /* New articles by message-id follow */
#define OK_NEWGROUPS    231     /* New newsgroups follow */
#define OK_XFERED       235     /* Article transferred successfully */
#define OK_STRMCHECK	238	/* check response / want article	*/
#define OK_STRMTAKE	239	/* takeit response / article received	*/
#define OK_POSTED       240     /* Article posted successfully */
#define OK_MODECMDOK	250	/* general mode command response (diablo specific) */
#define OK_AUTHSYS      280     /* Authorization system ok */
#define OK_AUTH         281     /* Authorization (user/pass) ok */

#define CONT_XFER       335     /* Continue to send article */
#define CONT_POST       340     /* Continue to post article */
#define NEED_AUTHINFO   380     /* authorization is required */
#define NEED_AUTHDATA   381     /* <type> authorization data required */

#define ERR_GOODBYE     400     /* Have to hang up for some reason */
#define ERR_NOGROUP     411     /* No such newsgroup */
#define ERR_NCING       412     /* Not currently in newsgroup */
#define ERR_NOCRNT      420     /* No current article selected */
#define ERR_NONEXT      421     /* No next article in this group */
#define ERR_NOPREV      422     /* No previous article in this group */
#define ERR_NOARTIG     423     /* No such article in this group */
#define ERR_NOART       430     /* No such article at all */
#define ERR_RESEND	431	/* please resend the article	*/
#define ERR_GOTIT       435     /* Already got that article, don't send */
#define ERR_XFERFAIL    436     /* Transfer failed */
#define ERR_XFERRJCT    437     /* ihave, Article rejected, don't resend */
#define ERR_STRMCHECK	438	/* check response / do not want article */
#define ERR_STRMTAKE	439	/* takeit response / article failed	*/
#define ERR_NOPOST      440     /* Posting not allowed */
#define ERR_POSTFAIL    441     /* Posting failed */
#define ERR_NOAUTH      480     /* authorization required for command */
#define ERR_AUTHSYS     481     /* Authorization system invalid */
#define ERR_AUTHREJ     482     /* Authorization data rejected */

#define ERR_COMMAND     500     /* Command not recognized */
#define ERR_CMDSYN      501     /* Command syntax error */
#define ERR_ACCESS      502     /* Access to server denied */
#define ERR_FAULT       503     /* Program fault, command not performed */
#define ERR_AUTHBAD     580     /* Authorization Failed */

#define ERR_UNKNOWN	990

/*
 * Streaming parameters
 */

#define STREAM_OFF	0
#define STREAM_RELOAD	1
#define STREAM_ON	2

#define STATE_EMPTY	0	/* empty slot, must be 0		*/
#define STATE_CHECK	1	/* check transmitted			*/
#define STATE_POSTED	2	/* takethis + article transmitted	*/
#define STATE_RETRY	3	/* retry after connection failure	*/

#define STREAMDRAIN	5	/* when we hit MAXSTREAM, allow it to drop */
				/* by this amount before we push again     */
#define STREAMFRAC	10	/* number of check responses before 	*/
				/* MaxStream is incremented		*/
#define MAXPENDBYTES	(1024 - (MAXSTREAM * 8)) /* maximum pending bytes*/
#define MAXDEFER	100	/* Don't defer more than this many entries
				 * This MUST be larger that 20 due to assumptions
				 * in the code. */

#define MAXREASON       100     /* maximal length of a peer response logged */

#define CMDBUFFSIZE	32768	/* Output buffer for NNTP traffic */

typedef struct Stream {
    int	st_State;		/* state	*/
    int st_DumpRCode;
    char *st_RelPath;		/* path		*/
    char *st_MsgId;		/* message id	*/
    off_t st_Off;		/* file offset	*/
    int32 st_Size;		/* file size	*/
    int32 st_CompSize;		/* compressed file size	*/
} Stream;

int connectTo(const char *hostName, const char *serviceName, int defPort);
int Transact(int cfd, const char *relPath, char *msgId, off_t off, int size, int cSize, int defers, char *stage, char *reason, char *buf, int *sentSize);
int DumpArticle(int cfd, const char *relPath, off_t off, int size, int cSize);
int writeLarge(int cfd, char *buffer, size_t size);
int StreamTransact(int cfd, const char *relPath, char *msgId, off_t off, int size, int cSize, int defers, char *stage, char *reason, char *buf, int *sentSize);
void StreamReload(int cfd);
Stream *LocateStream(const char *msgId, int state);
int RefilePendingStreams(FILE *fo);
int commandResponse(int cfd, char **rptr, const char *ctl, ...);
int commandWrite(int cfd, const void *buf, int bytes, int artdata);
void clearResponseBuf(void);
void readreset(int fd);
void dl_logit(const char *ctl, ...);
int readretry(char *buf, int size);
int readline(int fd, char *buf, int size, time_t t);
void logStats(const char *description);
int ValidMsgId(char *msgid);
void AttemptRemoveRenamedFile(struct stat *st, const char *spoolFile);
void cdinit(void);
char *cdmap(const char *path, off_t off, int *psize, int cSize, int *multiArtFile);
void cdunmap(char *ptr, int bytes, int multiArtFile, int compressed);
int extractFeedLine(const char *buf, char *relPath, char *msgId, off_t *poff, int *psize, int *pheadOnly, time_t *queuedTime, int *cSize);
void cdflush(void);
const char *dtstamp(void);
void clearCommandBuffer(void);
#ifdef NOTDEF
void copyCommandBuffer(FILE *fo);
#endif
int flushCommandBuffer(int cfd);
void flushCompressBuffer(int cfd);
void doArtLog(const char *class, char *msgid, char *message, const char *stage, const char *reason);

#ifdef CREDTIME
void credtime(int whos);
void credreset(void);
#else
#define credtime(whos)
#define credreset()
#endif

int LogAfter = 1000;
int CloseReopenAfter = 1000;
int Timeout = 600;
int DeleteDetectOpt = 0;
int WaitTime = 10;
int DelayTime = 0;
int Port = 119;
int TailOpt = 0;
int NoCheckOpt = 0;
int TxBufSize = 0;
int RxBufSize = 0;
int HeaderOnlyFeed = 0;
int KeepBytes = 0;
int GenLinesHeader = 0;
char ArtLog[100]; /* auto-initialised to "" */
FILE *Logfd = NULL;
struct stat CurSt;

Stream StreamAry[MAXSTREAM];
int MaxStream = MAXSTREAM;	/* Maximum number of streaming slots */
int NumStream = MAXSTREAM;	/* Current number of streaming slots */
int HiWaterMark =  0;
int StreamMode = 0;		/* set by stream negotiation	*/
int StreamPend = 0;		/* pending streaming requests	*/
int StreamRetry = 0;		/* retry after connection failure */
double AveragePend = 0.0;	/* sliding average of pending requests */
int TryStreaming = 1;
int BytesPend = 0;
int PipeOpt = 0;
int RealTimeOpt = 0;
char *NotifyOpt = NULL;
int ArticleStatOpt = 0;
int LogAfterCount = -1;
int CloseReopenCount = -1;
int KillFd = -1;
int WritesLosing = 0;
time_t TimeNow;			/* Very rough estimate of current time */
int TimeCounter = 100000;

/*
 * added to support compression, cmsedore@maxwell.syr.edu 12/4/97
 */


#define COMPRESS_OFF 0
#define COMPRESS_ON  1

int CompressMode = COMPRESS_OFF;
int CompressOn = 0;

#ifdef	USE_ZLIB

int32 CompressedBytes = 0;
int32 RawBytes = 0;
int32 CompressedBytesTotal = 0;
int32 RawBytesTotal = 0;
z_stream Z_Strm;
int CompressBufInPos = 0;
char CompressBufIn[8192]; 
char CompressBufOut[8192]; 
int CompressNextFlush = 0;

#endif	/* USE_ZLIB */

char *HostName = NULL;
char *OutboundIpName = NULL;
char *BatchFileCtl = NULL;
char CurrentBatchFile[1024];
char LastErrBuf[256];
int CBFIndex;
int BatchSeq = -1;
int NumBatches = 1;
int HeaderSize = 0;
int NotifyFd = -1;
int NotifyLockFd = -1;
int IPTOS=0;
time_t LastNotify = 0;
time_t LastFeedData = 0;

int TermFlag;

FeedStats	*Stats;
FeedStats	*HostStats;

int OurMs;
int TheirMs;
int MsCount;

int OurMsTotal;
int TheirMsTotal;
int MsCountTotal;
int WouldHaveRefiled;
int HasStatusLine;

void sigTerm(int sigNo);

void
Usage(char *progname)
{
    printf("Usage: %s -b batchfile -h hostname\n", progname);
    puts(
	    "-A[levels]      - turn on article logging, default all\n"
	    "                  levels is either all or comma-separated list\n"
	    "                  of accept,reject,defer,refuse,error\n"
	    "-B ip           - set source ip address for outbound connections\n"
	    "-b batchfile    - specify batchfile\n"
	    "-b template%d   - template containing %[xx]d\n"
	    "-c #            - close-reopen-after, default 1000\n"
	    "-D              - detect delete-out-from-under\n"
	    "-d[#]           - debug option\n"
	    "-H              - (header only) incl Bytes: hdr, pass body only for control msgs\n"
	    "-h host         - specify remote host\n"
	    "-i              - disable streaming & streaming check\n"
	    "-L              - generate Lines: header\n"
	    "-l #            - log-after-count, default 1000\n"
	    "-M #            - set max concurrent stream transactions\n"
	    "-N #            - number of batchfiles to process, template mode\n"
	    "-o label        - enable feed notification from diablo\n"
	    "-P #            - specify remote port (default: 119)\n"
	    "-p              - input on stdin rather then file\n"
#ifdef IP_TOS
	    "-Q              - set TOS using IP_TOS\n"
#endif
	    "-n[op]          - NOP"
	    "-P #            - specify destination tcp port\n"
	    "-R #            - set receive buffer size\n"
	    "-r              - realtime feed\n"
	    "-S #            - starting sequence #, template mode\n"
	    "-s \"<24chars>\"  - /bin/ps argv space for status\n"
	    "-T #            - set transmit buffer size\n"
	    "-t #            - specify timeout, default 600s\n"
	    "-W #            - delay sending articles from the queue time\n"
	    "-w #            - reconnect-after-failure delay\n"
    );
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    int eNoBat = 0;

    TimeNow = time(NULL);
    OpenLog("newslink", (DebugOpt > 0? LOG_PERROR: 0) | LOG_NDELAY | LOG_PID);
    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	ptr += 2;
	switch(ptr[-1]) {
	case 'A':
            StrnCpyNull(ArtLog, ((*ptr) ? ptr: "all"), sizeof(ArtLog));
            break;
	case 'B':
	    if (*ptr == 0)
		ptr = av[++i];
	    OutboundIpName = strdup(SanitiseAddr(ptr));
	    break;
	case 'b':
	    BatchFileCtl = (*ptr) ? ptr : av[++i];
	    break;
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'c':
	    CloseReopenAfter = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'D':
	    DeleteDetectOpt = 1;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'f':
	    TailOpt = 1;	/* sit on tailable file XXX	*/
	    break;
	case 'H':
	    HeaderOnlyFeed = 1;
	    if (*ptr == 'B')
		KeepBytes = 1;
	    break;
	case 'h':
	    HostName = (*ptr) ? ptr : av[++i];
	    break;
	case 'i':
	    TryStreaming = 0;
	    break;
	case 'I':
	    NoCheckOpt = 1;
	    break;
	case 'L':
	    GenLinesHeader = 1;
	    break;
	case 'l':
	    LogAfter = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'M':
	    MaxStream = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    if (MaxStream > MAXSTREAM)
		MaxStream = MAXSTREAM;
	    NumStream = MaxStream;
	    break;
	case 'N':
	    NumBatches = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'n':
	    /* NOP */
	    break;
	case 'o':
	    NotifyOpt = (*ptr) ? ptr : av[++i];
	    break;
	case 'P':
	    Port = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'p':
	    PipeOpt = 1;	/* input from pipe 	*/
	    break;
	case 'Q':
	    IPTOS = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'R':
	    /*
	     * note: must be large enough to hold check responses that
	     * may become pending while we are transmitting an article.
	     */
	    RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    if (RxBufSize < MAXSTREAM * (MAXMSGIDLEN + 64) && RxBufSize != 0)
	        RxBufSize = MAXSTREAM * (MAXMSGIDLEN + 64);
	    break;
	case 'r':
	    RealTimeOpt = (*ptr) ? strtol(ptr, NULL, 0) : -1;
	    break;
	case 'S':
	    BatchSeq = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 's':
	    ptr = (*ptr) ? ptr : av[++i];
	    SetStatusLine(ptr, strlen(ptr));
	    HasStatusLine = 1;
	    break;
	case 'T':
	    TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    if (TxBufSize != 0 && TxBufSize < 512)
	        TxBufSize = 512;
	    break;
	case 't':
	    Timeout = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'W':
	    DelayTime = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'w':
	    WaitTime = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'x':
	    ArticleStatOpt = 1;
	    break;
        case 'Z':
            CompressMode = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
            break;
	default:
	    Usage(av[0]);
	    break;
	}
    }

    LogAfterCount += LogAfter;
    CloseReopenCount += CloseReopenAfter;

    if (BatchFileCtl == NULL || HostName == NULL || i > ac) {
	Usage(av[0]);
    }

    rsignal(SIGPIPE, SIG_IGN);
    rsignal(SIGHUP, sigTerm);
    rsignal(SIGINT, sigTerm);
    rsignal(SIGTERM, sigTerm);
    rsignal(SIGALRM, sigTerm);

    cdinit();

    Stats = (FeedStats *)malloc(sizeof(FeedStats));
    if (Stats == NULL) {
	logit(LOG_CRIT, "Unable to malloc stats memory");
	exit(1);
    }
    strcpy(Stats->hostname, HostName);
    switch (DOpts.FeederRTStats) {
	case RTSTATS_NONE:
	    HostStats = NULL;
	    break;
	case RTSTATS_LABEL:
	    {
		char buf[255];
		char *p;
		strncpy(buf, BatchFileCtl, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if ((p = strstr(buf, ".S%05d")) != NULL)
		    *p = 0;
		HostStats = FeedStatsFindSlot(buf);
	    }
	    break;
	case RTSTATS_HOST:
	    HostStats = FeedStatsFindSlot(HostName);
	    break;
    }

    if (*BatchFileCtl == 0) {
	dl_logit("batchfile is null!");
	exit(0);
    }

    while (--NumBatches >= 0) {
	time_t ddTime;
	time_t ddCount = 0;
	time_t cdflushTime;
	int fd;
	int cfd = -1;

	HeaderSize = 0;

	Stats->SentStats.ConnectTotal = 0;
	Stats->SentStats.OfferedTotal = 0;
	Stats->SentStats.AcceptedTotal = 0;
	Stats->SentStats.RefusedTotal = 0;
	Stats->SentStats.RejectedTotal = 0;
	Stats->SentStats.DeferredTotal = 0;
	Stats->SentStats.DeferredFailTotal = 0;
	Stats->SentStats.RejectedBytesTotal = 0.0;
	Stats->SentStats.AcceptedBytesTotal = 0.0;

	Stats->SentStats.ConnectCnt = 0;
	Stats->SentStats.OfferedCnt = 0;
	Stats->SentStats.AcceptedCnt = 0;
	Stats->SentStats.RefusedCnt = 0;
	Stats->SentStats.RejectedCnt = 0;
	Stats->SentStats.DeferredCnt = 0;
	Stats->SentStats.DeferredFailCnt = 0;
	Stats->SentStats.RejectedBytes = 0.0;
	Stats->SentStats.AcceptedBytes = 0.0;

#ifdef	USE_ZLIB
	CompressedBytes = 0;
	RawBytes = 0;
	CompressedBytesTotal = 0;
	RawBytesTotal = 0;
#endif

	OurMs = 0;
	TheirMs = 0;
	MsCount = 0;

	OurMsTotal = 0;
	TheirMsTotal = 0;
	MsCountTotal = 0;

	snprintf(CurrentBatchFile, sizeof(CurrentBatchFile) - 32, DQueueHomePat, NewsHome);
	strcat(CurrentBatchFile, "/");
	CBFIndex = strlen(CurrentBatchFile);

	if (BatchSeq < 0) {
	    sprintf(CurrentBatchFile + CBFIndex, "%s", BatchFileCtl);
	} else {
	    sprintf(CurrentBatchFile + CBFIndex, BatchFileCtl, BatchSeq);
	    ++BatchSeq;
	}

	/*
	 * Open next batchfile.  If the open fails, we go onto the next
	 * batchfile.  However, if we are doing a realtime feed, we must
	 * deal with a race condition with diablo where the next realtime
	 * queue file may not yet exist.
	 */

	if (PipeOpt) {
	    fd = 0;
	} else {
	    fd = open(CurrentBatchFile, O_RDWR | (RealTimeOpt ? O_CREAT : 0), 0600);
	    if (fd < 0) {
		if (eNoBat == 0) {
		    eNoBat = 1;
		    dl_logit("no batchfile");
		}
		continue;
	    }

	    /*
	     * Lock batchfile
	     */
	    {
		struct stat st;

		if (xflock(fd, XLOCK_EX|XLOCK_NB) != 0) {
		    if (eNoBat == 0) {
			eNoBat = 1;
			dl_logit("batchfile already locked");
		    }
		    close(fd);

		    /*
		     * If we cannot get the lock and RealTimeOpt is
		     * set, we abort - someone else has a lock on the
		     * realtime file.  Otherwise we retry (w/ the next
		     * sequence number)
		     */

		    if (RealTimeOpt) {
			dl_logit("realtime batchfile already locked");
			break;
		    }
		    continue;
		}
		if (fstat(fd, &st) != 0) {
		    dl_logit("fstat failed: %s", strerror(errno));
		    exit(1);
		}
		if (st.st_nlink == 0) {
		    close(fd);
		    continue;
		}
		eNoBat = 0;
	    }
	    fprintf(stderr, "%s\n", CurrentBatchFile);
	} /* pipeopt */

	fstat(fd, &CurSt);
	WouldHaveRefiled = 0;

	Stats->SentStats.TimeStart = Stats->SentStats.DeltaStart =
					ddTime = cdflushTime = time(NULL);
	if (HostStats != NULL && HostStats->SentStats.TimeStart == 0) {
	    LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
	    HostStats->SentStats.TimeStart = Stats->SentStats.TimeStart;
	    LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
	}

	/*
	 * Connect to remote, send news
	 */

	if ((cfd = connectTo(HostName, NULL, Port)) >= 0) {
	    char buf[1024];
	    char relPath[1024];
	    char msgId[1024];
	    char path[1024];
	    off_t fileOff = 0;
	    int fileSize = 0;
	    int sentFileSize = 0;
	    int headOnly = 0;
	    time_t queuedTime = 0;
	    FILE *fo = NULL;
	    int connectCount = 0;
	    int bufInval = 1;
	    int loggedMark = 0;
	    int deferbegin = 0;
	    int deferend = 0;
	    int cSize = 0;
	    /* We kept a list of defer requests (431 response) and procedd
	     * it from time to time.
	     */
    	    char *deferbuf[MAXDEFER];

	    credtime(0);

	    if (PipeOpt)
		sprintf(path, "%s", CurrentBatchFile);
	    else
		sprintf(path, "%s.tmp", CurrentBatchFile);

	    stprintf("%s %s wait/in %d", HostName, CurrentBatchFile + CBFIndex, connectCount);

	    /*
	     * Loop if need to reconnect, there are pending streaming commands,
	     * or we can get a valid buffer.  Note that this allows us to halt
	     * readline() calls when we reach our streaming limit as well as to
	     * do a final-drain when we reach the end of the buffer.
	     */

	    for (;;) {

		if (TimeCounter++ >= 100)
		    TimeNow = time(NULL);

		/*
		 * If there is no active buffer and we have not reached
		 * our streaming limit, get next article to transmit, either
		 * from the defer buffer or read in another article control line.
		 */
		if (bufInval && 
		    StreamPend < NumStream - HiWaterMark && 
		    BytesPend < MAXPENDBYTES
		) {
		    if (DebugOpt > 1)
			printf("clear watermark\n");
		    HiWaterMark = 0;	/* reset WaterMark */
		    if (deferbegin < deferend &&
			(deferbegin || (deferend >= (MAXDEFER - 20)))) {
			/* We are at this moment clearing the defer backlog,
			 * or the defer buffer is full and it has to be
			 * cleared.
			 * We need to flush the defer queue some time before it 
			 * reaches its maximum capacity becase we might receive
			 * several 431 answers from the peer before reaching
			 * this point again.
			 */
			strncpy(buf, deferbuf[deferbegin], sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			zfreeStr(&SysMemPool, &deferbuf[deferbegin++]);
			bufInval = 0;
		    } else {
			/* There are no defers to be processed, so try to get
			 * something from the input file.
			 */
			if (deferbegin > 0)
			    /* We have to reset deferbegin in case we are switching
			     * back from "process from defer queue" mode to "process
			     * from file" mode.
			     */
			    deferbegin = deferend = 0;
			if ((bufInval = readretry(buf, sizeof(buf))) != 0)
			    bufInval = readline(fd, buf, sizeof(buf), TimeNow);
		    }
		}

		if (TimeCounter++ >= 100)
		    TimeNow = time(NULL);


		if (bufInval && StreamPend == 0 && deferbegin == 0 && deferend > 0) {
		    /* Start to begin spooling deferred articles at the end of 
		     * input file. Subsequent articles are retrieved by the 
		     * code above, but this test has to come after trying to
		     * read from file.
		     */
		    if (deferend < 20) {
			/* Sleep in case the defer queue is emptied at the end
			 * of input file, since in that case the defer might
			 * have happened just before. Alo, we don't need to
			 * wait if the defer queue already has a certain size.
			 * Note that infinite defer cycles cannot happen since 
			 * we do not add anything to the defer queue in case it 
			 * is being processed.
			 */
			sleep(5);
		    }
		    strncpy(buf, deferbuf[deferbegin], sizeof(buf) - 1);
		    buf[sizeof(buf) - 1] = '\0';
		    zfreeStr(&SysMemPool, &deferbuf[deferbegin++]);
		    bufInval = 0;
		}

		/*
		 * If there is nothing left to do, break out of the loop
		 */

		if (bufInval && StreamPend == 0)
		    break;

		/*
		 * Go.
		 */

		if (DebugOpt > 1)
		    printf("%s bufInval %d StreamPend %d/%d (%s)\n", dtstamp(),
					bufInval, StreamPend, NumStream,
					(bufInval) ? "?" : buf);

		if (!PipeOpt && (TimeNow - ddTime) > DDTIME) {
		    /*
		     * Check if queue file removed out from under us.  Stop
		     * processing if it has been.
		     */
		    struct stat st;

		    if (fstat(fd, &st) == 0 && st.st_nlink == 0) {
			if (DeleteDetectOpt || ddCount++ > 5) {
			    NumBatches = 0;
			    break;
			}
		    } else
			ddCount = 0;
		    ddTime = TimeNow;
		}

		if ((TimeNow - cdflushTime) > (CACHEFLUSHTIME / 3)) {
		    /*
		     * Flush old file handles
		     */
		    cdflush();
		    cdflushTime = TimeNow;
		}

		if (bufInval || 
		    extractFeedLine(buf, relPath, msgId, &fileOff, &fileSize,
					&headOnly, &queuedTime, &cSize) == 0
		) {
		    int t;
		    char stage[MAXREASON], reason[MAXREASON];

		    /*
		     * If header-only feed line and this is not a header-only
		     * feed, we do not have enough data to pass this on as a
		     * normal article so skip it.
		     */

		    if (bufInval == 0 && headOnly && HeaderOnlyFeed == 0) {
			bufInval = 1;
			continue;
		    }

		    /*
		     * If this is a delayed feed, then wait until we are
		     * allowed to send it.
		     */
		    if (DelayTime && queuedTime) {
			int sleepTime = (queuedTime + DelayTime) - TimeNow;
			if (DebugOpt)
			    printf("sleep %u (%u,%u,%u)\n",
					sleepTime,
					DelayTime, (int)queuedTime, (int)TimeNow);
			if (sleepTime > 0)
			    sleep(sleepTime);
		    }

		    /*
		     * State processing.
		     */

		    stprintf("%s %s process %d", HostName, CurrentBatchFile + CBFIndex, connectCount);

		    if (bufInval)
			t = Transact(cfd, NULL, msgId, 0, 0, 0, (deferbegin > 0),
				     stage, reason, buf, &sentFileSize);
		    else {
			struct stat st;
			if (fileSize == 0) {
			    if (DebugOpt > 1)
				printf("%s instant expired article\n", dtstamp());
			    bufInval = 1;

			    continue;
			} else if (ArticleStatOpt && stat(relPath, &st) == -1) {
			    if (DebugOpt > 1)
				printf("%s expired article\n", dtstamp());
			    bufInval = 1;

			    continue;
			} else 
			t = Transact(cfd, relPath, msgId, fileOff, fileSize,
				     cSize,
				     (deferbegin > 0), stage, reason, buf,
				     &sentFileSize);
		    }

		    if (DebugOpt > 1)
			printf("%s Transaction result: %d %s", dtstamp(), t, (bufInval) ? "<ibuf-empty>\n" : buf);

		    stprintf("%s %s wait/in %d", HostName, CurrentBatchFile + CBFIndex, connectCount);

		    switch(t) {
		    case T_STREAMING:
			/*
			 * operation in progress, will return the real status
			 * later.
			 */
			break;
		    case T_ACCEPTED:
			++connectCount;
			++Stats->SentStats.AcceptedCnt;
			++Stats->SentStats.AcceptedTotal;
			if (HeaderOnlyFeed) {
			    Stats->SentStats.AcceptedBytes += (double)HeaderSize;
			    Stats->SentStats.AcceptedBytesTotal += (double)HeaderSize;
			} else {
			    Stats->SentStats.AcceptedBytes += (double)sentFileSize;
			    Stats->SentStats.AcceptedBytesTotal += (double)sentFileSize;
			}

			if (HostStats != NULL) {
			    LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
			    ++HostStats->SentStats.AcceptedCnt;
			    ++HostStats->SentStats.AcceptedTotal;
			    if (HeaderOnlyFeed) {
				HostStats->SentStats.AcceptedBytes += (double)HeaderSize;
				HostStats->SentStats.AcceptedBytesTotal += (double)HeaderSize;
			    } else {
				HostStats->SentStats.AcceptedBytes += (double)sentFileSize;
				HostStats->SentStats.AcceptedBytesTotal += (double)sentFileSize;
			    }
			    LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			}

			if (DebugOpt > 1)
			    printf("fileSize = %d, HeaderSize = %d, HeaderOnlyFeed = %d\n",
				fileSize, HeaderSize, HeaderOnlyFeed);
			doArtLog("accept", msgId, "accepted", stage, reason);
			break;
		    case T_REFUSED:
			++connectCount;
			++Stats->SentStats.RefusedCnt;
			++Stats->SentStats.RefusedTotal;
			if (HostStats != NULL) {
			    LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
			    ++HostStats->SentStats.RefusedCnt;
			    ++HostStats->SentStats.RefusedTotal;
			    LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			}
			doArtLog("refuse", msgId, "refused", stage, reason);
			break;
		    case T_DEFERIT:
			/* We need to store the entry for later
			 * Drop it if the buffer is full (treat like reject)
			 * or we are already handling defers
			 */
			if (deferbegin == 0 && deferend < MAXDEFER) {
			    doArtLog("defer", msgId, "deferred", stage, reason);
			    deferbuf[deferend] = zallocStr(&SysMemPool, buf);
			    if (deferbuf[deferend] != NULL)
				deferend++;
			    ++Stats->SentStats.DeferredCnt;
			    ++Stats->SentStats.DeferredTotal;
			    if (HostStats != NULL) {
				LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
				++HostStats->SentStats.DeferredCnt;
				++HostStats->SentStats.DeferredTotal;
				LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			    }
			    break;
			} else {
			    doArtLog("error", msgId, deferbegin? "error-doubledefer": 
				"error-deferqueuefull", stage, reason);
			    ++Stats->SentStats.DeferredFailCnt;
			    ++Stats->SentStats.DeferredFailTotal;
			    if (HostStats != NULL) {
				LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
				++HostStats->SentStats.DeferredFailCnt;
				++HostStats->SentStats.DeferredFailTotal;
				LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			    }
			}
		    case T_REJECTED:
			++connectCount;
			++Stats->SentStats.RejectedCnt;
			++Stats->SentStats.RejectedTotal;
			if (HeaderOnlyFeed) {
			    Stats->SentStats.RejectedBytes += (double)HeaderSize;
			    Stats->SentStats.RejectedBytesTotal += (double)HeaderSize;
			} else {
			    Stats->SentStats.RejectedBytes += (double)sentFileSize;
			    Stats->SentStats.RejectedBytesTotal += (double)sentFileSize;
			}
			if (HostStats != NULL) {
			    LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
			    ++HostStats->SentStats.RejectedCnt;
			    ++HostStats->SentStats.RejectedTotal;
			    if (HeaderOnlyFeed) {
				HostStats->SentStats.RejectedBytes += (double)HeaderSize;
				HostStats->SentStats.RejectedBytesTotal += (double)HeaderSize;
			    } else {
				HostStats->SentStats.RejectedBytes += (double)sentFileSize;
				HostStats->SentStats.RejectedBytesTotal += (double)sentFileSize;
			    }
			    LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			}
			doArtLog("reject", msgId, "rejected", stage, reason);
			break;
		    case T_FAILED:
		    case T_FAILEDEXIT:
			/*
			 * If failed, attempt to reconnect to remote.
			 * if failed+exit, exit out
			 */
			break;
		    default:
			/* should not occur */
			break;
		    }

		    /*
		     * Many INND's are setup to close a connection after a
		     * certain period of time.  Thus, a failure could be a
		     * normal occurance.  If we have already had at least one
		     * successfull transaction, we attempt to reconnect when
		     * this case occurs.
		     */

		    if (t == T_FAILED || t == T_FAILEDEXIT) {
			if (connectCount > 0 && t != T_FAILEDEXIT && TermFlag == 0) {
			    dl_logit("Remote EOF, attempting to reconnect");
			    logStats("mark");
			    loggedMark = 1;
			    close(cfd);
			    clearCommandBuffer();
			    KillFd = -1;
			    credtime(OUR_DELAY);
			    (void)RefilePendingStreams(NULL); /* retry */
			    if ((cfd = connectTo(HostName, NULL, Port)) >= 0) {
				connectCount = 0;
				if (CloseReopenCount > 0)
				    CloseReopenCount = Stats->SentStats.OfferedTotal +
							CloseReopenAfter;

				credtime(0);
				continue;
			    }
			    credtime(0);
			}

			/*
			 * reopen failed: termination, cleanup
			 */

			NumBatches = 0;

			if (RealTimeOpt == 0) {
			    if (fo == NULL)
				fo = fopen(path, "w");
			    if (fo != NULL) {
				(void)RefilePendingStreams(fo);
				if (!bufInval)
				    fputs(buf, fo);
			    }
			} else {
			    WouldHaveRefiled += RefilePendingStreams(NULL);
			    if (!bufInval)
				++WouldHaveRefiled;
			}
			break; /* break out of for(EVER) */
		    }

		    /*
		     * buffer successfully dealt with, buffer is now invalid
		     */

		    bufInval = 1;

		    /*
		     * Bump transaction count
		     */

		    if (t != T_STREAMING && deferbegin == 0) {
			++Stats->SentStats.OfferedCnt;
			++Stats->SentStats.OfferedTotal;
			if (HostStats != NULL) {
			    LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
			    ++HostStats->SentStats.OfferedCnt;
			    ++HostStats->SentStats.OfferedTotal;
			    LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
			}
		    }

		    /*
		     * Log stats after specified count
		     */

		    if (LogAfterCount > 0 && Stats->SentStats.OfferedTotal >= LogAfterCount) {
			loggedMark = 1;
			logStats("mark");
			LogAfterCount += LogAfter;
		    }

		    /*
		     * Close remote to allow remote logging to occur after
		     * specified count (doesn't work with streaming yet)
		     */

		    if (
			StreamMode == STREAM_OFF &&
			CloseReopenCount > 0 && 
			Stats->SentStats.OfferedTotal >= CloseReopenCount
		    ) {
			char *ptr;

			(void)RefilePendingStreams(NULL); /* retry */
			flushCompressBuffer(cfd);
			flushCommandBuffer(cfd);
			commandResponse(cfd, &ptr, "quit\r\n");
			close(cfd);
			KillFd = -1;
			clearCommandBuffer();
			credtime(OUR_DELAY);
			if ((cfd = connectTo(HostName, NULL, Port)) < 0) {
			    credtime(0);
			    break;
			}
			credtime(0);
			connectCount = 0;
			CloseReopenCount += CloseReopenAfter;
		    }
		} else {
		    if (buf[0] != '#')
			dl_logit("Buffer syntax error: %s", buf);
		    bufInval = 1;
		}

		/*
		 * If a signal occured during the transaction, break out of
		 * our loop.
		 */

		if (TermFlag) {
		    dl_logit("Terminated with signal");
		    break;
		}
	    } /* for(EVER) */

	    /*
	     * Final delta stats, only if we had
	     * previously logged marks, otherwise the
	     * mark stats will be the same as the final
	     * stats and we do not bother logging the mark.
	     */

	    if (loggedMark != 0)
		logStats("mark");

	    /*
	     * Rewrite batchfile, but only if not a realtime dnewslink.
	     */

	    if (RealTimeOpt == 0) {
		if (StreamPend) {
		    if (fo == NULL)
			fo = fopen(path, "w");
		    if (fo) {
			(void)RefilePendingStreams(fo);
		    }
		}

		if (readline(fd, buf, sizeof(buf), TimeNow) == 0 || fo != NULL) {
		    struct stat st;

		    if (fo == NULL)
			fo = fopen(path, "w");
		    if (fo != NULL) {
			do {
			    fputs(buf, fo);
			} while (readline(fd, buf, sizeof(buf), TimeNow) == 0);
			fflush(fo);
			fclose(fo);
			fo = NULL;
		    }
		    if (DeleteDetectOpt && 
			!PipeOpt && 
			!RealTimeOpt &&
			(fstat(fd, &st) != 0 || st.st_nlink == 0)
		    ) {
			/*
			 * If a queue file gets deleted out from under
			 * us, we don't try to rename the rewrite file back.
			 */
			remove(path);
			NumBatches = 0;
		    } else if (PipeOpt == 0 && RealTimeOpt == 0) {
			rename(path, CurrentBatchFile);
		    }
		} else {
		    if (PipeOpt == 0 && RealTimeOpt == 0)
			remove(CurrentBatchFile);
		}
	    } else {
		/*
		 * refiling does not work if we are in realtime mode
		 */
		if (StreamPend)
		    WouldHaveRefiled += RefilePendingStreams(NULL);
		if (readline(fd, buf, sizeof(buf), TimeNow) == 0)
		    WouldHaveRefiled = 1;
	    }

	    /*
	     * Final overall stats
	     */

	    Stats->SentStats.DeltaStart = Stats->SentStats.TimeStart;
	    Stats->SentStats.OfferedCnt = Stats->SentStats.OfferedTotal;
	    Stats->SentStats.AcceptedCnt = Stats->SentStats.AcceptedTotal;
	    Stats->SentStats.RefusedCnt = Stats->SentStats.RefusedTotal;
	    Stats->SentStats.RejectedCnt = Stats->SentStats.RejectedTotal;
	    Stats->SentStats.DeferredCnt = Stats->SentStats.DeferredTotal;
	    Stats->SentStats.DeferredFailCnt = Stats->SentStats.DeferredFailTotal;
	    Stats->SentStats.RejectedBytes = Stats->SentStats.AcceptedBytesTotal;
	    Stats->SentStats.AcceptedBytes = Stats->SentStats.AcceptedBytesTotal;

	    OurMs += OurMsTotal;
	    TheirMs += TheirMsTotal;
	    MsCount += MsCountTotal;

#ifdef	USE_ZLIB
            RawBytes += RawBytesTotal;
            CompressedBytes += CompressedBytesTotal;
#endif

	    /*
	     * be nice
	     */

	    if (cfd >= 0) {
		char *ptr;

		commandResponse(cfd, &ptr, "quit\r\n");
		flushCompressBuffer(cfd);
		flushCommandBuffer(cfd);
		close(cfd);
		clearCommandBuffer();
		KillFd = -1;
		/* normal quit, do not set cfd to -1 	     */
		/* this allows us to go on to the next batch */

#ifdef	USE_ZLIB
		if (CompressOn > 0) {
		    int r;
		    if ((r = deflateEnd(&Z_Strm)) != Z_OK)
			logit(LOG_ERR, "deflateEnd error: %s", Z_Strm.msg);
		}
#endif
	    }

	    logStats("final");

	} /* cfd=connectTo */

	if (fd > 0) {
	    readreset(fd);
	    close(fd);
	}
	if (cfd < 0 || TermFlag)
	    break;
    } /* while, on NumBatches */

    if (Logfd != NULL) fclose(Logfd);
    if (NotifyOpt != NULL) {
	RegisterNotify(NotifyOpt, 0);
	CloseNotify(&NotifyFd, &NotifyLockFd, NotifyOpt);
    }
    exit(0);
}

void
setSocketOptions(int fd)
{
    if (fd >= 0) {
	int on = 1;

	/*
	 * Make sure keepalive is turned on to prevent infinite hangs
	 */
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));

	/*
	 * Set the transmit and receive buffer size
	 */
	if (TxBufSize) {
	    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(TxBufSize)) < 0) {
		logit(LOG_ERR, "setsockopt fd %d failed: %s", fd, strerror(errno));
	    }
	}
	if (RxBufSize) {
	    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(RxBufSize)) < 0) {
		logit(LOG_ERR, "setsockopt fd %d failed: %s", fd, strerror(errno));
	    }
	}

#ifdef IP_TOS
	/* Set TOS */
	if (IPTOS > 0)
	    setsockopt(fd, IPPROTO_IP, IP_TOS, (char *) &IPTOS, sizeof(IPTOS));
#endif
    }

}

/*
 * Figure out the ip address and port.  Try to use our cached lookup
 * info because a host might have multiple IN A's and we want to connect
 * to the same one we succesfully connected to before.
 */

void
getHostAddr(int *fd, const char *hostName, const char *serviceName, int defPort, struct sockaddr **addr, int *addrLen, int useSaved)
{
#ifdef INET6
    static struct addrinfo *res0 = NULL;
    static struct addrinfo *res = NULL;
    if (useSaved && res != NULL) {
	*addr = res->ai_addr;
	*addrLen = res->ai_addrlen;
	*fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    } else {
	struct addrinfo hints;
	char p[10];
	int error;

	/*
	 * Open a wildcard listening socket
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(p, sizeof(p), "%d", defPort);
	if (res0 == NULL) {
	    error = getaddrinfo(HostName, p, &hints, &res0);
	    if (error == 0) {
		res = res0;
	    } else {
		dl_logit("hostname lookup failure: %s:%s: %s\n",
				HostName, serviceName, gai_strerror(error));
		*fd = -1;
		*addr = NULL;
		return;
	    }
	} else if (res != NULL) {
	    res = res->ai_next;
	}
	if (res == NULL) {
	    *fd = -1;
	    *addr = NULL;
	} else {
	    *fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	    *addr = res->ai_addr;
	    *addrLen = res->ai_addrlen;
	}
    }

#else
    static struct sockaddr_in sin;

    if (useSaved) {
	*addr = (struct sockaddr *)&sin;
	*addrLen = sizeof(sin);
    } else {
	struct hostent *hent0 = NULL;
	static int hindex = 0;
	char *hent;

	memset(&sin, 0, sizeof(sin));

	if (hent0 == NULL) {
	    hent0 = gethostbyname(hostName);
	    if (hent0 == NULL) {
		dl_logit("hostname lookup failure: %s", strerror(errno));
		*fd = -1;
		*addr = NULL;
		return;
	    }
	    hindex = 0;
	} else {
	    hindex++;
	}
	hent = hent0->h_addr_list[hindex];
	if (hent == NULL) {
	    dl_logit("hostname lookup failure: %s", strerror(errno));
	    *fd = -1;
	    *addr = NULL;
	    hent0 = NULL;
	    return;
	} else {
	    sin.sin_family = hent0->h_addrtype;
	    memmove(&sin.sin_addr, hent, hent0->h_length);
	}
	{
	    struct servent *serv = serviceName ?
				getservbyname(serviceName, "tcp") : NULL;
	    if (serv != NULL) {
		sin.sin_port = serv->s_port;
	    } else {
		sin.sin_port = htons(defPort);
		if (serviceName) {
		    dl_logit("unable to lookup service '%s', using port %d", 
			serviceName, 
			defPort
		    );
		}
	    }
	}
	*addr = (struct sockaddr *)&sin;
	*addrLen = sizeof(sin);
    }

#endif	/* INET6 */

}

#ifdef INET6
void
bindOutboundInterface(int *fd, struct sockaddr *addr)
{
    if (OutboundIpName != NULL) {
	int error;
	struct addrinfo hints;
	static struct addrinfo *bres=NULL;
	struct addrinfo *loop;

	if (!bres) { 
	    memset(&hints, 0, sizeof(hints));
	    hints.ai_flags = AI_PASSIVE;
	    hints.ai_family = PF_UNSPEC;
	    hints.ai_socktype = SOCK_STREAM;
	    error = getaddrinfo(OutboundIpName, 0, &hints, &bres); 
	    if (error != 0) {
		dl_logit("local bind address lookup failure: %s",
							gai_strerror(error));
		close(*fd);
		*fd = -1;
	    }
	}

	for (loop = bres; loop; loop = loop->ai_next) {
		if ( addr->sa_family == loop->ai_family   && 
		     bind(*fd, loop->ai_addr, loop->ai_addrlen) == 0) {
			return; /* keep bres for next connection */
		}
	}
	dl_logit("local bind address failed: %s", strerror(errno));
	close(*fd);
	*fd = -1;
	freeaddrinfo(bres);
	bres=NULL;
   }
}
#else	/* INET4 */
void
bindOutboundInterface(int *fd)
{
    if (OutboundIpName != NULL) {
	struct hostent *host = gethostbyname(OutboundIpName);
	struct sockaddr_in bsin;

	bzero(&bsin, sizeof(bsin));

	if (host != NULL) {
	    bsin.sin_family = host->h_addrtype;
	    memmove(&bsin.sin_addr, host->h_addr, host->h_length);
	} else if (IsIpAddr(OutboundIpName)) {
	    bsin.sin_family = AF_INET;
	    bsin.sin_addr.s_addr = inet_addr(OutboundIpName);
	} else {
	    dl_logit("local bind address lookup failure: %s", strerror(errno));
	    close(*fd);
	    *fd = -1;
	}
	if (bind(*fd, (struct sockaddr *)&bsin, sizeof(bsin)) < 0) {
	    dl_logit("local bind address failed: %s", strerror(errno));
	    close(*fd);
	    *fd = -1;
	}
    }
}
#endif	/* INET6 */

int
connectTo(const char *hostName, const char *serviceName, int defPort)
{
    static int UseSaved = 0;
    int fd = -1;
    int cfd = -1;
    static int ConnectCount;
    struct sockaddr *addr = NULL;
    int addrLen = 0;
    int connected = 0;
    int connectattempts = 0;

    WritesLosing = 0;

    clearResponseBuf();

    ++ConnectCount;

    stprintf("%s %s connect: %d", HostName, CurrentBatchFile + CBFIndex, ConnectCount);

#ifdef	USE_ZLIB
    if (CompressOn > 0) {
	int r;
	if ((r = deflateEnd(&Z_Strm)) != Z_OK)
	    logit(LOG_ERR, "deflateEnd error: %s (at connect)", Z_Strm.msg);
    }
#endif
    CompressOn = 0;

    ++Stats->SentStats.ConnectCnt;
    ++Stats->SentStats.ConnectTotal;
    if (HostStats != NULL) {
	LockFeedRegion(HostStats, XLOCK_EX, FSTATS_OUT);
	++HostStats->SentStats.ConnectCnt;
	++HostStats->SentStats.ConnectTotal;
	LockFeedRegion(HostStats, XLOCK_UN, FSTATS_OUT);
    }

    stprintf("%s %s connect: %d", HostName, CurrentBatchFile + CBFIndex,
							Stats->SentStats.ConnectCnt);

/*
 * This gets a little messy.
 *
 * The INET6 based host lookups allow us to keep track of the result
 * of an address lookup across multiple getaddrinfo() calls. The INET4
 * lookups don't without making a copy of the result.
 *
 * This means we need to do a socket() and local bind() before we lookup
 * the host for INET4. In the INET6 case, we only want to do the bind()
 * after we have done the lookup, so that we can do a local bind() using
 * the correct protocol.
 *
 * Not sure if this is the right way, but that's how it is done for now.
 *
 */
    while (!TermFlag && !connected && ++connectattempts <= 5) {

#ifndef INET6
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (addr == NULL && fd != -1) {
	    setSocketOptions(fd);
	    bindOutboundInterface(&fd);
	}
#endif

	getHostAddr(&fd, hostName, serviceName, defPort, &addr, &addrLen, UseSaved);

#ifdef INET6
	if (addr != NULL && fd != -1) {
	    setSocketOptions(fd);
	    bindOutboundInterface(&fd,addr);
	}
#endif
	if (addr == NULL) {
	    close(fd);
	    fd = -1;
	    connected = -1;
	    UseSaved = 0;
	} else if (fd == -1) {
	    connected = 0;
	    UseSaved = 0;
	} else if (connect(fd, addr, addrLen) < 0) {
	    dl_logit("%s %s connect: %s", NetAddrToSt(0, addr, 1, 1, 1),
					CurrentBatchFile + CBFIndex,
					strerror(errno));
	    close(fd);
	    fd = -1;
	    stprintf("%s %s connect: fail", HostName,
					CurrentBatchFile + CBFIndex);
	    connected = 0;
	    UseSaved = 0;
	} else {
	    connected = 1;
	    connectattempts = 0;
	}
	if (connected == -1) {
	    if (RealTimeOpt)
		sleep(WaitTime);
	    else
		connectattempts = 999;
	}
    }

    /*
     * Sometimes setting the transmit and receive buffer sizes prior to
     * the connect does not work, because the connect() overrides the 
     * parameters based on the destination route.  So, we do it here as well.
     */

    setSocketOptions(fd);

#ifdef TCP_NODELAY
    /*
     * Turn on TCP_NODELAY
     */
    if (fd >= 0) {
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));
    }
#endif

    LastErrBuf[0] = 0;

    /*
     * get initial message from remote
     */

    UseSaved = 0;

    if (fd >= 0) {
	char *ptr;

	clearCommandBuffer();
	cfd = fd;

	/* Set KillFd here to avoid deadlock when the remote server fails
	 * to respond after initial connection
	 */
	KillFd = cfd;

	switch(commandResponse(cfd, &ptr, NULL)) {
	case OK_CANPOST:	/* innd	*/
	case OK_NOPOST:		/* nntpd may still allow news transfers */
	    break;
	default:
	    dl_logit("connect: %s", (ptr ? ptr : "(unknown error)"));
	    stprintf("%s %s connect: err resp", HostName, CurrentBatchFile + CBFIndex);
	    clearCommandBuffer();
	    KillFd = -1;
	    close(cfd);
	    cfd = -1;
	    fd = -1;
	    break;
	}
	credreset();
    }

    /*
     * Unless streaming is disabled, we attempt to turn on streaming.  If
     * it succeeds, we set the StreamMode to STREAM_RELOAD to resend any
     * queued streaming requests from a prior connection.
     */

    if (cfd >= 0) {
	char *ptr = NULL;
	char negotiated[255];

	negotiated[0] = 0;

	if (TryStreaming) {
	    switch(commandResponse(cfd, &ptr, "mode stream\r\n")) {
	    case OK_STREAMOK:
		StreamMode = STREAM_ON;
		strcat(negotiated,"streaming enabled");                
		dl_logit("connect: %s (streaming)", (ptr ? ptr : "<Unexpected EOF>"));
		stprintf("%s %s connect: stream", HostName, CurrentBatchFile + CBFIndex);
		break;
	    default:
		StreamMode = STREAM_OFF;
		strcat(negotiated,"streaming disabled");                
		dl_logit("connect: %s (nostreaming)", (ptr ? ptr : "<Unexpected EOF>"));
		stprintf("%s %s connect: nostream", HostName, CurrentBatchFile + CBFIndex);
		break;
	    }
	} else {
	    strcat(negotiated,"streaming disabled");                
	    dl_logit("connect (streaming disabled)");
	    stprintf("%s %s connect (streaming disabled)", HostName, CurrentBatchFile + CBFIndex);
	    StreamMode = STREAM_OFF;
	}

	/*
	 * added to support compression, cmsedore@maxwell.syr.edu 12/4/97
	 */
#ifdef	USE_ZLIB
	if (CompressMode > 0) {
	    switch(commandResponse(cfd, &ptr, "mode compress\r\n")) {
	    case OK_COMPRESSOK:
		CompressOn = 1;
		strcat(negotiated," compression enabled");
		bzero(&Z_Strm, sizeof(Z_Strm));
		if (deflateInit(&Z_Strm, CompressMode) != Z_OK)
		    logit(LOG_ERR, "deflateInit error: %s", Z_Strm.msg);
		dl_logit("connect: %s (%s)\n", (ptr ? ptr : "<Unexpected EOF>"),
								 negotiated);
		break;
	    default:
		CompressOn = 0;
		strcat(negotiated," compression disabled");
		CompressMode = COMPRESS_OFF;
		dl_logit("connect: (%s)\n", negotiated);
		break;
	    }
	} else {
	    CompressMode = COMPRESS_OFF;
	    CompressOn = 0;
	}
#endif

	stprintf("%s %s connect: %s", HostName, CurrentBatchFile + CBFIndex,
						negotiated);

	if (HeaderOnlyFeed) {
	    switch(commandResponse(cfd, &ptr, "mode headfeed\r\n")) {
	    case OK_MODECMDOK:
		dl_logit("connect: %s (header feed only)", (ptr ? ptr : "<Unexpected EOF>"));
		stprintf("%s %s connect: headfeed", HostName,
					CurrentBatchFile + CBFIndex);
		break;
	    default:
		dl_logit("mode headfeed failed: %s",
					(ptr ? ptr : "<Unexpected EOF>"));
		stprintf("%s %s connect: mode headfeed failed", HostName,
					CurrentBatchFile + CBFIndex);
		clearCommandBuffer();
		KillFd = -1;
		close(cfd);
		cfd = -1;
		sprintf(LastErrBuf, "mode headfeed command failed");
		break;
	    }
	}
    }

    /*
     * clean up
     */

    if (cfd < 0) {
	if (LastErrBuf[0] == 0)
	    sprintf(LastErrBuf, "connect-timeout");
	sleep(WaitTime / 3);
    } else {
	int n;

	n = fcntl(cfd, F_GETFL);
	if (n < 0) {
	    dl_logit("getting socket flags: %s", strerror(errno));
	} else {
	    if (fcntl(cfd, F_SETFL, n | O_NONBLOCK) < 0) {
		dl_logit("setting O_NONBLOCK: %s", strerror(errno));
	    }
	}

	UseSaved = 1;
    }

    KillFd = fd;
    return(cfd);
}

/*
 * Transact() - begin a streaming or non-streaming transaction, or
 *		complete a streaming transaction.
 */

int
Transact(int cfd, const char *relPath, char *msgId, off_t off, int size, int cSize, int defers, char *stage, char *reason, char *buf, int *sentSize)
{
    int r = 0;
    static char *ptr = NULL;

    if (sentSize)
	*sentSize = 0;

    StrnCpyNull(stage, "-", MAXREASON);
    StrnCpyNull(reason, "-", MAXREASON);
    /*
     * Handle streaming.  If r returns zero, we revert to the
     * old operation (this is only occurs if streaming is disabled).
     */

#ifdef NOTDEF
    if (StreamMode == STREAM_RELOAD) {
	StreamReload(cfd);
	StreamMode = STREAM_ON;
    }
#endif
    if (StreamMode == STREAM_ON) {
	r = StreamTransact(cfd, relPath, msgId, off, size, cSize, defers, stage, reason, buf, sentSize);
	if (r)
	    return(r);
    }

    if (relPath == NULL)
	return(T_STREAMING);

    /*
     * This is necessary to guarentee sufficient receive buffer
     * space.
     */
    if (strlen(msgId) > MAXMSGIDLEN) {
	StrnCpyNull(stage, "pre-ihave", MAXREASON);
	StrnCpyNull(reason, "Message-ID too long", MAXREASON);
	return(T_REJECTED);
    }

    switch(commandResponse(cfd, &ptr, "ihave %s\r\n", msgId)) {
    case CONT_XFER:
	break;
    case ERR_XFERRJCT:
	r = T_REJECTED;
	break;
    case ERR_GOTIT:
	r = T_REFUSED;
	break;
    case ERR_ACCESS:
    case ERR_FAULT:
    case ERR_AUTHBAD:
    case ERR_GOODBYE:
	r = T_FAILEDEXIT;
	break;
    case ERR_NOAUTH:
    default:
	r = T_FAILED;
	break;
    }
    StrnCpyNull(stage, "ihave", MAXREASON);
    StrnCpyNull(reason, (ptr ? ptr : "<Unexpected EOF>"), MAXREASON);
    if (r == 0) {
	r = DumpArticle(cfd, relPath, off, size, cSize);
	if (sentSize)
	    *sentSize = size;
	switch(commandResponse(cfd, &ptr, NULL)) {
	case OK_XFERED:
	    if (r == 0)
		r = T_ACCEPTED;
	    break;
	case ERR_GOTIT:
	    if (r == 0)
		r = T_REFUSED;
	    break;
	case ERR_XFERRJCT:
	    if (r == 0)
		r = T_REJECTED;
	    break;
	case ERR_XFERFAIL:
	default:
	    if (r == 0)
		r = T_FAILED;
	    break;
	}

	StrnCpyNull(stage, "ihave-dump", MAXREASON);
	StrnCpyNull(reason, (ptr ? ptr : "<Unexpected EOF>"), MAXREASON);

	/*
	 * If we couldn't send the article, we ignore the
	 * response code because the remote might send the
	 * wrong response to the null article.
	 */
	if (r < 0) {
	    StrnCpyNull(reason, "Error sending article (1)", MAXREASON);
	    r = T_REJECTED;
	}
    }
    return(r);
}

int
DumpArticle(int cfd, const char *relPath, off_t off, int size, int cSize)
{
    static char pathBase[PATH_MAX];
    char *path;
    char *ptr = NULL;
    char *base = NULL;
    int r = -1;
    int multiArtFile = 0;
    int wasControl = 0;
    int haveLines = 0;
    int doBytes = 1;
    int wireFormat = 0;
    int artSize = 0;
    SpoolArtHdr ah;

    HeaderSize = 0;

    if (*relPath == '/') {
	path = (char *)relPath;
    } else {
	path = pathBase;
	sprintf(path, "%s/%s", PatExpand(SpoolHomePat), relPath);
    }

    if (cfd >= 0 &&
	(base = ptr = cdmap(path, off, &size, cSize, &multiArtFile)) != NULL
    ) {
	int i;
	int b;
	int inHeaders = 1;

	r = 0;

	if (DebugOpt > 1) {
	    printf("%s >> (article, %d bytes)\n", dtstamp(), size);
	}

	/*
	 * In terms of credtime, we assume that our reading
	 * of the file is instantanious.  I don't want to
	 * call gettimeofday() for each line!
	 */

	credtime(OUR_DELAY);

	artSize = size;

	bcopy(base, &ah, sizeof(ah));

	if (artSize > 24 && (uint8)ah.Magic1 == (uint8)STORE_MAGIC1 &&
				(uint8)ah.Magic2 == (uint8)STORE_MAGIC2) {
	    artSize -= ah.HeadLen;
	    ptr += ah.HeadLen;
	    wireFormat = 0;
	    if (ah.StoreType & STORETYPE_WIRE)
		wireFormat = 1; 
	} else {
	    wireFormat = 0;
	}

	if (wireFormat && !HeaderOnlyFeed && !GenLinesHeader &&
#ifdef USE_ZLIB
			!CompressOn &&
#endif
			artSize > CMDBUFFSIZE ) { 
	    if (writeLarge(cfd, ptr, artSize) < 0) {
		if (base != NULL)
		    cdunmap(base, size, multiArtFile, cSize > 0);
		return(T_FAILED);
	    } else {
		if (base != NULL)
		    cdunmap(base, size, multiArtFile, cSize > 0);
		return(0);
	    }
	}
	    
	for (i = b = 0; i < artSize; b = i) {
	    /*
	     * if first character is a '.', escape it
	     */

	    if (ptr[i] == '.' && !wireFormat)
		if (commandWrite(cfd, ".", 1, 1) < 0)
		    return(T_FAILED);

	    /*
	     * dump the article (body in a single write if wireformat).
	     */

	    if (wireFormat && !inHeaders) {
		if (commandWrite(cfd, ptr + i, artSize - i, 1) < 0)
		    return(T_FAILED);
		i = artSize;
		continue;
	    }

	    while (i < artSize && ptr[i] != '\n')
		++i;
	    if (wireFormat && i > 0 && ptr[i-1] == '\r')
		--i;
	    if (commandWrite(cfd, ptr + b, i - b, 1) < 0)
		return(T_FAILED);

	    if (inHeaders) {
		HeaderSize += i - b;

		if (i - b > 8 && strncasecmp(ptr + b, "Control:", 8) == 0)
		    wasControl = 1;
		if (i - b > 6 && strncasecmp(ptr + b, "Lines:", 6) == 0)
		    haveLines = 1;
		if (KeepBytes && i - b > 6 && strncasecmp(ptr + b, "Bytes:", 6) == 0)
		    doBytes = 0;
	    }

	    /*
	     * skip newline. if i > fsize, we hit the end of the file without
	     * a terminating LF.  We don't have to do anything
	     * since we've already terminated the last line.
	     */
	    ++i;
	    if (wireFormat && ptr[i] == '\n')
		++i;

	    /*
	     * end of headers ?  We include the blank line of the headers
	     * terminate in the output, but don't push the rest of the 
	     * article.
	     *
	     * If the article is a control message, we push the whole thing
	     */
	    if (inHeaders && (i - b == 1 || (wireFormat && i - b == 2))) {
		/*
		 * add Lines: header, if requested. note that we can only
		 * do so if we are going to otherwise discard the rest of
		 * the article (HeaderOnlyFeed and not control).
		 */
		if(HeaderOnlyFeed && GenLinesHeader &&
		   haveLines == 0 && wasControl == 0) {
		    int lineCount = 0;
		    char tmp[32];

		    while (i < artSize) {
			while (i < artSize && ptr[i] != '\n')
			    ++i;
			if (i < artSize && ptr[i] == '\n')
			    ++lineCount;
			++i;
		    }

		    snprintf(tmp, sizeof(tmp), "Lines: %d\r\n", lineCount);
		    if (commandWrite(cfd, tmp, strlen(tmp), 1) < 0)
			return(T_FAILED);
		    HeaderSize += strlen(tmp);
		}

		/*
		 * add Bytes: header, dreaderd needs it!  header feeds 
		 * *require* it, but non-header-only feeds can calculate it 
		 * themselves.
		 */
		if (HeaderOnlyFeed && doBytes) {
		    char tmp[32];
		    snprintf(tmp, sizeof(tmp), "Bytes: %d\r\n", artSize);
		    if (commandWrite(cfd, tmp, strlen(tmp), 1) < 0)
			return(T_FAILED);
		    HeaderSize += strlen(tmp);
		}
		if (commandWrite(cfd, "\r\n", 2, 1) < 0)
		    return(T_FAILED);
		inHeaders = 0;
		if (HeaderOnlyFeed && wasControl == 0)
		    break;
	    } else {
		if (commandWrite(cfd, "\r\n", 2, 1) < 0)
		    return(T_FAILED);
	    }
	}
#ifdef	USE_ZLIB
	if (CompressOn > 0)
	    flushCompressBuffer(cfd);
#endif
	credtime(THEIR_DELAY);
    } else {
	if (DebugOpt > 1)
	    printf("%s >> (file not found or other error)\n", dtstamp());
    }
    if (base != NULL)
	cdunmap(base, size, multiArtFile, cSize > 0);

    if (!wireFormat || (HeaderOnlyFeed && !wasControl))
	commandResponse(cfd, NULL, ".\r\n");

    return(r);
}
	
#ifdef NOTDEF

/*
 * Handle streaming protocol
 *
 * StreamReload() - retransmit pending stream after disconnect/reconnect
 * StreamTransact() - normal stream state machine
 */

void
StreamReload(int cfd)
{
    int i;

    for (i = 0; i < MaxStream; ++i) {
	Stream *s = &StreamAry[i];

	if (s->st_State != STATE_EMPTY) {
	    commandResponse(cfd, NULL, "check %s\r\n", s->st_MsgId);
	    s->st_State = STATE_CHECK;
	}
    }
}

#endif

int
StreamTransact(int cfd, const char *relPath, char *msgId, off_t off, int size, int cSize, int defers, char *stage, char *reason, char *buf, int *sentSize)
{
    Stream *s;
    int r = T_STREAMING;

    /*
     * Another article to stream?
     */

    if (relPath) {
	if ((s = LocateStream(msgId, 0)) != NULL) {
	    StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
	    StrnCpyNull(reason, "No in stream", MAXREASON);
	    return(T_REFUSED);
	}
	s = &StreamAry[StreamPend];
	s->st_MsgId  = zallocStr(&SysMemPool, msgId);
	s->st_RelPath   = zallocStr(&SysMemPool, relPath);
	s->st_Off = off;
	s->st_Size = size;
	s->st_CompSize = cSize;
	s->st_DumpRCode = 0;
	if (NoCheckOpt) {
	    int r;

	    commandResponse(cfd, NULL, "takethis %s\r\n", s->st_MsgId);
	    r = DumpArticle(cfd, s->st_RelPath, s->st_Off, s->st_Size,
							s->st_CompSize);
	    /* Trap: This r is NOT the r returned by StreamTransact! */
	    s->st_State = STATE_POSTED;
	    s->st_DumpRCode = r;
	    if (msgId && s->st_MsgId)
		StrnCpyNull(msgId, s->st_MsgId, 1024);
	    StrnCpyNull(stage, "takethis-nocheck", MAXREASON);
	} else {
	    commandResponse(cfd, NULL, "check %s\r\n", s->st_MsgId);
	    s->st_State = STATE_CHECK;
	}
	BytesPend += strlen(s->st_MsgId);
	++StreamPend;
	AveragePend = StreamPend * (1 - 0.98) + AveragePend * 0.98;
	if (StreamPend == NumStream) {
	    HiWaterMark = STREAMDRAIN;
	    if (DebugOpt > 1)
		printf("set watermark\n");
	}
    }

    /*
     * Should we wait for a response ?
     *
     * note: code has been written to allow for non-blocking reads in the 
     * future.  For now, we simply enforce the pipeline by ensuring the
     * receive buffer is large enough.
     *
     * check command response:	  OK_STRMCHECK, ERR_STRMCHECK, ERR_RESEND?
     * takethis command response: OK_STRMTAKE,  ERR_STRMTAKE, ERR_RESEND
     * misc:			  ERR_GOODBYE
     */

    if ((relPath == NULL && StreamPend > 0) || 
	StreamPend >= NumStream - HiWaterMark
    ){
	char *ptr = NULL;
	Stream *s = NULL;
	int delMe = 0;

	switch(commandResponse(cfd, &ptr, NULL)) {
	case ERR_RESEND:
	    if ((s = LocateStream(ptr, STATE_POSTED)) != NULL) {
		delMe = 1;
		r = T_REJECTED;	/* XXX */
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
		if (s->st_DumpRCode < 0) {
		    StrnCpyNull(reason, "Error sending article (2)", MAXREASON);
		    r = T_REJECTED;
		}
		break;
	    }
	    if (!defers && ((s = LocateStream(ptr, STATE_CHECK)) != NULL)) {
		char cst[12];
		delMe = 1;
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, "stream", MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
		r = T_DEFERIT;
		if (s->st_CompSize > 0)
		    sprintf(cst, " C%d", s->st_CompSize);
		else
		    cst[0] = 0;
		sprintf(buf,"%s %s %lld,%d %c%s", s->st_RelPath, s->st_MsgId,
			(long long)s->st_Off, s->st_Size,
			HeaderOnlyFeed ? 'H' : ' ', cst);
		break;
	    }
	    /*
	     * fall through.  Consider a resend-later response to a check,
	     * which is really a completely illegal response, to be the same
	     * as OK_STRMCHECK if we are already processing the defers
	     * and let the 'takethis' statemachine deal with it.
	     */
	case OK_STRMCHECK:
	    if ((s = LocateStream(ptr, STATE_CHECK)) != NULL) {
		int r;

		commandResponse(cfd, NULL, "takethis %s\r\n", s->st_MsgId);
		r = DumpArticle(cfd, s->st_RelPath, s->st_Off, s->st_Size,
							s->st_CompSize);
		/*
		 * Trap: This r is NOT the r returned by StreamTransact!
		 * Note that we are not waiting for a response here. The
		 * response is handled in the next call of StreamTransact.
		 */
		s->st_State = STATE_POSTED;
		s->st_DumpRCode = r;
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, (defers? "resend-takethis": "takethis"), MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
	    }
#ifdef NOTDEF
	    if (NumStream < MaxStream && ++NumStreamFrac == STREAMFRAC) {
		++NumStream;
		NumStreamFrac = 0;
	    }
#endif
	    break;
	case ERR_STRMCHECK:
	    if ((s = LocateStream(ptr, STATE_CHECK)) != NULL) {
		delMe = 1;
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
		r = T_REFUSED;
	    }
#ifdef NOTDEF
	    if (NumStream < MaxStream && ++NumStreamFrac == STREAMFRAC) {
		++NumStream;
		NumStreamFrac = 0;
	    }
#endif
	    break;
	case OK_STRMTAKE:
	    if ((s = LocateStream(ptr, STATE_POSTED)) != NULL) {
		delMe = 1;
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
		r = T_ACCEPTED;
		if (sentSize)
		    *sentSize = s->st_Size;
		if (s->st_DumpRCode < 0) {
		    if (reason)
			StrnCpyNull(reason, "Error sending article (3)", MAXREASON);
		    r = T_REJECTED;
		}
	    }
#ifdef NOTDEF
	    if (NumStream < MaxStream && ++NumStreamFrac == STREAMFRAC) {
		++NumStream;
		NumStreamFrac = 0;
	    }
#endif
	    break;
	case ERR_STRMTAKE:
	case ERR_XFERRJCT:
	    /* 
	     * NOTE: ERR_XFERRJCT is an illegal response to a streaming
	     * takethis, but some news systems return it so...
	     *
	     * But note also that we do not wait for the response of a
	     * takethis, so this may very well a perfectly valid 
	     * post-dump reject.
	     *
	     * !!! How do we get the proper state to ArtLog? We should really
	     * distinguish between a ERR_XFERRJCT as response to a CHECK and
	     * ERR_XFERRJCT as a response to TAKETHIS.
	     */
	    if ((s = LocateStream(ptr, STATE_POSTED)) != NULL) {
		delMe = 1;
		/*r = T_REFUSED; actually, this is a reject */
	        r = T_REJECTED;
		if (sentSize)
		    *sentSize = s->st_Size;
		StrnCpyNull(msgId, s->st_MsgId, 1024);
		if (stage && !*stage)
		    StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
		StrnCpyNull(reason, ptr, MAXREASON);
		if (s->st_DumpRCode < 0) {
		    if (reason)
			StrnCpyNull(reason, "Error sending article (4)", MAXREASON);
		    r = T_REJECTED;
		}
	    }
	    break;
	case ERR_GOODBYE:
	case ERR_ACCESS:
	case ERR_FAULT:
	case ERR_AUTHBAD:
	    if (stage && !*stage)
		StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
	    StrnCpyNull(reason, (ptr ? ptr : "<Unexpected EOF>"), MAXREASON);
	    r = T_FAILEDEXIT;
	    break;
	default:
	    if (stage && !*stage)
		StrnCpyNull(stage, (defers? "resend-stream": "stream"), MAXREASON);
	    StrnCpyNull(reason, (ptr ? ptr : "<Unexpected EOF>"), MAXREASON);
	    r = T_FAILED;
	    break;
	}
	if (delMe) {
	    --StreamPend;
	    BytesPend -= strlen(s->st_MsgId);
	    zfreeStr(&SysMemPool, &s->st_RelPath);
	    zfreeStr(&SysMemPool, &s->st_MsgId);
	    if (s != &StreamAry[StreamPend]) {
		*s = StreamAry[StreamPend];
		memset(&StreamAry[StreamPend], 0, sizeof(Stream));
	    } else {
		memset(s, 0, sizeof(Stream));
	    }
	}
    }
    if (DebugOpt > 1)
	printf("%s StreamTransact: return %d\n", dtstamp(), r);
    return(r);
}

/*
 * Locate id.  Applies only to active id's.  STATE_RETRY id's are
 * not (yet) active.
 */

Stream *
LocateStream(const char *msgId, int state)
{
    int i;
    int idLen;

    if (msgId == NULL)
	return(NULL);
    while (*msgId && *msgId != '<')
	++msgId;
    for (idLen = 0; msgId[idLen] && msgId[idLen] != '>'; ++idLen)
	;
    if (msgId[idLen] == '>')
	++idLen;

    if (DebugOpt > 1)
	printf("%s MsgId(%*.*s,%d):", dtstamp(), idLen, idLen, msgId, state);

    for (i = 0; i < MaxStream; ++i) {
	Stream *s = &StreamAry[i];

	if (s->st_State == STATE_RETRY)
	    continue;

	if ((state == 0 || s->st_State == state) && 
	    s->st_State &&
	    strlen(s->st_MsgId) == idLen &&
	    strncmp(msgId, s->st_MsgId, idLen) == 0
	) {
	    if (DebugOpt > 1)
		printf(" found slot %d\n", i);
	    return(s);
	}
    }
    if (DebugOpt > 1)
	printf(" not found\n");
    return(NULL);
}

/*
 * Refile pending streams.  If we have an output file, the
 * pending streams are refiled to the file.  If we do not,
 * we leave them in the stream array and mark them for RETRY.
 * This will cause them to be regenerated as file input
 * later.
 *
 * It is possible for a previous Refile to mark as retry what
 * we now wish to write to a file.
 */

int
RefilePendingStreams(FILE *fo)
{
    int i;
    int n = 0;

    for (i = 0; i < MaxStream; ++i) {
	Stream *s = &StreamAry[i];

	if (s->st_State != STATE_EMPTY) {
	    /*
	     * If an output file is available, drain pending/retry entries
	     * to it and clear the stream entirely.  Otherwise change all
	     * entries to STREAM_RETRY.
	     */

	    if (fo) {
		if (s->st_Off || s->st_Size) {
		    fprintf(fo, "%s\t%s\t%d,%d\n", 
			s->st_RelPath, 
			s->st_MsgId,
			(int)s->st_Off, 
			(int)s->st_Size
		    );
		} else {
		    fprintf(fo, "%s\t%s\n", s->st_RelPath, s->st_MsgId);
		}
		zfreeStr(&SysMemPool, &s->st_MsgId);
		zfreeStr(&SysMemPool, &s->st_RelPath);
		if (s->st_State != STATE_RETRY) {
		    --StreamPend;
		} else {
		    --StreamRetry;
		}
		memset(s, 0, sizeof(Stream));
	    } else {
		if (s->st_State != STATE_RETRY) {
		    s->st_State = STATE_RETRY;
		    ++StreamRetry;
		    --StreamPend;
		}
	    }
	    ++n;
	}
    }
    if (StreamPend != 0) {
	dl_logit("stream array corrupt");
	StreamPend = 0;
    }
    BytesPend = 0;
    return(n);
}

/*
 * Misc subroutines
 */

void 
dl_logit(const char *ctl, ...)
{
    va_list va;
    char buf[1024];

    sprintf(buf, "%s:%s ", 
	((HostName) ? HostName : "<unknown>"),
	((CurrentBatchFile) ? CurrentBatchFile : "<unknown>")
    );
    va_start(va, ctl);
    vsprintf(buf + strlen(buf), ctl, va);
    va_end(va);
    logit(LOG_NOTICE, "%s", buf);
    if (DebugOpt > 0)
	fprintf(stderr, "%s newslink[%ld] %s\n", LogTime(), (long)getpid(), buf);
    if (DebugOpt > 1)
	printf("%s %s", dtstamp(), buf);
}


void 
logStats(const char *description)
{
    time_t t = time(NULL);
    int secs = t - Stats->SentStats.DeltaStart;

    /*
     * If we got something through, cut off LastErrBuf
     */

    if (Stats->SentStats.OfferedCnt)
	LastErrBuf[9] = 0;

    credtime(OUR_DELAY);

    /*
     * Log!
     */

    dl_logit("%s secs=%-4d acc=%-4d dup=%-4d rej=%-4d tot=%-4d bytes=%-4.0f (%d/min"
#ifdef CREDTIME
    " %d/%d mS" 
#endif
    ") avpend=%-4.1f %s",
	description,
	secs,
	Stats->SentStats.AcceptedCnt,
	Stats->SentStats.RefusedCnt,
	Stats->SentStats.RejectedCnt,
	Stats->SentStats.OfferedCnt,
	Stats->SentStats.AcceptedBytes,
	((secs) ? Stats->SentStats.OfferedCnt * 60 / secs : 0),
#ifdef CREDTIME
	((MsCount) ? OurMs / MsCount : -1),
	((MsCount) ?  TheirMs / MsCount : -1),
#endif
	AveragePend,
	LastErrBuf
    );
    if (Stats->SentStats.DeferredCnt > 0 || Stats->SentStats.DeferredFailCnt > 0)
	dl_logit("%s secs=%-4d defer=%-4d deferfail=%-4d",
	    description,
	    secs,
	    Stats->SentStats.DeferredCnt,
	    Stats->SentStats.DeferredFailCnt
	);
#ifdef	USE_ZLIB
    if (CompressOn > 0)
	dl_logit("%s compbytes=%ld decompbytes=%ld (%.2f%% compression)\n",
	    description,
	    CompressedBytes,
	    RawBytes,
	    100 - (((float)CompressedBytes / (float)RawBytes) * 100)
	);
#endif

    Stats->SentStats.DeltaStart = t;
    Stats->SentStats.OfferedCnt = 0;
    Stats->SentStats.AcceptedCnt = 0;
    Stats->SentStats.RefusedCnt = 0;
    Stats->SentStats.RejectedCnt = 0;
    Stats->SentStats.DeferredCnt = 0;
    Stats->SentStats.DeferredFailCnt = 0;
    Stats->SentStats.RejectedBytes = 0.0;
    Stats->SentStats.AcceptedBytes = 0.0;

#ifdef	USE_ZLIB
    RawBytesTotal+=RawBytes;
    CompressedBytesTotal+=CompressedBytes;
    RawBytes=0;
    CompressedBytes=0;
#endif

    MsCountTotal += MsCount;
    OurMsTotal += OurMs;
    TheirMsTotal += TheirMs;

    MsCount = 0;
    OurMs = 0;
    TheirMs = 0;
}

/*
 * commandResponse() optionally send command, optionally read a response
 */

static char Buf[16384];
static int Bufb;
static int Bufe;
static char Cmd[CMDBUFFSIZE];
static int Cmde;

void
clearResponseBuf(void)
{
    Bufb = Bufe = 0;
}

void
clearCommandBuffer(void)
{
    Cmde = 0;
}

#ifdef NOTDEF

void
copyCommandBuffer(FILE *fo)
{
    if (Cmde) {
	fwrite(Cmd, 1, Cmde, fo);
	Cmde = 0;
    }
}

#endif

void
flushCompressBuffer(int cfd)
{
#ifdef	USE_ZLIB
    if (CompressBufInPos > 0) {
	Z_Strm.next_in = CompressBufIn;
	Z_Strm.avail_in = CompressBufInPos;
	RawBytes += CompressBufInPos;
	while (Z_Strm.avail_in) {
	    Z_Strm.next_out = CompressBufOut;
	    Z_Strm.avail_out = sizeof(CompressBufOut);
	    if (deflate(&Z_Strm, Z_SYNC_FLUSH) != Z_OK)
		logit(LOG_ERR, "deflate error: %s", Z_Strm.msg);
	    commandWrite(cfd, CompressBufOut,
				(sizeof(CompressBufOut) - Z_Strm.avail_out), 2);
	    CompressedBytes += (sizeof(CompressBufOut) - Z_Strm.avail_out);
	}
	CompressBufInPos = 0;
    }   
#endif
}

int
flushCommandBuffer(int cfd)
{
    int b = 0;

    if (WritesLosing) {
	Cmde = 0;
	return(-1);
    }

    while (b != Cmde) {
	int r = 0;

	credtime(OUR_DELAY);

	errno = 0;
	r = write(cfd, Cmd + b, Cmde - b);
	if (r < 0) {
	    if (errno != EAGAIN && errno != EINTR &&
		errno != EWOULDBLOCK && errno != EINPROGRESS) {
		    Cmde = 0;
		    return(-1);
	    }
	    r = 0;
	}
	b += r;

#if USE_POLL
	if (b != Cmde) {
	    struct pollfd pfd = { 0 };
	    struct linger l;

	    pfd.fd = cfd;
	    pfd.events = POLLOUT;
	    if (poll(&pfd, 1, Timeout * 1000) == 0) {
		dl_logit("write timeout");
		WritesLosing = 1;
		l.l_onoff = 1;
		l.l_linger = 0;
		setsockopt(cfd, SOL_SOCKET, SO_LINGER, (void*)&l, sizeof(l));
		TimeCounter = 100000;
		break;
	    }
	}
#endif

	credtime(THEIR_DELAY);
    }

    Cmde = 0;
    return(0);
}

int
writeLarge(int cfd, char *buffer, size_t size)
{
    struct iovec tmpvec[3]; /* structures to store writev info */

    int status;	
    size_t offset, bytes=0;
    int iovleft=0,	/* iov's to handle */
	i;		/* first iov in buffer that has data left */

    if (WritesLosing) {
	Cmde = 0;
	return(-1);
    }

    if (Cmde > 0) { /* if there is something to write out in Cmd buffer */ 
        tmpvec[iovleft].iov_base=Cmd;
        tmpvec[iovleft].iov_len=Cmde;
        iovleft++;
        bytes += Cmde;
	Cmde=0;
    }
    tmpvec[iovleft].iov_base=buffer;
    tmpvec[iovleft].iov_len=size;
    iovleft++;

    bytes += size;

    i = 0;
    do {
        /* Write out what's left and return success if it's all written. */
	credtime(OUR_DELAY);
	errno = 0;
	status = writev(cfd, tmpvec + i, iovleft);
	if (status < 0) {
	    if (errno != EAGAIN && errno != EINTR &&
		errno != EWOULDBLOCK && errno != EINPROGRESS) {
		    return(-1);
	    }
	    status = 0; offset=0;
	} else {
            offset = status;
            bytes -= offset;
        }
#if USE_POLL
	if (bytes > 0) {
	    struct pollfd pfd = { 0 };
	    struct linger l;

	    pfd.fd = cfd;
	    pfd.events = POLLOUT;
	    if (poll(&pfd, 1, Timeout * 1000) == 0) {
		dl_logit("write timeout");
		WritesLosing = 1;
		l.l_onoff = 1;
		l.l_linger = 0;
		setsockopt(cfd, SOL_SOCKET, SO_LINGER, (void*)&l, sizeof(l));
		TimeCounter = 100000;
		break;
	    }
	}
#endif
	credtime(THEIR_DELAY);

        /* Skip full iovecs */
        for (; offset >= (size_t) tmpvec[i].iov_len && iovleft > 0; i++) {
            offset -= tmpvec[i].iov_len;
            iovleft--;
        }
	if (offset > 0) {
	    tmpvec[i].iov_base = (char *) tmpvec[i].iov_base + offset;
	    tmpvec[i].iov_len -= offset;
	}

    } while (iovleft > 0 && (status >= 0 || errno == EINTR));

    assert(bytes == 0);

    return status;
}

int
commandWrite(int cfd, const void *buf, int bytes, int artdata)
{
    if (CompressOn > 0 && artdata != 2) {
#ifdef	USE_ZLIB
	while (bytes > 0) {
	    CompressBufIn[CompressBufInPos++] = *(char *)buf++;
	    bytes--;
	    if (CompressBufInPos > 8000)
		flushCompressBuffer(cfd);
	}   
	if (!artdata)
	    flushCompressBuffer(cfd);
#endif
    } else {
	while (bytes > 0) {
	    int n = sizeof(Cmd) - Cmde;
	    if (n == 0) {
		if (flushCommandBuffer(cfd) < 0)
		    return(-1);
		n = sizeof(Cmd);
	    }
	    if (n > bytes)
		n = bytes;
	    bcopy(buf, Cmd + Cmde, n);
	    Cmde += n;
	    bytes -= n;
	    buf = (const char *)buf + n;
	}
    }
    return(0);
}

int
commandResponse(int cfd, char **rptr, const char *ctl, ...)
{
    va_list va;
    int r = ERR_UNKNOWN;
    int n;

    if (ctl) {
	char tmp[4096];

	va_start(va, ctl);
	vsnprintf(tmp, sizeof(tmp), ctl, va);
	va_end(va);

	/*
	 * copy to outgoing Cmd buffer
	 */

	if (commandWrite(cfd, tmp, strlen(tmp), 0) < 0)
	    return(ERR_FAULT);

	if (DebugOpt > 1) {
	    int i;

	    printf("%s >> ", dtstamp());
	    for (i = 0; tmp[i]; ++i) {
		if (isprint((int)tmp[i]))
		    printf("%c", tmp[i]);
		else
		    printf("[%02x]", tmp[i]);
	    }
	    puts("");
	}
    }
    if (rptr) {
#if USE_POLL == 0
	int alarmSet = 0;
#endif

	/*
	 * flush output that has built up.  It's important to try to
	 * do this in one write() because TCP_NODELAY has been set.
	 */

	flushCompressBuffer(cfd);
	flushCommandBuffer(cfd);

	*rptr = NULL;

	/*
	 * Find next complete line in buffer, read more data if
	 * necessary.
	 */

	n = 0;

	credtime(OUR_DELAY);

	do {
	    int i;

	    credtime(THEIR_DELAY);
	    Bufe += n;
	    for (i = Bufb; i < Bufe; ++i) {
		if (Buf[i] == '\n') {
		    Buf[i] = 0;
		    if (i != Bufb && Buf[i-1] == '\r')
			Buf[i-1] = 0;
		    *rptr = Buf + Bufb;
		    Bufb = i + 1;
		    r = strtol(*rptr, NULL, 10);
		    break;
		}
	    }
	    if (i != Bufe)
		break;
	    /*
	     * We may have run out of space in the
	     * buffer, try to left-justify the data
	     * to make more room.  If we can't, the line
	     * is just too long.
	     */
	    if (Bufe == sizeof(Buf) - 1) {
		if (Bufb == 0)
		    break;
		memcpy(Buf, Buf + Bufb, Bufe - Bufb);
		Bufe -= Bufb;
		Bufb = 0;
	    }
	    credtime(OUR_DELAY);

	    n = 0;

	    if (WritesLosing)
		break;

	    do {
		if (Timeout > 0) {
#if USE_POLL
		    struct pollfd pfd = { 0 };

		    pfd.fd = cfd;
		    pfd.events = POLLIN;
		    if (poll(&pfd, 1, Timeout * 1000) == 0) {
			TimeCounter = 100000;
			break;
		    }
#else
		    /*
		     * set an alarm.  alarm() is really inefficient
		     * syscallwise, so we only do it when we really need
		     * to read().
		     */
		    alarm(Timeout);
		    alarmSet = 1;
#endif
		}
		n = read(cfd, Buf + Bufe, sizeof(Buf) - Bufe - 1);
	    } while (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
	} while (n > 0);

	credtime(THEIR_DELAY);
	++MsCount;

	/*
	 * turn off alarm, only if we previously
	 * turned it on.
	 */
#if USE_POLL == 0
	if (alarmSet)
	    alarm(0);
#endif

	if (DebugOpt > 1)
	    printf("%s << %s\n", dtstamp(), (*rptr) ? *rptr : "<interrupt/error>");

	if (*rptr) {
	    strncpy(LastErrBuf, *rptr, 32);
	    LastErrBuf[32] = 0;
	} else {
	    strcpy(LastErrBuf, "<interrupt/error>");
	}
    } else {
	r = 0;
    }
    return(r);
}

void
sigTerm(int sigNo)
{
    TermFlag = 1;
    if (sigNo == SIGALRM) {
	if (KillFd >= 0)
	    close(KillFd);
    }
}

#ifdef CREDTIME

void
credtime(int whos)
{
    struct timeval tv;
    static struct timeval Tv;

    gettimeofday(&tv, NULL);

    if (whos) {
	int ms = (tv.tv_usec + 1000000 - Tv.tv_usec) / 1000 +
	     (tv.tv_sec - Tv.tv_sec - 1) * 1000;

	switch(whos) {
	case OUR_DELAY:
	    OurMs += ms;
	    break;
	case THEIR_DELAY:
	    TheirMs += ms;
	    break;
	}
    }
    Tv = tv;
}

void
credreset(void)
{
    OurMs = TheirMs = 0;
}

#endif

typedef struct {
    char rb_Buf[MAXCLINE-sizeof(int)*3];
    int  rb_Base;
    int  rb_Index;
    int	 rb_Len;
} RBlock;

RBlock *RBlockAry[MAXFILEDES];
MemPool	*BMemPool;

void
readreset(int fd)
{
    RBlock *rbs;

    if (fd < 0 || fd >= MAXFILEDES) {
	dl_logit("readreset: bad fd: %d", fd);
	return;
    }
    if ((rbs = RBlockAry[fd]) != NULL) {
	zfree(&BMemPool, rbs, sizeof(RBlock));
	RBlockAry[fd] = NULL;
    }
}

/*
 * readretry() - read 
 *
 */

int
readretry(char *buf, int size)
{
    int inval = 1;

    if (StreamRetry) {
	int i;

	for (i = 0; i < MaxStream; ++i) {
	    Stream *s = &StreamAry[i];

	    if (s->st_State == STATE_RETRY) {
		if (s->st_Off || s->st_Size) {
		    sprintf(buf, 
			"%s\t%s\t%lld,%d",
			s->st_RelPath, 
			s->st_MsgId, 
			(long long)s->st_Off,
			s->st_Size
		    );
		} else {
		    sprintf(buf, "%s\t%s", s->st_RelPath, s->st_MsgId);
		}
		memset(s, 0, sizeof(Stream));
		inval = 0;
		--StreamRetry;
		if (DebugOpt > 1)
		    printf("%s (retry from stream): %s\n", dtstamp(), buf);
		break;
	    }
	}
    }
    return(inval);
}

/*
 * readline() - read next line from input.  This routine may block if we 
 *		are in realtime mode (-r), but only if StreamPend == 0
 */

int
readline(int fd, char *buf, int size, time_t t)
{
    RBlock *rbs;
    int statCounter = 0;
    int loopCounter = 0;
    static int usleeptime = 100000;

    if (fd < 0 || fd >= MAXFILEDES) {
	dl_logit("readline: bad fd: %d", fd);
	return(-1);
    }
    if ((rbs = RBlockAry[fd]) == NULL) {
	rbs = zalloc(&BMemPool, sizeof(RBlock));
	RBlockAry[fd] = rbs;
    }
    
    /*
     * Ensure there is enough room for a full line
     */

    if (rbs->rb_Base > 0 && rbs->rb_Len > MAXCLINE / 2) {
	memmove(rbs->rb_Buf, rbs->rb_Buf + rbs->rb_Base, rbs->rb_Len - rbs->rb_Base);
	rbs->rb_Len -= rbs->rb_Base;
	rbs->rb_Index -= rbs->rb_Base;
	rbs->rb_Base = 0;
    }

    for (;;) {
	int n;

	/*
	 * Look for newline
	 */

	for (n = rbs->rb_Index; n < rbs->rb_Len; ++n) {
	    if (rbs->rb_Buf[n] == '\n') {
		int s = (++n) - rbs->rb_Base;

		if (s >= size)
		    s = size - 1;
		memmove(buf, rbs->rb_Buf + rbs->rb_Base, s);
		buf[s] = 0;

		if (n == rbs->rb_Len) {
		    rbs->rb_Base = 0;
		    rbs->rb_Index = 0;
		    rbs->rb_Len = 0;
		} else {
		    rbs->rb_Base = n;
		    rbs->rb_Index = n;
		}
		if (*buf != '#')
		    return(0);
	    }
	}
	rbs->rb_Index = n;

	/*
	 * None found, read and loop
	 */

	if (rbs->rb_Len == sizeof(rbs->rb_Buf)) {
	    dl_logit("Line too long in batchfile!");
	    return(-1);
	}
	n = read(fd, rbs->rb_Buf + rbs->rb_Len, sizeof(rbs->rb_Buf) - rbs->rb_Len);
	if (n <= 0) {
	    struct stat st;

	    /*
	     * If a read error occurs or we are not in realtime mode or the
	     * termination flag has been set, break out.  If we ARE in realtime
	     * mode and StreamPend is not 0, breakout.  But if StreamPend
	     * is 0 and we are in realtime mode, we block.
	     */

	    if (n < 0 || RealTimeOpt == 0 || StreamPend != 0 || TermFlag)
		break;

	    /*
	     * If we don't have any articles in realtime mode, flush the
	     * article file cache occasionally to make sure that deleted
	     * files are removed.
	     */
	    if (loopCounter++ > 60)
		cdflush();

	    /*
	     * sleep and retry the read.  Effectively a tail -f.  When the file
	     * is renamed AND a new realtime batchfile exists, we switch to the
	     * new realtime batch and attempt to remove the renamed one that
	     * we still have a lock on.  This only works if the batch file
	     * is named after the label / sequence file.
	     *
	     * If the file is renamed, we stay with the file until a new 
	     * realtime batch is available.  
	     */
	    {
		if (statCounter == 0 &&
				stat(CurrentBatchFile,&st) == 0 &&
				st.st_ino != CurSt.st_ino) {
		    /*
		     * do not attempt to remove the batchfile if we would have
		     * had to refile some of our entries.
		     */
		    if (WouldHaveRefiled == 0)
			AttemptRemoveRenamedFile(&CurSt, BatchFileCtl);
		    break;
		}
	    }

	    /*
	     * RealTimeOpt:  -1 for 'fast sleep' or 'poll' (if supported).
	     *	note: there is a race condition in poll when used on files
	     *  in FreeBSD, so we set the timeout to 1 second.
	     *
	     * RealTimeOpt: > 0 for sleep period in seconds.
	     */
	    if (NotifyOpt != NULL && ListenNotify(&NotifyFd, &NotifyLockFd, NotifyOpt)) {
		fd_set rfds;
		struct timeval tv = { 0, 0 };
		char buf;
		int res;
		int i = 0;

		if (LastNotify + 60 < LastFeedData) {
		    RegisterNotify(NotifyOpt, 1);
		    LastNotify = t;
		}
		FD_ZERO(&rfds);
		FD_SET(NotifyFd, &rfds);
		tv.tv_usec = 300000;
		if ((res = select(NotifyFd + 1, &rfds, NULL, NULL, &tv)) == 0)
		    TimeCounter += 40;
		else
		    TimeCounter += 10;
		while (res > 0 && (res = read(NotifyFd, &buf, 1)) == 1 &&
								i++ < 100) {
		    if (res > 0) {
			LastNotify = t;
		    } else if (res == -1 && errno != EAGAIN) {
			RegisterNotify(NotifyOpt, 0);
			CloseNotify(&NotifyFd, &NotifyLockFd, NotifyOpt);
			LastNotify = 0;
		    }
		}
		statCounter = (statCounter + 1) % 5;
	    } else if (RealTimeOpt < 0) {
#if USE_POLL && defined(POLLEXTEND) && USE_POLLEXTEND
	        struct pollfd pfd = { 0 };
		int r;

		pfd.fd = fd;
		pfd.events = POLLEXTEND;

		errno = 0;
		if ((r = poll(&pfd, 1, 100)) < 0)
		    dl_logit("poll() failed: %s", strerror(errno));
		statCounter = (statCounter + 1) % 50;
		TimeCounter += 10;
#else
#if HAS_USLEEP
		usleep(usleeptime);
		statCounter = (statCounter + 1) % 50;
		TimeCounter += usleeptime / 10000 + 10;
#else
		sleep(1);		/* 1s  */
		statCounter = (statCounter + 1) % 5;
		TimeCounter += 100;
#endif
#endif
	    } else {
		sleep(RealTimeOpt);
		statCounter = (statCounter + 1) % 5;
		TimeCounter += 100;
	    }
	    usleeptime = usleeptime + usleeptime / 2;
	    if (usleeptime > 1000000)
		usleeptime = 1000000;
	} else {
	    LastFeedData = t;
	    rbs->rb_Len += n;
	    usleeptime = usleeptime - usleeptime / 2;
	    if (usleeptime < 100)
		usleeptime = 100;
	}
    }
    return(-1);
}

int
ValidMsgId(char *msgid)
{
    int i;
    int l = strlen(msgid);

    if (l >= MAXMSGIDLEN)
	return(0);
    if (msgid[0] == '<') {
	for (i = 1; i < l; ++i) {
	    if (msgid[i] == ' ' || msgid[i] == '\t')
		break;
	    if (msgid[i] == '>') {
		if (msgid[i+1] == 0)
		    return(1);
		break;
	    }
	}
    }
    return(0);
}

void
AttemptRemoveRenamedFile(struct stat *st, const char *spoolFile)
{
    char buf[256];
    int fd;
    int begSeq = -1;

    snprintf(buf, sizeof(buf), "%s/.%s.seq", PatExpand(DQueueHomePat), spoolFile);
    if ((fd = open(buf, O_RDONLY)) >= 0) {
	int n;
	if ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
	    buf[n] = 0;
	    sscanf(buf, "%d", &begSeq);
	}
	close(fd);
    }

    /*
     * Attempt to locate the renamed file.  It is no big deal if we can't
     * find it.  We still have our lock on the file, so we can safely remove
     * it.
     */

    if (begSeq >= 0) {
	int n;

	for (n = 3; n >= 0; --n, ++begSeq) {
	    struct stat nst;

	    snprintf(buf, sizeof(buf), "%s/%s.S%05d", PatExpand(DQueueHomePat), spoolFile, begSeq);
	    if (stat(buf, &nst) == 0 && st->st_ino == nst.st_ino) {
		remove(buf);
		break;
	    }
	}
    }
}

/*
 * CDMAP() - map part of a file.  If off & *psize are 0, we map the whole
 *	     file.
 */

#define MAXMCACHE	32

typedef struct XCache {
    char	*mc_Path;
    int		mc_Fd;
    int		mc_Size;	/* only if off/psize are not known */
    time_t	mc_OpenTime;
} XCache;

XCache	XCacheAry[MAXMCACHE];

void
cdinit(void)
{
    int i;
    bzero(&XCacheAry, sizeof(XCacheAry));
    for (i = 0; i < MAXMCACHE; ++i) {
	XCacheAry[i].mc_Path  = NULL;
	XCacheAry[i].mc_Fd  = -1;
    }
}

char *
cdmap(const char *path, off_t off, int *psize, int cSize, int *multiArtFile)
{
    int i;
    XCache *mc;
    char *ptr = NULL;

    *multiArtFile = 0;
    if (*psize || off) {
	*multiArtFile = 1;
    }

    for (i = 0; i < MAXMCACHE; ++i) {
	if (XCacheAry[i].mc_Path == NULL)
	    break;
	if (strcmp(path, XCacheAry[i].mc_Path) == 0)
	    break;
    }
    if (i == MAXMCACHE) {
	i = random() % MAXMCACHE;

	mc = &XCacheAry[i];
	close(mc->mc_Fd);
	zfreeStr(&SysMemPool, &mc->mc_Path);
	mc->mc_Fd = -1;
	mc->mc_Size = 0;
	mc->mc_OpenTime = 0;
    }
    mc = &XCacheAry[i];

    if (mc->mc_Path == NULL) {
	if ((mc->mc_Fd = cdopen(path, O_RDONLY, 0)) >= 0) {
	    struct stat st;

	    st.st_size = 0;
	    fstat(mc->mc_Fd, &st);
	    mc->mc_Size = st.st_size;
	    mc->mc_Path = zallocStr(&SysMemPool, path);
	    mc->mc_OpenTime = time(NULL);
	}
    }

    /*
     * When mapping multi-article files, a \0 terminator must also be mapped.
     * There is no \0 terminator for regular files.
     */

    if (mc->mc_Fd >= 0) {
	if (!*multiArtFile)
	    *psize = mc->mc_Size;

	if (cSize > 0) {
#ifdef USE_ZLIB
	    char *p;
	    gzFile *gzf;
	    SpoolArtHdr tah = { 0 };

	    lseek(mc->mc_Fd, off, 0);
	    if (read(mc->mc_Fd, &tah, sizeof(tah)) != sizeof(tah))
		return(NULL);
	    if ((uint8)tah.Magic1 != (uint8)STORE_MAGIC1 &&
				(uint8)tah.Magic2 != (uint8)STORE_MAGIC2) {
		lseek(mc->mc_Fd, off, 0);
		tah.Magic1 = STORE_MAGIC1;
		tah.Magic2 = STORE_MAGIC2;
		tah.HeadLen = sizeof(tah);
		tah.ArtLen = *psize;
		tah.ArtHdrLen = *psize;
		tah.StoreLen = *psize;
		tah.StoreType = STORETYPE_TEXT;
		*multiArtFile = 1;
	    }
	    gzf = gzdopen(dup(mc->mc_Fd), "r");
	    if (gzf == NULL)
		return(NULL);

	    ptr = (char *)malloc(tah.ArtLen + tah.HeadLen + 2);
	    if (ptr == NULL) {
		logit(LOG_CRIT, "Unable to malloc %d bytes for article (%s)\n",
				tah.ArtLen + tah.HeadLen + 2, strerror(errno));
		gzclose(gzf);
		return(NULL);
	    }
	    p = ptr;
	    bcopy(&tah, p, tah.HeadLen);
	    p += tah.HeadLen;
	    if (gzread(gzf, p, tah.ArtLen) != tah.ArtLen) {
		free(ptr);
		return(NULL);
	    }
	    p[tah.ArtLen] = 0;
	    *psize = tah.ArtLen + tah.HeadLen;
	    gzclose(gzf);
#else
	    logit(LOG_CRIT, "Queue batch file indicates compressed file and compression not enabled");
#endif
	} else {
	    ptr = xmap(NULL, *psize + *multiArtFile, PROT_READ, MAP_SHARED, mc->mc_Fd, off);
	    if (ptr == NULL)
		return(ptr);
	    if (ptr && HeaderOnlyFeed==0) {
		if (DOpts.FeederPreloadArt)
		    xadvise(ptr, *psize, XADV_WILLNEED);
		else
		    xadvise(ptr, *psize, XADV_SEQUENTIAL);
	    }
	    if (*multiArtFile && ptr && ptr[*psize] != 0) {
		logit(LOG_CRIT, "article batch corrupted: %s @ %lld,%ld", path, off, *psize);
		xunmap(ptr, *psize + *multiArtFile);
		ptr = NULL;
	    }
	}
    }
    return(ptr);
}

void
cdunmap(char *ptr, int bytes, int multiArtFile, int compressed)
{
    if (compressed)
	free(ptr);
    else
	xunmap((caddr_t)ptr, bytes + multiArtFile);
}

/*
 * Flush the open article cache every 10 minutes to ensure that we
 * don't keep spool files open too long
 */
void
cdflush(void)
{
    int i;
    time_t t = time(NULL);
    for (i = 0; i < MAXMCACHE; ++i) {
	if (XCacheAry[i].mc_Fd >= 0 &&
			(t - XCacheAry[i].mc_OpenTime) > CACHEFLUSHTIME) {
	close(XCacheAry[i].mc_Fd);
	zfreeStr(&SysMemPool, &XCacheAry[i].mc_Path);
	XCacheAry[i].mc_Fd = -1;
	XCacheAry[i].mc_Size = 0;
	XCacheAry[i].mc_OpenTime = 0;
	}
    }
}

void
doArtLog(const char *class, char *msgid, char *message, const char *stage, const char *reason)
{
    if (strstr(ArtLog, "all") || strstr(ArtLog, class)) {
	/* The strstr is only an approximation, but the classes are
	 * sufficiently distinct for this to work.
	 */
	if (Logfd == NULL) {
	    char logfname[PATH_MAX];
	    sprintf(logfname, "%s/feedlog.%s", PatExpand(LogHomePat), HostName);
	    Logfd = fopen(logfname, "a");
	    if (Logfd == NULL)
	        return;
        }
    	fprintf(Logfd, "%s [%ld]: %s %s %s %s\n",
		LogTime(), (long)getpid(),
		msgid, message, stage? stage: "-", reason? reason: "-");
    	fflush(Logfd);
    }
}

#define ISWHITE(c)	((c)=='\n' || (c) == '\r' || (c) == ' ' || (c) == '\t')

/*
 * Extract a line from the dqueue file
 * Return 0 for success and -1 for failure
 */
int
extractFeedLine(const char *buf, char *relPath, char *msgId, off_t *poff, int *psize, int *pheadOnly, time_t *queuedTime, int *cSize)
{
    const char *s = buf;
    const char *b;
    int l1 = 0;
    int l2 = 0;

    if (*buf == '#')
	return(-1);

    *pheadOnly = 0;

    /*
     * RELPATH
     */
    for (b = s; *s && !ISWHITE(*s); ++s)
	;
    l1 = s - b;
    bcopy(b, relPath, l1);
    relPath[l1] = 0;

    while (ISWHITE(*s))
	++s;

    /*
     * MSGID
     */

    if (*s == '<') {
	for (b = s; *s && *s != '>'; ++s)
	    ;
	if (*s == '>') {
	    ++s;
	    l2 = s - b;
	    bcopy(b, msgId, l2);
	    msgId[l2] = 0;
	}
	while (ISWHITE(*s))
	    ++s;
    }

    /*
     * offset/size [flags]
     */

    b = s;
    if (isdigit((int)*s)) {
	long long offtmp;
	sscanf(s, "%lld,%d", &offtmp, psize);
	*poff = offtmp;
    }
    while ((s = strchr(s, ' ')) != NULL) {
	while (ISWHITE(*s))
	    ++s;
	switch (*s++) {
	    case 'D' :
		*queuedTime = atol(s);
		break;
	    case 'C' :
		*cSize = atol(s);
		break;
	    case 'H' :
		*pheadOnly = 1;
		break;
	}
    }

    if (l1 && l2 && ValidMsgId(msgId))
	return(0);
    return(-1);
}

const char *
dtstamp(void)
{
    struct timeval tv;
    static struct timeval stv;
    static char buf[64];
    int dt = 0;

    gettimeofday(&tv, NULL);
    if (stv.tv_sec) {
	dt = (tv.tv_sec - stv.tv_sec - 1) * 1000 + (tv.tv_usec + 1000000 - stv.tv_usec) / 1000;
    }
    stv = tv;
    sprintf(buf, "%4d.%03d ", dt / 1000, dt % 1000);
    return(buf);
}

