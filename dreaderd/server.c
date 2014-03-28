
/*
 * SERVER.C	- /news/dserver.hosts management
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void CheckServerConfig(time_t t, int force);
Prototype void LogServerInfo(Connection *conn, int fd);
Prototype void NNArticleRetrieveByMessageId(Connection *conn, const char *msgid, const char *msgopts, int grouphint, int TimeRcvd, int grpIter, artno_t endNo);
Prototype void NNServerIdle(Connection *conn);
Prototype void NNServerTerminate(Connection *conn);
Prototype void NNFinishSReq(Connection *conn, const char *ctl, int requeue);
Prototype void NNServerRequest(Connection *conn, const char *grp, const char *msgid, const int maxage, int req, int TimeRcvd, int grpIter, artno_t endNo);

Prototype int   ServersTerminated;
Prototype int	NReadServers;
Prototype int	NReadServAct;
Prototype int	NWriteServers;
Prototype int	NWriteServAct;

void NNServerStart(Connection *conn);
void QueueServerRequests(void);
int queueServerRequest(ServReq *sreq, int type, int maxq);
void queueDesc(ServReq *sreq, ForkDesc *desc);
int requeueServerRequest(Connection *conn, ServReq *sreq, int type, int maxq);
int AddServer(char *bindinfo, int txbufsize, int rxbufsize, const char *host, int type, int port, int flags, int pri, char *groups, const char *localspool, char *hashfeed, int cache, int cachemin, int cachemax, int cacheabletime, int retention, char *groupdef, double rratio, double cratio, char *login, char *password, int timeout, int timewarn);
void NNServerPrime1(Connection *conn);
void NNServerPrime2(Connection *conn);
void NNServerPrime3(Connection *conn);
void NNServerPrime4(Connection *conn);
void NNServerPrimeAuth1(Connection *conn);
void NNServerPrimeAuth2(Connection *conn);
void NNServerConnect(Connection *conn);

ServReq	*SReadBase;
ServReq **PSRead = &SReadBase;
ServReq *SWriteBase;
ServReq **PSWrite = &SWriteBase;
int	NReadServers;
int	NReadServAct;
int	NWriteServers;
int	NWriteServAct;
int	ServersTerminated;

/*
 * CheckServerConfig() - determine if configuration file has changed and
 *			 resynchronize with servers list.
 *
 *			 NOTE: a server cannot be destroyed until its current
 *			 pending request completes, but no new requests will
 *			 be queued to it during that time.
 */

void
setServerMaybeCloseFlag(ForkDesc *desc)
{
    Connection *conn = desc->d_Data;
    conn->co_Flags |= COF_MAYCLOSESRV;
    if (conn->co_Func == NNServerIdle)		/* wakeup idle server */
	FD_SET(desc->d_Fd, &RFds);
}

void
setServerClosedRemovals(ForkDesc *desc)
{
    Connection *conn;

    if ((conn = desc->d_Data) != NULL) {
	if (conn->co_Flags & COF_MAYCLOSESRV) {
	    NNServerTerminate(conn);
	}
    }
}

