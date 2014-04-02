
/*
 * DREADERD/MAIN.C - diablo newsreader
 *
 *	This is the startup for the dreaderd daemon.
 *
 *	The diablo newsreader requires one or remote diablo news boxes to 
 *	obtain articles from, but keeps track of article assignments to groups
 *	and overview information locally.
 *
 *	Operationally, dreaderd runs as a standalone multi-forking, 
 *	multi-threaded daemon.  It forks X resolvers and Y readers.  Each
 *	reader can support N connections (NxY total connections) and maintains
 *	its own links to one or more news servers (diablo, INN, whatever). 
 *	Overview information is utilized and articles are accessed by 
 *	message-id.
 *
 *	The diablo newsreader is also able to accept header-only feeds from
 *	one or more diablo servers via 'mode headfeed'.  Processing of these
 *	feeds is not multi-threaded, only processing of newsreader functions is
 *	multithreaded.  The diablo newsreader does not operate with a dhistory
 *	file.  If taking a feed from several sources you must also run the
 *	diablo server on the same box and use that to feed the dreaderd system,
 *	though the diablo server need not store any articles itself.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

#if NEED_TERMIOS
#include <sys/termios.h>
#endif

Prototype void RTClearStatus(int index, int count);
Prototype void RTUpdateStatus(int slot, const char *ctl, ...);
Prototype pid_t ForkPipe(int *pfd, void (*task)(int fd, const char *id), const char *description);
Prototype void ValidateTcpBufferSize(int *psize);

Prototype int NumReaderForks;
Prototype int ThisReaderFork;
Prototype int FeedOnlyServer;
Prototype int CoreDebugOpt;
Prototype int FastCopyOpt;
Prototype pid_t *ReaderPids;
Prototype char *RTStatus;
Prototype char *MyGroupHome;

void HandleDnsThread(ForkDesc *desc);
void deleteDnsResHash(DnsRes *dres, ForkDesc *desc);
int getReaderForkSlot(void);
void delReaderForkSlot(pid_t pid);
#ifdef INET6
void SendDns(int fd, struct sockaddr_storage *rsin, DnsRes *dres);
#else
void SendDns(int fd, struct sockaddr_in *rsin, DnsRes *dres);
#endif
void FinishRetain(int what);
void DoListen(ForkDesc *desc, time_t t);
void DoCommand(int fd);
void getVStats(FILE*fo);
void getStats(FILE*fo, int raw);
#ifdef	READER_BAN_LISTS
void SetBanList(char *ban);
int Banned(char *ip, time_t t);
void DumpBannedLists(FILE *fo);
void DumpBannedConfigs(FILE *fo);
#endif

char *RTStatus = NULL;
char *MyGroupHome;
char *PidFile;
char AdminVersion;
const char *SysLogDesc;
int CoreDebugOpt = 0;
int FastCopyOpt = 1;
int TxBufSize = 0;
int RxBufSize = 0;
int MaxSDnsForks = 0;		/* future				*/
int MaxConnects;
int ConnectCount;
int FailCount;
int ListenersEnabled = 1;
time_t TimeStart;
#ifdef	DREADER_CLIENT_TIMING
float ticksPerSecond;
#endif

int NumReaderFeedForks;
int NumReaderForks;
int NumDnsForks;
int NumSDnsForks;
int NumPending;			/* pending connections	*/
int NumActive;			/* active connections	*/

double TotalClientGroups = 0.0;
double TotalClientArticles = 0.0;
double TotalClientArtBytes = 0.0;
double TotalClientPosts = 0.0;
double TotalClientPostFail = 0.0;
double TotalClientPostBytes = 0.0;
double TotalSpoolArticles = 0.0;
double TotalSpoolBytes = 0.0;
double TotalFeedArticles = 0.0;
double TotalFeedBytes = 0.0;
double TotalFeedArtFail = 0.0;

int ufd;			/* drcmd listen fd */

int ThisReaderFork;
int FeedOnlyServer = -1;
pid_t *ReaderPids;

MemPool *DnsResPool;
DnsRes  *DnsResHash[DRHSIZE];

BindList *BindBase = NULL;

typedef struct PendingClose {
    int		fd;
    time_t	when;
    int		quickclose;
    char	msgbuf[1024];
    struct PendingClose *next;
} PendingClose;

PendingClose	*NeedClosing = NULL;
MemPool 	*PendingMemPool;
int		DelayedClose = 0;

#ifdef	READER_BAN_LISTS

typedef struct BanInfo {
    IPList *BanList;
    int BanHashSize;
    int BanLinkSize;
    int BanCount;
    time_t BanTime;
} BanInfo;

BanInfo	HostBan;
BanInfo	UserBan;
BanInfo	GroupBan;
BanInfo	GlobalBan;
char *LastBan = NULL;

#endif

#define RET_CLOSE       1
#define RET_PAUSE       2
#define RET_LOCK        3

typedef struct Retain {   
    struct Retain *re_Next;
    FILE        *re_Fi;
    FILE        *re_Fo;
    int         re_What;
} Retain;


Retain	*ReBase;
int	PausedCount = 0;
int	Exiting = 0;
MemPool	*ReMemPool;

