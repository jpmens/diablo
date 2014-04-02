
/*
 * DIABLO.C	INTERNET NEWS TRANSIT AGENT, By Matthew Dillon
 *
 * Diablo implements the news transfer portion of the INN command
 * set.  Basically ihave, check, takethis, mode stream, head, and stat.
 * Not much more.  The purpose is to transfer news as quickly and
 * as efficiently as possible.
 *
 * Diablo is a forking server.  It supports both standard non-streaming
 * ihave feeds and streaming check/takethis feeds.  In fact, it supports
 * streaming on its entire command set if you wish to use it that way.
 * It forks on every connection and is able to do history file lookups
 * and article reception in parallel.  History file updates use minimal
 * locking and take advantage of the flexibility of INN's return codes.
 *
 * An active file is not required, and processing for things such as a 
 * newsgroups file and Control messages is left for the dreaderd reader
 * process to handle.  Diablo itself is strictly a backbone 
 * redistribution/transit agent.
 *
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Modified 12/4/1997 to include support for compressed data streams.
 * Modifications (c) 1997, Christopher M Sedore, All Rights Reserved.
 * Modifications may be distributed freely as long as this copyright
 * notice is included without modification.
 */

#include "defs.h"

#if NEED_TERMIOS
#include <sys/termios.h>
#endif

typedef struct Feed {
    struct Feed *fe_Next;
    char	*fe_Label;
    int		fe_Fd;
    int		fe_NotifyFd;
    int		fe_Delayed;
    char	*fe_Buf;
    int		fe_BufIdx;
    int		fe_BufMax;
    int		fe_Failed;
} Feed;

typedef struct Retain {
    struct Retain *re_Next;
    FILE	*re_Fi;
    FILE	*re_Fo;
    int		re_What;
} Retain;

typedef struct Track {
    pid_t	tr_Pid;
    char	addr[64];
} Track;

#define RET_CLOSE	1
#define RET_PAUSE	2
#define RET_LOCK	3

#define REJMSGSIZE	1024

#define MINARTSIZE      80   /* Reject articles smaller than this. This is a conservative estimate */

void DiabloServer(int passedfd);
void DoAccept(int lfd);
void DoPipe(int fd);
void DoSession(int fd, int count);
void LogSession(void);
void LogSession2(void);
void DoCommand(int ufd);
void DoFeedNotify(FILE *fo, char *info);
void DoListNotify(FILE *fo, char *l);
void DoStats(FILE *fo, int dt, int raw);
int LoadArticle(Buffer *bi, const char *msgid, int noWrite, int headerOnly, char *refBuf, char *artType);
int SendArticle(const char *data, int fsize, FILE *fo, int doHead, int doBody);
void ArticleFileInit(void);
#ifdef USE_ZLIB
int ArticleFile(History *h, off_t *pbpos, int clvl, gzFile **cfile);
#else
int ArticleFile(History *h, off_t *pbpos, int clvl, char **cfile);
#endif
void ArticleFileCloseAll(void);
void ArticleFileCacheFlush(time_t t);
void ArticleFileClose(int i);
void ArticleFileTrunc(int artFd, off_t bpos);
void ArticleFileSetSize(int artFd);
void ngAddControl(char *nglist, int ngSize, const char *ctl);
void writePath(time_t t, char *path);

void writeFeedOut(const char *label, const char *file, const char *msgid, const char *offSize, int rt, int headOnly, const char *artType, const char *cSize);
void flushFeeds(int justClose);
void flushFeedOut(Feed *fe);

void FeedRSet(FILE *fo);
void FeedList(FILE *fo);
void FeedCommit(FILE *fo);
void FeedAddDel(FILE *fo, char *gwild, int add);

void FinishRetain(int what);
int QueueRange(const char *label, int *pqnum, int *pqarts, int *pqrun);
int countFds(fd_set *rfds);
int ArticleOpen(History *h, const char *msgid, char **pfi, int32 *rsize, int *pmart, int *pheadOnly, int *compressed);

void DoArtStats(int statgroup, int which, int bytes);
void DoSpoolStats(int which);

void Kill(pid_t pid, int sig);

typedef struct TotalStatsType {
    double	ArtsReceived;
    double	ArtsTested;
    double	ArtsBytes;
    double	ArtsFed;
} TotalStatsType;

typedef struct StoreStatsType {
    double	StoreBytes;
    double	StoreCompressedBytes;
} StoreStatsType;

FeedStats	Stats;
FeedStats	*HostStats;
TotalStatsType	TtlStats;
StoreStatsType	StoreStats;

char	*HName;
char	HLabel[256];
int	MaxFds = 0;
int	NumForks = 0;
int	ReadOnlyCount = 0;
int	PausedCount = 0;
fd_set	RFds;
Buffer	*PipeAry[MAXFDS];
Feed	*FeBase = NULL;
Retain	*ReBase = NULL;
Track	PidAry[MAXFDS];
volatile int Exiting = 0;
int	DidFork = 0;
int	NumForksExceeded = 0;
int	TxBufSize = 0;
int	RxBufSize = 0;
int	LogCount = 0;
time_t	SessionBeg = 0;
time_t	SessionMark = 0;
int	FeedTableReady = 0;
char	*DebugLabel = NULL;
char	PeerIpName[64];
hash_t	PeerIpHash;
int	HasStatusLine = 0;
MemPool	*ParProcMemPool;
int	ReadOnlyCxn = 0;		/* Read-only client connection */
int	ReadOnlyMode = 0;		/* Server switched to RO mode */
pid_t	HostCachePid = 0;
time_t	SpoolAllocTime = 0;

FILE	*PathFd = NULL;
time_t	PathFdT = 0;
time_t	PathFdFileT = 0;

#define DEBUGLOG(msgid,msg) { \
    if (DebugOpt) \
	ddprintf("%d ** %-50s\t%s", (int)getpid(), (msgid ? msgid : "??"), msg); \
}

#define SETREJECT(msg) { \
    if (rejBuf) \
       snprintf(rejBuf, REJMSGSIZE, "%d %s %s", size, artType, msg); \
}