void 
CheckServerConfig(time_t t, int force)
{
    static struct stat St = { 0 };
    struct stat st;
    const char *path = PatLibExpand(DServerHostsPat);

    /*
     * Assuming we can stat the file, if we haven't checked the server config
     * before or the server config time is different from when we last checked
     * AND the server config time is not now (so we don't read the file while
     * someone is writing it), then read the configuration file (again).
     * I've also got some reverse-time-index protection in there.
     *
     * NOTE: path only valid until next Pat*Expand() call, so be careful
     * with it's use.
     */

    if (force || (stat(path, &st) == 0 && 
	(St.st_mode == 0 || st.st_mtime != St.st_mtime) && 
	((long)(t - st.st_mtime) > 2 || (long)(t - st.st_mtime) < 0))
    ) {
	FILE *fi;

	if (force)
	    stat(path, &st);
	memcpy(&St, &st, sizeof(St));

	if ((fi = fopen(path, "r")) != NULL) {
	    char buf[PATH_MAX];

	    ServersTerminated = 0;

	    ScanThreads(THREAD_SPOOL, setServerMaybeCloseFlag);
	    ScanThreads(THREAD_POST, setServerMaybeCloseFlag);

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		char *host = strtok(buf, " \t\n");
		char *flags = "";
		char c;
		int addAsServer = 0;
		int addAsPoster = 0;
		int port = 119;
		int nflags = 0;
		int npri = 0;
		char *option;
		char *bindinfo = NULL;
		char *groups = NULL;
		char *localspool = NULL;
		char *hash = NULL;
		int txbufsize = 0;
		int rxbufsize = 0;
		char *login = NULL;
		char *password = NULL;
		int cache = CACHE_OFF;
		int cachemin = 0;
		int cachemax = 0;
		int cacheabletime = 0;
		int retention = 0;
		char *groupdef = NULL;
		double rratio = 4;
		double cratio = -1;
		int timeout = 0;
		int timewarn = 0;

		if (DOpts.ReaderCacheMode)
		    cache = CACHE_ON;
		if (host == NULL || host[0] == '#')
		    continue;
		if ((flags = strtok(NULL, " \t\n")) == NULL)
		    flags = "";
		while ((c = *flags) != 0) {
		    ForkDesc *desc = NULL;

		    switch(c) {
		    case 'A':
			nflags |= COF_MAXAGE;
			break;
		    case 'p':	/* port			*/
			port = strtol(flags + 1, NULL, 0);
			if (port == 0)
			    port = 119;
			break;
		    case 's':	/* spool		*/
			npri = strtol(flags + 1, NULL, 10);
			if ((desc = FindThreadId(THREAD_SPOOL, host)) != NULL) {
			    Connection *conn = desc->d_Data;
			    conn->co_Flags &= ~COF_MAYCLOSESRV;
			    desc->d_Pri = npri;
			} else {
			    addAsServer = 1;
			}
			break;
		    case 'M':
			nflags |= COF_MODEREADER;
			break;
		    case 'R':
			nflags |= COF_READONLY;
			break;
		    case 'o':	/* outgoing (post)	*/
			npri = strtol(flags + 1, NULL, 10);
			if ((desc = FindThreadId(THREAD_POST, host)) != NULL) {
			    Connection *conn = desc->d_Data;
			    conn->co_Flags &= ~COF_MAYCLOSESRV;
			    desc->d_Pri = npri;
			} else {
			    addAsPoster = 1;
			}
			break;
		    case 't':   /* slow response timer  */
			timewarn = strtol(flags + 1, NULL, 10);
			if (timewarn == 0)
			    timewarn = 10;
			break;
		    case 'T':   /* timeout              */
			timeout = strtol(flags + 1, NULL, 10);
			if (timeout == 0)
			    timeout = 180;
			break;
		    default:
			/*
			 * ignore unknown flag or number
			 */
			break;
		    }
		    ++flags;
		}
		while ((option = strtok(NULL, " \t\n")) != NULL) {
		    if (strncmp(option, "bind=", 5) == 0) {
			bindinfo = option + 5;
		    } else if (strncmp(option, "txbufsize=", 10) == 0) {
			txbufsize = strtol(option + 10, NULL, 0);
			ValidateTcpBufferSize(&txbufsize);
		    } else if (strncmp(option, "rxbufsize=", 10) == 0) {
			rxbufsize = strtol(option + 10, NULL, 0);
			ValidateTcpBufferSize(&rxbufsize);
		    } else if (strncmp(option, "groups=", 7) == 0) {
			groups = option + 7;
		    } else if (strncmp(option, "localspool=", 11) == 0) {
			localspool = option + 11;
		    } else if (strncmp(option, "hash=", 5) == 0) {
			hash = option + 5;
		    } else if (strncmp(option, "login=", 6) == 0) {
			login = option + 6;
			nflags |= COF_LOGIN;
		    } else if (strncmp(option, "password=", 9) == 0) {
			password = option + 9;
		    } else if (strncmp(option, "cache=", 6) == 0) {
			if (strncmp(option+6,"off", 3) == 0) {
			    cache = CACHE_OFF;
			} else if (strncmp(option+6,"on", 2) == 0) {
			    if (DOpts.ReaderCacheMode) {
				cache = CACHE_ON;
			    } else {
				logit(LOG_ERR, "Ignoring cache option as diablo.conf cache option is disabled");
			    }
			} else if (strncmp(option+6,"lazy", 4) == 0) {
			    if (DOpts.ReaderCacheMode) {
				cache = CACHE_LAZY;
			    } else {
				logit(LOG_ERR, "Ignoring cache option as diablo.conf cache option is disabled");
			    }
			} else if (strncmp(option+6,"scoring(", 8) == 0) {
			    char *p;
			    option += 14;
			    if (strchr(option, ')') == NULL) {
				logit(LOG_ERR, "Syntax error on scoring option");
			    } else {
				if (*option == ',') {
				    rratio = -1;
				    cratio = strtod(++option, NULL);
				} else if ((p = strchr(option, ',')) != NULL) {
				    rratio = strtod(option, NULL);
				    cratio = strtod(++p, NULL);
				} else {
				    rratio = strtod(option, NULL);
				    cratio = -1;
				}

				if (DOpts.ReaderCacheMode) {
				    cache = CACHE_SCOREBOARD;
				    OpenCacheHits();
				} else {
				    logit(LOG_ERR, "Ignoring cache option as diablo.conf cache option is disabled");
				}
			    }
			} else if (strncmp(option+6,"scoring", 7) == 0) {
			    if (DOpts.ReaderCacheMode) {
				cache = CACHE_SCOREBOARD;
				OpenCacheHits();
			    } else {
				logit(LOG_ERR, "Ignoring cache option as diablo.conf cache option is disabled");
			    }
			} else {
			    logit(LOG_ERR, "Unknown option '%s' - ignoring", option);
			}
		    } else if (strncmp(option, "cachemin=", 9) == 0) {
			cachemin = strtol(option + 9, NULL, 0);
		    } else if (strncmp(option, "cachemax=", 9) == 0) {
			cachemax = strtol(option + 9, NULL, 0);
		    } else if (strncmp(option, "cacheabletime=", 14) == 0) {
			cacheabletime = strtol(option + 14, NULL, 0);
		    } else if (strncmp(option, "retention=", 10) == 0) {
			retention = strtol(option + 10, NULL, 0);
		    } else if (strncmp(option, "groupdef=", 9) == 0) {
			groupdef = option+9;
		    } else {
			logit(LOG_ERR, "Unknown option '%s' - ignoring", option);
		    }
		}
		if (addAsServer) {
		    if (AddServer(bindinfo, txbufsize, rxbufsize, host,
				THREAD_SPOOL, port, nflags, npri,
				groups, localspool, hash, cache,
				cachemin, cachemax, cacheabletime,
				retention, groupdef,
				rratio, cratio, login, password,
				timeout, timewarn) == 0) {
			++NReadServers;
			++NReadServAct;
		    } else {
			++ServersTerminated;
		    }
		}
		if (addAsPoster) {
		    if (AddServer(bindinfo, txbufsize, rxbufsize, host,
				THREAD_POST, port, nflags, npri,
				groups, localspool, hash, cache,
				cachemin, cachemax, cacheabletime,
				retention, groupdef,
				rratio, cratio, login, password,
				timeout, timewarn) == 0) {
			++NWriteServers;
			++NWriteServAct;
		    } else {
			++ServersTerminated;
		    }
		}
	    }
	    fclose(fi);
	    ScanThreads(THREAD_SPOOL, setServerClosedRemovals);
	    ScanThreads(THREAD_POST, setServerClosedRemovals);
	}
    }
}