void
Usage(void)
{
    fprintf(stderr, "Usage: dreaderd -p pathhost [-A admin] [-B bindip[:port] [-b fd][-C file]\n");
    fprintf(stderr, "                [-c delaysecs] [-d [debugval] [-D dnsprocs ] [-F feedprocs]\n");
    fprintf(stderr, "                [-f val] [-h hostname ] [-M readerprocs ] [-N readthreads]\n");
    fprintf(stderr, "                [-P [bindip:]port ] [-R rxbufsize ]\n");
    fprintf(stderr, "                [-r rtstatusfile ] [ -S[T|R]# ] [-s statusline ]\n");
    fprintf(stderr, "                [-T txbufsize] [ -V ] [-X xrefhost ] [ -x xrefslavehost ] [ -Z ]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "   -p pathhost      Set the default Path: entry for this host:\n");
    fprintf(stderr, "   -A admin         Set the default newsadmin email for this host:\n");
    fprintf(stderr, "   -B bindip[:port] Bind to a specific interface\n");
    fprintf(stderr, "   -b fd            Provide an open socket to listen on\n");
    fprintf(stderr, "	-C file		specify diablo.config to use\n");
    fprintf(stderr, "   -c delaysecs     Delay closing of failed connections\n");
    fprintf(stderr, "   -d [debugval]    Enable debugging\n");
    fprintf(stderr, "   -D dnsprocs      Specify the number of DNS processes to start\n");
    fprintf(stderr, "   -F feedprocs     Specify the number of dedicated feed processes to start\n");
    fprintf(stderr, "   -f [0|1]         Disable/enable fast copy of data from spool to client\n");
    fprintf(stderr, "   -h hostname      Set the default banner hostname for this server\n");
    fprintf(stderr, "   -M readprocs     Specify the number of reader processes to start\n");
    fprintf(stderr, "   -N readthreads   Specify the number of threads per reader process\n");
    fprintf(stderr, "   -P [bindip]:port Bind to a specific port\n");
    fprintf(stderr, "   -R rxbufsize     Set the TCP rcv buffer size\n");
    fprintf(stderr, "   -r rtstatusfile  Set the filename to store realtime status data\n");
    fprintf(stderr, "   -S[T:R]#         Set TCP rx/tx buffer sizes\n");
    fprintf(stderr, "   -s statusline    Use this area of process space to write status data\n");
    fprintf(stderr, "   -T txbufsize     Set the TCP txmit buffer size\n");
    fprintf(stderr, "   -V               Print version\n");
    fprintf(stderr, "   -X               Set the default Xref: hostname shown to clients\n");
    fprintf(stderr, "   -x               Set the Xref: hostname that must match incoming\n");
    fprintf(stderr, "                       feed Xref: lines\n");
    fprintf(stderr, "   -Z               Enable core file debugging\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    int passedfd = -1;

    LoadDiabloConfig(ac, av);

    if (strcmp(DRIncomingLogPat, "NONE") == 0)
	DRIncomingLogPat = NULL;
    else
	IncomingLogPat = DRIncomingLogPat;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	int v;

	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected option: %s\n", ptr);
	    exit(1);
	}
	ptr += 2;

	v = (*ptr) ? strtol(ptr, NULL, 0) : 1;

	switch(ptr[-1]) {
	case 'A':
	    strdupfree(&DOpts.NewsAdmin, (*ptr) ? ptr : av[++i], "");
	    break;
	case 'B':
	    {
		BindList *bl = calloc(sizeof(BindList), 1);

		bl->bl_Port = NULL;
		bl->bl_Host = NULL;

		if (*ptr == 0)
		    ptr = av[++i];
		if (*ptr == '[') {
		    char *p = strchr(ptr, ']');
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			bl->bl_Port = strdup(p + 1);
		} else if (strrchr(ptr, ':')) {
		    bl->bl_Port = strdup(strrchr(ptr, ':') + 1);
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		} else {
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		}
		bl->bl_Next = BindBase;
		BindBase = bl;
	    }
	    break;
	case 'b':
	    passedfd = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'c':
	    DelayedClose = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'd':
	    DebugOpt = v;
	    break;
	case 'D':
	    DOpts.ReaderDns = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'F':
	    DOpts.ReaderFeedForks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'f':
	    FastCopyOpt = v;
	    break;
	case 'h':
	    strdupfree(&DOpts.ReaderHostName, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'M':
	    DOpts.ReaderForks = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'N':
	    DOpts.ReaderThreads = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'P':
	    {
		BindList *bl = calloc(sizeof(BindList), 1);

		bl->bl_Port = NULL;
		bl->bl_Host = NULL;

		if (*ptr == 0)
		    ptr = av[++i];
		if (*ptr == '[') {
		    char *p = strchr(ptr, ']');
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		    if (p != NULL && (p = strrchr(p, ':')) != NULL)
			bl->bl_Port = strdup(p + 1);
		} else if (strrchr(ptr, ':')) {
		    bl->bl_Port = strdup(strrchr(ptr, ':') + 1);
		    bl->bl_Host = strdup(SanitiseAddr(ptr));
		} else {
		    bl->bl_Port = strdup(ptr);
		}
		bl->bl_Next = BindBase;
		BindBase = bl;
	    }
	    break;
	case 'p':
	    strdupfree(&DOpts.ReaderPathHost, (*ptr) ? ptr : av[++i], "");
	    break;
	case 'R':
	    RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'r':
	    strdupfree(&RTStatus, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'S':
	    switch(*ptr) {
	    case 'T':
		++ptr;
		TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    case 0:
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    case 'R':
		++ptr;
		RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    default:
		fatal("bad option: -S (must be R#, T#, or #)");
		break;
	    }
	    break;
	case 's':
	    ptr = (*ptr) ? ptr : av[++i];
	    SetStatusLine(ptr, strlen(ptr));
	    break;
	case 'T':
	    TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'X':
	    strdupfree(&DOpts.ReaderXRefHost, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'x':
	    strdupfree(&DOpts.ReaderXRefSlaveHost, (*ptr) ? ptr : av[++i], NULL);
	    break;
	case 'Z':
	    CoreDebugOpt = 1;
	    break;
	default:
	    fprintf(stderr, "unknown option: %s\n", ptr - 2);
	    Usage();
	}
    }
    ValidateTcpBufferSize(&RxBufSize);
    ValidateTcpBufferSize(&TxBufSize);

    if (BindBase == NULL) {
	BindList *bl = calloc(sizeof(BindList), 1);

	bl->bl_Host = DOpts.ReaderBindHost;
	bl->bl_Port = DOpts.ReaderPort;

	bl->bl_Next = BindBase;
	BindBase = bl;
    }

    MaxConnects = DOpts.ReaderThreads * DOpts.ReaderForks;

    MyGroupHome = strdup(PatExpand(GroupHomePat));

    if (BindBase == NULL) {
	BindList *bl = calloc(sizeof(BindList), 1);

	bl->bl_Host = DOpts.ReaderBindHost;
	bl->bl_Port = DOpts.ReaderPort;

	bl->bl_Next = BindBase;
	BindBase = bl;
    }

    /*
     * To stdout, not an error, no arguments == user requesting 
     * options list.  We can do this because the -p option is mandatory.
     */

    if (DOpts.ReaderPathHost == NULL)
	Usage();

    /*
     * Can't do OpenLog() yet, but we do want to be able to log
     * errors, so initialize standard syslog.
     */
    openlog("dreaderd", (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID,
            LOG_NEWS);

    /*
     * bind to our socket prior to changing uid/gid
     */

    if (passedfd < 0)
    {
	BindList *bl;

	for (bl = BindBase; bl; bl = bl->bl_Next) {
	    int lfd;

#ifdef INET6
	    int error;
	    struct addrinfo hints;
	    struct addrinfo *res;
	    struct addrinfo *ai;

	    /*
	     * Open a wildcard listening socket
	     */
	    bzero(&hints, sizeof(hints));
	    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	    hints.ai_family = PF_UNSPEC;
	    hints.ai_socktype = SOCK_STREAM;
	    error = getaddrinfo(bl->bl_Host, bl->bl_Port, &hints, &res);
	    if (error != 0) {
		fprintf(stderr, "getaddrinfo: %s:%s: %s\n",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port, gai_strerror(error));
		exit(1);
	    }

	    for (ai = res; ai; ai = ai->ai_next) {

	    lfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	    if (ai->ai_family == AF_INET6 && (bl->bl_Host || res->ai_next)) {
		/*
		 * set INET6 socket to V6ONLY if we bind to a specific
		 * host or getaddrinfo tells us to use seperate wildcard
		 * listening sockets for v4 and v6.
		 */
		int on = 1;
		setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, 
			   (void *) &on, sizeof (on));
	    }
#else
	    struct sockaddr_in lsin;

	    lfd = socket(AF_INET, SOCK_STREAM, 0);
	    bzero(&lsin, sizeof(lsin));
	    if (bl->bl_Port == NULL)
		bl->bl_Port = DOpts.ReaderPort;
	    lsin.sin_port = strtol(bl->bl_Port, NULL, 0);
	    if (lsin.sin_port == 0) {
		struct servent *se;
		se = getservbyname(bl->bl_Port, NULL);
		if (se == NULL) {
		    fprintf(stderr, "Unknown service name: %s\n", bl->bl_Port);
		    exit(1);
		}
		lsin.sin_port = se->s_port;
	    } else {
		lsin.sin_port = htons(lsin.sin_port);
	    }

	    lsin.sin_family = AF_INET;

	    lsin.sin_addr.s_addr = INADDR_ANY;
	    if (bl->bl_Host != NULL) {
		struct hostent *he;

		if ((he = gethostbyname(bl->bl_Host)) != NULL) {
		    lsin.sin_addr = *(struct in_addr *)he->h_addr;
		} else {
		    lsin.sin_addr.s_addr = inet_addr(bl->bl_Host);
		    if (lsin.sin_addr.s_addr == INADDR_NONE) {
			perror("gethostbyname(BindHost)");
			exit(1);
		    }
		}
	    }
#endif
	    {
		int on = 1;

		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		setsockopt(lfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    }

	    if (TxBufSize > 0)
	        if (setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on listen fd %d: %m", 
							TxBufSize, lfd);
	        }
	    if (RxBufSize > 0)
	        if (setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on listen fd %d: %m", 
							RxBufSize, lfd);
	        }

#ifdef INET6
	    if (bind(lfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		perror("bind");
		exit(1);
	    }
#else
	    if (bind(lfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		perror("bind");
		exit(1);
	    }
#endif
	    listen(lfd, 256);
	    if (bl->bl_Host != NULL && strchr(bl->bl_Host, ':') != NULL)
		syslog(LOG_INFO, "Listening on [%s]:%s\n",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port);
	    else
		syslog(LOG_INFO, "Listening on %s:%s\n",
					bl->bl_Host ? bl->bl_Host : "ALL",
					bl->bl_Port);
	    AddThread("acceptor", lfd, -1, THREAD_LISTEN, -1, 0);
	}
#ifdef INET6
        freeaddrinfo(res);
        }
#endif
    } else {
	    {
		int on = 1;

		setsockopt(passedfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
		setsockopt(passedfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	    }
	    if (TxBufSize > 0)
	        if (setsockopt(passedfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on pass fd %d: %m", 
							TxBufSize, passedfd);
	        }
	    if (RxBufSize > 0)
	        if (setsockopt(passedfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int)) < 0) {
		    syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on pass fd %d: %m", 
							RxBufSize, passedfd);
	        }

	    listen(passedfd, 256);
	    AddThread("acceptor", passedfd, -1, THREAD_LISTEN, -1, 0);
    }

    /*
     * Fork if not in debug mode and detach from terminal
     */

    if (DebugOpt == 0) {
	pid_t pid;
	int fd;

	fflush(stdout);
	fflush(stderr);

	if ((pid = fork()) != 0) {
	    if (pid < 0) {
		perror("fork");
		exit(1);
	    }
	    exit(0);
	}
	/*
	 * child continues, close stdin, stdout, stderr, detach tty,
	 * detach process group.
	 */
	{
	    int fd = open("/dev/null", O_RDWR);
	    if (fd != 0)
		dup2(fd, 0);
	    if (fd != 1)
		dup2(fd, 1);
	    if (fd != 2)
		dup2(fd, 2);
	    if (fd > 2)
		close(fd);
	}
#if USE_TIOCNOTTY
	if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
	    ioctl(fd, TIOCNOTTY, 0);
	    close(fd);
	}
#endif
#if USE_SYSV_SETPGRP
        setpgrp();
#else
	setpgrp(0, 0);
#endif
    }

    /*
     * Ignore SIGPIPE, in case a write() fails we do not
     * want to segfault.
     */
    rsignal(SIGPIPE, SIG_IGN);

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

#ifdef	USE_PRCTL
    {
	struct rlimit rlp = { RLIM_INFINITY, RLIM_INFINITY };

	if (setrlimit(RLIMIT_CORE, &rlp) < 0) {
	    perror("setrlimit(RLIMIT_CORE)");
	}
	getrlimit(RLIMIT_CORE, &rlp);
	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
	    perror("prctl(PR_SET_DUMPABLE)");
	}
    }
#endif

    if (DOpts.ReaderHostName == NULL)
	SetMyHostName(&DOpts.ReaderHostName);
    if (*DOpts.ReaderHostName == 0) {
	free(DOpts.ReaderHostName);
	DOpts.ReaderHostName = NULL;
    }
    if (DOpts.NewsAdmin == NULL)
	SetNewsAdmin(&DOpts.NewsAdmin, DOpts.ReaderHostName);
    if (*DOpts.NewsAdmin == 0) {
	free(DOpts.NewsAdmin);
	DOpts.NewsAdmin = NULL;
    }
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderXRefSlaveHost, NULL);
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderPathHost, NULL);
    if (DOpts.ReaderXRefHost == NULL)
	strdupfree(&DOpts.ReaderXRefHost, DOpts.ReaderHostName, NULL);

    /*
     * Setup realtime status file name and remove file in case
     * dreaderd's are left over from a previous run.
     */

    if (RTStatus == NULL) {
	RTStatus = malloc(strlen(PatExpand(LogHomePat)) + 32);
	sprintf(RTStatus, "%s/dreaderd.status", PatExpand(LogHomePat));
    }
    remove(RTStatus);

    /*
     * RTStatus file initial open
     */

    RTStatusOpen(RTStatus, 0, 1);

    /*
     * open syslog
     */

    closelog();
    OpenLog("dreaderd", (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
    SysLogDesc = "dreaderd";

    /*
     * Save PID
     */

    {
	int fd;
	char buf[32];

	sprintf(buf, "%d\n", (int)getpid());

	PidFile = malloc(strlen(PatExpand(LogHomePat)) + 32);
	sprintf(PidFile, "%s/dreaderd.pid", PatExpand(LogHomePat));
	remove(PidFile);
	if ((fd = open(PidFile, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
	    logit(LOG_ERR, "unable to create %s\n", PidFile);
	    exit(1);
	}
	write(fd, buf, strlen(buf));
	close(fd);
    }

#ifdef	DREADER_CLIENT_TIMING
    ticksPerSecond=sysconf(_SC_CLK_TCK);
#endif

    /*
     * Cancel cache (inherited by children)
     */

    InitCancelCache();

    InstallAccessCache();

#ifdef	READER_BAN_LISTS
    bzero(&UserBan, sizeof(UserBan));
    bzero(&HostBan, sizeof(HostBan));
    bzero(&GroupBan, sizeof(GroupBan));
    bzero(&GlobalBan, sizeof(GlobalBan));
#endif

    ReaderPids = malloc((DOpts.ReaderForks + DOpts.ReaderFeedForks) * sizeof(pid_t));
    bzero(ReaderPids, (DOpts.ReaderForks + DOpts.ReaderFeedForks) * sizeof(pid_t));

    /*
     *  Add the UNIX domain socket listener for drcmd
     */
    {
        struct sockaddr_un soun;

        bzero(&soun, sizeof(soun));

        if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
            perror("udom-socket");
            exit(1);
        }
        soun.sun_family = AF_UNIX;

        sprintf(soun.sun_path, "%s", PatRunExpand(DReaderSocketPat));
        remove(soun.sun_path);
        if (bind(ufd, (struct sockaddr *)&soun,
			offsetof(struct sockaddr_un,
				sun_path[strlen(soun.sun_path)+1])) < 0) {
            perror("udom-bind");
            exit(1);
        }
        chmod(soun.sun_path, 0770);

#if NONBLK_ACCEPT_BROKEN
        /* HPUX is broken, see lib/config.h */
#else
        fcntl(ufd, F_SETFL, O_NONBLOCK);
#endif

        if (listen(ufd, 10) < 0) {
            perror("udom-listen");
            exit(1);
        }

	AddThread("drcmd", ufd, -1, THREAD_DRCMD, -1, 0);
    }
    /*
     * accept()/pipe-processing loop, fork child processes as required
     * to maintain requested parameters (initial fork is done as part of
     * the nominal re-fork code when a child dies).
     */

    logit(LOG_NOTICE, "Waiting for connections");
    TimeStart = time(NULL);
    for (;;) {
	/*
	 * handle required forks and child process cleanup
	 *
	 * (A) reader NNTP processes
	 */

	while (!Exiting && NumReaderForks < DOpts.ReaderForks) {
	    pid_t pid;
	    int fd;

	    ThisReaderFork = getReaderForkSlot();
	    FeedOnlyServer = 0;
	    pid = ForkPipe(&fd, ReaderTask, "reader");
	    FeedOnlyServer = -1;
	    if (pid < 0)
		break;
	    ReaderPids[ThisReaderFork] = pid;
	    AddThread("reader", fd, pid, THREAD_READER, -1, 0);
	    ++NumReaderForks;
	}

	/*
	 * (B) reader feeder-only processes ('mode reader' not available)
	 */

	while (!Exiting && NumReaderFeedForks < DOpts.ReaderFeedForks) {
	    pid_t pid;
	    int fd;

	    ThisReaderFork = getReaderForkSlot();
	    FeedOnlyServer = 1;
	    pid = ForkPipe(&fd, ReaderTask, "feed");
	    FeedOnlyServer = -1;
	    if (pid < 0)
		break;
	    ReaderPids[ThisReaderFork] = pid;
	    AddThread("feed", fd, pid, THREAD_FEEDER, -1, 0);
	    ++NumReaderFeedForks;
	}

	/*
	 * (C) standard DNS processes
	 */

	while (!Exiting && NumDnsForks < DOpts.ReaderDns) {
	    pid_t pid;
	    int fd;

	    if ((pid = ForkPipe(&fd, DnsTask, "reader-dns")) < 0)
		break;
	    AddThread("dns", fd, pid, THREAD_DNS, -1, 0);
	    ++NumDnsForks;
	}

#ifdef NOTDEF
	/*
	 * (D) server hostname resolver	(future)
	 */

	while (!Exiting && NumSDnsForks < MaxSDnsForks) {
	    pid_t pid;
	    int fd;

	    if ((pid = ForkPipe(&fd, DnsTask, "reader-sdns")) < 0)
		break;
	    AddThread("sdns", fd, pid, THREAD_SDNS, -1, 0);
	    ++NumSDnsForks;
	}
#endif

	/*
	 * (E) dead processes
	 */

	{
	    pid_t pid;

	    while ((pid = wait3(NULL, WNOHANG, NULL)) > 0) {
		ForkDesc *desc;

		if ((desc = FindThread(-1, pid)) != NULL) {
		    if (desc->d_Type == THREAD_DNS) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost dns resolver %d", (int)pid);
			--NumDnsForks;
			if (desc->d_FdPend >= 0) {
			    logit(LOG_NOTICE, "pending descriptor from %s on dead DNS pid closed",
				NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1)
			    );
			    close(desc->d_FdPend);
			    desc->d_FdPend = -1;
			    NumPending -= desc->d_Count; /* note: d_Count may already be 0 */
			    NumActive -= desc->d_Count;
			    desc->d_Count = 0;
			}
		    } else if (desc->d_Type == THREAD_READER) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost reader process %d", (int)pid);
			--NumReaderForks;
			delReaderForkSlot(pid);
			NumActive -= desc->d_Count;
			deleteDnsResHash(NULL, desc);
		    } else if (desc->d_Type == THREAD_FEEDER) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost feeder process %d", (int)pid);
			--NumReaderFeedForks;
			delReaderForkSlot(pid);
			NumActive -= desc->d_Count;
			deleteDnsResHash(NULL, desc);
		    } else if (desc->d_Type == THREAD_SDNS) {
			if (!Exiting)
			    logit(LOG_CRIT, "Lost sdns process %d", (int)pid);
			--NumSDnsForks;
		    } else {
			fatal("main loop, unknown thread type %d pid %d\n", desc->d_Type, (int)pid);
		    }
		    DelThread(desc);
		} else {
		    fatal("main loop, wait3() pid %d, pid is not known to me!", (int)pid);
		}
	    }
	}

#ifdef	READER_BAN_LISTS
	/*
	 * Check for changes in the Ban list
	 */
	if (DOpts.ReaderBan != NULL && LastBan != DOpts.ReaderBan) {
	    logit(LOG_INFO, "Setting ban list");
	    SetBanList(DOpts.ReaderBan);
	    LastBan = DOpts.ReaderBan;
	}
#endif
	/*
	 * (F) select core
	 *
	 * select on descriptors (listen socket and pipe sockets only),
	 * process results
	 */
	{
	    struct timeval tv;
	    fd_set rfds = SFds;
	    int i;

	    if (Exiting)
		NextTimeout(&tv, 50);
	    else
		NextTimeout(&tv, 10 * 1000);

	    stprintf("Connect=%d Failed=%d Dns=%d/%d Act=%d/%d", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
	    );
	    RTStatusUpdate(0, "Connect=%d Failed=%d Dns=%d/%d Act=%d/%d", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
	    );

	    select(MaxFds, &rfds, NULL, NULL, &tv);
	    gettimeofday(&CurTime, NULL);

	    for (i = 0; i < MaxFds; ++i) {
		if (FD_ISSET(i, &rfds)) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL) {
#ifdef	DREADER_CLIENT_TIMING
			struct tms start,end;

		    	times(&start);
#endif
			switch(desc->d_Type) {
			case THREAD_LISTEN:
			    DoListen(desc, CurTime.tv_sec);
			    break;
			case THREAD_DNS:
			    /*
			     * read query result from DNS handler, then
			     * issue appropriate action to descriptor.
			     */
			    HandleDnsThread(desc);
			    break;
			case THREAD_READER:
			case THREAD_FEEDER:
			    /*
			     * reader process returns a dres struc for each
			     * closed connection.  Update our ref counts.
			     */
			    {
				DnsRes dres;
				int n;
				int recv_fd;

				n = RecvMsg(desc->d_Fd, &recv_fd, &dres);
				if (n != sizeof(dres))
				    break;
				if (dres.dr_ResultFlags == DR_SERVER_STATS) {
				    TotalSpoolArticles += dres.dr_ArtCount;
				    TotalSpoolBytes += dres.dr_ByteCount;
				} else {
#ifdef	INET6
				    struct sockaddr_storage rsin;
				    bzero((void *)&rsin, sizeof(rsin));

				    memcpy(&rsin, &dres.dr_Addr,
					    sizeof(struct sockaddr_storage));
#else
				    struct sockaddr_in rsin;

				    bzero(&rsin, sizeof(rsin));
				    memcpy(&rsin, &dres.dr_Addr, sizeof(rsin));
#endif
				    if (dres.dr_ResultFlags == DR_REQUIRE_DNS) {
					SendDns(recv_fd, &rsin, &dres);
				    } else {
					dres.dr_ResultFlags = DR_SESSEXIT_RPT;
					SendDns(recv_fd, &rsin, &dres);
					close(recv_fd);
				    }
				    --NumActive;
				    --desc->d_Count;
				    deleteDnsResHash(&dres, desc);
				}
			    }
			    break;
			case THREAD_DRCMD:
			    DoCommand(desc->d_Fd);
			    break;
			default:
			    fatal("Unknown descriptor type %d", desc->d_Type);
			    break;
			}
#ifdef	DREADER_CLIENT_TIMING
			if ((desc = FindThread(i, -1)) != NULL) {
			    times(&end);
			    desc->d_utime+=end.tms_utime-start.tms_utime;
			    desc->d_stime+=end.tms_stime-start.tms_stime;
			}
#endif
		    } else {
			fatal("select returned unknown descriptor %d", i);
		    }
		}
	    }
	    (void)ScanTimers(1, 0);
	}

	/*
	 * Disable listen socket if we have too many connections pending or
	 * we are full up, enable listen socket otherwise.  NOTE: NumPending
	 * is included in the NumActive number.
	 */
	if (NumPending < NumDnsForks && 
	    NumActive < NumReaderForks * DOpts.ReaderThreads
	) {
	    if (ListenersEnabled == 0) {
		int i;

		ListenersEnabled = 1;
		for (i = 0; i < MaxFds; ++i) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL && 
			desc->d_Fd >= 0 &&
			desc->d_Type == THREAD_LISTEN
		    ) {
			FD_SET(desc->d_Fd, &SFds);
		    }
		}
	    }
	} else {
	    if (ListenersEnabled == 1) {
		int i;

		ListenersEnabled = 0;
		for (i = 0; i < MaxFds; ++i) {
		    ForkDesc *desc;

		    if ((desc = FindThread(i, -1)) != NULL && 
			desc->d_Fd >= 0 &&
			desc->d_Type == THREAD_LISTEN
		    ) {
			FD_CLR(desc->d_Fd, &SFds);
		    }
		}
	    }
	}
	InstallAccessCache();
	/* Clean out the delayed closes */
	if (NeedClosing != NULL) {
	    PendingClose *pc;
	    PendingClose *ppc = NULL;
	    time_t timenow = time(NULL);

	    for (pc = NeedClosing; pc != NULL; ppc = pc, pc = pc->next) {
		if (pc->when > timenow)
		    continue;
		write(pc->fd, pc->msgbuf, strlen(pc->msgbuf));
		close(pc->fd);
		pc->fd = -1;
		++FailCount;
		if (!pc->quickclose) {
		    --NumActive;
		}
		if (pc == NeedClosing)
		    NeedClosing = pc->next;
		if (ppc != NULL)
		    ppc->next = pc->next;
		zfree(&PendingMemPool, pc, sizeof(PendingClose));
	    }
	}
	if (Exiting && !NumReaderFeedForks &&
		!NumReaderForks && !NumDnsForks && !NumSDnsForks)
	    break;
    }
    FinishRetain(RET_CLOSE);
    RTStatusClose();
    logit(LOG_NOTICE, "Exiting");
    return(0);
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
            zfree(&ReMemPool, ret, sizeof(Retain));
        } else {
            pret = &ret->re_Next;
        }
    }
} 