void
Usage(void)
{
    fprintf(stderr, "Usage: diablo -p pathhost [-A admin] [ -B bindip[:port] [-b fd]\n");
    fprintf(stderr, "              [-c commonpath ] [-d[#] ] [-e pcomexpire] [-F spamfilter]\n");
    fprintf(stderr, "              [-h hostname] [-M maxperremote ] [ -P [bindip:]port ]\n");
    fprintf(stderr, "              [-R rxbufsize] [-S spamopts ] [ -s statusline ]\n");
    fprintf(stderr, "              [-T txbufsize] [-V ] [ -X xrefhost ] [ -x xrefsync] [ -Z ]\n");
    fprintf(stderr, "              server\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "  -A admin         Set the reported admin email address\n");
    fprintf(stderr, "  -B bindip[:port] Set the bind interface\n");
    fprintf(stderr, "  -b fd            Pass an open socket to listen on\n");
    fprintf(stderr, "  -c commonpath    Set a common pathhost entry\n");
    fprintf(stderr, "  -d [#]           Enable debugging\n");
    fprintf(stderr, "  -e pcomexpire    Set age of history cache\n");
    fprintf(stderr, "  -F spamfilter    Set path to external spamfilter\n");
    fprintf(stderr, "  -h hostname      Set reported hostname\n");
    fprintf(stderr, "  -M maxperremote  Set max incoming concurrent connections per remote host\n");
    fprintf(stderr, "  -P [bindip:]port Set listen port\n");
    fprintf(stderr, "  -R rxbufsize     Set TCP receive buffer size port\n");
    fprintf(stderr, "  -S spamopts      Enable internal spamfilter options\n");
    fprintf(stderr, "  -s statusline    Use this area of process space to write status data\n");
    fprintf(stderr, "  -T txbufsize     Set TCP transmit buffer size port\n");
    fprintf(stderr, "  -V               Display version and exit\n");
    fprintf(stderr, "  -X xrefhost      Set hostname used in generated Xref: lines\n");
    fprintf(stderr, "  -x xrefsync      Enable/disable updating of NX field in dactive from Xref:\n");
    fprintf(stderr, "  -Z               Reject articles containing a nul (\\0) character\n");
    exit(1);
}

int
main(int ac, char **av)
{
    char *op = NULL;
    int passedfd = -1;

    /*
     * On many modern UNIX systems, buffers for stdio are not allocated
     * until the first read or write AND, generally, large buffers (like 64K)
     * are allocated.  Since we print to stdout and stderr but do not really
     * need the buffers, we make them smaller.
     */

    LoadDiabloConfig(ac, av);
    (void)hhash("x");		/* prime hash table prior to forks */

    rsignal(SIGPIPE, SIG_IGN);
    SessionBeg = SessionMark = time(NULL);

    srandom((int32)SessionBeg ^ (getpid() * 100));
    random();
    random();

    bzero(&Stats, sizeof(Stats));
    bzero(&TtlStats, sizeof(TtlStats));

    /*
     * Options
     */

    {
	int i;
	char *p;
	PathListType *pl = DOpts.PathList;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-') {
		if (op) {
		    fprintf(stderr, "service option specified twice (%s/%s)\n", ptr, op);
		    exit(1);
		}
		op = ptr;
		continue;
	    }
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'b':
		passedfd = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    case 'A':
	    case 'N':
		strdupfree(&DOpts.NewsAdmin, (*ptr) ? ptr : av[++i], "");
		break;
	    case 'B':
		if (*ptr == 0)
		    ptr = av[++i];
                if (*ptr == '[') {
                    char *p = strchr(ptr, ']');
                    strdupfree(&DOpts.FeederBindHost, SanitiseAddr(ptr), NULL);
                    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			strdupfree(&DOpts.FeederPort, p + 1, NULL);
		} else if ((p = strrchr(ptr, ':')) != NULL) {
                    *p++ = 0;
                    strdupfree(&DOpts.FeederPort, p, NULL);
                    strdupfree(&DOpts.FeederBindHost, SanitiseAddr(ptr), NULL);
		} else {
                    strdupfree(&DOpts.FeederBindHost, SanitiseAddr(ptr), NULL);
		}
		break;
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'c':
		{
		    char *p = (*ptr) ? ptr : av[++i];
		    if (*p == 0) 		/* 0-length string no good */
			break;
		    if (pl == NULL) {
			pl = zalloc(&SysMemPool, sizeof(PathListType));
			DOpts.PathList = pl;
		    } else {
			pl->next = zalloc(&SysMemPool, sizeof(PathListType));
			pl = pl->next;
		    }
		    pl->pathent = strdup(p);
		    pl->pathtype = 2;
		    pl->next = NULL;
		}
		break;
	    case 'd':
		if (isdigit((int)(unsigned char)*ptr)) {
		    DebugOpt = strtol(ptr, NULL, 0);
		} else {
		    --ptr;
		    while (*ptr == 'd') {
			++DebugOpt;
			++ptr;
		    }
		}
		break;
	    case 'e':
		SetCommand(stderr, "precommittime", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'F':
		SetCommand(stderr, "feederfilter", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'h':
		SetCommand(stderr, "feederhostname", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'M':
		SetCommand(stderr, "maxconnect", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'P':
		if (*ptr == 0)
		    ptr = av[++i];
                if (*ptr == '[') {
                    char *p = strchr(ptr, ']');
                    strdupfree(&DOpts.FeederBindHost, SanitiseAddr(ptr), NULL);
                    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			strdupfree(&DOpts.FeederPort, p + 1, NULL);
		} else if ((p = strrchr(ptr, ':')) != NULL) {
                    *p++ = 0;
                    strdupfree(&DOpts.FeederPort, p, NULL);
                    strdupfree(&DOpts.FeederBindHost, SanitiseAddr(ptr), NULL);
		} else {
                    strdupfree(&DOpts.FeederPort, ptr, NULL);
		}
		break;
	    case 'p':
		strdupfree(&DOpts.FeederPathHost, (*ptr) ? ptr : av[++i], "");
		if (pl == NULL) {
		    pl = zalloc(&SysMemPool, sizeof(PathListType));
		    DOpts.PathList = pl;
		} else {
		    pl->next = zalloc(&SysMemPool, sizeof(PathListType));
		    pl = pl->next;
		}
		pl->pathent = strdup(DOpts.FeederPathHost);
		pl->pathtype = 1;
		pl->next = NULL;
		break;
	    case 'R':
		RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		if (RxBufSize != 0 && RxBufSize < 4096)
		    RxBufSize = 4096;
		break;
	    case 'r':   
		if (!*ptr)
                ptr = av[++i];
		DOpts.FeederMaxAcceptAge = TimeSpec(ptr, "d");
		if (DOpts.FeederMaxAcceptAge == -1)
		    Usage();
		break;
	    case 'S':
		SetCommand(stderr, "internalfilter", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 's':
		SetStatusLine(ptr - 2, strlen(ptr - 2));
		HasStatusLine = 1;
		break;
	    case 'T':
		TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    case 'V':
		PrintVersion();
		break;
	    case 'X':
		SetCommand(stderr, "feederxrefhost", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'x':
		ptr = (*ptr) ? ptr : av[++i];
		SetCommand(stderr, "feederxrefhost", (*ptr) ? ptr : av[++i], NULL);
		break;
	    case 'Z':
		SetCommand(stderr, "rejectartswithnul", "1", NULL);
		break;
	    default:
		fprintf(stderr, "unknown option: %s\n", ptr - 2);
		Usage();
	    }
	}
	if (i > ac) {
	    fprintf(stderr, "expected argument to last option\n");
	    Usage();
	}
    }
     
    if (DOpts.FeederHostName == NULL)
	SetMyHostName(&DOpts.FeederHostName);
    if (DOpts.NewsAdmin == NULL)
	SetNewsAdmin(&DOpts.NewsAdmin, DOpts.FeederHostName);
    if (*DOpts.NewsAdmin == 0) {
	free(DOpts.NewsAdmin);
	DOpts.NewsAdmin = NULL;
    }

    /*
     * For our Path: insertion
     */
    if (DOpts.FeederPathHost == NULL) {
	fprintf(stderr, "No '-p newspathname' specified\n");
	Usage();
    }

    if(DOpts.FeederXRefHost == NULL && DOpts.FeederPathHost != NULL) 
	DOpts.FeederXRefHost = DOpts.FeederPathHost;

    /*
     * The chdir is no longer required, but we do it anyway to
     * have a 'starting point' to look for cores and such.
     */
    if (chdir(PatExpand(SpoolHomePat)) < 0) {
	fprintf(stderr, "%s: chdir('%s'): %s\n", av[0],
				PatExpand(SpoolHomePat), strerror(errno));
	exit(1);
    }

    if (op == NULL) {
	fprintf(stderr, "Must specify service option: (server)\n");
	Usage();
    } else if (strcmp(op, "server") == 0) {
	DiabloServer(passedfd);
    } else {
	fprintf(stderr, "unknown service option: %s\n", op);
	Usage();
	exit(1);
    }
    return(0);
}

void
Kill(pid_t pid, int sig)
{
    if (kill(pid, sig) < 0) {
	logit(LOG_ERR, "kill pid %d (%d) failed: %s", pid, sig, strerror(errno));
    }
}

/*
 * This needs fork.  What we are trying to accomplish
 * is to ensure that all the pipes from the children
 * are flushed back to the parent and the parent
 * writes them out before exiting.  Otherwise
 * feed redistribution might fail.
 */

void
sigHup(int sigNo)
{
    Exiting = 1;
    if (DidFork) {
	if (FeedFo) {
	    fflush(FeedFo);
	    fclose(FeedFo);
	    FeedFo = NULL;
	}
	ArticleFileCloseAll();
	DiabFilterClose(0);
	LogSession();	/* try, may not work */
	exit(1);
    } else {
	if (Exiting == 0) {
	    int i;

	    Exiting = 1;
	    for (i = 0; i < MAXFDS; ++i) {
		if (PidAry[i].tr_Pid) {
		    Kill(PidAry[i].tr_Pid, SIGHUP);
		}
	    }
	}
    }
}

void
sigUsr1(int sigNo)
{
    ++DebugOpt;
}

void
sigUsr2(int sigNo)
{
    DebugOpt = 0;
}

void
sigAlrm(int sigNo)
{
    if (ReadOnlyCxn) {
	ReadOnlyMode = 1;
	if (DidFork) {
	    struct sockaddr_un soun;
	    int ufd;
	    FILE *fo;

	    memset(&soun, 0, sizeof(soun));
	    if((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		/* Can't make socket; hang ourselves */
		logit(LOG_INFO, "Unable to create UNIX socket: %s\n",
			strerror(errno));
		sigHup(SIGHUP);
	    }

	    soun.sun_family = AF_UNIX;
	    sprintf(soun.sun_path, "%s", PatRunExpand(DiabloSocketPat));
	    if (connect(ufd, (struct sockaddr *)&soun,
		offsetof(struct sockaddr_un,
			 sun_path[strlen(soun.sun_path)+1])) < 0) {
		/* Can't connect; hang ourselves */
		logit(LOG_INFO, "Unable to connect to master %s: %s\n",
			soun.sun_path, strerror(errno));
		sigHup(SIGHUP);
	    }

	    fo = fdopen(ufd, "w");

	    fprintf(fo, "child-is-readonly\n");
	    fprintf(fo, "quit\n");
	    fflush(fo);

	    fclose(fo);

	    close(ufd);
	}
    } else {
	sigHup(SIGHUP);
    }
}

/*
 * DIABLOSERVER() - The master server process.  It accept()s connections
 *		    and forks off children.
 */

void
DiabloServer(int lfd)
{
    int ufd;
    int fdrotor = 0;

    /*
     * Detach
     */

    if (DebugOpt == 0) {
	pid_t pid = fork();

	if (pid < 0) {
	    perror("fork");
	    exit(1);
	}
	if (pid > 0) {
	    exit(0);
	}

	/*
	 * Child continues
	 */

	DDUseSyslog = 1;

	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);
	freopen("/dev/null", "r", stdin);
#if USE_TIOCNOTTY
	{
	    int fd = open("/dev/tty", O_RDWR);
	    if (fd >= 0) {
		ioctl(fd, TIOCNOTTY, 0);
		close(fd);
	    }
	}
#endif
#if USE_SYSV_SETPGRP
	setpgrp();
#else
	setpgrp(0, 0);
#endif
    }

    /*
     * select()/signal() setup
     */

    FD_ZERO(&RFds);

    rsignal(SIGHUP, sigHup);
    rsignal(SIGINT, sigHup);
    rsignal(SIGTERM, sigHup);
    rsignal(SIGUSR1, sigUsr1);
    rsignal(SIGUSR2, sigUsr2);
    rsignal(SIGALRM, sigAlrm);

    /*
     * logs, socket setup
     */

    OpenLog("diablo", (DebugOpt ? LOG_PERROR : 0)|LOG_PID|LOG_NDELAY);

    if (lfd == -1) {
#ifdef	INET6
	int error;
	struct addrinfo hints;
	struct addrinfo *res;

	/*
	 * Open a wildcard listening socket
	 * that listens on both v6 and v4.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = DOpts.FeederBindHost ? PF_UNSPEC : PF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(DOpts.FeederBindHost, DOpts.FeederPort,
							&hints, &res);
	if (error != 0) {
	    fprintf(stderr, "getaddrinfo: %s:%s: %s\n",
			DOpts.FeederBindHost ? DOpts.FeederBindHost : "ALL",
			DOpts.FeederPort, gai_strerror(error));
	    exit(1);
	}
	if ((lfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
	    perror("socket");
	    exit(1);
	}
	if (!DOpts.FeederBindHost && res->ai_family == PF_INET6) {
	    int on = 0;
	    setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY,
		       (void *) &on, sizeof (on));
	}
#else
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));

	/*
	 * listen socket for news
	 */
	memset(&sin, 0, sizeof(sin));
	if ((lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	    perror("socket");
	    exit(1);
	}

	sin.sin_family = AF_INET;

	/*
	 * Work out the interface to bind to
	 */
	if (DOpts.FeederBindHost == NULL) {
		sin.sin_addr.s_addr = INADDR_ANY;
	} else {
	    if (strtol(DOpts.FeederBindHost, NULL, 0) > 0) {
		sin.sin_addr.s_addr = inet_addr(DOpts.FeederBindHost);
	    } else {
		struct hostent *he;

		if ((he = gethostbyname(DOpts.FeederBindHost)) != NULL) {
		    sin.sin_addr = *(struct in_addr *)he->h_addr;
		} else {
		    fprintf(stderr, "Unknown bind host: %s\n",
							DOpts.FeederBindHost);
		    exit(1);
		}
	    }
	}
	/*
	 * Work out the port to bind to
	 */
	{
	    struct servent *sen;
	    int port;
	    if (DOpts.FeederPort == NULL) {
		DOpts.FeederPort = "nntp";
	    } else if ((port = strtol(DOpts.FeederPort, NULL, 0)) != 0) {
		sin.sin_port = htons(port);
	    } else {
		if ((sen = getservbyname(DOpts.FeederPort, "tcp")) != NULL) {
		    sin.sin_port = sen->s_port;
		} else {
		    fprintf(stderr, "Unknown service: %s\n", DOpts.FeederPort);
		    exit(1);
		}
	    }
	}

#endif	/* INET6 */

	{
	    int on = 1;
	    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
	    setsockopt(lfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    printf("Setsockopt\n");
	}
	if (TxBufSize) {
	    if (setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		logit(LOG_ERR, "setsockopt lfd %d failed: %s", lfd, strerror(errno));
	    }
	}
	if (RxBufSize) {
	    if (setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		logit(LOG_ERR, "setsockopt lfd %d failed: %s", lfd, strerror(errno));
	    }
	}

#ifdef INET6
	{
	    if (bind(lfd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("bind");
		exit(1);
	    }
	    freeaddrinfo(res);
	}
#else
	if (bind(lfd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    perror("bind");
	    exit(1);
	}
#endif
	/*
	 * I'm not clear on why this is duplicated from above.  JG20051012
	 */
	{
	    int on = 1;
	    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
	    setsockopt(lfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    printf("Setsockopt\n");
	}
	if (TxBufSize) {
	    if (setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		logit(LOG_ERR, "setsockopt lfd %d failed: %s", lfd, strerror(errno));
	    }
	}
	if (RxBufSize) {
	    if (setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		logit(LOG_ERR, "setsockopt lfd %d failed: %s", lfd, strerror(errno));
	    }
	}

#if NONBLK_ACCEPT_BROKEN
	/* HPUX is broken, see lib/config.h */
#else
	fcntl(lfd, F_SETFL, O_NONBLOCK);
#endif
    }

    if (listen(lfd, 10) < 0) {
	perror("listen");
	exit(1);
    }
    if (DOpts.FeederBindHost != NULL &&
				strchr(DOpts.FeederBindHost, ':') != NULL)
	logit(LOG_INFO, "Listening on [%s]:%s\n",
			DOpts.FeederBindHost ? DOpts.FeederBindHost : "ALL",
			DOpts.FeederPort);
    else
	logit(LOG_INFO, "Listening on %s:%s\n",
			DOpts.FeederBindHost ? DOpts.FeederBindHost : "ALL",
			DOpts.FeederPort);

    FD_SET(lfd, &RFds);
    if (MaxFds <= lfd)
	MaxFds = lfd + 1;

    /*
     * Upward compatibility hack - older versions of diablo create
     * the unix domain socket as root.  We have to make sure the
     * file path is cleared out so we can create the socket as user news.
     */
    remove(PatRunExpand(DiabloSocketPat));

    /*
     * change my uid/gid
     */

    {
	struct passwd *pw = getpwnam("news");
	struct group *gr = getgrnam("news");
	gid_t gid;

	if (pw == NULL) {
	    perror("getpwnam('news')");
	    exit(1);
	}
	if (gr == NULL) {
	    perror("getgrnam('news')");
	    exit(1);
	}
	gid = gr->gr_gid;
	setgroups(1, &gid);
	setgid(gr->gr_gid);
	setuid(pw->pw_uid);
    }

    /*
     * UNIX domain socket
     */

    {
	struct sockaddr_un soun;

	memset(&soun, 0, sizeof(soun));

	if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	    perror("udom-socket");
	    exit(1);
	}
	soun.sun_family = AF_UNIX;

	sprintf(soun.sun_path, "%s", PatRunExpand(DiabloSocketPat));
	remove(soun.sun_path);
	if (bind(ufd, (struct sockaddr *)&soun, offsetof(struct sockaddr_un, sun_path[strlen(soun.sun_path)+1])) < 0) {
	    perror("udom-bind");
	    exit(1);
	}
	chmod(soun.sun_path, 0770);

#if NONBLK_ACCEPT_BROKEN
	/* HPUX is broken, see lib/config.h */
#else
	fcntl(ufd, F_SETFL, O_NONBLOCK);
#endif

	if (listen(ufd, 200) < 0) {
	    perror("udom-listen");
	    exit(1);
	}

	FD_SET(ufd, &RFds);
	if (MaxFds <= ufd)
	    MaxFds = ufd + 1;
    }

    /*
     * Call InitPreCommit() and InitSpamFilter() to setup any shared memory
     * segments.  These need to be created and mapped prior to any forks that
     * we do.
     */

    InitPreCommit();
    SetSpamFilterOpt();
    if (DOpts.SpamFilterOpt != NULL)
	InitSpamFilter();

    /*
     * Initial load of dnewsfeeds file.  We recheck every select
     * (but it doesn't call stat() every time).  The master server
     * requires the entire file to be parsed, so we do not supply
     * a label.
     */

    LoadSpoolCtl(0, 1);		/* check spool partitions if specified */ 
    LoadNewsFeed(0, 1, NULL);

    {
	struct stat st;

	HostCachePid = LoadHostAccess(time(NULL), 1, DOpts.HostCacheRebuildTime);
	if (stat(PatDbExpand(DHostsCachePat), &st) < 0) {
	    logit(LOG_INFO, "No host cache - waiting for build to complete");
	    waitpid(HostCachePid, NULL, 0);
	    logit(LOG_INFO, "Host cache rebuild complete");
	}
    }

    logit(LOG_INFO, "Waiting for connections");
    /*
     * Main Loop
     */

    while (NumForks || Exiting == 0) {
	fd_set rfds = RFds;
	int i;
	int n;
	struct timeval tv = { 5, 0 };
	time_t t;

	if ((ReadOnlyMode || PausedCount) && (NumForks == ReadOnlyCount)) {
	    FinishRetain(RET_PAUSE);
	}

	t = time(NULL);

	LoadSpoolCtl(t, 0);	/* check spool partitions if specified */ 
	LoadNewsFeed(t, 0, NULL);
	if (HostCachePid == 0)
	    HostCachePid = LoadHostAccess(t, 0, DOpts.HostCacheRebuildTime);

	n = select(MaxFds, &rfds, NULL, NULL, &tv);

	if (lfd != -1 && FD_ISSET(lfd, &rfds))
	    DoAccept(lfd);
	if (ufd != -1 && FD_ISSET(ufd, &rfds))
	    DoCommand(ufd);

	/* exit this loop when either all fds have been examined, or
	 * when 10 ready pipes have been processed.
	 */
	n = 10;
	i = fdrotor;
	do {
	    if (i != lfd && i != ufd && FD_ISSET(i, &rfds)) {
		--n;
		DoPipe(i);
	    }
	    if (++i >= MaxFds)
		i = 0;
	} while (n && i != fdrotor);
	fdrotor = i;

	{
	    pid_t pid;

	    while ((pid = wait3(NULL, WNOHANG, NULL)) > 0) {
		int i;

		if (pid == HostCachePid) {
		    HostCachePid = 0;
		    continue;
		}

		for (i = 0; i < MaxFds; ++i) {
		    if (PidAry[i].tr_Pid == pid) {
			bzero(&PidAry[i], sizeof(PidAry[0]));
		    }
		}
	    }

	    if (pid < 0 && errno != ECHILD)
		logit(LOG_EMERG, "wait3 failed: %s", strerror(errno));
	}
	if (NumForks >= MAXFORKS || Exiting) {
	    if (NumForksExceeded == 0) {
		if (Exiting) {
		    logit(LOG_WARNING, "Exiting, waiting on children");
		} else {
		    logit(LOG_WARNING, "NumForks exceeded");
		}
		NumForksExceeded = 1;
	    }
	    if (lfd >= 0)
		FD_CLR(lfd, &RFds);
	    if (Exiting) {
		close(lfd);
		lfd = -1;
	    }
	} else {
	    if (NumForksExceeded) {
		if (NumForksExceeded == 1)
		    logit(LOG_WARNING, "NumForks ok, reaccepting");
		NumForksExceeded = 0;
	    }
	    FD_SET(lfd, &RFds);
	}
    }
    LogSession2();
    flushFeeds(0);
    ClosePathLog(1);
    CloseArtLog(1);
    DiabFilterClose(1);
    if (DOpts.SpamFilterOpt != NULL)
	TermSpamFilter();
    sleep(2);

    if (ShutdownCleanup && (strcmp(ShutdownCleanup, "NONE") != 0)) {
	system(ShutdownCleanup);
    }
    FinishRetain(RET_PAUSE);
    FinishRetain(RET_CLOSE);
}

/*
 * DOACCEPT()	- accept and deal with a new connection
 */

void
DoAccept(int lfd)
{
    int fd;
    int delaylen;
    int curtime = (int)(time(NULL));
    int dt = (int)(SessionMark);
    char addrst[NI_MAXHOST];
#ifdef INET6
    struct sockaddr_storage res;
    int reslen = sizeof(res);
#else
    struct sockaddr_in asin;
    ACCEPT_ARG3_TYPE alen = sizeof(asin);
#endif

    if (Exiting)
	return;

#ifdef INET6
    if ((fd = accept(lfd, (struct sockaddr *)&res, &reslen)) >= 0) {
#else
    if ((fd = accept(lfd, (struct sockaddr *)&asin, &alen)) >= 0) {
#endif
	int fds[2] = { -1, -1 };
	int ok = 0;
	int count = 0;

	fcntl(fd, F_SETFL, 0);

	/*
	 * Handle connection limit.  Note that we assume the TCP write buffer
	 * is large enough to hold our response message so the write() does
	 * not block.
	 */

	addrst[0] = 0;
	{
#ifdef INET6
	    char *st = NetAddrToSt(fd, NULL, 1, 0, 0);

	    if (st != NULL) {
		strcpy(addrst, st);
#else
	    struct sockaddr_in rsin;
	    ACCEPT_ARG3_TYPE alen = sizeof(rsin);

	    if (getpeername(fd, (struct sockaddr *)&rsin, &alen) == 0) {
		strncpy(addrst, inet_ntoa(rsin.sin_addr), sizeof(addrst) - 1);
		addrst[sizeof(addrst) - 1] = '\0';
#endif
		if (DOpts.MaxPerRemote) {
		    int i;

		    for (i = 0; i < MaxFds; ++i) {
			if (strcmp(PidAry[i].addr, addrst) == 0)
			    ++count;
		    }
		    if (DOpts.MaxPerRemote > 0 && count >= DOpts.MaxPerRemote) {
			char buf[256];

			logit(
			    LOG_WARNING, 
			    "Connect Limit exceeded (from -M/diablo.config) for %s (%d)",
			    addrst,
			    count
			);
			snprintf(
			    buf, sizeof(buf),
			    "502 %s: parallel connection limit is %d\r\n",
			    DOpts.FeederHostName,
			    DOpts.MaxPerRemote
			);
			write(fd, buf, strlen(buf));
			ok = -1;
		    }
		}
		sprintf(PeerIpName, "%s", addrst);
		bhash(&PeerIpHash, PeerIpName, strlen(PeerIpName));
	    } else if (DOpts.MaxPerRemote) {
		ok = -1;
	    }
	}

	SpoolAllocTime = time(NULL);
	if (AllocateSpools(SpoolAllocTime) == -1) {
	    char buf[256];
	    snprintf(buf, sizeof(buf), "502 %s: temporary server error\r\n",
						DOpts.FeederHostName);
	    write(fd, buf, strlen(buf));
	    sleep(2);
	    ok = -1;
	}

	if (ok == 0 && pipe(fds) == 0 && fds[0] < MAXFDS) {
	    pid_t pid;

	    /*
	     * we MUST clear our FILE stdio so a call to
	     * exit() doesn't flush FILE structures to
	     * the wrong descriptors!
	     *
	     * WARNING WARNING!  There cannot be a single
	     * critical FILE handle open for which we may
	     * close it's underlying descriptor!
	     */

	    fflush(stdout);
	    fflush(stderr);

	    if ((pid = fork()) == 0) {
		int i;
		time_t SessionCheck = SessionBeg = SessionMark = time(NULL);

		flushFeeds(1);	/* close feed descriptors without flushing */
		CloseIncomingLog();
		ClosePathLog(0);
		CloseArtLog(0);
		DiabFilterClose(0);

		if (HasStatusLine)
		    stprintf("%s", addrst);

		CloseLog(NULL, 1);

		for (i = 0; i < MaxFds; ++i) {
		    if (i > 2 && i != fds[1] && i != fd && i != ZoneFd && i != BodyFilterFd && i != NphFilterFd) {
			if (PipeAry[fd] != NULL) {
			    bclose(PipeAry[fd], 0);
			    PipeAry[fd] = NULL;
			}
			close(i);
		    }
		}

		OpenLog("diablo", (DebugOpt ? LOG_PERROR : 0)|LOG_PID|LOG_NDELAY);

		if (HLabel[0] != 0)
		    nice(FeedPriority(HLabel));

		FeedFo = fdopen(fds[1], "w");
#ifdef INET6
		if ((HName = Authenticate(fd, (struct sockaddr *)&res, addrst, HLabel)) == NULL) {
#else
		if ((HName = Authenticate(fd, (struct sockaddr *)&asin,
						addrst, HLabel)) == NULL) {
#endif
		    FILE *fo = fdopen(fd, "w");

		    if (fo == NULL) {
			logit(LOG_CRIT, "fdopen() of socket failed");
			exit(1);
		    }
		    if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
			xfprintf(fo, "502 %s: Transfer permission denied to %s - %s (DIABLO %s-%s)\r\n",
						DOpts.FeederHostName,
						addrst,
						DOpts.NewsAdmin,
						VERS, SUBREV);
		    else
			xfprintf(fo, "502 %s: Transfer permission denied to %s\r\n",
						DOpts.FeederHostName,
						addrst);
		    logit(LOG_INFO, "Connection %d from %s (no permission)",
			fds[0],
			addrst
		    );
		    exit(0);
		}
		if (HLabel[0] == 0) {
		    FILE *fo = fdopen(fd, "w");

		    if (fo == NULL) {
			logit(LOG_CRIT, "fdopen() of socket failed");
			exit(1);
		    }
		    if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
			xfprintf(fo, "502 %s DIABLO Misconfiguration, label missing in dnewsfeeds, contact %s\r\n",
						DOpts.FeederHostName,
						DOpts.NewsAdmin);
		    else
			xfprintf(fo, "502 %s DIABLO Misconfiguration, label missing in dnewsfeeds\r\n",
						DOpts.FeederHostName);
		    logit(LOG_CRIT, "Diablo misconfiguration, label for %s not found in dnewsfeeds", HName);
		    exit(0);
		}

		if (strcmp(HLabel, "%STATS") == 0) {
		    FILE *fo = fdopen(fd, "w");
		    int dt = (int)(time(NULL) - SessionCheck);

		    if (fo == NULL) {
			logit(LOG_CRIT, "fdopen() of socket failed");
			exit(1);
		    }
		    DoStats(fo, dt, 0);
		    fflush(fo);
		    exit(0);
		}

		DidFork = 1;

		if (HasStatusLine)
		    stprintf("%s", HName);

		delaylen = FeedInDelay(HLabel);

		if((curtime - dt) < delaylen) {
			FILE *fo = fdopen(fd, "w");

			if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
				xfprintf(fo, "400 %s: System starting up - Try again in a few minutes - %s (DIABLO %s-%s)\r\n",
						DOpts.FeederHostName,
						DOpts.NewsAdmin,
						VERS, SUBREV);
			else
				xfprintf(fo, "400 %s: System starting up - Try again in a few minutes\r\n",
						DOpts.FeederHostName);
			logit(LOG_INFO, "System starting up: %s is delayed for %d seconds", DOpts.FeederHostName, delaylen);
			exit(0);
		}

		logit(LOG_INFO, "Connection %d from %s %s",
		    fds[0],
		    HName,
		    addrst
		);

		/*
		 * Free the parent process memory pool, which the child does not
		 * use (obviously!)
		 */

		freePool(&ParProcMemPool);

		/*
		 * Free memory used by DiabFilter as well, because it's only
		 * used in the parent.
		 */

		DiabFilter_freeMem();

		/*
		 * Simple session debugging support
		 */
		if (DebugLabel && HLabel[0] && strcmp(DebugLabel, HLabel) == 0) {
		    char path[256];
		    int tfd;

		    sprintf(path, "/tmp/diablo.debug.%d", (int)getpid());
		    DebugOpt = 1;
		    remove(path);
		    if ((tfd = open(path, O_EXCL|O_CREAT|O_TRUNC|O_RDWR, 0600)) >= 0) {
			close(tfd);
			freopen(path, "a", stderr);
			freopen(path, "a", stdout);
			printf("Debug label %s pid %d\n", HLabel, (int)getpid());
		    } else {
			printf("Unable to create %s\n", path);
		    }
		}

		DoSession(fd, count);
		LogSession();
		logit(LOG_INFO, "Disconnect %d from %s %s (%d elapsed)",
		    fds[0],
		    HName,
		    addrst,
		    (int)(time(NULL) - SessionBeg)
		);
		exit(0);
	    }
	    if (pid < 0) {
		logit(LOG_EMERG, "fork failed: %s", strerror(errno));
	    } else {
		ok = 1;
		++NumForks;
		PidAry[fds[0]].tr_Pid = pid;
		strncpy(PidAry[fds[0]].addr, addrst,
					sizeof(PidAry[fds[0]].addr) - 1);
		PidAry[fds[0]].addr[sizeof(PidAry[fds[0]].addr) - 1] = '\0';
	    }
	}
	close(fd);
	if (fds[1] >= 0)
	    close(fds[1]);

	if (ok > 0) {
	    fcntl(fds[0], F_SETFL, O_NONBLOCK);
	    FD_SET(fds[0], &RFds);
	    if (MaxFds <= fds[0])
		MaxFds = fds[0] + 1;
	} else {
	    if (fds[0] >= 0) {
		logit(LOG_WARNING, "Maximum file descriptors exceeded");
		close(fds[0]);
	    } else if (ok == 0 && fds[0] < 0) {
		logit(LOG_EMERG, "pipe() failed: %s", strerror(errno));
	    }
	}
    }
}

/*
 * DOPIPE()	- handle data returned from our children over a pipe
 */

int 
fwCallBack(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize)
{
    writeFeedOut(hlabel, path, msgid, offsize, ((plfo > 0) ? 1 : 0), headOnly, artType, cSize);
    TtlStats.ArtsFed += 1.0;
    return(0);
}

void
DoPipe(int fd)
{
    char *ptr;
    int maxCount = 2;
    int bytes;

    if (PipeAry[fd] == NULL)
	PipeAry[fd] = bopen(fd, 1);

    while ((ptr = egets(PipeAry[fd], &bytes)) != NULL && ptr != (char *)-1) {
	char *s1;

	ptr[bytes - 1] = 0;	/* replace newline with NUL */

	if (DebugOpt > 2)
	    ddprintf("%d << %*.*s", (int)getpid(), bytes, bytes, ptr);

	s1 = strtok(ptr, "\t\n");

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
	    char *artType = strtok(NULL, "\t\n");
	    char *cSize = strtok(NULL, "\t\n");

	    if (DebugOpt > 2) {
		ddprintf(
		    "%d SOUTLINE: %s %s %s %s %s HO=%s AT=%s %s\n",
		    (int)getpid(), 
		    path, 
		    offsize,
		    msgid,
		    nglist,
		    npath, 
		    headOnly != NULL ? headOnly : "",
		    artType != NULL ? artType : "",
		    cSize != NULL? cSize : ""
		);
	    }

	    if (path && offsize && msgid && nglist && npath && headOnly) {
		int spamArt = 0;

		bytes = 0;
		{
		    char *p;
		    if ((p = strchr(offsize, ',')) != NULL)
			bytes = strtol(p + 1, NULL, 0);
		}

		if (DOpts.FeederFilter != NULL && 
			FeedSpam(2, nglist, npath, dist, artType, bytes))
		{
		    static char loc[PATH_MAX];

		    snprintf(loc, sizeof(loc), "%s:%s", path, offsize);
		    spamArt = DiabFilter(DOpts.FeederFilter, loc, DOpts.WireFormat);
		}
		FeedWrite(1, fwCallBack, msgid, path, offsize, nglist,
				npath, dist, headOnly, artType, spamArt, cSize);
		{
		    TtlStats.ArtsBytes += (double)bytes;
		}
		TtlStats.ArtsReceived += 1.0;
		if (++LogCount == 1024) {
		    LogCount = 0;
		    LogSession2();
		}
		WritePath(npath);
		WriteArtLog(npath, bytes, artType, nglist);
	    }
	} else if (strncmp(s1, "FLUSH", 5) == 0) {
	    flushFeeds(0);
	}

	if (--maxCount == 0)
	    break;
    }

    bextfree(PipeAry[fd]);	/* don't keep large buffers around */

    if (ptr == (void *)-1) {
	if (PipeAry[fd] != NULL) {
	    bclose(PipeAry[fd], 0);
	    PipeAry[fd] = NULL;
	}
	close(fd);
	FD_CLR(fd, &RFds);
	--NumForks;
    }
}

/*
 * WRITEFEEDOUT()	- the master parent writes to the outgoing feed files
 * FLUSHFEEDOUT()
 */

void
writeFeedOut(const char *label, const char *file, const char *msgid, const char *offSize, int rt, int headOnly, const char *artType, const char *cSize)
{
    Feed *fe;
    char delayTime[16];

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
	if (strchr(label, '/') != NULL) {
	    char buf[PATH_MAX];
	    char *p;
	    snprintf(buf, sizeof(buf), "%s/%s", PatExpand(DQueueHomePat), label);
	    if ((p = strrchr(buf, '/')) != NULL) {
		struct stat st;
		*p = 0;
		if (stat(buf, &st) != 0 && mkdir(buf, 0755) != 0)
		    logit(LOG_ERR, "Unable to create dqueue path for %s (%s)",
							label, strerror(errno));
	    }
	    snprintf(buf, sizeof(buf), "%s/.%s", PatExpand(DQueueHomePat), label);
	    if ((p = strrchr(buf, '/')) != NULL) {
		struct stat st;
		*p = 0;
		if (stat(buf, &st) != 0 && mkdir(buf, 0755) != 0)
		    logit(LOG_ERR, "Unable to create dqueue path for %s (%s)",
							label, strerror(errno));
	    }
	}
	fe->fe_Fd = xopen(O_APPEND|O_RDWR|O_CREAT, 0644, "%s/%s",
					PatExpand(DQueueHomePat), label);
	if (fe->fe_Fd >= 0) {
	    int bsize;

	    fe->fe_Buf = pagealloc(&bsize, 1);
	    fe->fe_BufMax = bsize;
	    fe->fe_BufIdx = 0;
	    fe->fe_Failed = 0;
	    fe->fe_Delayed = IsDelayed(label);
	    fe->fe_NotifyFd = -1;
#if 0
	    if (1) {
		int fds[2];
		char *argv[9] = { NULL };
		argv[0] = "/news/dbin/dnewslink";
		argv[1] = "dnewslink";
		argv[2] = "-bzz";
		argv[3] = "-p";
		argv[4] = "-h127.0.0.1";
		argv[5] = "-P8119";
		argv[6] = "-r-1";
		argv[7] = NULL;
		if (RunProgramPipe(fds, RPF_STDOUT, argv, NULL) > 0) {
		    fe->fe_PipeFd = fds[1];
		} else {
		    logit(LOG_ERR, "Unable to start feed pipe");
		}
	    }
#endif
	} else {
	    logit(LOG_ERR, "Unable to open/create dqueue entry for %s (%s)",
							label, strerror(errno));
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

	    if (fe->fe_Delayed)
		sprintf(delayTime, " D%d", (int)time(NULL));
	    else
		delayTime[0] = 0;
	    sprintf(fe->fe_Buf + fe->fe_BufIdx, "%s %s %s%s%s%s%s\n",
		file, msgid, offSize, 
		(headOnly ? " H" : ""),
		delayTime,
		cSize != NULL ? " C" : "",
		cSize != NULL ? cSize : ""
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
	if (fe->fe_NotifyFd >= 0)
	    close(fe->fe_NotifyFd);
	fe->fe_NotifyFd = -1;
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
	if (fe->fe_NotifyFd != -1) {
	    if (write(fe->fe_NotifyFd, &n, 1) < 0 && errno != EAGAIN) {
		close(fe->fe_NotifyFd);
		fe->fe_NotifyFd = -1;
	    }
	}
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

/*
 * DOSESSION()	- a child process to handle a diablo connection
 */

void
DoSession(int fd, int count)
{
    Buffer *bi;
    FILE *fo;
    char *buf;
    int streamMode = 0;
#ifdef	USE_ZLIB
    int compressMode = 0;
#endif
    int headerOnly = 0;
    int syntax = 0;
    int unimp = 0;
    int nfd;

    /*
     * reinitialize random generator
     */

    srandom((int32)random() ^ (getpid() * 100) ^ (int32)time(NULL));

    /*
     * Fixup pipe
     */

    {
	nfd = dup(fd);
	if (nfd < 0) {
	    logit(LOG_CRIT, "DoSession: dup()");
	    exit(1);
	}
	bi = bopen(nfd, DOpts.FeederBufferSize);
	fo = fdopen(fd, "w");

	if (bi == NULL || fo == NULL) {
	    logit(LOG_CRIT, "DoSession() bopen failure");
	    exit(1);
	}
    }
    if (PausedCount) {
	if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
	    xfprintf(fo, "502 %s: System currently paused - %s (DIABLO %s-%s)\r\n",
						DOpts.FeederHostName,
						DOpts.NewsAdmin,
						VERS, SUBREV);
	else
	    xfprintf(fo, "502 %s: System currently paused\r\n",
						DOpts.FeederHostName);
	fflush(fo);
	exit(0);
    }

    LoadNewsFeed(0, 1, HLabel);	/* free old memory and load only our label */

    ConfigNewsFeedSockOpts(HLabel, nfd, fd);

    if (ReadOnlyMode && !FeedReadOnly(HLabel)) {
	xfprintf(fo, "502 %s DIABLO is currently in read-only mode\r\n",
		DOpts.FeederHostName);
	fflush(fo);
	exit(0);
    }

    HistoryOpen(NULL, 0);

    switch(FeedValid(HLabel, &count)) {
    case FEED_VALID:
	break;
    case FEED_MAXCONNECT:
	xfprintf(fo, "502 %s DIABLO parallel connection limit is %d\r\n",
	    DOpts.FeederHostName,
	    count
	);
	logit(
	    LOG_WARNING, 
	    "Connect Limit exceeded (from dnewsfeeds) for %s", HName
	);
	fflush(fo);
	exit(0);
	/* not reached */
    case FEED_MISSINGLABEL:
	if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
	    xfprintf(fo, "502 %s DIABLO misconfiguration, label missing in dnewsfeeds, contact %s\r\n",
						DOpts.FeederHostName,
						DOpts.NewsAdmin);
	else
	    xfprintf(fo, "502 %s DIABLO misconfiguration, label missing in dnewsfeeds\r\n",
						DOpts.FeederHostName);
	logit(LOG_CRIT, "Diablo misconfiguration, label %s not found in dnewsfeeds", HLabel);
	fflush(fo);
	exit(0);
	/* not reached */
    }

    ArticleFileInit();		/* initialize article file cache	   */
    ReadOnlyCxn = FeedReadOnly(HLabel);
				/* force read-only?			   */

    if ((DOpts.FeederActiveEnabled && (DOpts.FeederXRefSync || DOpts.FeederXRefSlave == 0)) || DOpts.FeederActiveDrop)
	InitDActive(ServerDActivePat); /* initialize dactive.kp if enabled   */

    bzero(&Stats, sizeof(Stats));
    switch (DOpts.FeederRTStats) {
	case RTSTATS_NONE:
		HostStats = NULL;
		break;
	case RTSTATS_LABEL:
		HostStats = FeedStatsFindSlot(HLabel);
		break;
	case RTSTATS_HOST:
		HostStats = FeedStatsFindSlot(HName);
		break;
    }
    if (HostStats != NULL) {
	++HostStats->RecStats.ConnectCnt;
	if (HostStats->RecStats.TimeStart == 0)
	    HostStats->RecStats.TimeStart = time(NULL);
	LockFeedRegion(HostStats, XLOCK_UN, FSTATS_IN);
	LockFeedRegion(HostStats, XLOCK_EX, FSTATS_SPOOL);
	++HostStats->SpoolStats.ConnectCnt;
	if (HostStats->SpoolStats.TimeStart == 0)
	    HostStats->SpoolStats.TimeStart = time(NULL);
	LockFeedRegion(HostStats, XLOCK_UN, FSTATS_SPOOL);
    }
    bzero(&StoreStats, sizeof(StoreStats));

    /*
     * print header, start processing commands
     */

    if (DOpts.DisplayAdminVersion && DOpts.NewsAdmin != NULL)
	xfprintf(fo, "%d %s NNTP Service Ready - %s (DIABLO %s-%s)\r\n",
					ReadOnlyCxn ? 201 : 200,
					DOpts.FeederHostName,
					DOpts.NewsAdmin,
					VERS, SUBREV);
    else
	xfprintf(fo, "%d %s NNTP Service Ready\r\n",
					ReadOnlyCxn ? 201 : 200,
					DOpts.FeederHostName);
    fflush(fo);

    while ((buf = bgets(bi, NULL)) != NULL && buf != (char *)-1) {
	char *cmd;


	if (DebugOpt > 2) {
	    ddprintf("%d << %s", (int)getpid(), buf);
	}

	cmd = strtok(buf, " \t\r\n");

	if (cmd == NULL)
	    continue;

	if (ReadOnlyCxn && (strcasecmp(cmd, "ihave") == 0)) {
	    xfprintf(fo, "480 Read-only connection.\r\n");
	} else if (strcasecmp(cmd, "ihave") == 0) {
	    const char *msgidbuf = "";
	    const char *msgid = MsgId(strtok(NULL, "\r\n"), &msgidbuf);
	    int pre = 0;

	    TtlStats.ArtsTested += 1.0;
	    DoArtStats(STATS_OFFERED, STATS_IHAVE, 0);

	    /*
	     * The PreCommit cache also doubles as a recent history 'hit'
	     * cache, so check it first.
	     */

	    if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "435 %s Bad Message-ID\r\n", msgidbuf);
	    } else if (ArtHashIsFiltered(msgid)) {
		LogIncoming("%s - %s %s", HLabel, msgid, "IFILTER");
		xfprintf(fo, "435 %s\r\n", msgid);
		DoArtStats(STATS_REFUSED, STATS_REF_IFILTHASH, 0);
	    } else
#if DO_PCOMMIT_POSTCACHE
	    if ((pre = PreCommit(msgid, PC_PRECOMM)) < 0) {
		if (pre < -1 && !FeedPrecommitReject(HLabel)) {
		    xfprintf(fo, "436 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_PRECOMMIT, 0);
		} else {
		    xfprintf(fo, "435 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_POSTCOMMIT, 0);
		}
	    } else if (HistoryLookup(msgid, NULL) == 0) {
#if USE_PCOMMIT_RW_MAP
		(void)PreCommit(msgid, PC_POSTCOMM);
#endif
		xfprintf(fo, "435 %s\r\n", msgid);
		DoArtStats(STATS_REFUSED, STATS_REF_HISTORY, 0);
#else /* !DO_PCOMMIT_POSTCACHE */
	    if (HistoryLookup(msgid, NULL) == 0
	    ) {
		xfprintf(fo, "435 %s\r\n", msgid);
		DoArtStats(STATS_REFUSED, STATS_REF_HISTORY, 0);
	    } else if ((pre = PreCommit(msgid, PC_PRECOMM)) < 0) {
		if (pre < -1 && !FeedPrecommitReject(HLabel)) {
		    xfprintf(fo, "436 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_PRECOMMIT, 0);
		} else {
		    xfprintf(fo, "435 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_POSTCOMMIT, 0);
		}
#endif  /* DO_PCOMMIT_POSTCACHE */
#if USE_OFFER_FILTER
	    } else if (OfferIsFiltered(HLabel, msgid)) {
		xfprintf(fo, "435 %s Unwanted\r\n", msgidbuf);
		DoArtStats(STATS_REFUSED, STATS_REF_BADMSGID, 0);
#endif  /* USE_OFFER_FILTER */
	    } else {	/* Not found in history or pre/post commit cache */
		int r;
		char rejMsg[REJMSGSIZE];
		char artType[10] = "";

		rejMsg[0] = 0;

		xfprintf(fo, "335 %s\r\n", msgid);
		fflush(fo);
		switch((r = LoadArticle(bi, msgid, 0, headerOnly, rejMsg, artType))) {
		case RCOK:
		    xfprintf(fo, "235\r\n");	/* article posted ok	*/
		    break;
		case RCALREADY:
		    /*
		     * see RELEASE_NOTES V1.16-test8.  435 changed to 437.
		     */
		    LogIncoming("%s - %s %s", HLabel, msgid, "Duplicate");
		    xfprintf(fo, "437 Duplicate\r\n");	/* already have it */
#ifdef NOTDEF
		    xfprintf(fo, "435\r\n");	/* already have it	*/
#endif
		    break;
		case RCTRYAGAIN:
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "436 I/O error, try again later\r\n");
		    break;
		case RCREJECT:
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "437 Rejected %s\r\n", rejMsg);
		    break;
		case RCERROR:
		    /*
		     * protocol error during transfer (e.g. no terminating .)
		     */
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "436 Protocol error, missing terminator\r\n");
		    break;
		default:
		    /*
		     * An I/O error of some sort (e.g. disk full).
		     */
		    LogIncoming("%s - %s %s", HLabel, msgid, strerror(-r));
                    logit(LOG_ERR, "%-20s %s code 400 File Error: %s",
                        HName,
                        msgid,
                        strerror(-r)
                    ); 
		    LogSession();
		    sleep(30);	/* reduce remote reconnection rate */
		    xfprintf(fo, "400 File Error: %s\r\n", strerror(-r));
		    fflush(fo);
		    ArticleFileCloseAll();
		    ClosePathLog(1);
		    CloseArtLog(1);
		    DiabFilterClose(0);
		    sleep(5);
		    exit(1);
		    break;	/* not reached */
		}
	    }
	} else if (ReadOnlyCxn && (strcasecmp(cmd, "check") == 0)) {
	    xfprintf(fo, "480 Read-only connection.\r\n");
	} else if (strcasecmp(cmd, "check") == 0) {
	    const char *msgidbuf = "";
	    const char *msgid = MsgId(strtok(NULL, "\r\n"), &msgidbuf);
	    int pre = 0;

	    TtlStats.ArtsTested += 1.0;
	    DoArtStats(STATS_OFFERED, STATS_CHECK, 0);

	    /*
	     * The PreCommit cache may also double as a recent history 'hit'
	     * cache, so check it first in that case. 
	     */

	    if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "438 %s Bad Message-ID\r\n", msgidbuf);
		DoArtStats(STATS_REFUSED, STATS_REF_BADMSGID, 0);
	    } else if (ArtHashIsFiltered(msgid)) {
		LogIncoming("%s - %s %s", HLabel, msgid, "IFILTER");
		xfprintf(fo, "438 %s\r\n", msgid);
		DoArtStats(STATS_REFUSED, STATS_REF_IFILTHASH, 0);
	    } else
#if DO_PCOMMIT_POSTCACHE
	    if ((pre = PreCommit(msgid, PC_PRECOMM)) < 0) {
		if (pre < -1 && !FeedPrecommitReject(HLabel)) {
		    xfprintf(fo, "431 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_PRECOMMIT, 0);
		} else {
		    xfprintf(fo, "438 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_POSTCOMMIT, 0);
		}
	    } else if (HistoryLookup(msgid, NULL) == 0) {
#if USE_PCOMMIT_RW_MAP
		(void)PreCommit(msgid, PC_POSTCOMM);
#endif
		xfprintf(fo, "438 %s\r\n", msgid);
		DoArtStats(STATS_REFUSED, STATS_REF_HISTORY, 0);
#else  /* !DO_PCOMMIT_POSTCACHE */
	    if (HistoryLookup(msgid, NULL) == 0
	    ) {
		xfprintf(fo, "438 %s\r\n", msgid);
		(void)PreCommit(msgid, PC_POSTCOMM);
		DoArtStats(STATS_REFUSED, STATS_REF_HISTORY, 0);
	    } else if ((pre = PreCommit(msgid, PC_PRECOMM)) < 0) {
		if (pre < -1 && !FeedPrecommitReject(HLabel)) {
		    xfprintf(fo, "431 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_PRECOMMIT, 0);
		} else {
		    xfprintf(fo, "438 %s\r\n", msgid);
		    DoArtStats(STATS_REFUSED, STATS_REF_POSTCOMMIT, 0);
		}
#endif  /* DO_PCOMMIT_POSTCACHE */
#if USE_OFFER_FILTER
	    } else if (OfferIsFiltered(HLabel, msgid)) {
		xfprintf(fo, "438 %s Unwanted\r\n", msgidbuf);
		DoArtStats(STATS_REFUSED, STATS_REF_BADMSGID, 0);
#endif  /* USE_OFFER_FILTER */
	    } else {
		xfprintf(fo, "238 %s\r\n", msgid);
	    }
	} else if (ReadOnlyCxn && (strcasecmp(cmd, "takethis") == 0)) {
	    xfprintf(fo, "480 Read-only connection.\r\n");
	} else if (strcasecmp(cmd, "takethis") == 0) {
	    const char *msgid = MsgId(strtok(NULL, "\r\n"), NULL);
	    int r;
	    int alreadyResponded = 0;
	    char rejMsg[REJMSGSIZE];
	    char artType[10] = "";

	    rejMsg[0] = 0;

	    TtlStats.ArtsTested += 1.0;
	    DoArtStats(0, STATS_TAKETHIS, 0);
	    if (ArtHashIsFiltered(msgid)) {
		LogIncoming("%s - %s %s", HLabel, msgid, "IFILTER");
		xfprintf(fo, "439 %s\r\n", msgid);
		fflush(fo);
		LoadArticle(bi, msgid, 1, headerOnly, NULL, NULL);
		r = RCALREADY;
		alreadyResponded = 1;
	    } else if (HistoryLookup(msgid, NULL) == 0) {
		LogIncoming("%s - %s %s", HLabel, msgid, "Duplicate");
		xfprintf(fo, "439 %s\r\n", msgid);
		fflush(fo);
		LoadArticle(bi, msgid, 1, headerOnly, NULL, NULL);
		r = RCALREADY;
		alreadyResponded = 1;
	    } else {
		r = LoadArticle(bi, msgid, 0, headerOnly, rejMsg, artType);
	    }

	    if (alreadyResponded == 0) {
		switch(r) {
		case RCOK:
		    xfprintf(fo, "239 %s\r\n", msgid);	/* thank you */
		    break;
		case RCALREADY:
		    /* already have it or do not requeue it */
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "439 %s %s\r\n", msgid, rejMsg);
		    break;
		case RCTRYAGAIN:
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "431 %s %s\r\n", msgid, rejMsg);
		    break;
		case RCREJECT:
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "439 %s %s\r\n", msgid, rejMsg);
		    break;
		case RCERROR:
		    /* article failed due to something, do not req */
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    xfprintf(fo, "431 %s %s\r\n", msgid, rejMsg);
		    break;
		default:
		    /*
		     * An I/O error of some sort (e.g. disk full).
		     */
		    strcat(rejMsg, ", ");
		    strcat(rejMsg, strerror(-r));
		    LogIncoming("%s - %s %s", HLabel, msgid, rejMsg);
		    logit(LOG_ERR, "%-20s %s code 400 File Error: %s",
			HName,
			msgid,
			strerror(-r)
		    ); 
		    LogSession();
		    sleep(30);	/* reduce remote reconnection rate */
		    xfprintf(fo, "400 File Error: %s %s, %s\r\n", msgid, rejMsg, strerror(-r));
		    fflush(fo);
		    ArticleFileCloseAll();
		    ClosePathLog(1);
		    CloseArtLog(1);
		    DiabFilterClose(0);
		    sleep(5);
		    exit(1);
		    break;	/* not reached */
		}
	    }
	} else if (strcasecmp(cmd, "whereis") == 0 && FeedWhereIs(HLabel)) {
	    const char *msgid = MsgId(strtok(NULL, " \t\r\n"), NULL);
	    const char *how = strtok(NULL, " \t\r\n");
	    History h;

	    if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "443 Bad Message-ID\r\n");
	    } else if (HistoryLookup(msgid, &h) == 0 && !H_EXPIRED(h.exp)) {
		char path[PATH_MAX];
		if (how != NULL && strncmp(how, "REL", 3) == 0)
		    ArticleFileName(path, sizeof(path), &h, ARTFILE_FILE_REL);
		else
		    ArticleFileName(path, sizeof(path), &h, ARTFILE_FILE);
		xfprintf(fo, "223 0 whereis %s in %s offset %i length %i\r\n", msgid, path, h.boffset, h.bsize) ;
	    } else {
		if (H_EXPIRED(h.exp) && h.iter != (unsigned short)-1) {
		    xfprintf(fo, "430 Article expired\r\n");
		} else {
		    xfprintf(fo, "430 Article not found\r\n");
		}
	    }
	} else if (strcasecmp(cmd, "xrectime") == 0) {
	    const char *msgid = MsgId(strtok(NULL, "\r\n"), NULL);
	    History h;

	    if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "443 Bad Message-ID\r\n");
	    } else if (HistoryLookup(msgid, &h) == 0 && !H_EXPIRED(h.exp)) {
		xfprintf(fo, "223 %d\r\n", h.gmt * 60);
	    } else {
		if (H_EXPIRED(h.exp) && h.iter != (unsigned short)-1) {
		    xfprintf(fo, "430 Article expired\r\n");
		} else {
		    xfprintf(fo, "430 Article not found\r\n");
		}
	    }
	} else if (strcasecmp(cmd, "head") == 0 ||
		   strcasecmp(cmd, "body") == 0 ||
		   strcasecmp(cmd, "article") == 0
	) {
	    const char *msgid = MsgId(strtok(NULL, " \t\r\n"), NULL);
	    const char *fetchopt = strtok(NULL, " \t\r\n");
	    char *data = NULL;
	    int32 fsize = 0;
	    int pmart = 0;
	    int headOnly = 0;
	    int compressed = 0;
	    uint32 maxage = 0;
	    int error = 0;
	    History h;
	    enum ArtState { AS_ARTICLE, AS_BODY, AS_HEAD } as = AS_HEAD;

	    switch(cmd[0]) {
	    case 'b':
	    case 'B':
		as = AS_BODY;
		break;
	    case 'a':
	    case 'A':
		as = AS_ARTICLE;
		break;
	    default:	/* default, must be AS_HEAD */
		as = AS_HEAD;
		break;
	    }
	    h.exp = 0;

	    if (fetchopt && *fetchopt) {
		char *optptr = (char *)fetchopt;
		while (*optptr) {
		    switch(*optptr) {
		        case 'a':
			    maxage = strtoul(optptr + 1, &optptr, 10);
			    break;
			default:
			    optptr++;
			    error++;
			    break;
		    }
		}
	    }
	    if (error) {
		xfprintf(fo, "443 Bad Command Options\r\n");
	    } else if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "443 Bad Message-ID\r\n");
		switch(as) {
		case AS_BODY:
		    DoSpoolStats(STATS_S_BODYERR);
		    break;
		case AS_ARTICLE:
		    DoSpoolStats(STATS_S_ARTICLEERR);
		    break;
		default:
		    DoSpoolStats(STATS_S_HEADERR);
		    break;
		}
	    } else if (HistoryLookup(msgid, &h) == 0 && !H_EXPIRED(h.exp)) {
		if (ArticleOpen(&h, msgid, &data, &fsize, &pmart, &headOnly, &compressed) != 0)
		    data = NULL;
		if (maxage && maxage < ((int)(time(NULL)) - h.gmt * 60)) {
		    xfprintf(fo, "430 Article prohibited\r\n");
		    switch(as) {
		    case AS_BODY:
			DoSpoolStats(STATS_S_BODYPRO);
			break;
		    case AS_ARTICLE:
			DoSpoolStats(STATS_S_ARTICLEPRO);
			break;
		    default:
			DoSpoolStats(STATS_S_HEADPRO);
			break;
		    }
		    data = NULL;
		} else if (data && headOnly && as != AS_HEAD) {
		    xfprintf(fo, "430 Article not found\r\n");
		    switch(as) {
		    case AS_BODY:
			DoSpoolStats(STATS_S_BODYMISS);
			break;
		    case AS_ARTICLE:
			DoSpoolStats(STATS_S_ARTICLEMISS);
			break;
		    default:
			DoSpoolStats(STATS_S_HEADMISS);
			break;
		    }
		    if (DebugOpt > 2)
			ddprintf(">> (NO DATA: BODY/ARTICLE REQUEST FOR HEADER-ONLY STORE)");
		} else if (data) {
		    int doHead = 0;
		    int doBody = 0;
		    int bytes;

#ifdef	STATS_ART_AGE
		    logit(LOG_INFO, "articleage %s %d", msgid, (int)(time(NULL)) - h.gmt * 60);
#endif	/*STATS_ART_AGE*/

		    switch(as) {
		    case AS_BODY:
			xfprintf(fo, "222 0 body %s\r\n", msgid);
			doBody = 1;
			DoSpoolStats(STATS_S_BODY);
			break;
		    case AS_ARTICLE:
			xfprintf(fo, "220 0 article %s\r\n", msgid);
			doHead = 1;
			doBody = 1;
			DoSpoolStats(STATS_S_ARTICLE);
			break;
		    default:
			doHead = 1;
			xfprintf(fo, "221 0 head %s\r\n", msgid);
			DoSpoolStats(STATS_S_HEAD);
			break;
		    }
		    if (doBody && !compressed) {
			if (DOpts.SpoolPreloadArt)
			    xadvise(data, fsize, XADV_WILLNEED);
			xadvise(data, fsize, XADV_SEQUENTIAL);
		    }

		    if (DebugOpt > 2)
			ddprintf(">> (DATA)");

		    bytes = SendArticle(data, fsize, fo, doHead, doBody);

		    Stats.SpoolStats.ArtsBytesSent += (double)bytes;
		    if (HostStats != NULL) {
			LockFeedRegion(HostStats, XLOCK_EX, FSTATS_SPOOL);
			HostStats->SpoolStats.ArtsBytesSent += (double)bytes;
			LockFeedRegion(HostStats, XLOCK_UN, FSTATS_SPOOL);
		    }
		} else {
		    xfprintf(fo, "430 Article not found\r\n");
		    switch(as) {
		    case AS_BODY:
			DoSpoolStats(STATS_S_BODYEXP);
			break;
		    case AS_ARTICLE:
			DoSpoolStats(STATS_S_ARTICLEEXP);
			break;
		    default:
			DoSpoolStats(STATS_S_HEADEXP);
			break;
		    }
		    if (DebugOpt > 2)
			ddprintf(">> (NO DATA: UNABLE TO FIND ARTICLE)");
		}
	    } else {
		if (H_EXPIRED(h.exp) && h.iter != (unsigned short)-1) {
		    xfprintf(fo, "430 Article expired\r\n");
		    switch(as) {
		    case AS_BODY:
			DoSpoolStats(STATS_S_BODYEXP);
			break;
		    case AS_ARTICLE:
			DoSpoolStats(STATS_S_ARTICLEEXP);
			break;
		    default:
			DoSpoolStats(STATS_S_HEADEXP);
			break;
		    }
		} else {
		    xfprintf(fo, "430 Article not found\r\n");
		    switch(as) {
		    case AS_BODY:
			DoSpoolStats(STATS_S_BODYMISS);
			break;
		    case AS_ARTICLE:
			DoSpoolStats(STATS_S_ARTICLEMISS);
			break;
		    default:
			DoSpoolStats(STATS_S_HEADMISS);
			break;
		    }
		}
	    }
	    if (data) {
		if (compressed)
		    free(data);
		else
		    xunmap(data, fsize + pmart);
	    }
	} else if (strcasecmp(cmd, "stat") == 0) {
	    const char *msgid = MsgId(strtok(NULL, "\r\n"), NULL);
	    History h;

	    h.exp = 0;
	    if (strcmp(msgid, "<>") == 0) {
		xfprintf(fo, "443 Bad Message-ID\r\n");
		DoSpoolStats(STATS_S_STATERR);
	    } else if (HistoryLookup(msgid, &h) == 0 && !H_EXPIRED(h.exp)) {
		char path[128];
		struct stat st;

		/*
		 * Make sure article file hasn't been removed
		 */
		ArticleFileName(path, sizeof(path), &h, ARTFILE_FILE);
		if (h.bsize == 0)  {
		    xfprintf(fo, "430 Article not found\r\n");
		    DoSpoolStats(STATS_S_STATEXP);
		} else if (stat(path, &st) == 0) {
		    xfprintf(fo, "223 0 %s\r\n", msgid);
		    DoSpoolStats(STATS_S_STAT);
		} else {
		    xfprintf(fo, "430 Article expired\r\n");
		    DoSpoolStats(STATS_S_STATEXP);
		}
	    } else {
		if (H_EXPIRED(h.exp) && h.iter != (unsigned short)-1) {
		    xfprintf(fo, "430 Article expired\r\n");
		    DoSpoolStats(STATS_S_STATEXP);
		} else {
		    xfprintf(fo, "430 Article not found\r\n");
		    DoSpoolStats(STATS_S_STATMISS);
		}
	    }
	} else if (strcasecmp(cmd, "feedrset") == 0) {
	    if (HLabel[0]) {
		FeedRSet(fo);
	    } else {
		xfprintf(fo, "490 Operation not allowed\r\n");
	    }
	} else if (strcasecmp(cmd, "feedlist") == 0) {
	    if (HLabel[0]) {
		FeedList(fo);
	    } else {
		xfprintf(fo, "490 Operation not allowed\r\n");
	    }
	} else if (strcasecmp(cmd, "feedcommit") == 0) {
	    if (HLabel[0]) {
		if (FeedTableReady)
		    FeedCommit(fo);
		else
		    xfprintf(fo, "491 No feedrset/add/del\r\n");
	    } else {
		xfprintf(fo, "490 Operation not allowed\r\n");
	    }
	} else if (strcasecmp(cmd, "feedadd") == 0) {
	    char *p = strtok(NULL, " \t\r\n");
	    if (p) {
		if (HLabel[0]) {
		    if (FeedTableReady)
			FeedAddDel(fo, p, 1);
		    else
			xfprintf(fo, "491 No feedrset\r\n");
		} else {
		    xfprintf(fo, "490 Operation not allowed\r\n");
		}
	    } else {
		xfprintf(fo, "491 Syntax Error\r\n");
	    }
	} else if (strcasecmp(cmd, "feeddel") == 0) {
	    char *p = strtok(NULL, " \t\r\n");
	    if (p) {
		if (HLabel[0]) {
		    if (FeedTableReady)
			FeedAddDel(fo, p, -1);
		    else
			xfprintf(fo, "491 No feedrset\r\n");
		} else {
		    xfprintf(fo, "490 Operation not allowed\r\n");
		}
	    } else {
		xfprintf(fo, "491 Syntax Error\r\n");
	    }
	} else if (strcasecmp(cmd, "feeddelany") == 0) {
	    char *p = strtok(NULL, " \t\r\n");
	    if (p) {
		if (HLabel[0]) {
		    if (FeedTableReady)
			FeedAddDel(fo, p, -2);
		    else
			xfprintf(fo, "491 No feedrset\r\n");
		} else {
		    xfprintf(fo, "490 Operation not allowed\r\n");
		}
	    } else {
		xfprintf(fo, "491 Syntax Error\r\n");
	    }
	} else if (strcasecmp(cmd, "mode") == 0) {
	    char *p = strtok(NULL, " \t\r\n");
	    if (p && strcasecmp(p, "stream") == 0) {
		streamMode = 1;
		xfprintf(fo, "203 StreamOK.\r\n");
	    } else if (p && strcasecmp(p, "headfeed") == 0) {
		headerOnly = 1;
		xfprintf(fo, "250 Mode Command OK.\r\n");
	    } else if (p && strcasecmp(p, "readonly") == 0) {
		ReadOnlyCxn = 1;
		xfprintf(fo, "250 Mode Command OK.\r\n");
	    } else if (p && strcasecmp(p, "artfeed") == 0) {
		headerOnly = 0;
		xfprintf(fo, "250 Mode Command OK.\r\n");
	    } else if (p && strcasecmp(p, "reader") == 0) {
		unimp = 1;
#ifdef	USE_ZLIB
	    } else if (p && strcasecmp(p, "compress") == 0) {
		/* added for compression support */
		bi->bu_CBuf = (CompressBuffer *)malloc(sizeof(CompressBuffer));
                bzero(bi->bu_CBuf,sizeof(CompressBuffer));
                inflateInit(&bi->bu_CBuf->z_str);
                compressMode = 1;
		xfprintf(fo, "207 Compression enabled\r\n");
#endif
	    } else {
		syntax = 1;
	    }
	} else if (strcasecmp(cmd, "outq") == 0) {
	    int qnum;
	    int qarts;
	    int qrun;

	    if (HLabel[0] && QueueRange(HLabel, &qnum, &qarts, &qrun) == 0) {
		xfprintf(fo, "290 qfile-backlog=%d arts=%d now-running=%d\r\n", 
		    qnum,
		    qarts,
		    qrun
		);
	    } else {
		xfprintf(fo, "491 No queue info available\r\n");
	    }
	} else if (strcasecmp(cmd, "authinfo") == 0) {
	    xfprintf(fo, "281 Authentication ok, no authentication required\r\n");
/*
	} else if (strcasecmp(cmd, "stats") == 0) {
	    int dt = (int)(time(NULL) - SessionBeg);
	    DoStats(fo, dt, 0);
	    fflush(fo);
	    break;
*/
	} else if (strcasecmp(cmd, "quit") == 0) {
	    xfprintf(fo, "205 %s closing channel.\r\n", DOpts.FeederHostName);
	    fflush(fo);
	    break;
	} else if (strcasecmp(cmd, "help") == 0) {
	    xfprintf(fo, "100 Legal commands\r\n");
	    xfprintf(fo, "\tauthinfo\r\n"
			"\thelp\r\n"
			"\tihave\r\n"
			"\tcheck\r\n"
			"\ttakethis\r\n"
			"\tmode\r\n"
			"\tquit\r\n"
			"\thead\r\n"
			"\tstat\r\n"
			"\toutq\r\n"
			"\tfeedrset\r\n"
			"\tfeedadd grpwildcard\r\n"
			"\tfeeddel grpwildcard\r\n"
			"\tfeeddelany grpwildcard\r\n"
			"\tfeedlist\r\n"
			"\tfeedcommit\r\n"
	    );
	    xfprintf(fo, ".\r\n");
	} else {
	    syntax = 1;
	    if (strcasecmp(cmd, "list") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "group") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "last") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "newgroups") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "newnews") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "next") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "post") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "slave") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "xhdr") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "xpath") == 0)
		unimp = 1;
	    if (strcasecmp(cmd, "xreplic") == 0)
		unimp = 1;
	    if (unimp)
		syntax = 0;
	}
	if (unimp) {
	    xfprintf(fo, "500 command not implemented\r\n");
	    unimp = 0;
	}
	if (syntax) {
	    xfprintf(fo, "500 Syntax error or bad command\r\n");
	    syntax = 0;
	}
	fflush(fo);
	if (HasStatusLine) {
	    stprintf("ihav=%-4d chk=%-4d rec=%-4d ent=%-4d %s",
		Stats.RecStats.Stats[STATS_IHAVE],
		Stats.RecStats.Stats[STATS_CHECK],
		Stats.RecStats.Stats[STATS_OFFERED],
		Stats.RecStats.Stats[STATS_ACCEPTED],
		HName
	    );
	}
    }