/*
 * Log some spool server stats on an hourly basis
 */
void
LogServerInfo(Connection *conn, int fd)
{
    time_t now = time(NULL);

    if (!conn->co_LastServerLog)
	conn->co_LastServerLog = now;
    if (!conn->co_ServerByteCount ||
			conn->co_LastServerLog + 300 > now)
	return;
    logit(LOG_INFO, "info server %s articles=%ld bytes=%ld req=%ld notfound=%ld err=%ld",
		(conn->co_Desc->d_Id ? conn->co_Desc->d_Id : "UNKNOWN"),
		conn->co_ServerArticleCount,
		conn->co_ServerByteCount,
		conn->co_ServerArticleRequestedCount,
		conn->co_ServerArticleNotFoundErrorCount,
		conn->co_ServerArticleMiscErrorCount
    );
    {
	DnsRes dr;
	dr.dr_ResultFlags = DR_SERVER_STATS;
	dr.dr_ArtCount = conn->co_ServerArticleCount;
	dr.dr_ByteCount = conn->co_ServerByteCount;
	SendMsg(fd, -1, &dr);
    }
    conn->co_ServerByteCount = 0;
    conn->co_ServerArticleCount = 0;
    conn->co_ServerArticleRequestedCount = 0;
    conn->co_ServerArticleNotFoundErrorCount = 0;
    conn->co_ServerArticleMiscErrorCount = 0;
    conn->co_LastServerLog = now;
}

GroupList *
makeGroupList(Connection *conn, char *groups)
{
    GroupList *gr = NULL;
    GroupList *grStart = NULL;
    char *p;

    if (groups == NULL)
	return(NULL);
    for (p = strtok(groups, ","); p != NULL; p = strtok(NULL, ",")) {
	GroupList *g = zalloc(&conn->co_MemPool, sizeof(GroupList));
	g->group = zallocStr(&conn->co_MemPool, p);
	g->next = NULL;
	if (gr != NULL)
	    gr->next = g;
	gr = g;
	if (grStart == NULL)
	    grStart = gr;
    }
    return(grStart);
}