void
DoListen(ForkDesc *desc, time_t t)
{

    /*
     * accept new descriptor, depend on socket
     * buffer for write to not block if descriptor
     * beyond what we can handle.
     */
    {
        char errbuf[1024];
#ifdef INET6
	struct sockaddr_storage rsin;
#else
	struct sockaddr_in rsin;
#endif
	int rslen = sizeof(rsin);
	int fd;

	bzero(&rsin, sizeof(rsin));
	fd = accept(desc->d_Fd, (struct sockaddr *)&rsin, &rslen);

	if (PausedCount) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Server is temporarily unavailable, please try later%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
	    write(fd, errbuf, strlen(errbuf));
	    close(fd);
	    fd = -1;
	    ++ConnectCount;
	    ++FailCount;
	    return;
	}

	if (fd >= MAXFDS) {
	    logit(LOG_CRIT, "fd beyond MAXFDS: %d, closing", fd);
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Too many open descriptors, please try later%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
	    write(fd, errbuf, strlen(errbuf));
	    close(fd);
	    fd = -1;
	    ++ConnectCount;
	    ++FailCount;
	    return;
	}

	if (fd >= 0) {
#ifdef	READER_BAN_LISTS
	    if (Banned(inet_ntoa(rsin.sin_addr), t)) {
		PendingClose *pc;
		logit(LOG_INFO, "rejected auto-banned IP %s", inet_ntoa(rsin.sin_addr));
		snprintf(errbuf, sizeof(errbuf), "502 %s: Access denied to your node%s%s\r\n",
					DOpts.ReaderHostName,
					(DOpts.NewsAdmin != NULL) ? " - " : "",
					safestr(DOpts.NewsAdmin, ""));
		if (DelayedClose) {
		    for (pc = NeedClosing; pc != NULL && pc->next != NULL;
								pc = pc->next);
		    if (pc == NULL) {
			pc = zalloc(&PendingMemPool, sizeof(PendingClose));
			NeedClosing = pc;
		    } else {
			pc->next = zalloc(&PendingMemPool, sizeof(PendingClose));
			pc = pc->next;
		    }
		    pc->fd = fd;
		    strncpy(pc->msgbuf, errbuf, sizeof(pc->msgbuf) - 1);
		    pc->msgbuf[sizeof(pc->msgbuf) - 1] = '\0';
		    pc->when = time(NULL) + DelayedClose;
		    pc->quickclose = 1;
		    pc->next = NULL;
		} else {
		    write(fd, errbuf, strlen(errbuf));
		    close(fd);
		    fd = -1;
		}
		++ConnectCount;
		++FailCount;
		return;
	    }
#endif
	    /*
	     * queue descriptor information to DNS
	     * resolver and put descriptor on hold.
	     */
	    SendDns(fd, &rsin, NULL);
	}
    }
}