#ifdef	USE_ZLIB
    if (bi->bu_CBuf) {
	logit(LOG_INFO, "%-20s compbytes=%.0f decompbytes=%.0f (%.2f%% compression)",
			HName,
			bi->bu_CBuf->orig,
			bi->bu_CBuf->decomp,
			100 - ((bi->bu_CBuf->orig/bi->bu_CBuf->decomp)*100)
	);
    }
#endif

    bclose(bi, 1);
    fclose(fo);
    ArticleFileCloseAll();
}

/*
 * Send a mmap'ed article to a FILE, doing conversion if necessary
 */
int
SendArticle(const char *data, int fsize, FILE *fo, int doHead, int doBody)
{
    int b;
    int i;
    int inHeader = 1;
    int bytes = 0;
    SpoolArtHdr ah;

    bcopy(data, &ah, sizeof(ah));

    if (fsize > 24 && (uint8)ah.Magic1 == STORE_MAGIC1 &&
				(uint8)ah.Magic2 == STORE_MAGIC2) {
	fsize -= ah.HeadLen;
	data += ah.HeadLen;
	if (doHead && doBody)
	    ;
	else if (doHead) {
	    fsize = ah.ArtHdrLen;
	} else if (doBody) {
	    data += ah.ArtHdrLen;
	    fsize -= ah.ArtHdrLen;
	    if (ah.StoreType & STORETYPE_WIRE) {
		data += 2;
		fsize -= 2;
	    }
	}
	if (ah.StoreType & STORETYPE_WIRE) {
	    if (doBody) {
		return(fwrite(data, 1, fsize, fo));
	    } else {
		int b;
		b = fwrite(data, 1, fsize, fo);
		xfprintf(fo, ".\r\n");
		return(b + 3);
	    }
	}
    } else if (*data == 0) {
	data++;
    }

    for (i = b = 0; i < fsize; b = i) {
	int separator = 0;

	/*
	 * find the end of line
	 */
	while (i < fsize && data[i] != '\n')
	    ++i;

	/*
	 * if in the headers, check for a blank line
	 */

	if (inHeader && i - b == 0) {
	    inHeader = 0;
	    separator = 1;
	    if (doBody == 0)
		break;
	}

	/*
	 * if printing the headers and/or the body, do any
	 * appropriate escape, write the line non-inclusive
	 * of the \n, then write a CR+LF.
	 *
	 * the blank line separating the header and body
	 * is only printed for the 'article' command.
	 */

	if ((inHeader && doHead) || (!inHeader && doBody)) {
	    if (separator == 0 || (doHead && doBody)) {
		if (data[b] == '.')
		    fputc('.', fo);
		if ((i - b) > 0)
		    fwrite(data + b, 1, i - b, fo);
		bytes += (i - b);
		fwrite("\r\n", 1, 2, fo);
		bytes += 2;
	    }
	}

	++i;	/* skip the nl */

	/*
	 * if i > fsize, we hit the end of the file without
	 * a terminating LF.  We don't have to do anything
	 * since we've already terminated the last line.
	 */
    }
    xfprintf(fo, ".\r\n");
    return(bytes + 3);
}
/*
 * LOADARTICLE()	- get an article from the remote
 *
 * If noWrite==1, then we know we already have the article but have to
 * accept it because the peer is currently sending it. In this case we
 * just ignore it when it arrives
 *
 */