int
AddServer(char *bindinfo, int txbufsize, int rxbufsize, const char *host, int type, int port, int flags, int pri, char *groups, const char *localspool, char *hashfeed, int cache, int cachemin, int cachemax, int cacheabletime, int retention, char* groupdef, double rratio, double cratio, char *login, char *password, int timeout, int timewarn)
{
    ForkDesc *desc;
    int fd;
    struct sockaddr_in sin;
    Connection *conn;

    /*
     * connect() to the host (use asynchronous connect())
     */

    bzero(&sin, sizeof(sin));
    {
	struct hostent *he = gethostbyname(host);
        if (he != NULL) {
	    sin.sin_family = he->h_addrtype;
	    memmove(&sin.sin_addr, he->h_addr, he->h_length);
	} else if (strtol(host, NULL, 0) != 0) {
	    sin.sin_family = AF_INET;
	    sin.sin_addr.s_addr = inet_addr(host);
	} else {
	    logit(LOG_ERR, "hostname lookup failure: %s\n", host);
	    return(-1);
	}
    }
    sin.sin_port = htons(port);

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	logit(LOG_ERR, "socket() call failed on host %s\n", host);
	return(-1);
    }

    if (bindinfo != NULL) {
	struct hostent *he;
	struct sockaddr_in lsin;
	bzero(&lsin, sizeof(lsin));
	if ((he = gethostbyname(bindinfo)) != NULL) {
	    lsin.sin_addr = *(struct in_addr *)he->h_addr;
	} else {
	    lsin.sin_addr.s_addr = inet_addr(bindinfo);
	    if (lsin.sin_addr.s_addr == INADDR_NONE) {
		logit(LOG_ERR, "Unknown host for bindhost option: %s\n",
								bindinfo);
		return(-1);
	    }
	}
	lsin.sin_family = AF_INET;
	lsin.sin_port = 0;
	if (bind(fd, (struct sockaddr *) &lsin, sizeof(lsin)) < 0) {
	    logit(LOG_ERR, "failed to bind source address %s (%s)",
						bindinfo, strerror(errno));
	    return(-1);
	}

    }
    {
	int on = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on)) < 0) {
	    syslog(LOG_ERR, "setsockopt SO_KEEPALIVE on srv fd %d: %m", fd);
	}
	if (txbufsize > 0)
	    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&txbufsize, sizeof(int)) < 0) {
		syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on srv fd %d: %m", 
							txbufsize, fd);
	    }
	if (rxbufsize > 0)
	    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)&rxbufsize, sizeof(int)) < 0) {
		syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on srv fd %d: %m", 
							rxbufsize, fd);
	    }
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);	/* asynchronous connect() */
    errno = 0;
    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	if (errno != EINPROGRESS) {
	    close(fd);
	    logit(LOG_ERR, "connect() call failed on host %s:%d: %s\n",
						host, port, strerror(errno));
	    return(-1);
	}
    }

    /*
     * add thread.  Preset d_Count to LIMSIZE to prevent the server
     * from being allocated for client requests until we know
     * we have a good connection.
     */

    desc = AddThread(host, fd, -1, type, -1, pri);
    FD_SET(desc->d_Fd, &RFds);
    conn = InitConnection(desc, NULL);
    if (flags & COF_LOGIN) {
	if (password) {
	    snprintf(conn->co_Auth.dr_AuthUser, sizeof(conn->co_Auth.dr_AuthUser), "%s", login);
	    snprintf(conn->co_Auth.dr_AuthPass, sizeof(conn->co_Auth.dr_AuthPass), "%s", password);
	} else {
	    logit(LOG_ERR, "no password specified with login: %s\n", login);
	    flags &= ~COF_LOGIN;
	}
    }
    conn->co_Flags |= flags;
    conn->co_Flags |= COF_INPROGRESS | COF_ININIT;
    desc->d_Count = THREAD_LIMSIZE;
    conn->co_Auth.dr_ReaderDef = NULL;
    conn->co_ListCacheGroups = makeGroupList(conn, groups);
    conn->co_Retention = retention;
    conn->co_GroupDef = makeGroupList(conn, groupdef);
    conn->co_RequestHash = DiabHashFeedParse(&conn->co_MemPool, hashfeed);
    if (hashfeed && conn->co_RequestHash == NULL)
      logit(LOG_ERR, "error parsing hash=%s\n", hashfeed);
    if (localspool != NULL)
	desc->d_LocalSpool = zallocStr(&conn->co_MemPool, localspool);
    desc->d_Cache = cache;
    desc->d_CacheMin = cachemin;
    desc->d_CacheMax = cachemax;
    desc->d_CacheableTime = cacheabletime;
    desc->d_ReadNewRatio = rratio;
    desc->d_CacheReadRatio = cratio;
    desc->d_Timeout = timeout;
    desc->d_Timewarn = timewarn;

    NNServerConnect(conn);
    if (DebugOpt)
	printf("Added Server (type=%d fd=%d pid=%d)\n", type, fd, (int)getpid());
    return(0);
}

/*
 * QUEUESERVERREADREQUEST() - move requests to the appropriate server.
 *
 *	New article fetch and post requests are placed on the SRead/SWrite
 *	lists.  This function moves as many of those requests as possible to
 *	actual spool & post server queues.
 *
 *	Even though a server can only handle one request at a time, we 
 *	allow up to N requests (THREAD_QSIZE/PSIZE) to be queued to an
 *	actual server in order to judge load.  If the load exceeds the
 *	queue limit, FindLeastUsedThread will start queueing to higher priority
 *	servers.
 */

void
QueueServerRequests(void)
{
    ServReq	*sreq;

    while ((sreq = SReadBase) != NULL) {
	if ((SReadBase = sreq->sr_Next) == NULL)
	    PSRead = &SReadBase;
	sreq->sr_Next = NULL;
	if (queueServerRequest(sreq, THREAD_SPOOL, THREAD_QSIZE) < 0) {
	    *PSRead = sreq;
	    PSRead = &sreq->sr_Next;
	    break;
	}
    }
    while ((sreq = SWriteBase) != NULL) {
	if ((SWriteBase = sreq->sr_Next) == NULL)
	    PSWrite = &SWriteBase;
	sreq->sr_Next = NULL;
	if (queueServerRequest(sreq, THREAD_POST, THREAD_PSIZE) < 0) {
	    *PSWrite = sreq;
	    PSWrite = &sreq->sr_Next;
	    break;
	}
    }
}

int
cbQueueReqFindThread(void *cbData, void *data)
{
    int result;
    ServReq *sreq = cbData;
    Connection *conn = data;

    if (conn->co_RequestHash &&
	    !HM_CheckForMatch(conn->co_RequestHash, sreq->sr_MsgId, HMOPER_MATCHONE))
	return(0);

    if (conn->co_Retention > 0 && sreq->sr_TimeRcvd &&
	    conn->co_Retention < (time(NULL) - sreq->sr_TimeRcvd))
	return(0);

    if (conn->co_GroupDef && sreq->sr_CConn->co_Auth.dr_GroupDef->gr_Name &&
	    !GroupFindWild(sreq->sr_CConn->co_Auth.dr_GroupDef->gr_Name, conn->co_GroupDef))
	return(0);

    if (sreq->sr_Group == NULL || conn->co_ListCacheGroups == NULL)
	result = 1;
    else
	result = GroupFindWild(sreq->sr_Group, conn->co_ListCacheGroups);
    return(result);
}

/*
 * queueServerRequest() - find best server to queue request to, return 0
 *			  on success, -1 if the request could not be queued.
 *
 *			  On success, the request will have been properly
 *			  queued.
 */