/*
 * DOCOMMAND() - handles a control connection
 */

void
DoCommand(int ufd)
{
    int fd;
    int retain = 0;
#ifdef INET6
    struct sockaddr_storage asin;
#else
    struct sockaddr_in asin;
#endif
    ACCEPT_ARG3_TYPE alen = sizeof(asin);

    /*
     * This can be a while() or an if(), but apparently some OS's fail to
     * properly handle O_NONBLOCK for accept() on unix domain sockets so...
     */

    if ((fd = accept(ufd, (struct sockaddr *)&asin, &alen)) > 0) {
	FILE *fi;
	FILE *fo;

	fcntl(fd, F_SETFL, 0);
	fo = fdopen(dup(fd), "w");
	fi = fdopen(fd, "r");

	if (fi && fo) {
	    char buf[MAXLINE];

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		char *s1;
		char *s2;

		if (DebugOpt)
		    printf("%d << %s", (int)getpid(), buf);

		if ((s1 = strtok(buf, " \t\r\n")) == NULL)
		    break;
		s2 = strtok(NULL, "\n");
		if (strcmp(s1, "status") == 0) {
		    fprintf(fo, "Connect=%d Failed=%d Dns=%d/%d Act=%d/%d", 
			ConnectCount, FailCount,
			NumPending, DOpts.ReaderDns, 
			NumActive, MaxConnects
		    );
		} else if (strcmp(s1, "version") == 0) {
		    fprintf(fo, "200 DREADERD Version %s - %s\n", VERS, SUBREV);
		} else if (strcmp(s1, "vstats") == 0) {
		    getVStats(fo);
		} else if (strcmp(s1, "stats") == 0) {
		    getStats(fo, 0);
		} else if (strcmp(s1, "rawstats") == 0) {
		    getStats(fo, 1);
#ifdef	READER_BAN_LISTS
		} else if (strcmp(s1, "banlist") == 0) {
		    DumpBannedLists(fo);
		} else if (strcmp(s1, "banconfig") == 0) {
		    DumpBannedConfigs(fo);
#endif
		} else if (strcmp(s1, "pause") == 0) {
		    /* retain = RET_PAUSE; */
		    ++PausedCount;
		    fprintf(fo, "200 Pause, count %d.\n", PausedCount);
		} else if (strcmp(s1, "go") == 0) {
		    if (PausedCount)
			--PausedCount;
		    fprintf(fo, "200 Resume, count %d\n", PausedCount);
		} else if (strcmp(s1, "quit") == 0) {
		    fprintf(fo, "200 Goodbye\n");
		    fprintf(fo, ".\n");
		    break;
		} else if (strcmp(s1, "exit") == 0 || strcmp(s1, "aexit")==0) {
		    int i;

		    if (s1[0] == 'e')
			retain = RET_CLOSE;

		    fprintf(fo, "211 Exiting\n");
		    fflush(fo);

		    if (Exiting == 0) {
			ForkDesc *desc;
			Exiting = 1;

			logit(LOG_INFO, "Shutting down sub-processes");
			for (i = 0; i < MaxFds; ++i)
			if ((desc = FindThread(i, -1)) != NULL) {
			    switch(desc->d_Type) {
			    case THREAD_DNS:
				if (desc->d_Pid)
				    kill(desc->d_Pid, SIGKILL);
				break;
			    case THREAD_READER:
			    case THREAD_FEEDER:
				if (desc->d_Pid)
				    kill(desc->d_Pid, SIGHUP);
				break;
			    case THREAD_LISTEN:
				close(desc->d_Fd);
				break;
			    case THREAD_DRCMD:
				break;
			    }
			}
		    } else {
			fprintf(fo, "200 Exit is already in progress\n");
		    }
		} else if (strcmp(s1, "config") == 0) {
		    if (s2 != NULL) {
			char *opt;
			char *cmd;
			int cmdErr = 0;
			if ((cmd = strtok(s2, " \t\n")) != NULL &&
			    (opt = strtok(NULL, " \t\n")) != NULL) {
			    if (SetCommand(fo, cmd, opt, &cmdErr) == 0) {
				CheckConfigOptions(1);
				fprintf(fo, "Set '%s' to '%s'\n", cmd, opt);
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
			    DumpConfigOptions(fo, s2, CONF_READER);
			}
		    } else {
			DumpConfigOptions(fo, NULL, CONF_READER);
		    }
		} else if (strcmp(s1, "help") == 0) {
		    fprintf(fo,"Supported commands:\n");
		    fprintf(fo,"   help      you are looking at it\n");
		    fprintf(fo,"   status    short status line\n");
		    fprintf(fo,"   version   return dreaderd version\n");
		    fprintf(fo,"   config    view/set run-time config\n");
		    fprintf(fo,"   stats     general server statistics\n");
		    fprintf(fo,"   rawstats  general server statistics (without pretty printing)\n");
		    fprintf(fo,"   vstats    virtual host statistics\n");
#ifdef	READER_BAN_LISTS
		    fprintf(fo,"   banconfig show current ban configs\n");
		    fprintf(fo,"   banlist   show the current ban lists\n");
#endif
		    fprintf(fo,"   go        resume accepting new connections\n");
		    fprintf(fo,"   exit      close all connections and exit\n");
		} else {
		    fprintf(fo, "500 Unrecognized command: %s\n", s1);
		}
		if (retain == 0)
		    fprintf(fo, ".\n");
		fflush(fo);
		/* if (retain) */
		    break;
	    }
	}
	if (retain) {
	    Retain *ret = zalloc(&ReMemPool, sizeof(Retain));

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
    }
}

#ifdef INET6
void
SendDns(int fd, struct sockaddr_storage *rsin, DnsRes *dres)
#else
void
SendDns(int fd, struct sockaddr_in *rsin, DnsRes *dres)
#endif
{
    ForkDesc *best;
#ifdef INET6
    struct sockaddr_storage lsin;
#else
    struct sockaddr_in lsin;
#endif
    int lslen = sizeof(lsin);

    /*
     * Check for auth changes
     */
    InstallAccessCache();

    bzero(&lsin, sizeof(lsin));

    if (getsockname(fd, (struct sockaddr *)&lsin, &lslen) < 0) {
	logit(LOG_ERR, "getsockname(%d) call failed: %s", fd, strerror(errno));
    }

    ++ConnectCount;

    best = FindLeastUsedThread(THREAD_DNS, 1, 0, NULL, -1, NULL, NULL);

    if (best != NULL) {
	DnsReq dreq;

	++NumActive;
	++NumPending;
	++best->d_Count;

	bzero(&dreq, sizeof(dreq));
	bcopy(rsin, &dreq.dr_RSin, sizeof(dreq.dr_RSin));
	bcopy(&lsin, &dreq.dr_LSin, sizeof(dreq.dr_LSin));
	if (dres != NULL) {
	    dreq.dr_ResultFlags = dres->dr_ResultFlags;
	    strcpy(dreq.dr_AuthUser, dres->dr_AuthUser);
	    strcpy(dreq.dr_AuthPass, dres->dr_AuthPass);
	    dreq.dr_ByteCount = dres->dr_ByteCount;
	    dreq.dr_GrpCount = dres->dr_GrpCount;
	    dreq.dr_ArtCount = dres->dr_ArtCount;
	    dreq.dr_PostBytes = dres->dr_PostBytes;
	    dreq.dr_PostCount = dres->dr_PostCount;
	    dreq.dr_PostFailCount = dres->dr_PostFailCount;
	    dreq.dr_ByteCountArticle = dres->dr_ByteCountArticle;
	    dreq.dr_ByteCountHead = dres->dr_ByteCountHead;
	    dreq.dr_ByteCountBody = dres->dr_ByteCountBody;
	    dreq.dr_ByteCountList = dres->dr_ByteCountList;
	    dreq.dr_ByteCountXover = dres->dr_ByteCountXover;
	    dreq.dr_ByteCountXhdr = dres->dr_ByteCountXhdr;
	    dreq.dr_ByteCountOther = dres->dr_ByteCountOther;
	    dreq.dr_SessionLength = time(NULL) - dres->dr_TimeStart;
	}

        best->d_FdPend = fd;
	bcopy(rsin, &best->d_SaveRSin, sizeof(best->d_SaveRSin));
	write(best->d_Fd, &dreq, sizeof(dreq));
	/* 
	 * ignore return code.  If DNS process died, it
	 * will be cleaned up later.
	 */
    } else {
	char errbuf[1024];
	logit(LOG_CRIT, "Unable to find spare DNS thread - rejecting connection");
	snprintf(errbuf, sizeof(errbuf), "400 %s: Server too busy - please try later%s%s\r\n",
	        DOpts.ReaderHostName,
		(DOpts.NewsAdmin != NULL) ? " - " : "",
		safestr(DOpts.NewsAdmin, ""));
	write(fd, errbuf, strlen(errbuf));
	close(fd);
	fd = -1;
	++FailCount;
    }
}

pid_t
ForkPipe(int *pfd, void (*task)(int fd, const char *id), const char *description)
{
    int fds[2] = { 0, 0 };
    pid_t pid;

    *pfd = -1;

    /*
     * NOTE: must use socketpair in order to be able to pass descriptors.
     */

    if (socketpair(PF_UNIX, SOCK_STREAM, IPPROTO_IP, fds) < 0) {
	logit(LOG_ERR, "socketpair() call failed");
	return((pid_t)-1);
    }
    if (fds[0] > MAXFDS || fds[1] > MAXFDS) {
	logit(LOG_ERR, "pipe desc > MAXFDS: %d/%d/%d", fds[0], fds[1], MAXFDS);
	close(fds[0]);
	close(fds[1]);
	return((pid_t)-1);
    }

    *pfd = fds[0];

    fflush(stdout);
    fflush(stderr);

    /*
     * Some systems can't deal with syslog through a
     * fork.
     */

    CloseLog(NULL, 1);
    pid = fork();
    if (pid == 0) {
	OpenLog(description, (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
	SysLogDesc = description;
    } else {
	OpenLog(SysLogDesc, (DebugOpt ? LOG_PERROR : 0) | LOG_NDELAY | LOG_PID);
    }

    if (pid == 0) {
	freePool(&DnsResPool);
	stprintf("%s startup", description);
	/* note: DnsResHash not longer valid in children */
	/* note: fds[1] is a normal descriptor, not a non-blocking desc */
	ResetThreads();
	close(*pfd);
	task(fds[1], description);
	_exit(1);
    } else if (pid < 0) {
	logit(LOG_ERR, "fork() failed: %s", strerror(errno));
	close(*pfd);
	close(fds[1]);
	*pfd = -1;
    } else if (pid > 0) {
	close(fds[1]);
	if (DebugOpt)
	    printf("forked %s child process %d\n", description, (int)pid);
    }
    return(pid);
}


void
HandleDnsThread(ForkDesc *desc)
{
    DnsRes dres;
    ForkDesc *reader = NULL;
    static int RandCounter = 0;
    const char *error1 = NULL;
    const char *error2 = NULL;
    char errbuf[1024], *p;
    int r, bantime;
    char tmpaddr[NI_MAXHOST];

    errno = 0;
    if ((r = read(desc->d_Fd, &dres, sizeof(dres))) != sizeof(dres)) {
	/*
	 * The dns subprocesses return a fixed-size structure.  If we 
	 * get part of it, we should get the whole thing but may not 
	 * since the descriptor is set for non-blocking.  Handle the
	 * case.
	 */
	if (r > 0) {
	    int n;

	    n = read(desc->d_Fd, (char *)&dres + r, sizeof(dres) - r);
	    if (n > 0)
		r += n;
	}
	if (r != sizeof(dres)) {
	    if (errno != EINTR && 
		errno != EWOULDBLOCK && 
		errno != EINPROGRESS &&
		r != 0
	    ) {
		/*
		 * let the child process cleanup any operations in
		 * progress.
		 */
		logit(LOG_CRIT, "read %d error on DNS subprocess, killing %d (%s)", r, (int)desc->d_Pid, strerror(errno));
		kill(desc->d_Pid, 9);
		NumActive -= desc->d_Count;
		NumPending -= desc->d_Count;
		desc->d_Count = 0;
	    }
	    return;
	}
    }
    --desc->d_Count;
    --NumPending;

    if (dres.dr_ResultFlags & DR_SESSEXIT_RPT) {
	/* we don't need the rest of this */
	--NumActive;
	return;
    }

    if (isdigit(dres.dr_ReaderName[0]) && isdigit(dres.dr_ReaderName[1]) && isdigit(dres.dr_ReaderName[2])) {
	p = dres.dr_ReaderName;
	if (! strncmp(p, "600 ", 4)) {
	    /* 
	     * Deep six a user via authentication 
	     * Return: 110 600 <time-in-mins> 500 Your connection is rejected
	     * Perpetuating the ugly return code chaining
	     * Requires reader ban lists to actually work,
	     * and actually only tweaks the host banned count.
	     */

	    p += 4;
	    bantime = strtol(p, NULL, 10) * 60;
	    for (; *p && *p != ' '; p++) {
	    }
	    for (; *p && *p == ' '; p++) {
	    }

#ifdef	READER_BAN_LISTS
	    if (HostBan.BanCount > 0)
	        IPListAdd(&HostBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				HostBan.BanCount,
				time(NULL) + bantime,
				HostBan.BanHashSize,
				HostBan.BanLinkSize);
#endif

	}
	snprintf(errbuf, sizeof(errbuf), "%s\r\n", p);
	error1 = errbuf;
	/* Point error2 at errbuf + 4 to catch the specific error */
	/* error2 = "CustomError"; */
	error2 = errbuf + 4;
    }

    if (error1 == NULL) {
        if ((r = ReadAccessCache()) < 0) {
	    logit(LOG_CRIT, "Fatal: Unable to load access cache");
	    exit(1);
        }
        SetAuthDetails(&dres, dres.dr_ReaderName);
        dres.dr_TimeStart = time(NULL);
        if (r == 1)
	    ClearOldAccessMap();

        if (dres.dr_Code == 0) {
	    snprintf(errbuf, sizeof(errbuf), "502 %s: Access denied to your node%s%s\r\n",
		    dres.dr_VServerDef->vs_HostName,
		    *dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
		    dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "Unauthorized";
        }

        if (dres.dr_Flags & DF_STATUS) {
            snprintf(errbuf, sizeof(errbuf), "180 %s status: connect=%d failed=%d dns=%d/%d act=%d/%d\r\n", dres.dr_VServerDef->vs_HostName,
		    ConnectCount, FailCount, 
		    NumPending, DOpts.ReaderDns,
		    NumActive, MaxConnects
		    );
	    write(desc->d_FdPend, errbuf, strlen(errbuf));
	    close(desc->d_FdPend);
	    desc->d_FdPend = -1;
	    --NumActive;
	    deleteDnsResHash(&dres, desc);
	    return;
        }
    }

    /*
     * Pass the descriptor to the correct thread.  A DF_FEED only connection
     * can be passed to a feed-specific thread.
     */

    if (error1 == NULL) {
	if ((dres.dr_Flags & (DF_FEED|DF_READ|DF_POST)) == DF_FEED) {
	    reader = FindLeastUsedThread(THREAD_FEEDER, DOpts.ReaderThreads, 0, &RandCounter, -1, NULL, NULL);
	    dres.dr_Flags |= DF_FEEDONLY;
	} else {
	    reader = FindLeastUsedThread(THREAD_READER, DOpts.ReaderThreads, 0, &RandCounter, -1, NULL, NULL);
	}
	if (reader == NULL) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Global connection limit reached, please try later%s%s\r\n",
	        DOpts.ReaderHostName,
		(DOpts.NewsAdmin != NULL) ? " - " : "",
		safestr(DOpts.NewsAdmin, ""));
	    error1 = errbuf;
	    error2 = "ServerMaxedOut";
	}
    }

    /*
     * Add DnsRes to hash table
     */
    strncpy(tmpaddr, NetAddrToSt(0, (struct sockaddr *)&dres.dr_Addr, 1, 0, 0),
							sizeof(tmpaddr) - 1);
    tmpaddr[sizeof(tmpaddr) - 1] = '\0';
    if (error1 == NULL && tmpaddr != NULL) {
	int hi = shash(tmpaddr) & DRHMASK;
	int hmi;
	DnsRes *dr;
	int gcount = 0;
	int hcount = 0;
	int ucount = 0;
	int icount = 0;
	int vcount = 0;

	/*
	 * Until later, the counters only reflect the values of
	 * sessions prior to the current one. This makes it easier to
	 * calculate the rate limit.
	 */
	for (dr = DnsResHash[hi]; dr; dr = dr->dr_HNext) {
	    if (strcmp(tmpaddr, NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 0, 0)) == 0) {
		++hcount;
		if (*dres.dr_IdentUser && *dr->dr_IdentUser &&
			strcmp(dres.dr_IdentUser, dr->dr_IdentUser) == 0)
		    ++icount;
	    }
	}
	if (dres.dr_ReaderDef->rd_MaxConnPerGroup ||
			dres.dr_ReaderDef->rd_MaxConnPerUser) {
	    for (hmi=0; hmi < DRHSIZE; hmi++) 
		for (dr = DnsResHash[hmi]; dr; dr = dr->dr_HNext) {
		    if (*dres.dr_AuthUser && *dr->dr_AuthUser &&
			    strcmp(dres.dr_AuthUser, dr->dr_AuthUser) == 0)
			++ucount;
		    if (strcmp(dres.dr_ReaderName, dr->dr_ReaderName) == 0)
			++gcount;
		    if (strcmp(dres.dr_VServer, dr->dr_VServer) == 0)
			++vcount;
		}
	}

	/*
	 * Tax the rate limit based on number of connections
	 */
	{
	    int count = 0;
	    if (ucount)
		count = ucount;
	    else if (icount)
		count = icount;
	    else if (hcount)
		count = hcount;
	    dres.dr_ConnCount = count;
	}

	/*
	 * Now we add the current connection before we print the totals
	 * and work out the limit restrictions
	 */
	gcount++;
	hcount++;
	ucount++;
	icount++;
	vcount++;

	if ((dres.dr_Flags & DF_QUIET) == 0)
	    logit(LOG_INFO, "counters %s%s%s%s%s %s %s tcount=%d/%d vcount=%d/%d gcount=%d/%d hcount=%d/%d ucount=%d/%d icount=%d byte=%d",
			(dres.dr_AuthUser[0] ? dres.dr_AuthUser : ""),
			(dres.dr_AuthUser[0] ? "/" : ""),
			(dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
			(dres.dr_IdentUser[0] ? "@" : ""),
			dres.dr_Host, dres.dr_ReaderDef->rd_Name,
			dres.dr_VServerDef->vs_Name,
			NumActive, dres.dr_ReaderDef->rd_MaxConnTotal,
			vcount, dres.dr_ReaderDef->rd_MaxConnPerVs,
			gcount, dres.dr_ReaderDef->rd_MaxConnPerGroup,
			hcount, dres.dr_ReaderDef->rd_MaxConnPerHost,
			ucount, dres.dr_ReaderDef->rd_MaxConnPerUser,
			icount, dres.dr_ReaderDef->rd_ByteLimit);

	if (dres.dr_ReaderDef->rd_MaxConnTotal &&
		NumActive > dres.dr_ReaderDef->rd_MaxConnTotal) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Server connection limit reached%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromTotal";
#ifdef	READER_BAN_LISTS
	    if (GlobalBan.BanCount > 0)
		IPListAdd(&GlobalBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				GlobalBan.BanCount,
				time(NULL) + GlobalBan.BanTime,
				GlobalBan.BanHashSize,
				GlobalBan.BanLinkSize);
#endif
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerVs &&
		vcount > dres.dr_ReaderDef->rd_MaxConnPerVs) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: VServer connection limit reached%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromVServer";
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerGroup &&
		gcount > dres.dr_ReaderDef->rd_MaxConnPerGroup) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: You have reached the connection limit for your access classification%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromGroup";
#ifdef	READER_BAN_LISTS
	    if (GroupBan.BanCount > 0)
		IPListAdd(&GroupBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				GroupBan.BanCount,
				time(NULL) + GroupBan.BanTime,
				GroupBan.BanHashSize,
				GroupBan.BanLinkSize);
#endif
	}

	if (dres.dr_ReaderDef->rd_MaxConnPerHost &&
		hcount > dres.dr_ReaderDef->rd_MaxConnPerHost) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Your host connection limit reached%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromHost";
#ifdef	READER_BAN_LISTS
	    if (HostBan.BanCount > 0)
		IPListAdd(&HostBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				HostBan.BanCount,
				time(NULL) + HostBan.BanTime,
				HostBan.BanHashSize,
				HostBan.BanLinkSize);
#endif
	}
	if (dres.dr_ReaderDef->rd_MaxConnPerUser &&
		(ucount > dres.dr_ReaderDef->rd_MaxConnPerUser ||
		 icount > dres.dr_ReaderDef->rd_MaxConnPerUser)) {
	    snprintf(errbuf, sizeof(errbuf), "400 %s: Your per-user connection limit reached%s%s\r\n",
				dres.dr_VServerDef->vs_HostName,
				*dres.dr_VServerDef->vs_NewsAdm ? " - " : "",
				dres.dr_VServerDef->vs_NewsAdm);
	    error1 = errbuf;
	    error2 = "TooManyFromUser";
#ifdef	READER_BAN_LISTS
	    if (UserBan.BanCount > 0)
		IPListAdd(&UserBan.BanList,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 0, 1),
				UserBan.BanCount,
				time(NULL) + UserBan.BanTime,
				UserBan.BanHashSize,
				UserBan.BanLinkSize);