#define	LAERR_ACTIVEDROP	0x000001
#define	LAERR_PATHTAB		0x000002
#define	LAERR_NGTAB		0x000004
#define	LAERR_ARTHASNUL		0x000008
#define	LAERR_MINSIZE		0x000010
#define	LAERR_HEADER		0x000020
#define	LAERR_TOOOLD		0x000040
#define	LAERR_SPOOL		0x000080
#define	LAERR_IO		0x000100
#define	LAERR_MSGID		0x000200
#define	LAERR_NOBYTES		0x000400
#define	LAERR_NOGROUPS		0x000800
#define	LAERR_GRPFILTER		0x001000
#define	LAERR_INCFILTER		0x002000
#define	LAERR_INTSPAM		0x004000
#define	LAERR_EXTSPAM		0x008000
#define	LAERR_POSTDUP		0x010000
#define	LAERR_ARTINCOMPL	0x020000
#define	LAERR_REJSPOOL		0x040000
#define	LAERR_TOOBIG		0x080000
#define	LAERR_HEADERTOOBIG	0x100000
#define	LAERR_NOHDREND		0x200000
#define	LAERR_ARTHASBARECR	0x400000

int
LoadArticle(Buffer *bi, const char *msgid, int noWrite, int headerOnly, char *rejBuf, char *artType)
{
    /*
     * create article file
     */
    int retcode = RCERROR;
    int size = 0;
    off_t bpos = -1;
    time_t t = time(NULL);
    History h = { 0 };
    int CompressLvl = -1;
    SpoolArtHdr artHdr;

    h.hv = hhash(msgid);

    if (rejBuf)
	strncpy(rejBuf, "0 000000 UnknownReject", REJMSGSIZE);

    /*
     * Obtain the file used to handle this article.  The file may already
     * be cached... generally, we wind up with one new file every 10 minutes.
     * to the file at the same time, we will be ok.
     */

    errno = 0;	/* in case noWrite is true */

    h.boffset = 1;	/* force new queue file format in ArticleFileName */
    h.bsize = 1;

    /*
     * Flush the article file cache to allow dexpire to delete old
     * files if necessary. This closes files older then 10 min.
     */
    ArticleFileCacheFlush(t);

    {
	Buffer *buffer = NULL;
	char *p;
	int inHeader = 1;
	int lastNl = 0;
	int thisNl = 1;
	int skipHeader = 0;
	int haveMsgId = 0;
	int haveBytes = 0;
	int artError = 0;
	int artFd = -1;
	int hdrDate = 0; /* number of Date: headers */
	int hdrSubject = 0;
	int hdrPath = 0;
	int hdrXref = 0;
	int hdrNewsgroups = 0;
	int hdrMessageId = 0;
	int haveApproved = 0;
	int bytes;
	int compressedSize = -1;
	int lncount = -1;
	int responded = 0;
	int headerEndLine;
	int bodyLines = 0;
	int addRejectToHistory = 0;
	int arttype = ARTTYPE_DEFAULT;
	char nglist[MAXLINE];
	char npath[MAXLINE];
	char xrefdata[MAXLINE];
	char dateBuf[64];
	char subject[80];
	char nntpPostingHost[64];
	char nntpXPostingHost[64];
	char control[64];
	char dist[80];
	int bps;
	int aflen = 0;
	int headerLen = 0;

	struct timespec delay;
	int delay_counter = 0;
	int delay_counter_max = 0;

	delay.tv_sec = 0;

	FeedGetThrottle(HLabel, (int *)&(delay.tv_nsec), &delay_counter_max);

	FeedGetMaxInboundRate(HLabel, &bps);

	nglist[0] = 0;
	npath[0] = 0;
	dateBuf[0] = 0;
	nntpPostingHost[0] = 0;
	nntpXPostingHost[0] = 0;
	subject[0] = 0;
	control[0] = 0;
	dist[0] = 0;
	xrefdata[0] = 0;

	if (BodyFilterFd != -1)
	    bhash_init();

	if (noWrite == 0) {
	    buffer = bopen(-1, DOpts.FeederBufferSize);
	    if (buffer == NULL) {
		logit(LOG_CRIT, "Unable to allocate memory buffer for incoming article");
		LogSession();
		ArticleFileCloseAll();
		ClosePathLog(1);
		CloseArtLog(1);
		DiabFilterClose(0);
		exit(1);
	    }
	}

	if (DOpts.FeederArtTypes && artType != NULL) {
	    arttype = ARTTYPE_DEFAULT;
	    InitArticleType();
	}
	while ((p = bgets(bi, &bytes)) != NULL && p != (char *)-1) {

	    if (delay_counter_max) {
		delay_counter++;
		if (delay_counter == delay_counter_max) {
		    delay_counter = 0;
		    if (delay.tv_nsec)
			nanosleep(&delay, NULL);
		}
	    }

	    size += bytes;

	    if (DOpts.FeederMaxArtSize > 0 && size > DOpts.FeederMaxArtSize)
		artError |= LAERR_TOOBIG;

	    lastNl = thisNl;
	    thisNl = (p[bytes-1] == '\n');
	    headerEndLine = 0;

	    if (artError == 0 && (DOpts.RejectArtsWithNul ||
						DOpts.RejectArtsWithBareCR)) {
		int i;
		for (i = 0; i < bytes; ++i) {
		    if (DOpts.RejectArtsWithNul && p[i] == 0) {
			artError |= LAERR_ARTHASNUL;
			break;
		    }
		    if (DOpts.RejectArtsWithBareCR && i > 0 && \
					p[i-1] == '\r' && p[i] != '\n') {
			artError |= LAERR_ARTHASBARECR;
			break;
		    }
		}
	    }

	    /*
	     * defer lone CR's (only occurs if the buffer overflows)
	     * replace CRLF's with LF's
	     */

	    if (!DOpts.WireFormat) {
		if (p[bytes-1] == '\r') {
		    bunget(bi, 1);
		    --bytes;
		} else if (bytes > 1 && p[bytes-1] == '\n' && p[bytes-2] == '\r') {
		    p[bytes-2] = '\n';
		    --bytes;
		}
		aflen = 1;
	    } else {
		aflen = 2;
	    }

	    /*
	     * Look for end of article
	     */
	    if (lastNl &&
			((DOpts.WireFormat && bytes == 3 &&
						strncmp(p, ".\r\n", 3) == 0) ||
			(!DOpts.WireFormat && bytes == 2 &&
						strncmp(p, ".\n", 2) == 0))) {
		if (inHeader)
		    artError |= LAERR_NOHDREND;
		if (DOpts.WireFormat && buffer != NULL &&
						!skipHeader && !artError)
		    bwrite(buffer, p, bytes);
		if (retcode == RCERROR)
		    retcode = RCOK;
		break;
	    }

	    /*
	     * Look for dot escape.  bytes may be 0 after this.  NOTE! 
	     * THERE IS NO TERMINATOR
	     */
	    if (!DOpts.WireFormat) {
		if (lastNl && *p == '.') {
		    ++p;
		    --bytes;
		}
	    }

	    if (artError != 0)
		continue;

	    /*
	     * See if we can categorise the article type
	     */
	    if (DOpts.FeederArtTypes && artType != NULL)
		arttype = ArticleType(p, bytes - aflen, inHeader);

	    /*
	     * Extract certain header information
	     */
	    if (inHeader && buffer != NULL) {
		char hch;
		/*
		 * if skipping a header-in-progress
		 */
		if (skipHeader) {
		    /*
		     * We didn't get the whole line from the previous header,
		     * so this line is part of the previous header.
		     */
		    if (lastNl == 0) 
			continue;

		    /*
		     * Header begins with a space or tab, it is a continuation
		     * from a previous header.
		     */
		    if (bytes && (p[0] == ' ' || p[0] == '\t'))
			continue;

		    /*
		     * header complete, skip terminated
		     */

		    skipHeader = 0;
		}

		/*
		 * If not skipping header and this is a continuation of a
		 * prior header due to a buffer overflow, write it out and 
		 * continue.  (this way we can support long paths independant
		 * of the buffer size).
		 */
		if (lastNl == 0) {
		    bwrite(buffer, p, bytes);
		    continue;
		}

		/*
		 * The header is too big to be sane
		 */
		if (DOpts.FeederMaxHeaderSize > 0 &&
				headerLen > DOpts.FeederMaxHeaderSize) {
		    artError |= LAERR_HEADERTOOBIG;
		    continue;
		}

		hch = tolower(*p);
		if (bytes <= 2 && thisNl &&
			(p[0] == '\n' || (p[0] == '\r' && p[1] == '\n'))) {
		    /*
		     * We have reached the end of the headers
		     * Do a bunch of checks on what we have so that we
		     * don't have to save the article if we don't want it
		     */

		    if (control[0])
			ngAddControl(nglist, sizeof(nglist), control);

		    /*
		     * handle Xref: header.  If 'activedrop yes' is set 
		     * and none of the groups can be found in the active 
		     * file, the article is dropped.
		     */
		    if (DOpts.FeederActiveEnabled) {
			if (!DOpts.FeederXRefSlave && DOpts.FeederXRefHost) {
			    /*
			     * Handle Approved: header, if 'activedrop
			     * yes'. Drop if the article does not contain
			     * an Approved: header and is posted to at
			     * least one moderated group.
			     */
			    if (DOpts.FeederActiveDrop &&
				    (haveApproved == 0) &&
				    (CheckForModeratedGroups(nglist) == 1))
			    {
				artError |= LAERR_ACTIVEDROP;
			    } else if (GenerateXRef(buffer, nglist,
					DOpts.FeederXRefHost, control) == 0 &&
					DOpts.FeederActiveDrop &&
					strncmp(control, "newgroup", 8) != 0) {
				artError |= LAERR_ACTIVEDROP;
			    }

			} else if (DOpts.FeederXRefSync) {
			    if (UpdateActiveNX(xrefdata) == 0 &&
							DOpts.FeederActiveDrop)
				artError |= LAERR_ACTIVEDROP;
			}
		    } else if (DOpts.FeederActiveDrop) {
			if (InActive(nglist) == 0 &&
				strncmp(control, "newgroup", 8) != 0) {
			    artError |= LAERR_ACTIVEDROP;
			}
		    }

		    headerLen = bsize(buffer);
		    inHeader = 0;
		    headerEndLine = 1;

		    /* End of headers */

#ifdef	LOG_FEED_SUPERCEDES
		/*
		   This is a test to catch Supersedes: headers and test
		   them a bit.  For now, we simply log them
		 */
		} else if (hch =='s' && bytes >= 11 &&
			   strncasecmp(p, "Supersedes:", 11) == 0) {
    			char lbuf[100];
			diablo_strlcpynl(lbuf, p, bytes, sizeof(lbuf));
			logit(LOG_DEBUG, "%s Supercede Detect %s %d %s %d",
			       lbuf, msgid, h.exp, nglist, lncount);
#endif	/* LOG_FEED_SUPERCEDES */
		} else if (hch =='b' && bytes >= 6 &&
			   strncasecmp(p, "Bytes:", 6) == 0) {
		    haveBytes = 1;
		    /* write thru */
		} else if (hch =='l' && bytes >= 6 &&
			   strncasecmp(p, "Lines:", 6) == 0) {
		    char lbuf[32];
		    diablo_strlcpynl(lbuf, p + 6, bytes - 6, sizeof(lbuf));
		    lncount = strtol(lbuf, NULL, 10);
		    /* write thru */
		} else if (hch == 'p' && bytes >= 5 &&
			   strncasecmp(p, "Path:", 5) == 0) {
		    char ipfail[128];
		    int idx = 0;
		    static int ReportedNoMatch = 0;
		    PathListType *pl;

		    hdrPath++;
		    /*
		     * Path: line, prepend our path element and store in npath.
		     */
		    p += 5;
		    bytes -= 5;

		    while (bytes && (*p == ' ' || *p == '\t')) {
			++p;
			--bytes;
		    }
		    /*
		     * FeedAdd breaks if Path: header contains a tab.  Drop
		     * the article (the idiots who put the tab in there have
		     * to fix their news system).
		     */
		    {
			int i;

			for (i = 0; i < bytes; ++i) {
			    if (p[i] == '\t' &&
					p[i+1] != '\r' && p[i+1] != '\n') {
				artError |= LAERR_PATHTAB;
			    }
			}
		    }

		    /*
		     * check first path element against aliases
		     * in dnewsfeeds file.
		     */
		    if (PathElmMatches(HLabel, p, bytes, &idx) < 0) {
			sprintf(ipfail, "%s.MISMATCH!", PeerIpName);
			if (ReportedNoMatch == 0) {
			    ReportedNoMatch = 1;
			    logit(LOG_NOTICE, "%-20s Path element fails to match aliases: %*.*s in %s",
				    HName, idx, idx, p, msgid);
			}
		    } else {
			strcpy(ipfail, "");
		    }
		    /*
		     * write out new Path: line.  FeederPathHost can be
		     * an empty string (make news node act as a bridge).
		     *
		     * write out all Path: options in the order they
		     * were specified on the command-line. This allows
		     * multiple entries to be specified
		     */
		    bwrite(buffer, "Path: ", 6);
		    for (pl = DOpts.PathList; pl != NULL; pl = pl->next) {
			if (pl->pathtype == 1 && pl->pathent[0]) {
			    bwrite(buffer, pl->pathent, strlen(pl->pathent));
			    bwrite(buffer, "!", 1);
			}
			if (pl->pathtype == 2 && 
				(CommonElmMatches(pl->pathent, p, bytes - aflen) < 0)) {
			    bwrite(buffer, pl->pathent, strlen(pl->pathent));
			    bwrite(buffer, "!", 1);
			}
		    }
		    bwrite(buffer, ipfail, strlen(ipfail));
		    diablo_strlcpynl(npath, p, bytes - aflen, sizeof(npath));
		} else if (hch == 'x' && bytes >= 5 &&
			   strncasecmp(p, "Xref:", 5) == 0) {
		    hdrXref++;
		    if (DOpts.FeederXRefSlave == 0) {
			skipHeader = 1;
		    } else if (DOpts.FeederXRefSync) {
			/* Keep Xref: for storing NX record */
			diablo_strlcpynl(xrefdata, p + 5, bytes - 5 - aflen,
							sizeof(xrefdata));
			if (DebugOpt > 2)
			    ddprintf("FOUND XREF INFO: %s\n",xrefdata);
		    }
		    /* write thru */
		} else if (hch == 'd' && bytes >= 13 &&
			   strncasecmp(p, "Distribution:", 13) == 0) {
		    diablo_strlcpynl(dist, p + 13, bytes - 13 - aflen, sizeof(dist));
		    /* write thru */
		} else if (hch == 'n' && bytes >= 11 &&
			   strncasecmp(p, "Newsgroups:", 11) == 0) {
		    hdrNewsgroups++;
		    diablo_strlcpynl(nglist, p + 11, bytes - 11 - aflen, MAXLINE);

		    /*
		     * FeedAdd breaks if newsgroups: header contains a
		     * tab, but we allow trailing tabs because many systems 
		     * are screwed and add one.  diablo_strlcpynl will deal with it.
		     */
		    {
			int i;

			for (i = 11; i < bytes; ++i) {
			    if (p[i] == '\t' &&
					p[i+1] != '\n' && p[i+1] != '\r') {
				artError |= LAERR_NGTAB;
			    }
			}
		    }
		} else if (hch == 's' && bytes >= 8 &&
			   strncasecmp(p, "Subject:", 8) == 0) {
		    hdrSubject++;
		    /*
		     * copy & remove newline
		     */
		    diablo_strlcpynl(subject, p + 8, bytes - 8 - aflen, sizeof(subject));
		    /* 
		     * if the body is the same but the subject is different
		     * (aka: control messages), include them in the body
		     * hash so the SpamFilter does not filter them.
		     */
		    if (BodyFilterFd != -1)
			bhash_update(p, bytes);
		} else if (hch == 'd' && bytes >= 5 &&
			   strncasecmp(p, "Date:", 5) == 0) {
		    hdrDate++;
		    if (DOpts.FeederMaxAcceptAge >= 0) {
		    	diablo_strlcpynl(dateBuf, p + 5, bytes - 5 - aflen, sizeof(dateBuf));
		    }
		} else if (NphFilterFd != -1 &&
			   hch == 'n' &&
			   bytes >= 18 && 
			   strncasecmp(p, "NNTP-Posting-Host:", 18) == 0) {
		    diablo_strlcpynl(nntpPostingHost, p + 18, bytes - 18 - aflen,
						sizeof(nntpPostingHost));
#ifdef	USE_X_ORIGINAL_NPH
		} else if (hch == 'x' && NphFilterFd != -1 && bytes >= 29 &&
			   (strncasecmp(p, "X-Original-NNTP-Posting-Host:", 29) == 0 )) {
			diablo_strlcpynl(nntpXPostingHost, p + 29, bytes - 29,
					 sizeof(nntpXPostingHost));
#endif
		} else if (hch =='m' && bytes >= 11 &&
				strncasecmp(p, "Message-ID:", 11) == 0) {
		    char *ps;
		    char *pe;

		    hdrMessageId++;
		    for (ps = p + 11; ps - p < bytes && *ps != '<'; ++ps)
			;
		    for (pe = ps; pe - p < bytes && *pe != '>'; ++pe)
			;

		    if (pe - p < bytes) {
			int l = pe - ps + 1;
			if (strlen(msgid) == l && strncmp(msgid, ps, l) == 0)
			    haveMsgId = 1;
		    }
		    if (haveMsgId == 0) {
			int l = bytes - (ps - p);

			logit(LOG_NOTICE, "%-20s message-id mismatch, command: %s, article: %*.*s",
			    HName,
			    msgid,
			    l, l, ps
			);
		    }
		} else if (hch == 'c' && bytes > 8 &&
			   strncasecmp(p, "Control:", 8) == 0) {
		    int i = 8;
		    int j = 0;

		    while (i < bytes && (p[i] == ' ' || p[i] == '\t'))
			++i;
		    while (j < sizeof(control) - 1 && i < bytes && isalpha((int)p[i]))
			control[j++] = p[i++];
		    control[j] = 0;
		} else if (hch == 'a' && bytes >= 9 &&
			   strncasecmp(p, "Approved:", 9) == 0) {
		    haveApproved = 1;
		    /* write thru */
		}
	    } /* inHeader */

	    /*
	     * If not in header hash the article body for the spam filter.
	     * This is relatively easy to defeat (the NNTP-Posting-Host:
	     * is less so), but will still catch a shitload of spam since
	     * the article body is usually 100% duplicated.
	     */

	    if (buffer != NULL && !skipHeader && !artError) {
		if (BodyFilterFd != -1 && !inHeader && !headerEndLine) {
		    bhash_update(p, bytes);
		    bodyLines++;
		}
		bwrite(buffer, p, bytes);
	    }
	} /* while bgets */

	if (artType)
	    sprintf(artType, "%06x", arttype);

	/*
	 * noWrite==1 means we received an article that we knew was going
	 * to be rejected when LoadArticle was called.
	 * i.e: A history lookup returned positive on a TAKETHIS
	 */
	if (noWrite)
	    artError |= LAERR_POSTDUP;

	/*
	 * If retcode == RCERROR then we didn't get a complete article, so
	 * reject it. Hopefully we get it from somewhere else.
	 *
	 * XXX Is it worth asking remote to try again? Could cause loops.
	 */
	if (retcode == RCERROR)
	    artError |= LAERR_ARTINCOMPL;

	/*
	 * Check that the article has a minimum size. An article of size
	 * 3 is quite usual if you have a peer which offers you an article,
	 * but doesn't actually deliver it (e.g: it no longer exists).
	 */
	if (retcode == RCOK && size < MINARTSIZE)
	    artError |= LAERR_MINSIZE;

	/*
	 * NOTE: The following header checks could be done just after
	 * receiving the article header, but then we won't catch them
	 * if there is no article body.
	 */

	/*
	 * Check that exactly one of each of the important
	 *headers exists
	 */
	if (!artError && (hdrDate != 1 || hdrSubject != 1 ||
				hdrMessageId != 1 || hdrPath != 1 ||
				hdrXref > 1 || hdrNewsgroups != 1)) {
	    logit(LOG_INFO, "%-20s header error in article %s: #Date=%d, #Subject=%d, #MessageId=%d, #Path=%d, #Xref=%d, #Newsgroups=%d",
					HName, msgid,
					hdrDate, hdrSubject, hdrMessageId,
					hdrPath, hdrXref, hdrNewsgroups
	    );
	    artError |= LAERR_HEADER;
	}

	/*
	 * Check that the article Message-ID: matches the
	 * msgid offered
	 */
	if (!artError && haveMsgId == 0)
	    artError |= LAERR_MSGID;

	/*
	 * A header-only feed must supply a Bytes: header
	 */
	if (headerOnly && haveBytes == 0)
	    artError |= LAERR_NOBYTES;

	/*
	 * An article with no newsgroups is a bit pointless
	 */
	if (!artError && nglist[0] == 0)
	    artError |= LAERR_NOGROUPS;

	/*
	 * If the messsage is too old, reject it.
	 *
	 * XXX reject the article if parsedate doesn't have
	 * a clue ?
	 */
	if (!artError && hdrDate && dateBuf[0]) {
	    time_t tart = parsedate(dateBuf);

	    if (tart != (time_t)-1) {
		int32 dt = t - tart;
		if (dt > DOpts.FeederMaxAcceptAge || dt < -DOpts.FeederMaxAcceptAge)
		    artError |= LAERR_TOOOLD;
	    }
	}

	/*
	 * If the article from this incoming feed is filtered
	 * out due to group firewall.  The default, if no
	 * filter directives match, is to not filter
	 * (hence > 0 rather then >= 0)
	 */

	if (!artError && IsFiltered(HLabel, nglist) > 0)
		artError |= LAERR_GRPFILTER;

	/*
	 * Check that the article passes the incoming filter(s)
	 */
	if (retcode == RCOK && !artError &&
		FeedFilter(HLabel, nglist, npath, dist, artType, size))
	    artError |= LAERR_INCFILTER;

	/*
	 * XXX put other inbound filters here.  Be careful in regards to
	 *     what gets added to the history file and what does not, the
	 *     message may be valid when received via some other path.
	 */

	/*
	 * If everything is ok, check the spam filter
	 */

	if (retcode == RCOK && !artError && DOpts.SpamFilterOpt != NULL &&
			FeedSpam(1, nglist, npath, dist, artType, size)){
	    int rv = 0;
	    int how = 0;

#ifdef	USE_X_ORIGINAL_NPH
	    if (nntpXPostingHost[0])
		strcpy(nntpPostingHost, nntpXPostingHost);
#endif

	    if (nntpPostingHost[0])
		rv = FeedQuerySpam(HLabel, nntpPostingHost);

	    if (rv == 0) {
		SpamInfo spamInfo;

		bzero(&spamInfo, sizeof(spamInfo));
		bhash_final(&spamInfo.BodyHash);
		if (nntpPostingHost[0]) {
		    spamInfo.PostingHost = nntpPostingHost;
		    memcpy(&spamInfo.PostingHostHash, md5hash(nntpPostingHost),
					sizeof(spamInfo.PostingHostHash));
		}
		spamInfo.Lines = bodyLines;
		spamInfo.MsgIdHash = hhash(msgid);
		rv = SpamFilter(t, &spamInfo, &how);
	    }

	    if (rv < 0) {
		/*
		 * Reject the article as being spam
		 */ 

		if ((-rv & 31) == 1) {
		    logit(LOG_INFO, "SpamFilter/%s copy #%d: %s %d %s (%s)", 
			((how == 0) ? "dnewsfeeds" : ((how == 1) ?
					"by-post-rate" : "by-dup-body")),
			((how == 0) ? -1 : -rv),
			msgid, bodyLines, nntpPostingHost, subject
		    );
		}
		if (DebugOpt > 1)
		    printf("SpamFilter: %s\t%s\n", msgid, nntpPostingHost);
		artError |= LAERR_INTSPAM;
	    }
	}

	/*
	 * Find which spool the article should reside on and open up
	 * a buffer fd - only if we want the article (noWrite == 0)
	 *
	 * Note that if the message is to be rejected, the article
	 * is set to a zero size on the spool, effectively deleting it
	 */

	if (retcode == RCOK && !artError && buffer != NULL) {
	    int interval = 0;
	    char z = 0;
	    uint16 spool = 0;

	    h.exp = 0;
	    spool = GetSpool(msgid, nglist, size, arttype, HLabel, &interval, &CompressLvl);
	    if (interval && t - SpoolAllocTime >= interval) {
		if (DebugOpt)
		    printf("Allocating new spools\n");
		ArticleFileCloseAll();
		SpoolAllocTime = t;
		if (AllocateSpools(SpoolAllocTime) == -1) {
		    sleep(10);
		    exit(-1);
		}
		spool = GetSpool(msgid, nglist, size, arttype,
						HLabel, NULL, &CompressLvl);
	    }
	    h.gmt = SpoolDirTime();
	    if (spool <= 100) {
#ifdef USE_ZLIB
		gzFile *cfile = NULL;
#else
		char *cfile = NULL;
#endif
		h.exp = spool + 100;
		h.bsize = bsize(buffer) + sizeof(artHdr);
		artFd = ArticleFile(&h, &bpos, CompressLvl, &cfile);
		if (artFd >= 0) {
		    h.bsize = bsize(buffer) + sizeof(artHdr);
		    artHdr.Magic1 = STORE_MAGIC1;
		    artHdr.Magic2 = STORE_MAGIC2;
		    artHdr.Version = STOREAPI_REVISION;
		    artHdr.StoreType = (DOpts.WireFormat ?
					STORETYPE_WIRE : STORETYPE_TEXT);
		    if (cfile != NULL)
			artHdr.StoreType |= STORETYPE_GZIP;
		    artHdr.HeadLen = sizeof(SpoolArtHdr);
		    artHdr.ArtHdrLen = headerLen;
		    artHdr.ArtLen = bsize(buffer);
		    artHdr.StoreLen = h.bsize + 1;
		    write(artFd, &artHdr, sizeof(artHdr));
		    bsetfd(buffer, artFd);
#ifdef USE_ZLIB
		    if (cfile != NULL)
			bsetcompress(buffer, cfile);
		    bflush(buffer);
		    if (cfile != NULL) {
			gzclose(cfile);
			bsetcompress(buffer, NULL);
		    }
#else
		    bflush(buffer);
#endif
		} else {
		    artError |= LAERR_IO;
		}
	    } else {
		/*
		 * This means we don't have metaspool object for this
		 * article.  Reject/Accept it and add it to history.
		 */
		artFd = -1;
		if ((int16)spool > -3)
		    artError |= LAERR_SPOOL;
		if ((int16)spool == -2)
		    artError |= LAERR_REJSPOOL;
	    }

	    /*
	     * Write out the article
	     */
	    h.boffset = (uint32)bpos;
#ifdef USE_ZLIB
	    if (CompressLvl >= 0 && CompressLvl <= 9) {
		off_t filePos;

		compressedSize = btell(buffer) - bpos;
		h.bsize = bzwrote(buffer) + sizeof(artHdr);
		filePos = lseek(artFd, 0, SEEK_CUR);
		lseek(artFd, bpos, SEEK_SET);
		artHdr.StoreLen = compressedSize;
		write(artFd, &artHdr, sizeof(artHdr));
		lseek(artFd, filePos, SEEK_SET);
	    }
#endif
	    bwrite(buffer, &z, 1);		/* terminator (sanity check) */
	    bflush(buffer);
	    if (DebugOpt > 1)
		ddprintf("%s: b=%08lx artFd=%d boff=%d bsize=%d",
			msgid, (long)buffer, artFd,
			(int)h.boffset, (int)h.bsize
		);
	}

	/*
	 * Now report on any accumulated errors
	 *
	 * NOTE: The ordering is important as it determines which errors
	 * are given priority
	 */

	if (!responded && artError > 0) {
	    if (artError & LAERR_POSTDUP) {
		DoArtStats(STATS_REJECTED, STATS_REJ_POSDUP, size);
		DEBUGLOG(msgid, "PostDuplicate2");
		SETREJECT("PostDuplicate2");
	    } else if (artError & LAERR_ARTINCOMPL) {
		DoArtStats(STATS_REJECTED, STATS_REJ_ARTINCOMPL, size);
		DEBUGLOG(msgid, "ArtIncomplete");
		SETREJECT("ArtIncomplete");
	    } else if (artError & LAERR_MINSIZE){
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_TOOSMALL, size);
		DEBUGLOG(msgid, "TooSmall");
		SETREJECT("TooSmall");
	    } else if (artError & LAERR_TOOBIG){
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_TOOBIG, size);
		DEBUGLOG(msgid, "TooBig");
		SETREJECT("TooBig");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_HEADER){
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_HDRERROR, size);
		DEBUGLOG(msgid, "MissingHeader");
		SETREJECT("MissingHeader");
	    } else if (artError & LAERR_NOGROUPS) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_MISSHDRS, size);
		DEBUGLOG(msgid, "NoNewsgroups");
		SETREJECT("NoNewsgroups");
	    } else if (artError & LAERR_ACTIVEDROP) {
		DoArtStats(STATS_REJECTED, STATS_REJ_NOTINACTV, size);
		DEBUGLOG(msgid, "NotInActive");
		SETREJECT("NotInActive");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_MSGID) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_MSGIDMIS, size);
		DEBUGLOG(msgid, "MsgIdMismatch");
		SETREJECT("MsgIdMismatch");
	    } else if (artError & LAERR_TOOOLD) {
		DoArtStats(STATS_REJECTED, STATS_REJ_TOOOLD, size);
		DEBUGLOG(msgid, "TooOld");
		SETREJECT("TooOld");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_PATHTAB) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_PATHTAB, size);
		DEBUGLOG(msgid, "PathTab");
		SETREJECT("PathTab");
	    } else if (artError & LAERR_NGTAB) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_NGTAB, size);
		DEBUGLOG(msgid, "NewsgroupsTab");
		SETREJECT("NewsgroupsTab");
	    } else if (artError & LAERR_ARTHASNUL) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_ARTNUL, size);
		DEBUGLOG(msgid, "ArtHasNul");
		SETREJECT("artHasNul");
	    } else if (artError & LAERR_NOBYTES) {
		(void)PreCommit(msgid, PC_DELCOMM);
	    } else if (artError & LAERR_ARTHASBARECR) {
		DoArtStats(STATS_REJECTED, STATS_REJ_BARECR, size);
		DEBUGLOG(msgid, "ArtHasBareCR");
		SETREJECT("artHasBareCR");
		(void)PreCommit(msgid, PC_DELCOMM);
	    } else if (artError & LAERR_NOBYTES) {
		DoArtStats(STATS_REJECTED, STATS_REJ_NOBYTES, size);
		DEBUGLOG(msgid, "HdrOnlyNoBytes");
		SETREJECT("HdrOnlyNoBytes");
	    } else if (artError & LAERR_GRPFILTER) {
		DoArtStats(STATS_REJECTED, STATS_REJ_GRPFILTER, size);
		DEBUGLOG(msgid, "GroupFilter");
		SETREJECT("GroupFilter");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_INCFILTER) {
		DoArtStats(STATS_REJECTED, STATS_REJ_INCFILTER, size);
		DEBUGLOG(msgid, "IncomingFilter");
		SETREJECT("IncomingFilter");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_INTSPAM) {
		DoArtStats(STATS_REJECTED, STATS_REJ_INTSPAMFILTER, size);
		DEBUGLOG(msgid, "InternalSpamFilter");
		SETREJECT("InternalSpamFilter");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_EXTSPAM) {
		DoArtStats(STATS_REJECTED, STATS_REJ_EXTSPAMFILTER, size);
		DEBUGLOG(msgid, "ExternalSpamFilter");
		SETREJECT("ExternalSpamFilter");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_REJSPOOL) {
		DoArtStats(STATS_REJECTED, STATS_REJ_NOSPOOL, size);
		DEBUGLOG(msgid, "RejSpool");
		SETREJECT("Unwanted");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_SPOOL) {
		DoArtStats(STATS_REJECTED, STATS_REJ_NOSPOOL, size);
		DEBUGLOG(msgid, "NoSpool");
		SETREJECT("NoSpool");
		addRejectToHistory = 1;
	    } else if (artError & LAERR_IO) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_IOERROR, size);
		DEBUGLOG(msgid, "IOError/Missing-dir");
		SETREJECT("IOError/Missing-dir");
	    } else if (artError & LAERR_NOHDREND) {
		(void)PreCommit(msgid, PC_DELCOMM);
		DoArtStats(STATS_REJECTED, STATS_REJ_NOHDREND, size);
		DEBUGLOG(msgid, "No end of header");
		SETREJECT("NoHdrEnd");
	    } else if (artError & LAERR_HEADERTOOBIG) {
		DoArtStats(STATS_REJECTED, STATS_REJ_BIGHEADER, size);
		DEBUGLOG(msgid, "Header too big");
		SETREJECT("HeaderTooBig");
		addRejectToHistory = 1;
	    }
	    responded = 1;
	    retcode = RCREJECT;
	}

	if (DebugOpt > 1)
	    ddprintf("nglist: %s", nglist);

	/*
	 * check output file for error, only if retcode == RCOK.
	 * If there was a problem, change the return code
	 * to RCTRYAGAIN.
	 */

	if (retcode == RCOK && buffer != NULL) {
	    int rcode;
	    if ((rcode = berror(buffer)) != 0) {
		sleep(1);		/* failsafe */
		logit(LOG_CRIT, "%s Error writing article file (%s)",
						HName, strerror(rcode));
		DoArtStats(STATS_REJECTED, STATS_REJ_FAILSAFE, size);
		DEBUGLOG(msgid, "FileWriteError");
		SETREJECT("FileWriteError");
		/* Don't add to history */
		(void)PreCommit(msgid, PC_DELCOMM);
		responded = 1;
		retcode = RCTRYAGAIN;
	    }
	}

	if (buffer != NULL) {
	    /*
	     * close our buffered I/O without closing the underlying
	     * descriptor (bclose() does not flush!).
	     */
	    bflush(buffer);
	    bclose(buffer, 0);
	    buffer = NULL;
	}

	/*
	 * If the article does not have a distribution, add one.
	 */

	if (dist[0] == 0)
	    strcpy(dist, "world");

	/*
	 * If everything is ok, commit the message
	 */

	if (retcode == RCOK) {
	    int rhist;

#if USE_SYSV_SIGNALS
	    sighold(SIGHUP);
	    sighold(SIGALRM);
#else
	    int smask = sigblock((1 << SIGHUP) | (1 << SIGALRM));
#endif
	    if (headerOnly)
		h.exp |= EXPF_HEADONLY;

	    /*
	     * If we didn't store the article, reflect that in history
	     * and don't offer to feeds
	     */
	    if (artFd == -1) {
		h.boffset = 0;
		h.bsize = 0;
		h.gmt = t / 60;
		h.iter = 0;
		h.exp |= EXPF_EXPIRED;
	    }

	    rhist = HistoryAdd(msgid, &h);
	    (void)PreCommit(msgid, PC_POSTCOMM);

	    if (artFd == -1) {
		SETREJECT("DontStore");
		LogIncoming("%s + %s %s", HLabel, msgid, rejBuf);
	    } else if (rhist == RCOK) {
		char cSize[20];
		if (compressedSize != -1)
		    sprintf(cSize, "%d", compressedSize);
		else
		    cSize[0] = 0;
		StoreStats.StoreBytes += h.bsize;
		if (compressedSize != -1)
		    StoreStats.StoreCompressedBytes += compressedSize;
		if (FeedAdd(msgid, t, &h, nglist, npath, dist,
					headerOnly, artType, cSize) < 0) {
		    /*
		     * If we lose our pipe, exit immediately.
		     */
		    ArticleFileCloseAll();
		    LogSession();
		    ClosePathLog(1);
		    CloseArtLog(1);
		    DiabFilterClose(0);
		    exit(1);
		}
	    } else if (rhist == RCALREADY) {
		DoArtStats(STATS_REJECTED, STATS_REJ_POSDUP, size);
		DEBUGLOG(msgid, "PostDuplicate");
		SETREJECT("PostDuplicate");
		responded = 1;
		retcode = RCALREADY;
	    } else {
		/*
		 * XXX: This should not happen
		 */
		DoArtStats(STATS_REJECTED, STATS_REJ_IOERROR, size);
		DEBUGLOG(msgid, "IOErrorX");
		SETREJECT("IOErrorX");
		(void)PreCommit(msgid, PC_DELCOMM);
		responded = 1;
		retcode = RCREJECT;
	    }
#if USE_SYSV_SIGNALS
	    sigrelse(SIGHUP);
	    sigrelse(SIGALRM);
#else
	    sigsetmask(smask);
#endif
	} else if (addRejectToHistory) {
	    int rhist;

#if USE_SYSV_SIGNALS
	    sighold(SIGHUP);
	    sighold(SIGALRM);
#else
	    int smask = sigblock((1 << SIGHUP) | (1 << SIGALRM));
#endif

	    h.boffset = 0;
	    h.bsize = 0;
	    h.gmt = t / 60;
	    h.iter = (unsigned short)-1;
	    h.exp |= EXPF_EXPIRED;
	    rhist = HistoryAdd(msgid, &h);
	    (void)PreCommit(msgid, PC_POSTCOMM);

#if USE_SYSV_SIGNALS
	    sigrelse(SIGHUP);
	    sigrelse(SIGALRM);
#else
	    sigsetmask(smask);
#endif
	}

	DoArtStats(STATS_RECEIVED, 0, size);

	if (retcode == RCOK) {
	    if (control[0]) {
		DoArtStats(STATS_ACCEPTED, STATS_CONTROL, size);
		DEBUGLOG(msgid, "AcceptedControl");
	    } else {
		DoArtStats(STATS_ACCEPTED, 0, size);
		DEBUGLOG(msgid, "Accepted");
	    }
	} else {
	    if (!responded) {
		logit(LOG_ERR, "Internal error: retcode=%d, but responded=0. Please report.\n",
		       retcode);
	    }
	}

	if ((Stats.RecStats.Stats[STATS_RECEIVED] % 1024) == 0)
	    LogSession();

	/*
	 * Record the current append position, possibly
	 * updating the filesize in the article file cache.
	 *
	 * If we created a file but the return code is
	 * not RCOK, truncate the file.
	 */
	if (artFd >= 0)
	    ArticleFileSetSize(artFd);

	if (retcode != RCOK && artFd >= 0) {
	    ArticleFileTrunc(artFd, bpos);
	}
    }
    return(retcode);
}