int
queueServerRequest(ServReq *sreq, int type, int maxq)
{
    ForkDesc *desc;
    static int Randex1 = -1;

    desc = FindLeastUsedThread(type, maxq, 0, &Randex1, -1,
						cbQueueReqFindThread, sreq);

    if (desc != NULL) {
	sreq->sr_NoPass = desc->d_Fd;
	queueDesc(sreq, desc);
	return(0);
    } 
    logit(LOG_ERR, "Unable to find any spools (or spool threads too busy) for request %s ", sreq->sr_MsgId);
    return(-1);
}

void
queueDesc(ServReq *sreq, ForkDesc *desc)
{
    Connection *conn = desc->d_Data;
    ServReq **psreq = &conn->co_SReq;

    while (*psreq)
	psreq = &(*psreq)->sr_Next;
    *psreq = sreq;

    sreq->sr_SConn = conn;

    ++desc->d_Count;
    if (desc->d_Type == THREAD_SPOOL)
	++NReadServAct;
    else
	++NWriteServAct;

    sreq->sr_Rolodex = desc->d_Fd;

    if (DebugOpt)
	printf("Request %s queued to fd %d\n", sreq->sr_MsgId, desc->d_Fd);

    /*
     * If server was idle, kick it.
     */
    if (conn->co_Func == NNServerIdle)
	NNServerIdle(conn);
}

/*
 * requeueServerRequest() - if request failed with previous server,
 *			    requeue for next server.
 *
 *	This function requeues a request to another server given the previous
 *	server.  It will attempt to send the request to all servers at the
 *	current priority and then will go to the next priority level, and
 *	so on, return -1 if the request could not be queued.
 *
 *	When requeueing a request, queue limits are relaxed in order to
 *	ensure that the request does not skip to higher priority queues
 *	due to high load.
 */

int
requeueServerRequest(Connection *conn, ServReq *sreq, int type, int maxq)
{
    static int Randex2;
    ForkDesc *desc;

    /*
     * Find the next server
     */

    desc = FindLeastUsedThread(
	type,
	maxq * 2,
	conn->co_Desc->d_Pri,
	&sreq->sr_Rolodex, 
	sreq->sr_NoPass,
	cbQueueReqFindThread,
	sreq
    );

    if (desc == NULL) {
	desc = FindLeastUsedThread(
	    type, 
	    maxq * 2, 
	    conn->co_Desc->d_Pri + 1, 
	    &Randex2, 
	    -1,
	    cbQueueReqFindThread,
	    sreq
	);
	if (desc) {
	    sreq->sr_NoPass = desc->d_Fd;
	    if (DebugOpt)
		printf("Requeue %s to nextpri %d\n", sreq->sr_MsgId, desc->d_Pri);
	} else {
	    if (DebugOpt)
		printf("Requeue %s failed\n", sreq->sr_MsgId);
	}
    } else {
	if (DebugOpt)
	    printf("Requeue %s to priority %d\n", sreq->sr_MsgId, desc->d_Pri);
    }
    if (desc) {
	queueDesc(sreq, desc);
	return(0);
    } 
    return(-1);
}

/*
 * FreeSReq()
 */

void
FreeSReq(ServReq *sreq)
{

    /*
     * If cache file write in progress, abort it.  allow the
     * fclose() to release the lock after the truncation.
     *
     * We NULL the FILE * out even though we free the structure
     * so potential memory corruption doesn't mess with random 
     * (future) files.
     */
    if (sreq->sr_Cache != NULL) {
	fflush(sreq->sr_Cache);
	AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
	fclose(sreq->sr_Cache);
	sreq->sr_Cache = NULL;
    }

    zfreeStr(&SysMemPool, &sreq->sr_Group);
    zfreeStr(&SysMemPool, &sreq->sr_MsgId);

    zfree(&SysMemPool, sreq, sizeof(ServReq));
}

/*
 * NNServerRequest()
 *
 *	NOTE: don't get confused by co_SReq, it serves two functions.  It
 *	placeholds a single request from a client in client Connection 
 *	structures, which this call handles, and placeholds MULTIPLE client
 *	requests in server Connection structures.
 */

void
NNServerRequest(Connection *conn, const char *grp, const char *msgid, const int maxage, int req, int TimeRcvd, int grpIter, artno_t endNo)
{
    ServReq *sreq = zalloc(&SysMemPool, sizeof(ServReq));

    sreq->sr_CConn = conn;
    sreq->sr_SConn = NULL;	/* for clarity: not assigned to server yet */
    sreq->sr_Time = time(NULL);
    sreq->sr_Group = grp ? zallocStr(&SysMemPool, grp) : NULL;
    sreq->sr_MsgId = msgid ? zallocStr(&SysMemPool, msgid) : NULL;
    sreq->sr_MaxAge = maxage;
    sreq->sr_Cache = NULL;
    sreq->sr_TimeRcvd = TimeRcvd;
    sreq->sr_GrpIter = grpIter;
    sreq->sr_endNo = endNo;

    conn->co_SReq = sreq;	/* client has active sreq		*/

    FD_CLR(conn->co_Desc->d_Fd, &RFds);

    if (req == SREQ_RETRIEVE) {
	*PSRead = sreq;
	PSRead = &sreq->sr_Next;
    } else if (req == SREQ_POST) {
	*PSWrite = sreq;
	PSWrite = &sreq->sr_Next;
    }
    QueueServerRequests();
}

/*
 * NNFinishSReq() - finish up an SReq, but requeue to a new server if requested
 *		    (i.e. article not found on old server).
 */