#endif
	}
	if (error1 == NULL) {
	    dr = zalloc(&DnsResPool, sizeof(DnsRes));
	    *dr = dres;
	    dr->dr_HNext = DnsResHash[hi];
	    dr->dr_ReaderPid = reader->d_Pid;
	    DnsResHash[hi] = dr;
	}
    }

    /*
     * error return
     */

    if (error1 != NULL) {
	PendingClose *pc;
	if ((dres.dr_Flags & DF_QUIET) == 0) {
	    logit(
		LOG_NOTICE, 
		"connection to %s from %s%s%s (%s) rejected: %s", 
		dres.dr_VServerDef->vs_Name,
		(dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
		(dres.dr_IdentUser[0] ? "@" : ""),
		dres.dr_Host,
		NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1),
		error2
	    );
	}
	if (!DelayedClose) {
	    write(desc->d_FdPend, error1, strlen(error1));
	    close(desc->d_FdPend);
	    desc->d_FdPend = -1;
	    ++FailCount;
	    --NumActive;
	    return;
	}

	/*
	 * Add the fd to the "pending close" list
	 * They will be closed after the DelayedClose timeout
	 */
	for (pc = NeedClosing; pc != NULL && pc->next != NULL; pc = pc->next);
	if (pc == NULL) {
	    pc = zalloc(&PendingMemPool, sizeof(PendingClose));
	    NeedClosing = pc;
	} else {
	    pc->next = zalloc(&PendingMemPool, sizeof(PendingClose));
	    pc = pc->next;
	}
	pc->fd = desc->d_FdPend;
	strncpy(pc->msgbuf, error1, sizeof(pc->msgbuf) - 1);
	pc->msgbuf[sizeof(pc->msgbuf) - 1] = '\0';
	pc->when = time(NULL) + DelayedClose;
	pc->quickclose = 0;
	pc->next = NULL;
	desc->d_FdPend = -1;
	deleteDnsResHash(&dres, desc);

	return;
    }

    /*
     * Leave NumActive bumped, transfer descriptor to subprocess
     *
     * pass the descriptor to an appropriate subprocess
     */

    if (SendMsg(reader->d_Fd, desc->d_FdPend, &dres) < 0) {
	char errbuf[1024];
	logit(LOG_ERR, 
	    "connect: error passing file descriptor: %s, killing reader pid %d", 
	    strerror(errno),
	    reader->d_Pid
	);
	if (DebugOpt)
	    printf("sendmsg() failed: %s\n", strerror(errno));
	snprintf(errbuf, sizeof(errbuf), "400 %s: Server error, fd pass failed%s%s\r\n",
	    DOpts.ReaderHostName,
	    (DOpts.NewsAdmin != NULL) ? " - " : "",
	    safestr(DOpts.NewsAdmin, ""));
	write(desc->d_FdPend, errbuf, strlen(errbuf));
	close(desc->d_FdPend);
	desc->d_FdPend = -1;
	++FailCount;
	--NumActive;
	deleteDnsResHash(&dres, desc);

	if (reader->d_Pid > 0)
	    kill(reader->d_Pid, SIGQUIT);
	return;
    }
    close(desc->d_FdPend);
    desc->d_FdPend = -1;
    ++reader->d_Count;

    if ((dres.dr_Flags & DF_QUIET) == 0) {
	logit(LOG_INFO,
	    "connect to %s from %s%s%s%s%s (%s)", 
	    dres.dr_VServerDef->vs_Name,
	    (dres.dr_AuthUser[0] ? dres.dr_AuthUser : ""),
	    (dres.dr_AuthUser[0] ? "/" : ""),
	    (dres.dr_IdentUser[0] ? dres.dr_IdentUser : ""),
	    (dres.dr_IdentUser[0] ? "@" : ""),
	    dres.dr_Host,
	    NetAddrToSt(0, (struct sockaddr *)&desc->d_SaveRSin, 1, 1, 1)
	);
    }
}