void
ngAddControl(char *nglist, int ngSize, const char *ctl)
{
    /*
     * If a control message, append 'control.CTLNAME' to grouplist
     */
    int l = strlen(nglist);

    if (l && nglist[l-1] == '\n')
	--l;
    if (l && nglist[l-1] == '\r')
	--l;

    if (strlen(ctl) + l < ngSize - 11)
	sprintf(nglist + l, ",control.%s", ctl);
}


/*
 * LOGSESSION()	- Log statistics for a session
 */

void
LogSession(void)
{
    time_t t = time(NULL);
    int32 dt = t - SessionMark;
    int32 nuse;

    nuse = Stats.RecStats.Stats[STATS_CHECK] +
					Stats.RecStats.Stats[STATS_IHAVE];
    if (nuse < Stats.RecStats.Stats[STATS_RECEIVED])
	nuse = Stats.RecStats.Stats[STATS_RECEIVED];

    if (StoreStats.StoreCompressedBytes > 0.0) {
	logit(LOG_INFO, "store secs=%d artbytes=%.0f compbytes=%.0f",
	 	dt,
		StoreStats.StoreBytes,
		StoreStats.StoreCompressedBytes
	);
	bzero(&StoreStats, sizeof(StoreStats));
    }

    logit(LOG_NOTICE, "%-20s secs=%d ihave=%d chk=%d takethis=%d rec=%d acc=%d ref=%d precom=%d postcom=%d his=%d badmsgid=%d ifilthash=%d rej=%d ctl=%d spam=%d err=%d recbytes=%.0f accbytes=%.0f rejbytes=%.0f (%d/sec)",
	HName,
	dt,
	Stats.RecStats.Stats[STATS_IHAVE],
	Stats.RecStats.Stats[STATS_CHECK],
	Stats.RecStats.Stats[STATS_TAKETHIS],
	Stats.RecStats.Stats[STATS_RECEIVED],
	Stats.RecStats.Stats[STATS_ACCEPTED],
	Stats.RecStats.Stats[STATS_REFUSED],
	Stats.RecStats.Stats[STATS_REF_PRECOMMIT],
	Stats.RecStats.Stats[STATS_REF_POSTCOMMIT],
	Stats.RecStats.Stats[STATS_REF_HISTORY],
	Stats.RecStats.Stats[STATS_REF_BADMSGID],
	Stats.RecStats.Stats[STATS_REF_IFILTHASH],
	Stats.RecStats.Stats[STATS_REJECTED],
	Stats.RecStats.Stats[STATS_CONTROL],
	Stats.RecStats.Stats[STATS_REJ_INTSPAMFILTER] +
				Stats.RecStats.Stats[STATS_REJ_EXTSPAMFILTER],
	Stats.RecStats.Stats[STATS_REJ_ERR],
	Stats.RecStats.ReceivedBytes,
	Stats.RecStats.AcceptedBytes,
	Stats.RecStats.RejectedBytes,
	nuse / ((dt == 0) ? 1 : dt)
    );
    if (Stats.SpoolStats.Arts[STATS_S_ARTICLE] ||
			Stats.SpoolStats.Arts[STATS_S_HEAD] ||
			Stats.SpoolStats.Arts[STATS_S_BODY] ||
			Stats.SpoolStats.Arts[STATS_S_STAT])
	logit(LOG_INFO, "%-20s spoolstats secs=%d stat=%d head=%d body=%d article=%d bytes=%.0f",
		HName,
		dt,
		Stats.SpoolStats.Arts[STATS_S_STAT],
		Stats.SpoolStats.Arts[STATS_S_HEAD],
		Stats.SpoolStats.Arts[STATS_S_BODY],
		Stats.SpoolStats.Arts[STATS_S_ARTICLE],
		Stats.SpoolStats.ArtsBytesSent
    );
    if (Stats.RecStats.Stats[STATS_REJECTED] > 0)
	logit(LOG_INFO, "%-20s rejstats rej=%d failsafe=%d misshdrs=%d tooold=%d grpfilt=%d intspamfilt=%d extspamfilt=%d incfilter=%d nospool=%d ioerr=%d notinactv=%d pathtab=%d ngtab=%d posdup=%d hdrerr=%d toosmall=%d incompl=%d nul=%d nobytes=%d proto=%d msgidmis=%d nohdrend=%d bighdr=%d barecr=%d err=%d",
	HName,
	Stats.RecStats.Stats[STATS_REJECTED],
	Stats.RecStats.Stats[STATS_REJ_FAILSAFE],
	Stats.RecStats.Stats[STATS_REJ_MISSHDRS],
	Stats.RecStats.Stats[STATS_REJ_TOOOLD],
	Stats.RecStats.Stats[STATS_REJ_GRPFILTER],
	Stats.RecStats.Stats[STATS_REJ_INTSPAMFILTER],
	Stats.RecStats.Stats[STATS_REJ_EXTSPAMFILTER],
	Stats.RecStats.Stats[STATS_REJ_INCFILTER],
	Stats.RecStats.Stats[STATS_REJ_NOSPOOL],
	Stats.RecStats.Stats[STATS_REJ_IOERROR],
	Stats.RecStats.Stats[STATS_REJ_NOTINACTV],
	Stats.RecStats.Stats[STATS_REJ_PATHTAB],
	Stats.RecStats.Stats[STATS_REJ_NGTAB],
	Stats.RecStats.Stats[STATS_REJ_POSDUP],
	Stats.RecStats.Stats[STATS_REJ_HDRERROR],
	Stats.RecStats.Stats[STATS_REJ_TOOSMALL],
	Stats.RecStats.Stats[STATS_REJ_ARTINCOMPL],
	Stats.RecStats.Stats[STATS_REJ_ARTNUL],
	Stats.RecStats.Stats[STATS_REJ_NOBYTES],
	Stats.RecStats.Stats[STATS_REJ_PROTOERR],
	Stats.RecStats.Stats[STATS_REJ_MSGIDMIS],
	Stats.RecStats.Stats[STATS_REJ_NOHDREND],
	Stats.RecStats.Stats[STATS_REJ_BIGHEADER],
	Stats.RecStats.Stats[STATS_REJ_BARECR],
	Stats.RecStats.Stats[STATS_REJ_ERR]
    );
    bzero(&Stats, sizeof(Stats));
    SessionMark = t;
}