void
NNFinishSReq(Connection *conn, const char *ctl, int requeue)
{
    ServReq *sreq;
    time_t now = time(NULL), took;

    if ((sreq = conn->co_SReq)) {
	/*
	 * took a long time?  note it
	 */

	took = now - sreq->sr_Time;
	if (conn->co_Desc->d_Timewarn && (took > conn->co_Desc->d_Timewarn)) {
	    logit(LOG_ERR, "server %s took %d second%s to answer request %s", conn->co_Desc->d_Id, took, took == 1 ? "" : "s", sreq->sr_MsgId);
	}

	/*
	 * dequeue request from server side
	 */
	conn->co_SReq = sreq->sr_Next;
	sreq->sr_Next = NULL;

	if (conn->co_Desc) {
	    --conn->co_Desc->d_Count;
	    if (conn->co_Desc->d_Type == THREAD_POST)
		--NWriteServAct;
	    else
		--NReadServAct;
	}

	conn->co_Flags &= ~COF_INPROGRESS;

	/*
	 * if requeue requested, attempt to requeue.  requeueServerRequest
	 * returns 0 on success, -1 on failure, so we have to reverse the 
	 * sense of the return value.
	 */

	if (requeue) {
	    if (conn->co_Desc->d_Type == THREAD_POST)
		requeue= requeueServerRequest(conn, sreq, THREAD_POST, THREAD_PSIZE);
	    else
		requeue= requeueServerRequest(conn, sreq, THREAD_SPOOL,THREAD_QSIZE);
	    requeue = !requeue;
	}

	/*
	 * If no requeue, closeout the request.
	 */

	if (requeue == 0) {
	    /*
	     * can be NULL if client was terminated
	     */
	    if (sreq->sr_CConn) {
		if (ctl) {
		    if (ctl[1] == '4' || ctl[0] == '4') {
			/*
			 * x4x codes are POST results of some sort -
			 * this result only appears here via NNPostResponseX.
			 * we probably want to log this response to the
			 * client as it may be an informative error message
			 * unlike the average spool server transaction
			 * which we probably don't care about.
			 *
			 * 4xx codes may be spool failures - these should
			 * also be of interest
			 */
		        MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_TMBuf, "%s", ctl);
		    } else {
		        MBWrite(&sreq->sr_CConn->co_TMBuf, ctl, strlen(ctl));
		    }
		}
		/*
		 * set FCounter to 1 to prevent further recursion, which might
		 * feed back and screw up the state machine for this
		 * connection.
		 */
		sreq->sr_CConn->co_FCounter = 1;
		sreq->sr_CConn->co_SReq = NULL;
		NNCommand(sreq->sr_CConn);
	    }
	    FreeSReq(sreq);
	}
    }

    /*
     * XXX what if NNCommand does something which hits a server which
     * kills the server ?  boom, conn will bad illegal. XXX
     */

    NNServerIdle(conn);
}

/*
 * we have to send a garbage command to prevent INN's nnrpd from timing
 * out in 60 seconds upon initial connect.
 */

void
NNServerPrime1(Connection *conn)
{
    if (conn->co_Flags & COF_LOGIN) {
	MBPrintf(&conn->co_TMBuf, "authinfo user %s\r\n", conn->co_Auth.dr_AuthUser);
	NNServerPrimeAuth1(conn);
    } else if (conn->co_Flags & COF_MODEREADER) {
	MBPrintf(&conn->co_TMBuf, "mode reader\r\n");
	NNServerPrime2(conn);
    } else if (conn->co_Flags & COF_READONLY) {
	MBPrintf(&conn->co_TMBuf, "mode readonly\r\n");
	NNServerPrime4(conn);
    } else {
	MBPrintf(&conn->co_TMBuf, "mode thisbetterfail\r\n");
	NNServerPrime3(conn);
    }
}

void
NNServerPrime2(Connection *conn)
{
    int len;
    char *ptr;

    conn->co_Func = NNServerPrime2;
    conn->co_State = "prime2";

    if ((len = MBReadLine(&conn->co_RMBuf, &ptr)) != 0) {
	int code = strtol(ptr, NULL, 10);
	if (code >= 200 && code <= 299) {
	    logit(LOG_INFO, "mode-reader(%s) %s", conn->co_Desc->d_Id, ptr);
	    NNServerStart(conn);
	} else {
	    logit(LOG_ERR, "mode-reader(%s) failed: %s", conn->co_Desc->d_Id, ptr);
	    NNServerTerminate(conn);
	}
    }
}

void
NNServerPrime3(Connection *conn)
{
    int len;
    char *buf;

    conn->co_Func = NNServerPrime3;
    conn->co_State = "prime2";

    /*
     * any return code or EOF.  0 means we haven't gotten the
     * return code yet.
     */

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) != 0) {
	NNServerStart(conn);
    }
}

void
NNServerPrime4(Connection *conn)
{
    int len;
    char *ptr;

    conn->co_Func = NNServerPrime4;
    conn->co_State = "prime4";

    if ((len = MBReadLine(&conn->co_RMBuf, &ptr)) != 0) {
	int code = strtol(ptr, NULL, 10);
	if (code >= 200 && code <= 299)
	    logit(LOG_INFO, "mode-readonly(%s) %s", conn->co_Desc->d_Id, ptr);
	else
	    logit(LOG_ERR, "mode-readonly(%s) failed: %s", conn->co_Desc->d_Id, ptr);
	NNServerStart(conn);
    }
}