void
ValidateTcpBufferSize(int *psize)
{
    if (*psize != 0 && *psize < 512) {
	logit(LOG_INFO, "TCP buffer size of %d too small, setting to 512", *psize);
	*psize = 512;
    }
    if (*psize > 8*256*1024) {
	logit(LOG_INFO, "TCP buffer size of %d too big, setting to 2048K", *psize);
	*psize = 8*256*1024;
    }
}

void
deleteDnsResHash(DnsRes *dres, ForkDesc *desc)
{
    if (dres == NULL) {
	int i;

	for (i = 0; i < DRHSIZE; ++i) {
	    DnsRes **pdr = &DnsResHash[i];
	    DnsRes *dr;

	    while ((dr = *pdr) != NULL) {
		if (dr->dr_ReaderPid == desc->d_Pid) {
		    logit(
			LOG_NOTICE,
			"killed from %s%s%s%s%s (%s)",
			(dr->dr_AuthUser[0] ? dr->dr_AuthUser : ""),
			(dr->dr_AuthUser[0] ? "/" : ""),
			(dr->dr_IdentUser[0] ? dr->dr_IdentUser : ""),
			(dr->dr_IdentUser[0] ? "@" : ""),
			dr->dr_Host,
			NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 1, 1)
		    );
		    *pdr = dr->dr_HNext;
		    zfree(&DnsResPool, dr, sizeof(DnsRes));
		} else {
		    pdr = &dr->dr_HNext;
		}
	    }
	}
    } else {
	int hi = shash(NetAddrToSt(0, (struct sockaddr *)&dres->dr_Addr, 1, 0, 0)) & DRHMASK;
	DnsRes **pdr = &DnsResHash[hi];
	DnsRes *dr;

	while ((dr = *pdr) != NULL) {
	    if (dr->dr_ReaderPid == desc->d_Pid &&
		memcmp(&dr->dr_Addr, &dres->dr_Addr, sizeof(dr->dr_Addr)) == 0
	    ) {
		if ((dr->dr_Flags & DF_QUIET) == 0) {
		    logit(
			LOG_INFO,
#ifdef	DREADER_CLIENT_TIMING
			"closed from %s%s%s%s%s (%s) groups=%u arts=%u bytes=%.0f posts=%d postbytes=%d postfail=%d vserv=%s secs=%d utime=%.3f stime=%.3f",
#else
			"closed from %s%s%s%s%s (%s) groups=%u arts=%u bytes=%.0f posts=%d postbytes=%d postfail=%d vserv=%s secs=%d",
#endif
			(dr->dr_AuthUser[0] ? dr->dr_AuthUser : ""),
			(dr->dr_AuthUser[0] ? "/" : ""),
			(dr->dr_IdentUser[0] ? dr->dr_IdentUser : ""),
			(dr->dr_IdentUser[0] ? "@" : ""),
			dr->dr_Host,
			NetAddrToSt(0, (struct sockaddr *)&dr->dr_Addr, 1, 1, 1),
			dres->dr_GrpCount,
			dres->dr_ArtCount,
			dres->dr_ByteCount,
			dres->dr_PostCount,
			dres->dr_PostBytes,
			dres->dr_PostFailCount,
			dres->dr_VServer,
#ifdef	DREADER_CLIENT_TIMING
			time(NULL) - dres->dr_TimeStart,
			desc->d_stime/ticksPerSecond,
			desc->d_utime/ticksPerSecond
#else
			time(NULL) - dres->dr_TimeStart
#endif
		    );
		}
		if (dr->dr_Flags & DF_FEEDONLY) {
		    TotalFeedArticles += dres->dr_PostCount;
		    TotalFeedBytes += dres->dr_PostBytes;
		    TotalFeedArtFail += dres->dr_PostFailCount;
		} else {
		    TotalClientGroups += dres->dr_GrpCount;
		    TotalClientArticles += dres->dr_ArtCount;
		    TotalClientArtBytes += dres->dr_ByteCount;
		    TotalClientPosts += dres->dr_PostCount;
		    TotalClientPostBytes += dres->dr_PostBytes;
		    TotalClientPostFail += dres->dr_PostFailCount;
		}
		*pdr = dr->dr_HNext;
		zfree(&DnsResPool, dr, sizeof(DnsRes));
		break;
	    } else {
		pdr = &dr->dr_HNext;
	    }
	}
    }
}