void
LogSession2(void)
{
    int dt = (int)(time(NULL) - SessionMark);

    logit(LOG_INFO, 
	"DIABLO uptime=%d:%02d arts=%s tested=%s bytes=%s fed=%s",
	dt / 3600,
	dt / 60 % 60,
	ftos(TtlStats.ArtsReceived),
	ftos(TtlStats.ArtsTested),
	ftos(TtlStats.ArtsBytes),
	ftos(TtlStats.ArtsFed)
    );
}

void
DoStats(FILE *fo, int dt, int raw)
{
    if (raw)
	xfprintf(fo, "211 DIABLO timenow=%ld uptime=%d:%02d arts=%.0f tested=%.0f bytes=%.0f fed=%.0f\r\n",
		time(NULL),
		dt / 3600, dt / 60 % 60,
		TtlStats.ArtsReceived,
		TtlStats.ArtsTested,
		TtlStats.ArtsBytes,
		TtlStats.ArtsFed
    );
    else
	xfprintf(fo, "211 DIABLO timenow=%ld uptime=%d:%02d arts=%s tested=%s bytes=%s fed=%s\r\n",
		time(NULL),
		dt / 3600, dt / 60 % 60,
		ftos(TtlStats.ArtsReceived),
		ftos(TtlStats.ArtsTested),
		ftos(TtlStats.ArtsBytes),
		ftos(TtlStats.ArtsFed)
    );
}