void
NNServerPrimeAuth1(Connection *conn)
{
    int len;
    char *ptr;

    conn->co_Func = NNServerPrimeAuth1;
    conn->co_State = "primeauth1";

    if ((len = MBReadLine(&conn->co_RMBuf, &ptr)) != 0) {
	int code = strtol(ptr, NULL, 10);
	if (code >= 300 && code <= 399) {
	    MBPrintf(&conn->co_TMBuf, "authinfo pass %s\r\n", conn->co_Auth.dr_AuthPass);

	    NNServerPrimeAuth2(conn);
	} else {
	    logit(LOG_ERR, "authinfo-user(%s) failed: %s", conn->co_Desc->d_Id, ptr);
	    NNServerTerminate(conn);
	}
    }
}

void
NNServerPrimeAuth2(Connection *conn)
{
    int len;
    char *ptr;

    conn->co_Func = NNServerPrimeAuth2;
    conn->co_State = "primeauth2";

    if ((len = MBReadLine(&conn->co_RMBuf, &ptr)) != 0) {
	int code = strtol(ptr, NULL, 10);
	if (code >= 200 && code <= 299) {
	    logit(LOG_INFO, "authinfo-pass(%s) %s", conn->co_Desc->d_Id, ptr);
	    NNServerStart(conn);
	} else {
	    logit(LOG_ERR, "authinfo-pass(%s) failed: %s", conn->co_Desc->d_Id, ptr);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * NNSERVERSTART() - start normal server operations.  Clean up from connect
 *		     code, make server available to clients.
 */

void
NNServerStart(Connection *conn)
{
    conn->co_Desc->d_Count = 0;
    conn->co_Flags &= ~(COF_INPROGRESS|COF_ININIT);

    if (conn->co_Desc->d_Type ==  THREAD_SPOOL)
	--NReadServAct;
    else
	--NWriteServAct;

    NNServerIdle(conn);
}

/*
 * NNSERVERIDLE() - server idle, wait for EOF or start next queued request.
 *		    If no more queued requests, try to requeue from unqueued
 *		    requests.
 */

void
NNServerIdle(Connection *conn)
{
    int len;
    char *buf;

    /*
     * Ooops, not really idle.  This can happen due to the recursive
     * nature of much of the code.
     */

    if ((conn->co_Flags & (COF_INPROGRESS|COF_CLOSESERVER)) > 0)
	return;

    conn->co_Func  = NNServerIdle;
    conn->co_State = "sidle";

    /*
     * Check for an unexpected condition on server, i.e. data or
     * EOF on the input where we didn't expect any.
     */

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) != 0) {
        if (len > 1) {
	    buf[len - 2] = 0;
	    logit(LOG_ERR, "Server closed connection: %s: %s",
					conn->co_Desc->d_Id, buf);
        } else {
	    logit(LOG_ERR, "Server closed connection: %s",
					conn->co_Desc->d_Id);
        }
	conn->co_Desc->d_Count = THREAD_LIMSIZE;
        NNServerTerminate(conn);
        return;
    }
    if (conn->co_SReq) {
	conn->co_Flags |= COF_INPROGRESS;

	if (conn->co_Desc->d_Type == THREAD_POST)
	    NNPostCommand1(conn);
	else
	    NNSpoolCommand1(conn);
    } else {
	QueueServerRequests();
    }
}

/*
 * NNServerTerminate() - terminate a server connection.  Usually occurs
 *			 if the server goes down or the related process
 *			 on the server is killed.   Any client request
 *			 queued to the server is requeued to another
 *			 server.
 */

void
NNServerTerminate(Connection *conn)
{
    ServReq *sreq;

    if ((conn->co_Flags & COF_CLOSESERVER) == COF_CLOSESERVER)
	return;

    while ((sreq = conn->co_SReq) != NULL) {
	conn->co_SReq = sreq->sr_Next;
	--conn->co_Desc->d_Count;
	sreq->sr_Next = NULL;
	sreq->sr_SConn = NULL;
	sreq->sr_Time = time(NULL);
	if (conn->co_Desc->d_Type == THREAD_POST) {
	    *PSWrite = sreq;
	    PSWrite = &sreq->sr_Next;
	} else {
	    *PSRead = sreq;
	    PSRead = &sreq->sr_Next;
	}
	if (conn->co_Desc->d_Type == THREAD_POST)
	    --NWriteServAct;
	else
	    --NReadServAct;
    }

    /*
     * If we bumped the active count for the duration of the startup and
     * terminated prior to the startup completing, we have to fix it here.
     */

    if (conn->co_Flags & COF_ININIT) {
	conn->co_Flags &= ~COF_ININIT;
	if (conn->co_Desc->d_Type == THREAD_POST)
	    --NWriteServAct;
	else
	    --NReadServAct;
    }

    /*
     * closeup the server.  If d_Count is non-zero we have
     * to cleanup our reference counts, but then we set d_Count
     * to ensure nothing else gets queued to the server
     * between calling NNTerminate() and the actual termination.
     */

    conn->co_Flags |= COF_CLOSESERVER;
    if (conn->co_Desc->d_Type == THREAD_POST)
	--NWriteServers;
    else
	--NReadServers;

    ++ServersTerminated;	/* flag for rescan/reopen  */

    conn->co_Desc->d_Count = THREAD_LIMSIZE;
    conn->co_Flags |= COF_INPROGRESS;

    QueueServerRequests();	/* try to requeue requests */
    NNTerminate(conn);
}

/*
 * NNSERVERCONNECT() - initial connection, get startup message (co_UCounter
 *			starts out 1, we do not clear it and make the server
 *			available until we get the startup message)
 */

void
NNServerConnect(Connection *conn)
{
    char *buf;
    char *ptr;
    int len;
    int code;

    conn->co_Func  = NNServerConnect;
    conn->co_State = "sconn";

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) <= 0) {
	if (len < 0) {
	    logit(LOG_ERR, "connect(%s) failed", conn->co_Desc->d_Id);
	    NNServerTerminate(conn);
	}
	if ( (conn->co_SessionStartTime + 60 ) < CurTime.tv_sec) {
	    logit(LOG_ERR, "connect(%s) timed out",  conn->co_Desc->d_Id);
	    NNServerTerminate(conn);
	}
	return;
    }

    ptr = buf;
    code = strtol(ptr, NULL, 10);
    if (code == 200) {
	logit(LOG_INFO, "connect(%s) %s", conn->co_Desc->d_Id, buf);
    } else if (code == 201) {
	if (conn->co_Desc->d_Type == THREAD_POST)
	    logit(LOG_INFO, "connect(%s) %s", conn->co_Desc->d_Id, buf);
	else
	    logit(LOG_INFO, "connect(%s) %s", conn->co_Desc->d_Id, buf);
    } else {
	logit(LOG_ERR, "connect(%s) unrecognized banner: %s", conn->co_Desc->d_Id, buf);
	NNServerTerminate(conn);
	return;
    }
    NNServerPrime1(conn);
}