int
getReaderForkSlot(void)
{
    int i;

    for (i = 0; i < DOpts.ReaderForks + DOpts.ReaderFeedForks; ++i) {
	if (ReaderPids[i] == 0)
	    return(i);
    }
    return(0); /* XXX panic */
}

void
delReaderForkSlot(pid_t pid)
{
    int i;

    for (i = 0; i < DOpts.ReaderForks + DOpts.ReaderFeedForks; ++i) {
	if (ReaderPids[i] == pid)
	    ReaderPids[i] = 0;
    }
}

typedef struct vscount {
	Vserver *vserver;
	int count;
	struct vscount *next;
} vscount;

typedef struct rdcount {
	ReaderDef *reader;
	int count;
	struct vscount *vslist;
	struct rdcount *next;
} rdcount;


MemPool *StatsPool;

void
addVSlist(vscount **vslistp, DnsRes *dr)
{
    vscount *vslist = *vslistp;
    vscount *vs;
    vscount *pvs = NULL;

    if (vslist == NULL) {
	vslist = zalloc(&StatsPool, sizeof(vscount));
	vslist->vserver = dr->dr_VServerDef;
	vslist->count = 1;
	vslist->next = NULL;
	*vslistp = vslist;
    } else {
	for (vs = vslist; vs != NULL; pvs = vs, vs = vs->next) {
	    if (vs->vserver == dr->dr_VServerDef) {
		vs->count++;
		break;
	    }
	}
	if (vs == NULL) {
	    vs = zalloc(&StatsPool, sizeof(vscount));
	    vs->vserver = dr->dr_VServerDef;
	    vs->count = 1;
	    vs->next = NULL;
	    pvs->next = vs;
	}
    }
}