/*
 * DOCOMMAND() - handles a control connection
 */

void
DoCommand(int ufd)
{
    int fd;
    int retain = 0;
    struct sockaddr_in asin;
    ACCEPT_ARG3_TYPE alen = sizeof(asin);
    int count = 20;

    /*
     * This can be a while() or an if(), but apparently some OS's fail to
     * properly handle O_NONBLOCK for accept() on unix domain sockets so...
     */

    while (--count > 0) {
	FILE *fi;
	FILE *fo;

	if ((fd = accept(ufd, (struct sockaddr *)&asin, &alen)) <= 0)
	    break;
	fcntl(fd, F_SETFL, 0);
	fo = fdopen(dup(fd), "w");
	fi = fdopen(fd, "r");

	if (fi && fo) {
	    char buf[MAXLINE];

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		char *s1;
		char *s2;

		if (DebugOpt)
		    printf("%d << %s\n", (int)getpid(), buf);

		if ((s1 = strtok(buf, " \t\r\n")) == NULL)
		    continue;
		s2 = strtok(NULL, "\n");
		if (strcmp(s1, "status") == 0) {
		    fprintf(fo, "211 Paused=%d Exiting=%d Forks=%d NFds=%d MaxFds=%d\r\n",
			PausedCount, Exiting, NumForks, countFds(&RFds), MaxFds
		    );
		} else if (strcmp(s1, "version") == 0) {
		    fprintf(fo, "211 DIABLO Version %s - %s\r\n", VERS, SUBREV);
		} else if (strcmp(s1, "flush") == 0) {
		    fprintf(fo, "211 Flushing feeds\n");
		    fflush(fo);
		    flushFeeds(0);
		    CloseIncomingLog();
		    ClosePathLog(1);
		    CloseArtLog(1);
		} else if (strcmp(s1, "pause") == 0) {
		    retain = RET_PAUSE;
		    ++PausedCount;
		    ReadOnlyMode = 0;
		    ReadOnlyCount = 0;
		    fprintf(fo, "200 Pause, count %d.\n", PausedCount);
		    if (PausedCount == 1) {
			int i;

			for (i = 0; i < MAXFDS; ++i) {
			    if (PidAry[i].tr_Pid) {
				Kill(PidAry[i].tr_Pid, SIGHUP);
			    }
			}
		    }
		} else if (strcmp(s1, "readonly") == 0) {
		    retain = RET_PAUSE;
		    ReadOnlyMode = 1;
		    ReadOnlyCount = 0;
		    fprintf(fo, "200 Read-only mode %d.\n", ReadOnlyMode);
		    {
			int i;

			for (i = 0; i < MAXFDS; ++i) {
			    if (PidAry[i].tr_Pid) {
				Kill(PidAry[i].tr_Pid, SIGALRM);
			    }
			}
		    }
		} else if (strcmp(s1, "child-is-readonly") == 0) {
		    ReadOnlyCount++;
		} else if (strcmp(s1, "go") == 0) {
		    int i;

		    if ((PausedCount == 1) && ReadOnlyMode) {
			for (i = 0; i < MAXFDS; ++i) {
			    if (PidAry[i].tr_Pid) {
				Kill(PidAry[i].tr_Pid, SIGHUP);
			    }
			}
			ReadOnlyMode = 0;
		    }

		    if (PausedCount)
			--PausedCount;

		    if (ReadOnlyMode && !PausedCount)
			ReadOnlyMode = 0;

		    fprintf(fo, "200 Resume, count %d\n", PausedCount);
		} else if (strcmp(s1, "quit") == 0) {
		    fprintf(fo, "200 Goodbye\n");
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "stats") == 0) {
		    int dt = (int)(time(NULL) - SessionBeg);
		    DoStats(fo, dt, 0);
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "rawstats") == 0) {
		    int dt = (int)(time(NULL) - SessionBeg);
		    DoStats(fo, dt, 1);
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "spaminfo") == 0) {
		    if (DOpts.SpamFilterOpt != NULL)
			DumpSpamFilterCache(fo, 0);
		    else
			fprintf(fo, "Internal spamfilter disabled\n");
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "feednotify") == 0) {
		    DoFeedNotify(fo, s2);
		    break;
		} else if (strcmp(s1, "listnotify") == 0) {
		    DoListNotify(fo, s2);
		    break;
		} else if (strcmp(s1, "dumpfeed") == 0) {
		    if (s2 != NULL)
			DumpFeedInfo(fo, s2);
		    else
			DumpAllFeedInfo(fo);
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "info") == 0) {
		    fprintf(fo, "Max filedescriptors in use: %d\n", MaxFds);
		    fprintf(fo, "Internal Spamfilter: %s\n",
				DOpts.SpamFilterOpt != NULL ? "enabled" : "disabled");
		    fprintf(fo, "External Spamfilter: %s\n",
				DOpts.FeederFilter ? "enabled" : "disabled");
		    fprintf(fo, "Readonly mode: %s\n",
				ReadOnlyMode ? "on" : "off");
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "exit") == 0 || strcmp(s1, "aexit")==0) {
		    int i;

		    if (s1[0] == 'e')
			retain = RET_CLOSE;

		    fprintf(fo, "211 Exiting\n");
		    fflush(fo);

		    if (Exiting == 0) {
			Exiting = 1;
			for (i = 0; i < MAXFDS; ++i) {
			    if (PidAry[i].tr_Pid) {
				Kill(PidAry[i].tr_Pid, SIGHUP);
			    }
			}
		    } else {
			fprintf(fo, "200 Exit is already in progress\n");
		    }
		    flushFeeds(0);
		} else if (strcmp(s1, "debug") == 0) {
		    char *s2 = strtok(NULL, " \t\r\n");

		    zfreeStr(&SysMemPool, &DebugLabel);

		    if (s2) {
			DebugLabel = zallocStr(&SysMemPool, s2);
			fprintf(fo, "200 Debugging '%s'\n", DebugLabel);
		    } else {
			DebugLabel = NULL;
			fprintf(fo, "200 Debugging turned off\n");
		    }
		} else if (strcmp(s1, "dumphist") == 0) {
		    /* There's a better way to do this JG200103050847 */
		    const char *f1 = PatDbExpand(DumpHistPat);

		    if (f1 && (strcmp(f1, "NONE") != 0)) {
			system(f1);
		    } else {
			fprintf(fo, "400 No dumphist program configured\n");
		    }
		} else if (strcmp(s1, "config") == 0) {
		    if (s2 != NULL) {
			char *opt;
			char *cmd;
			if ((cmd = strtok(s2, " \t")) != NULL &&
			    (opt = strtok(NULL, " \t")) != NULL) {
			    int cmdErr = 0;
			    if (SetCommand(fo, cmd, opt, &cmdErr) == 0) {
				CheckConfigOptions(1);
				fprintf(fo, "Set '%s' to '%s'\n", cmd, opt);
				InitSpamFilter();
			    } else if (cmdErr == 1) {
				fprintf(fo, "Invalid config command: %s %s\n",
								cmd, opt);
				fprintf(fo, "Usage: config command option\n");
			    } else {
				fprintf(fo, "Invalid option for config command: %s %s\n",
								 cmd, opt);
				fprintf(fo, "Usage: config command option\n");
			    }
			} else {
			    DumpConfigOptions(fo, s2, CONF_FEEDER);
			}
		    } else {
			DumpConfigOptions(fo, NULL, CONF_FEEDER);
		    }
		} else if (strcmp(s1, "help") == 0) {
		    fprintf(fo,"Supported commands:\n");
		    fprintf(fo,"   help     you are looking at it\n");
		    fprintf(fo,"   status   short status line\n");
		    fprintf(fo,"   info     show some running configuration options\n");
		    fprintf(fo,"   version  return diablo version\n");
		    fprintf(fo,"   pause    close all incoming connections and stop accepting new ones\n");
		    fprintf(fo,"   go       continue after a pause\n");
		    fprintf(fo,"   readonly don't allow incoming articles\n");
		    fprintf(fo,"   config   view/set run-time config\n");
		    fprintf(fo,"   stats    general server statistics\n");
		    fprintf(fo,"   rawstats general server statistics (without pretty printing)\n");
		    fprintf(fo,"   spaminfo dump internal spamfilter cache\n");
		    fprintf(fo,"   dumpfeed dump in-memory copy of outgoing feed details\n");  
		    fprintf(fo,"   exit     close all connections and exit\n");
		} else {
		    fprintf(fo, "500 Unrecognized command: %s\n", s1);
		}
		if (retain == 0)
		    fprintf(fo, ".\n");
		fflush(fo);
		if (retain)
		    break;
	    }
	}
	if (retain) {
	    Retain *ret = zalloc(&ParProcMemPool, sizeof(Retain));

	    ret->re_Next = ReBase;
	    ret->re_Fi = fi;
	    ret->re_Fo = fo;
	    ret->re_What = retain;
	    ReBase = ret;
	} else {
	    if (fi)
		fclose(fi);
	    if (fo)
		fclose(fo);
	}
	{
	    fd_set rfds = RFds;
	    struct timeval tv = { 0, 0 };

	    select(ufd + 1, &rfds, NULL, NULL, &tv);
	    if (!FD_ISSET(ufd, &rfds))
		count = 1;
	}
    }
}

/*
 * Handle feed registration and notification
 *
 * We get a dicmd command of 'feednotify switch: on|off:label
 *
 * We then send 1 byte packets to the Unix domain DGRAM socket when
 * an article has been flushed to the dqueue file for that label.
 */
void
DoFeedNotify(FILE *fo, char *info)
{
    Feed *fe;
    int which = 1;

    if (info == NULL)
	return;
    if (strncmp(info, "off", 3) == 0)
	which = 0;
    info = strchr(info, ':');
    if (info == NULL)
	    return;
    info++;
    for (fe = FeBase; fe != NULL; fe = fe->fe_Next) {
	    if (strcmp(info, fe->fe_Label) == 0)
		break;
    }
    if (fe == NULL)
	return;
    if (which == 0) {
	if (fe->fe_NotifyFd != -1) {
	    close(fe->fe_NotifyFd);
	    fe->fe_NotifyFd = -1;
	}
	return;
    }
    if (fe->fe_NotifyFd != -1) {
	close(fe->fe_NotifyFd);
	fe->fe_NotifyFd = -1;
    }
    fe->fe_NotifyFd = OpenFeedNotify(fe->fe_Label);
}

void
DoListNotify(FILE *fo, char *l)
{
    Feed *fe;

    for (fe = FeBase; fe != NULL; fe = fe->fe_Next) {
	if (l == NULL || strcmp(l, fe->fe_Label) == 0)
	    fprintf(fo, "%3s %s\n", 
				(fe->fe_NotifyFd == -1) ? "off" : "on",
				fe->fe_Label);
    }
}

/*
 * REMOTE FEED CODE
 */

FILE *Ft;
int  FtNumCommit;

void
FeedRSet(FILE *fo)
{
    Ft = xfopen("w", "%s/%s.new", PatExpand(FeedsHomePat), HLabel);
    if (Ft != NULL) {
	FeedTableReady = 1;
	FtNumCommit = 0;
	xfprintf(fo, "290 Label Reset\r\n");
    } else {
	xfprintf(fo, "490 file create failed\r\n");
    }
}