/*
 *  NNARTICLERETRIEVEBYMESSAGEID() - retrieve article by message-id
 *
 *	Retrieve an article by its message id and write the article
 *	to the specified connection.
 *
 *	(a) attempt to fetch the article from cache (positive or negative hit)
 *
 *	(b) initiate the state machine to attempt to fetch the article from a
 *	    remote server.
 *
 *	(c) on remote server fetch completion, cache the article locally
 *
 *	(d) place article in transmission buffer for connection, transmitting
 *	    it, then return to the command state.
 *
 *	COM_ARTICLEWVF	retrieve article from remote by message-id but retrieve
 *			headers by co_ArtNo.
 *
 *	COM_ARTICLE	retrieve entire article from remote by message-id
 *
 *	COM_...
 */

void
NNArticleRetrieveByMessageId(Connection *conn, const char *msgid, const char *msgopts, int grouphint, int TimeRcvd, int grpIter, artno_t endNo)
{   
    int maxage = conn->co_Auth.dr_ReaderDef->rd_MaxAgePolicy;

    /*
     * (a) retrieve from cache if caching is enabled
     */

    if (DOpts.ReaderCacheMode) {
	int valid;
	int size;
	int cfd;

	valid = OpenCache(msgid, &cfd, &size);

	if (valid > 0) {
	    /*
	     * good cache
	     */
	    const char *map;
	    if (DebugOpt)
		printf("good cache\n");
	    if ((map = xmap(NULL, size, PROT_READ, MAP_SHARED, cfd, 0)) != NULL) {
		xadvise(map, size, XADV_WILLNEED);
		if (conn->co_ArtMode != COM_BODYNOSTAT) {
		    MBLogPrintf(conn, &conn->co_TMBuf, "%03d %lld %s %s\r\n", 
			GoodRC(conn),
			((conn->co_ArtMode==COM_ARTICLEWVF)?artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering):0),
			msgid,
			GoodResId(conn)
		    );
		}
		if (conn->co_ArtMode != COM_STAT) {
		    DumpArticleFromCache(conn, map, size, grpIter, endNo);
		    MBPrintf(&conn->co_TMBuf, ".\r\n");
		}
		xunmap((void *)map, size);
	    } else {
		if (conn->co_ArtMode == COM_BODYNOSTAT)
		    MBLogPrintf(conn, &conn->co_TMBuf, "(article not available)\r\n.\r\n");
		else if (conn->co_RequestFlags == ARTFETCH_ARTNO)
		    MBLogPrintf(conn, &conn->co_TMBuf, "423 No such article number in this group\r\n");
		else
		    MBLogPrintf(conn, &conn->co_TMBuf, "430 No such article\r\n");
	    }
	    close(cfd);
	    NNCommand(conn);
	    return;
	} else if (valid < 0) {
	    /*
	     * negatively cached
	     */
	    if (DebugOpt)
		printf("neg cache\n");

	    if (conn->co_ArtMode == COM_BODYNOSTAT)
		MBLogPrintf(conn, &conn->co_TMBuf, "(article not available)\r\n.\r\n");
	    else if (conn->co_RequestFlags == ARTFETCH_ARTNO)
		MBLogPrintf(conn, &conn->co_TMBuf, "423 No such article number in this group\r\n");
	    else
		MBLogPrintf(conn, &conn->co_TMBuf, "430 No such article\r\n");
	    NNCommand(conn);
	    return;
	}
    }

    /*
     * (b)(c)(d)
     */
    if (DebugOpt)
	printf("bad cache\n");

    if (msgopts && *msgopts) {
	if (*msgopts == 'a') {
	    maxage = atoi(msgopts + 1);
	}
    }
    NNServerRequest(conn, grouphint ? conn->co_GroupName : NULL, msgid, maxage, SREQ_RETRIEVE, TimeRcvd, grpIter, endNo);
    NNWaitThread(conn);
}