void
getVStats(FILE *fo)
{
    int hmi;
    DnsRes *dr;
    int vcount = 0;
    int rcount = 0;
    rdcount *rdlist = NULL;
    vscount *vslist = NULL;
    vscount *vs;
    rdcount *rd;
    vscount *pvs = NULL;
    rdcount *prd = NULL;

    for (hmi=0; hmi < DRHSIZE; hmi++) 
	for (dr = DnsResHash[hmi]; dr; dr = dr->dr_HNext) {
	    SetAuthDetails(dr, dr->dr_ReaderName);
	    if (rdlist == NULL) {
		rdlist = zalloc(&StatsPool, sizeof(rdcount));
		rdlist->reader = dr->dr_ReaderDef;
		rdlist->count = 1;
		rdlist->next = NULL;
		rdlist->vslist = NULL;
		addVSlist(&rdlist->vslist, dr);
	    } else {
		for (rd = rdlist; rd != NULL; prd = rd, rd = rd->next) {
		    if (rd->reader == dr->dr_ReaderDef) {
			addVSlist(&rd->vslist, dr);
			rd->count++;
			break;
		    }
		}
		if (rd == NULL) {
		    rd = zalloc(&StatsPool, sizeof(rdcount));
		    rd->reader = dr->dr_ReaderDef;
		    rd->count = 1;
		    rd->next = NULL;
		    rd->vslist = NULL;
		    addVSlist(&rd->vslist, dr);
		    prd->next = rd;
		}
	    }
	    addVSlist(&vslist, dr);
	}
    rcount = 0;
    for (rd = rdlist; rd != NULL; prd = rd, rd = rd->next) {
	for (vs = rd->vslist; vs != NULL; pvs = vs, vs = vs->next)
	    fprintf(fo, "reader %s=%d vs %s=%d\n",
			rd->reader->rd_Name, rd->count,
			vs->vserver->vs_Name, vs->count);
	rcount++;
    }
    fprintf(fo, "readergroups in use = %d\n", rcount);
    vcount = 0;
    for (vs = vslist; vs != NULL; pvs = vs, vs = vs->next) {
	fprintf(fo, "vserver %s=%d\n", vs->vserver->vs_Name, vs->count);
	vcount++;
    }
    fprintf(fo, "vservers in use = %d\n", vcount);
    freePool(&StatsPool);
}

void
getStats(FILE *fo, int raw)
{
    fprintf(fo, "UPTIME %d\n", (int)(time(NULL) - TimeStart));
    fprintf(fo, "SERVER Connect=%d Failed=%d Dns=%d/%d Act=%d/%d\n", 
		ConnectCount, FailCount,
		NumPending, DOpts.ReaderDns, 
		NumActive, MaxConnects
    );
    if (raw) {
	fprintf(fo, "CLIENT Groups=%.0f Articles=%.0f Bytes=%.0f\n", 
		TotalClientGroups, TotalClientArticles, TotalClientArtBytes
	);
	fprintf(fo, "POST Posts=%.0f PostBytes=%.0f PostsFailed=%.0f\n", 
		TotalClientPosts, TotalClientPostBytes, TotalClientPostFail
	);
	fprintf(fo, "SPOOL Articles=%.0f Bytes=%.0f\n", 
		TotalSpoolArticles, TotalSpoolBytes
	);
	fprintf(fo, "FEED Articles=%.0f Bytes=%.0f ArtFail=%.0f\n", 
		TotalFeedArticles, TotalFeedBytes,
		TotalFeedArtFail
	);
    } else {
	fprintf(fo, "CLIENT Groups=%.0f Articles=%.0f Bytes=%s\n", 
		TotalClientGroups, TotalClientArticles,
		ftos(TotalClientArtBytes)
	);
	fprintf(fo, "POST Posts=%.0f PostBytes=%s PostsFailed=%.0f\n", 
		TotalClientPosts, ftos(TotalClientPostBytes),
		TotalClientPostFail
	);
	fprintf(fo, "SPOOL Articles=%.0f Bytes=%s\n", 
		TotalSpoolArticles, ftos(TotalSpoolBytes)
	);
	fprintf(fo, "FEED Articles=%.0f Bytes=%s ArtFail=%.0f\n", 
		TotalFeedArticles, ftos(TotalFeedBytes),
		TotalFeedArtFail
	);
    }
}

#ifdef	READER_BAN_LISTS

void
SetBanList(char *ban)
{
    BanInfo *bi = NULL;

    if (ban == NULL)
	return;
    while (*ban) {
	switch (*ban) {
	    case ' ':
	    case '\t':
		ban++;
		break;
	    case 'U':
		bi = &UserBan;
		ban++;
		break;
	    case 'H':
		bi = &HostBan;
		ban++;
		break;
	    case 'G':
		bi = &GroupBan;
		ban++;
		break;
	    case 'L':
		bi = &GlobalBan;
		ban++;
		break;
	    case 's':
		bi->BanHashSize = atoi(++ban);
		while (isdigit((int)*ban))
		    ban++;
		break;
	    case 'l':
		bi->BanLinkSize = atoi(++ban);
		while (isdigit((int)*ban))
		    ban++;
		break;
	    case 'c':
		bi->BanCount = atoi(++ban);
		while (isdigit((int)*ban))
		    ban++;
		break;
	    case 't':
		bi->BanTime = atoi(++ban);
		while (isdigit((int)*ban))
		    ban++;
		break;
	    default:
		logit(LOG_ERR, "Unknown readerban option '%s'", ban);
		return;
	}
    }
}

int
Banned(char *ip, time_t t)
{
    if (UserBan.BanCount > 0 &&
		IPListCheck(UserBan.BanList, ip, 1, t + UserBan.BanTime, t, UserBan.BanHashSize) == 0)
	return(1);
    if (HostBan.BanCount > 0 &&
		IPListCheck(HostBan.BanList, ip, 1, t + HostBan.BanTime, t, HostBan.BanHashSize) == 0)
	return(1);
    if (GroupBan.BanCount > 0 &&
		IPListCheck(GroupBan.BanList, ip, 1, t + GroupBan.BanTime, t, GroupBan.BanHashSize) == 0)
	return(1);
    if (GlobalBan.BanCount > 0 &&
		IPListCheck(GlobalBan.BanList, ip, 1, t + GlobalBan.BanTime, t, GlobalBan.BanHashSize) == 0)
	return(1);
    return(0);
}

void
DumpBannedLists(FILE *fo)
{
    if (UserBan.BanCount > 0) {
	fprintf(fo, "User bans:\n");
	IPListDump(fo, UserBan.BanList, UserBan.BanHashSize);
    }
    if (HostBan.BanCount > 0) {
	fprintf(fo, "Host bans:\n");
	IPListDump(fo, HostBan.BanList, HostBan.BanHashSize);
    }
    if (GroupBan.BanCount > 0) {
	fprintf(fo, "Group bans:\n");
	IPListDump(fo, GroupBan.BanList, GroupBan.BanHashSize);
    }
    if (GlobalBan.BanCount > 0) {
	fprintf(fo, "Global bans:\n");
	IPListDump(fo, GlobalBan.BanList, GlobalBan.BanHashSize);
    }
}

void
DumpBannedConfigs(FILE *fo)
{
    if (UserBan.BanCount > 0)
	fprintf(fo, "user  : count=%10d  time=%ld\n", UserBan.BanCount,
							UserBan.BanTime);
    if (HostBan.BanCount > 0)
	fprintf(fo, "host  : count=%10d  time=%ld\n", HostBan.BanCount,
							HostBan.BanTime);
    if (GroupBan.BanCount > 0)
	fprintf(fo, "group : count=%10d  time=%ld\n", GroupBan.BanCount,
							GroupBan.BanTime);
    if (GlobalBan.BanCount > 0)
	fprintf(fo, "global: count=%10d  time=%ld\n", GlobalBan.BanCount,
							GlobalBan.BanTime);
}

#endif	/* READER_BAN_LISTS */