void
FeedList(FILE *fo)
{
    FILE *f;
    char buf[MAXGNAME + 20];
    f = xfopen("r", "%s/%s", PatExpand(FeedsHomePat), HLabel);
    if (f != NULL) {
	char *cmd;
	char *pat;
	char *p;
	xfprintf(fo, "291 Feed list follows\r\n");
	while (!feof(f) && fgets(buf, sizeof(buf), f) != NULL) {
	    buf[strlen(buf) - 1] = '\0';
	    cmd = buf;
	    for (p = cmd; *p && !isspace((int)*p); p++);
	    *p = '\0';
	    if (strcmp(cmd, "addgroup") == 0)
		cmd = "feedadd";
	    else if (strcmp(cmd, "delgroup") == 0)
		cmd = "feeddel";
	    else if (strcmp(cmd, "delgroupany") == 0)
		cmd = "feeddelany";
	    for (pat = ++p; *pat && isspace((int)*pat); pat++);
	    xfprintf(fo, "%s %s\r\n", cmd, pat);
	}
	fclose(f);
	xfprintf(fo, ".\r\n");
    } else {
	xfprintf(fo, "490 No feed information available\r\n");
    }
}

void
FeedCommit(FILE *fo)
{
    char path1[256+64];
    char path2[256+64];

    sprintf(path1, "%s/%s.new", PatExpand(FeedsHomePat), HLabel);
    sprintf(path2, "%s/%s", PatExpand(FeedsHomePat), HLabel);
    fflush(Ft);
    if (ferror(Ft)) {
	remove(path1);
	xfprintf(fo, "490 file write failed\r\n");
    } else {
	if (FtNumCommit == 0) {
	    xfprintf(fo, "290 empty feed list, reverting to initial\r\n");
	    remove(path1);
	    remove(path2);
	} else {
	    xfprintf(fo, "290 feed commit complete\r\n");
	    rename(path1, path2);
	}
        TouchNewsFeed();
    }
    fclose(Ft);
    FeedTableReady = 0;
}

void
FeedAddDel(FILE *fo, char *gwild, int add)
{
    int i;

    if (strlen(gwild) > MAXGNAME - 8) {
	xfprintf(fo, "490 wildcard too long\r\n");
	return;
    }
    if (FtNumCommit == MAXFEEDTABLE) {
	xfprintf(fo, "490 too many entries, max is %d\r\n", MAXFEEDTABLE);
	return;
    }
    for (i = 0; gwild[i]; ++i) {
	if (isalnum((int)gwild[i]))
	    continue;
	if (gwild[i] == '*')
	    continue;
	if (gwild[i] == '?')
	    continue;
	if (gwild[i] == '.')
	    continue;
	if (gwild[i] == '-')
	    continue;
	if (gwild[i] == '+')
	    continue;
	xfprintf(fo, "490 illegal character: %c\r\n", gwild[i]);
	break;
    }
    if (gwild[i] == 0) {
	xfprintf(fo, "290 ok\r\n");
	fprintf(Ft, "%s\t%s\n",
		((add == 1) ? "addgroup" :
		 (add == -1) ? "delgroup" :
		 /* add == -2 */ "delgroupany"), gwild);
	++FtNumCommit;
    }
}

void
FinishRetain(int what)
{
    Retain *ret;
    Retain **pret;

    for (pret = &ReBase; (ret = *pret) != NULL; ) {
	if (ret->re_What == what) {
	    if (ret->re_Fo)
		fprintf(ret->re_Fo, "200 Operation Complete\n.\n");
	    *pret = ret->re_Next;
	    if (ret->re_Fi)
		fclose(ret->re_Fi);
	    if (ret->re_Fo)
		fclose(ret->re_Fo);
	    zfree(&ParProcMemPool, ret, sizeof(Retain));
	} else {
	    pret = &ret->re_Next;
	}
    }
}

int
QueueRange(const char *label, int *pqnum, int *pqarts, int *pqrun)
{
    FILE *fi = xfopen("r", "%s/.%s.seq", PatExpand(DQueueHomePat), label);
    int r = -1;

    if (fi) {
	int qbeg;
	int qend;
	if (fscanf(fi, "%d %d", &qbeg, &qend) == 2) {
	    r = 0;
	    *pqrun = 0;
	    *pqarts = -1;
	    *pqnum = qend - qbeg;

	    if (*pqnum < 500000) {
		int i;

		*pqarts = 0;
		for (i = qbeg; i < qend; ++i) {
		    FILE *fj = xfopen("r", "%s/%s.S%05d", PatExpand(DQueueHomePat), label, i);
		    if (fj) {
			char buf[256];

			while (fgets(buf, sizeof(buf), fj) != NULL) {
			    ++*pqarts;
			}
			if (xflock(fileno(fj), XLOCK_EX|XLOCK_NB) < 0)
			    ++*pqrun;
			fclose(fj);
		    }
		}
	    }
	}
	fclose(fi);
    }
    return(r);
}

int
countFds(fd_set *rfds)
{
    int i;
    int count = 0;

    for (i = 0; i < MAXFDS; ++i) {
	if (FD_ISSET(i, rfds))
	    ++count;
    }
    return(count);
}

/*
 * ArticleFile() - calculate article filename, open, and cache descriptors.
 *		   assigns h->iter, uses h->exp and h->gmt.  Assign *pbpos
 *		   and lseek's the descriptor to the start position for the
 *		   next article write.  The file is exclusively locked.
 *
 *		   NOTE!  We never create 'older' directories.  This could
 *		   lead to an offset,size being stored for a message, the
 *		   spool file getting deleted, then the spool file getting
 *		   recreated and a new message written.  Then an attempt to 
 *		   feed the old message would result in corrupt data.
 *
 *		   There are a number of post-write reject cases, including
 *		   spam cache hits, file too large, in-transit history 
 *		   collisions, and so forth.  Rather then ftruncate() the
 *		   spool file, the feeder now simply lseek's back and 
 *		   overwrites the dead article.  Diablo will do a final
 *		   ftruncate() on the file when the descriptor is finally
 *		   closed.
 */

typedef struct AFCache {
    int		af_Fd;
    off_t	af_AppendOff;	/* cached append offset		*/
    off_t	af_FileSize;	/* calculated current file size	*/
    uint32	af_Slot;	/* 10 minute bounded    	*/
    uint16	af_Iter;
    uint16	af_Spool;
    time_t	af_OpenTime;
} AFCache;

AFCache AFAry[MAXDIABLOFDCACHE];
int AFNum;
time_t AFFlushTime = 0;

void
ArticleFileInit(void)
{
    int i;

    bzero(&AFAry, sizeof(AFAry));
    for (i = 0; i < MAXDIABLOFDCACHE; ++i)
	AFAry[i].af_Fd = -1;
    AFFlushTime = time(NULL);
}

int
#ifdef USE_ZLIB
ArticleFile(History *h, off_t *pbpos, int clvl, gzFile **cfile)
#else
ArticleFile(History *h, off_t *pbpos, int clvl, char **cfile)
#endif
{
    AFCache	*af = NULL;
    int		rfd = -1;

    /*
     * Look for entry in cache.
     */

    {
	int i;

	for (i = 0; i < AFNum; ++i) {
	    AFCache *raf = &AFAry[i];

	    if (raf->af_Slot == h->gmt && raf->af_Spool == H_SPOOL(h->exp) &&
			raf->af_FileSize + h->bsize < SPOOL_MAX_FILE_SIZE) {
		af = raf;
		break;
	    }
	}
    }

    /*
     * Add new entry to cache.  If we hit the wall, close the last entry
     * in the cache.  If we are in feeder mode, the cache is degenerate with
     * only one entry.  If in reader mode, the cache is reasonably-sized.
     */

    if (af == NULL) {
	int cnt;
	int maxdfd = MAXDIABLOFDCACHE;

	/*
	 * blow away LRU if cache is full
	 */

	if (AFNum == maxdfd) {
	    ArticleFileClose(maxdfd - 1);
	    --AFNum;
	}

	/*
	 * our new entry
	 */

	af = &AFAry[AFNum];

	bzero(af, sizeof(AFCache));

	af->af_Iter = (uint16)(PeerIpHash.h1 ^ (PeerIpHash.h1 >> 16));
	af->af_Fd = -1;
	af->af_Slot = h->gmt;
	af->af_Spool = H_SPOOL(h->exp);

	for (cnt = 0; cnt < 10000; ++cnt) {
	    char path[PATH_MAX];

	    errno = 0;
	    af->af_Iter &= 0x7FFF;

	    h->iter = af->af_Iter;
	    ArticleFileName(path, sizeof(path), h, ARTFILE_FILE);

	    if ((af->af_Fd = open(cdcache(path), O_RDWR|O_CREAT, 0644)) >= 0) {
		struct stat st;

		if (xflock(af->af_Fd, XLOCK_EX|XLOCK_NB) < 0 || 
		    (fstat(af->af_Fd, &st), st.st_nlink) == 0 ||
		     st.st_size + h->bsize >= SPOOL_MAX_FILE_SIZE
		) {
		    close(af->af_Fd);
		    errno = 0;
		    af->af_Fd = -1;
		    ++af->af_Iter;	/* bump iteration */
		    continue;		/* try again 	  */
		}
		++AFNum;
		af->af_OpenTime = time(NULL);
		break;
	    }

	    /*
	     * Does the directory need creating?
	     */
	    if (errno == ENOENT) {
		char tpath[PATH_MAX];
		if (DebugOpt)
		    logit(LOG_DEBUG, "Creating spool directory %s - should already be created", tpath);
		ArticleFileName(tpath, sizeof(tpath), h, ARTFILE_DIR);
		if (mkdir(tpath, 0755) == 0)
		    continue;
	    }
		

	    if (errno == EEXIST) {
		++af->af_Iter;		/* bump iteration */
		continue;		/* try again 	  */
	    }

	    logit(LOG_CRIT, "unable to open/create article file %s (%s) - aborting",
							path, strerror(errno));
	    /*
	     * The intermediate directory is missing
	     */

	    sleep(1);
	    if (errno == ENOSPC || errno == EIO || errno == EMFILE || 
		errno == ENFILE
	    ) {
		break;
	    }

	    /*
	     * The spool article directory is missing. This should not
	     * happen so if it does, abort horribly.
	     */
	    ArticleFileCloseAll();
	    LogSession();
	    ClosePathLog(1);
	    CloseArtLog(1);
	    DiabFilterClose(0);
	    exit(1);

	} /* for */

	if (af->af_Fd >=0 ) {
	    af->af_FileSize = lseek(af->af_Fd, 0L, 2);
	    af->af_AppendOff = af->af_FileSize;
	}
    }

    /*
     * If we have a good descriptor, set the append position and
     * make sure the entry is at the beginning of the cache.
     */

    if ((rfd = af->af_Fd) >= 0) {
	*pbpos = af->af_AppendOff;
	lseek(rfd, af->af_AppendOff, 0);

#ifdef USE_ZLIB
	if (clvl >= 0 && clvl <= 9) {
	    char mode[10];

	    snprintf(mode, sizeof(mode), "ab%d", clvl);
	    *cfile = gzdopen(dup(rfd), mode);
	    if (*cfile == NULL)
		logit(LOG_ERR, "Unable to gzdopen - not compressing");
	}
#endif
	h->iter = af->af_Iter;
	if (af != &AFAry[0]) {
	    AFCache tmp = *af;
	    memmove(
		&AFAry[1],	/* dest 	*/
		&AFAry[0],	/* source 	*/
		(char *)af - (char *)&AFAry[0]
	    );
	    AFAry[0] = tmp;
	}
    }
    return(rfd);
}

void
ArticleFileCloseAll(void)
{
    int i;

    for (i = 0; i < MAXDIABLOFDCACHE; ++i) {
	if (AFAry[i].af_Fd >= 0)
	    ArticleFileClose(i);
    }
}

void
ArticleFileCacheFlush(time_t t)
{
    int i;

    if (t - AFFlushTime < 600)
	return;
    for (i = 0; i < MAXDIABLOFDCACHE; ++i) {
	if (AFAry[i].af_Fd >= 0 && (t - AFAry[i].af_OpenTime) >= 600)
	    ArticleFileClose(i);
    }
    AFFlushTime = t;
}

void
ArticleFileClose(int i)
{
    AFCache *aftmp = &AFAry[i];

    if (aftmp->af_AppendOff < aftmp->af_FileSize)
	ftruncate(aftmp->af_Fd, aftmp->af_AppendOff);
    close(aftmp->af_Fd);
    bzero(aftmp, sizeof(AFCache));
    aftmp->af_Fd = -1;
}

void
ArticleFileTrunc(int artFd, off_t bpos)
{
    AFCache *af =  &AFAry[0];

    if (af->af_Fd != artFd) {
	logit(LOG_ERR, "internal artFd mismatch %d/%d", artFd, af->af_Fd);
	return;
    }
    if (bpos >= 0)
	af->af_AppendOff = bpos;
}

void
ArticleFileSetSize(int artFd)
{
    AFCache *af =  &AFAry[0];
    off_t bpos = lseek(artFd, 0L, 1);

    if (af->af_Fd != artFd) {
	logit(LOG_ERR, "internal artFd mismatch %d/%d", artFd, af->af_Fd);
	return;
    }
    if (bpos > af->af_FileSize)
	af->af_FileSize = bpos;
    af->af_AppendOff = bpos;
}

int
MapArticle(int fd, char *fname, char **base, History *h, int *extra, int *artSize, int *compressedFormat)
{
    SpoolArtHdr tah = { 0 };

    /* 
     * Fetch the spool header for the article, this tells us how it was
     * stored 
     */

    lseek(fd, h->boffset, 0);
    if (read(fd, &tah, sizeof(SpoolArtHdr)) != sizeof(SpoolArtHdr)) {
        close(fd);
        logit(LOG_ERR, "Unable to read article header (%s)\n",
							strerror(errno));
        return(-1);
    }

    *compressedFormat = (tah.StoreType & STORETYPE_GZIP) ? 1 : 0;

    if (*compressedFormat) {
#ifdef USE_ZLIB
	gzFile *gzf;
	char *p;

	if ((uint8)tah.Magic1 != STORE_MAGIC1 &&
					(uint8)tah.Magic2 != STORE_MAGIC2) {
	    lseek(fd, h->boffset, 0);
	    tah.Magic1 = STORE_MAGIC1;
	    tah.Magic2 = STORE_MAGIC2;
	    tah.HeadLen = sizeof(tah);
	    tah.ArtLen = h->bsize;
	    tah.ArtHdrLen = h->bsize;
	    tah.StoreLen = h->bsize;
	    tah.StoreType = STORETYPE_GZIP;
	}
	gzf = gzdopen(dup(fd), "r");
	if (gzf == NULL) {
	    logit(LOG_ERR, "Error opening compressed article\n");
	    return(-1);
	}
    	*base = (char *)malloc(tah.ArtLen + tah.HeadLen + 2);
	if (*base == NULL) {
	    logit(LOG_CRIT, "Unable to malloc %d bytes for article (%s)\n",
				tah.ArtLen + tah.HeadLen + 2, strerror(errno));
	    gzclose(gzf);
	    return(-1);
	}
	p = *base;
	bcopy(&tah, p, tah.HeadLen);
	p += tah.HeadLen;
	if (gzread(gzf, p, tah.ArtLen) != tah.ArtLen) {
	    logit(LOG_ERR, "Error uncompressing article\n");
	    free(*base);
	    return(-1);
	}
	
	p[tah.ArtLen] = 0;
	*artSize = tah.ArtLen + tah.HeadLen;
	*compressedFormat = 1;
	gzclose(gzf);
#else
        logit(LOG_ERR, "Article was stored compressed and compression support has not been enabled\n");
#endif
    } else {
	    *base = xmap(
		NULL, 
		h->bsize + 1, 
		PROT_READ,
		MAP_SHARED, 
		fd, 
		h->boffset
	    );
	    *artSize = h->bsize;
    }

    if (*base == NULL) {
	logit(LOG_ERR, "Unable to map file %s: %s (%d,%d,%d)\n",
					fname,
					strerror(errno),
					(int)(h->boffset - *extra),
					 (int)(h->bsize + *extra + 1),
					*extra
	);
	*artSize = 0;
	return(-1);
    }
    return(0);
}

int
ArticleOpen(History *h, const char *msgid, char **pfi, int32 *rsize, int *pmart, int *pheadOnly, int *compressed)
{
    int r = -1;
    int z = 0;

    if (pheadOnly)
	*pheadOnly = (int)(h->exp & EXPF_HEADONLY);

    if (compressed != NULL) {
	if (SpoolCompressed(H_SPOOL(h->exp)))
	    *compressed = 1;
	else
	    *compressed = 0;
    } else {
	compressed = &z;
    }
	
    if (pfi) {
	char path[PATH_MAX];
	int fd;

	ArticleFileName(path, sizeof(path), h, ARTFILE_FILE);

	/*
	 * multi-article file ?  If so, articles are zero-terminated
	 */

	*pfi = NULL;
	*rsize = 0;
	*pmart = 1;

	/*
	 * get the file
	 */
	if ((fd = cdopen(path, O_RDONLY, 0)) >= 0) {
	    r = MapArticle(fd, path, pfi, h, pmart, rsize, compressed);

	    /*
	     * Sanity check.  Look for 0x00 guard character, make sure
	     * first char isn't 0x00, and make sure last char isn't 0x00
	     * (the last character actually must be an LF or the NNTP
	     * protocol wouldn't have worked).
	     */
	    if (*pfi == NULL ||
			h->bsize == 0 ||
			(*pfi)[0] == 0 ||
			(*pfi)[*rsize-1] == 0 ||
			(*pfi)[*rsize] != 0
	    ) {
		logit(LOG_ERR, "corrupt spool entry for %s@%d,%d %s",
		    path,
		    (int)h->boffset,
		    (int)h->bsize,
		    msgid
		);
		if (*pfi) {
		    if (*compressed)
			free(*pfi);
		    else
			xunmap(*pfi, *rsize);
		    *pfi = NULL;
		}
	    } 
		
	}
	if (fd != -1)
	    close(fd);
    }
   if (pfi && *pfi)
	r = 0;
    return(r);
}

void
DoArtStats(int statgroup, int which, int bytes)	{
    ++Stats.RecStats.Stats[statgroup];
    ++Stats.RecStats.Stats[which];
    switch (statgroup) {
	case STATS_ACCEPTED:
	    Stats.RecStats.AcceptedBytes += (double)bytes;
	    break;
	case STATS_RECEIVED:
	    Stats.RecStats.ReceivedBytes += (double)bytes;
	    break;
	default:
	    Stats.RecStats.RejectedBytes += (double)bytes;
	    break;
    }
    if (HostStats != NULL) {
	LockFeedRegion(HostStats, XLOCK_EX, FSTATS_IN);
	++HostStats->RecStats.Stats[statgroup];
	++HostStats->RecStats.Stats[which];
	switch (statgroup) {
	    case STATS_ACCEPTED:
		HostStats->RecStats.AcceptedBytes += (double)bytes;
		break;
	    case STATS_RECEIVED:
		HostStats->RecStats.ReceivedBytes += (double)bytes;
		break;
	    default:
		HostStats->RecStats.RejectedBytes += (double)bytes;
		break;
	}
	LockFeedRegion(HostStats, XLOCK_UN, FSTATS_IN);
    }
}

void
DoSpoolStats(int which) {
    ++Stats.SpoolStats.Arts[which];
    if (HostStats != NULL) {
	LockFeedRegion(HostStats, XLOCK_EX, FSTATS_SPOOL);
	++HostStats->SpoolStats.Arts[which];
	LockFeedRegion(HostStats, XLOCK_UN, FSTATS_SPOOL);
    }
}

