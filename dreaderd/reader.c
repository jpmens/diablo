
/*
 * DREADERD/READER.C - reader task
 *
 *	Reader task, main control loop(s).
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void ReaderTask(int fd, const char *id);
Prototype void NNCommand(Connection *conn);
Prototype void NNCommand2(Connection *conn);
Prototype void NNBadCommandUse(Connection *conn);
Prototype void NNUnknownCommand(Connection *conn);
Prototype void NNTerminate(Connection *conn);
Prototype void NNAuthDone(Connection *conn);
Prototype void NNWriteHello(Connection *conn);
Prototype void NNWaitThread(Connection *conn);
Prototype void NNTPHelp(Connection *conn, char **pptr);
Prototype Connection *InitConnection(ForkDesc *desc, DnsRes *dres);
Prototype void DeleteConnection(Connection *conn);
Prototype void StatusUpdate(Connection *conn, const char *ctl, ...);
Prototype void LogCmd(Connection *conn, char dir, char *msg);
Prototype void GroupStats(Connection *conn);

Prototype KPDB *KDBActive;

void HandleReaderMsg(ForkDesc *desc);
int makeReaderSlot(void);
void freeReaderSlot(int slot);
void sigHup(int SigNo);
void sigUsr1(int SigNo);
void sigSegVReader(int sigNo);

KPDB *KDBActive;
int   TFd = -1;		/* thread's return fd to the parent */
int   NumReaders;
int   *ReaderSlotAry;
int   TerminatePending = 0;
int   CanTerminate = 0;
time_t   TerminateTime = 0;

void LogCmd(Connection *conn, char dir, char *msg)
{
    char *ptr, tmp = '\0';
    int doLog;

    doLog = DOpts.ReaderDetailLog;
    if (conn->co_Auth.dr_ReaderDef &&
	conn->co_Auth.dr_ReaderDef->rd_LogCmd >= 0)
	doLog = conn->co_Auth.dr_ReaderDef->rd_LogCmd;
    if (doLog == 0)
	return;

    if (conn->co_Desc->d_Slot < 0) {
	/*
	 * Oh, lame.  Diablo generally doesn't strip \r from inputted text
	 * lines, and seeing caret-M in syslog drives me nuts.  I'm not
	 * sure which is more lame, the problem or the hack/workaround
	 *
	 * JG200012210208
	 */
	if ((ptr = strpbrk(msg, "\r\n"))) {
	    tmp = *ptr;
	    *ptr = '\0';
	}
	logit(LOG_DEBUG, "%s [%d] %c %s", conn->co_Desc->d_Id, getpid(), dir, msg);
	if (ptr) {
	    *ptr = tmp;
	}
    } else {
	logit(LOG_DEBUG, "%s%s%s%s%s [%d/%d] %c %s", 
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host, getpid(), conn->co_Desc->d_Slot,
		dir, msg);
    }
}

void
ReaderTask(int fd, const char *id)
{
    time_t dtime = 0;
    time_t ltime = 0;
    time_t itime = 0;
    time_t ftime = 0;
    time_t atime = 0;
    int counter = 0;
    int forceallcheck = 0;
    int check_disconn_counter = 0;

    TFd = fd;

    /*
     * [re]open RTStatus
     */
    RTStatusOpen(RTStatus, ThisReaderFork * DOpts.ReaderThreads + 1, DOpts.ReaderThreads);

    /*
     * Since we setuid(), we won't core.  This is for debugging
     */
    if (CoreDebugOpt || (DOpts.ReaderCrashHandler != NULL &&
			strcasecmp(DOpts.ReaderCrashHandler, "none") != 0)) {
	signal(SIGSEGV, sigSegVReader);
	signal(SIGBUS, sigSegVReader);
	signal(SIGFPE, sigSegVReader);
	signal(SIGILL, sigSegVReader);
    }

    signal(SIGHUP, sigHup);
    signal(SIGUSR1, sigUsr1);

    /*
     * Setup thread for passed pipe
     */
    ResetThreads();
    AddThread("reader", fd, -1, THREAD_READER, -1, 0);

    FD_SET(fd, &RFds);

    /*
     * Open KPDB database for active file
     */

    if ((KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR)) == NULL) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(ReaderDActivePat));
	sleep(60);
	exit(1);
    }

    LoadExpireCtl(1);

    /*
     * Only startup connections to backend spools for reader threads
     */
    if (!FeedOnlyServer)
	CheckServerConfig(time(NULL), 1);

    /*
     * Selection core
     */

    while (!TerminatePending || NReadServAct || NumReaders) {
	/*
	 *  select core
	 */
	struct timeval tv;
	fd_set rfds = RFds;
	fd_set wfds = WFds;
	fd_set read_only_to_find_eof_fds;
	int i, sel_r;

	if (TerminatePending) {
	    if (TerminateTime == 0)
		TerminateTime = time(NULL) + 2;
	    if (TerminateTime < time(NULL) || !NumReaders)
		CanTerminate = 1;
	}

	/*
	 *  Get next scheduled timeout, no more then 2 seconds
	 *  (x 10 counter counts = 20 seconds max for {d,i,f}time 
	 *  check)
	 *
	 * If we are terminating, then speed up the select to clear
	 * out the connections.
	 *
	 */

	if (TerminatePending)
	    NextTimeout(&tv, 50);
	else
	    NextTimeout(&tv, 2 * 1000);

	stprintf("%s readers=%02d spoolsrv=%d/%d postsrv=%d/%d",
	    id,
	    NumReaders,
	    NReadServAct, NReadServers, 
	    NWriteServAct, NWriteServers
	);

	/* Check for disconnected clients every 50 times through the loop */
	FD_ZERO(&read_only_to_find_eof_fds);
	if (++check_disconn_counter == 50) {
	    for (i = 0; i < MaxFds; ++i) {
		if (FD_ISSET(i, &wfds) && (!(FD_ISSET(i, &rfds)))) {
		    FD_SET(i, &rfds);
		    FD_SET(i, &read_only_to_find_eof_fds);
		}
	    }
	    check_disconn_counter = 0;
	}

#if USE_AIO
	AIOUnblockSignal();
#endif
	sel_r = select(MaxFds, &rfds, &wfds, NULL, &tv);
#if USE_AIO
	AIOBlockSignal();
#endif

	gettimeofday(&CurTime, NULL);

	if(sel_r < 0 && errno != EINTR)
	    logit(LOG_CRIT,
		  "select error: %s (rfds=0x%x, wfds=0x%x)",
		  strerror(errno),
		  rfds,
		  wfds);

	/*
	 * select is critical, don't make unnecessary system calls.  Only
	 * test the time every 10 selects (20 seconds worst case), and
	 * only check for a new server configuration file every 60 seconds 
	 * after the initial load.  This may rearrange THREAD_SPOOL and
	 * THREAD_POST threads.
	 *
	 * We do not startup spool/post servers for feed-only forks
	 *
	 * However, flush overview cache even for feed-only forks.
	 */

	if (FeedOnlyServer <= 0) {
	    if (++counter == 10) {
		time_t t = CurTime.tv_sec;
		if (ltime) {
		    dtime += t - ltime;
		    itime += t - ltime;
		    ftime += t - ltime;
		    atime += t - ltime;
		}

		/*
		 * Check for server config change once a minute
		 */
		if (dtime < -5 || dtime >= 5) {
		    if (!TerminatePending)
			CheckServerConfig(t, ServersTerminated);
		    dtime = 0;
		}

		/*
		 * Flush overview every 30 seconds to allow dexpireover to work
		 */
		if (ftime < -5 || ftime >= 30) {
		    FlushOverCache();
		    LoadExpireCtl(0);
		    ftime = 0;
		}

		/*
		 * Poll all active descriptors once every 5 minutes.  This
		 * will work around a linux embrionic close bug that
		 * doesn't wakeup select(), and is used to idle-timeout
		 * connections. XXX
		 */
		if (itime < -5 || itime >= 300) {
		    rfds = RFds;
		    itime = 0;
		}

		/*
		 * Force a check all of FD's every 30 seconds to handle
		 * idle and session timeouts
		 */
		if (atime < -5 || atime >= 30) {
		    forceallcheck = 1;
		    atime = 0;
		}
		ltime = t;
		counter = 0;
	    }
	} else {
	    /*
	     * For a feed-only server, we only flush the overview FD
	     * cache every 5 minutes, and with a greater granularity.
	     * It should cycle much faster than that normally, and this
	     * is to prevent idle feed-only forks from keeping locks.
	     *
	     */

	    if (++counter == 10) {
		time_t t = CurTime.tv_sec;
		if (ltime) {
		    ftime += t - ltime;
		}

		if (ftime < -5 || ftime >= 300) {
		    FlushOverCache();
		    ftime = 0;
		}
		ltime = t;
		counter = 0;
	    }
	}

	for (i = 0; i < MaxFds; ++i) {
	    if (FD_ISSET(i, &rfds) && FD_ISSET(i, &read_only_to_find_eof_fds)) {
		char junk_byte;
		int ret_val;
		/*
		 * This FD is not marked for reading, but select() claims
		 * it has something to say. We don't actually want to read
		 * from it, but we do want to close it if the associated
		 * connection is dead.
		 */
		FD_CLR(i, &rfds);

		/* Use recv() with MSG_PEEK to see if it's closed.
		 * We shouldn't block because we're O_NONBLOCK.
		 */
		ret_val = recv(i, &junk_byte, 1, MSG_PEEK);

		/* If ret_val is zero, this means the socket is closed.
		 * Blast it. Otherwise, ignore it.
		 */
		if(ret_val == 0) {
		    ForkDesc *desc;
		    if((desc = FindThread(i, -1)) != NULL) {
			Connection *conn = desc->d_Data;

			if(conn) {
			    NNTerminate(conn);
			    DeleteConnection(conn);
			}
			DelThread(desc);
		    }
		}
	    }
	}

	for (i = 0; i < MaxFds; ++i) {
	    if (forceallcheck || TerminatePending ||
		FD_ISSET(i, &rfds) || FD_ISSET(i, &wfds)) {
		ForkDesc *desc;

		if ((desc = FindThread(i, -1)) != NULL) {
		    Connection *conn = desc->d_Data;

		    if (conn) {
			/*
			 * handle output I/O (optimization)
			 */

			MBFlush(conn, &conn->co_TMBuf);
			conn->co_FCounter = 0;
		    } 

		    /*
		     * Function dispatch
		     */

		    switch(desc->d_Type) {
		    case THREAD_READER:
			if (FD_ISSET(i, &rfds) || FD_ISSET(i, &wfds))
			    HandleReaderMsg(desc);
			break;
		    case THREAD_NNTP:		/* client	  */
			conn->co_Func(conn);
			if (conn->co_Auth.dr_ResultFlags & DR_REQUIRE_DNS) {
			    /* Go back to parent for DNS check */
			    conn->co_Auth.dr_Code = 0;
			    conn->co_TMBuf.mh_WEof = 1;
			}
			break;
		    case THREAD_SPOOL:		/* spool server	  */
		    case THREAD_POST:		/* posting server */
			conn->co_Func(conn);
			LogServerInfo(conn, TFd);
			break;
		    default:
			/* panic */
			break;
		    }

		    /*
		     * do not call MBFlush after the function because the
		     * function may be waiting for write data to drain and
		     * we don't want to cause write data to drain here and
		     * then not get a select wakeup later.
		     *
		     * check for connection termination
		     */

		    if (conn) {
			int idleTimeout = 0;
			if (conn->co_Auth.dr_ReaderDef) {
			    if (conn->co_Auth.dr_ReaderDef->rd_IdleTimeout &&
				    conn->co_LastActiveTime +
				    conn->co_Auth.dr_ReaderDef->rd_IdleTimeout <=
				    CurTime.tv_sec) {
				logit(LOG_INFO, "timeout idle %s",
				    conn->co_Auth.dr_Host);
				MBLogPrintf(conn,
				    &conn->co_TMBuf,
				    "400 %s: Idle timeout.\r\n",
				    conn->co_Auth.dr_VServerDef->vs_HostName
				);
				idleTimeout = 1;
				NNTerminate(conn);
			    }
			    if (conn->co_Auth.dr_ReaderDef->rd_SessionTimeout &&
				    conn->co_SessionStartTime +
				    conn->co_Auth.dr_ReaderDef->rd_SessionTimeout <=
				    CurTime.tv_sec) {
				logit(LOG_INFO, "timeout session %s",
				    conn->co_Auth.dr_Host);
				MBLogPrintf(conn,
				    &conn->co_TMBuf,
				    "400 %s: Session timeout.\r\n",
				    conn->co_Auth.dr_VServerDef->vs_HostName
				);
				idleTimeout = 1;
				NNTerminate(conn);
			    }
			}
			if ((!conn->co_Auth.dr_Code &&
				desc->d_Type == THREAD_NNTP) ||
			    idleTimeout ||
			    (conn->co_RMBuf.mh_REof && 
			    conn->co_TMBuf.mh_WEof &&
			    conn->co_TMBuf.mh_MBuf == NULL) ||
			    (TerminatePending &&
					!(conn->co_Flags & COF_MAYNOTCLOSE))
			) {
			    DeleteConnection(conn);
			    DelThread(desc);
			}
		    }
		}
	    }
	}
	forceallcheck = 0;
	(void)ScanTimers(1, 0);
	if (CanTerminate)
	    break;
    }
    RTStatusClose();
    KPDBClose(KDBActive);
    exit(0);
}

void
HandleReaderMsg(ForkDesc *desc)
{
    int r;
    int recv_fd;
    DnsRes  dres;

    if ((r = RecvMsg(desc->d_Fd, &recv_fd, &dres)) == sizeof(DnsRes)) {
	if (recv_fd >= MAXFDS) {
	    logit(LOG_WARNING, "fd too large %d/%d, increase MAXFDS for select. Closing fd", recv_fd, MAXFDS);
	    /*
	     * Tell the main server that we are done with the connection
	     */
	    fcntl(TFd, F_SETFL, 0);
	    SendMsg(TFd, recv_fd, &dres);
	    fcntl(TFd, F_SETFL, O_NONBLOCK);
	} else if (recv_fd >= 0) {
	    ForkDesc *ndesc;
	    Connection *conn;
	    char vsbuf[11];
	    char hsbuf[31];

	    if (ReadAccessCache() == 1) {
		ScanThreads(THREAD_NNTP, UpdateAuthDetails);
		ScanThreads(THREAD_SPOOL, UpdateAuthDetails);
		ScanThreads(THREAD_READER, UpdateAuthDetails);
		ScanThreads(THREAD_SPOOL, UpdateAuthDetails);
		ScanThreads(THREAD_FEEDER, UpdateAuthDetails);
		ClearOldAccessMap();
	    }
	    SetAuthDetails(&dres, dres.dr_ReaderName);

	    ndesc = AddThread("client", recv_fd, -1, THREAD_NNTP, makeReaderSlot(), 0);
	    ++NumReaders;
	    if (DebugOpt)
		printf("add thread fd=%d\n", recv_fd);

	    FD_SET(ndesc->d_Fd, &WFds);	/* will cause immediate effect */
	    conn = InitConnection(ndesc, &dres);
	    if (conn->co_Auth.dr_Flags & DF_FEED)
		conn->co_Flags |= COF_SERVER;

	    snprintf(vsbuf, sizeof(vsbuf), "%s", conn->co_Auth.dr_VServerDef->vs_Name);

	    snprintf(hsbuf, sizeof(hsbuf), "%s%s%s%s%s",
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host);
	    RTStatusBase(conn->co_Desc->d_Slot, "ACTV %-10s %-30s", vsbuf, hsbuf);

	    StatusUpdate(conn, "(startup)");
	    if (conn->co_Auth.dr_ResultFlags & DR_REQUIRE_DNS)
		NNAuthDone(conn);
	    else
		NNWriteHello(conn);
	} else {
	    if (DebugOpt)
		printf("recvmsg(): EOF1\n");
	    DelThread(desc);
	    TerminatePending = 1;
	}
    }

    /*
     * If recv error, check errno.  If temporary error,
     * leave r negative (select loop).  Set r = 0 to 
     * terminate.
     */

    if (r != sizeof(DnsRes) && r != -1) {
	if (DebugOpt)
	    printf("recvmsg(): Bad size read from RecvMsg\n");
    }
    if (r < 0) {
	if (errno != EINTR &&
	    errno != EWOULDBLOCK &&
	    errno != EAGAIN
	) {
	    r = 0;
	}
    }

    /*
     * EOF (or error)
     */

    if (r == 0) {
	if (DebugOpt)
	    printf("recvmsg(): EOF/error from parent %s\n", strerror(errno));
	DelThread(desc);
	TerminatePending = 1;
    }
}

Connection *
InitConnection(ForkDesc *desc, DnsRes *dres)
{
    MemPool    *pool = NULL;
    Connection *conn = zalloc(&pool, sizeof(Connection));

    desc->d_Data = conn;

    if (dres)
	conn->co_Auth = *dres;

    conn->co_Desc = desc;
    conn->co_MemPool = pool;
    gettimeofday(&conn->co_RateTv, NULL);
    conn->co_LastActiveTime = conn->co_SessionStartTime = conn->co_RateTv.tv_sec;
    MBInit(&conn->co_TMBuf, desc->d_Fd, &conn->co_MemPool, &conn->co_BufPool);
    MBInit(&conn->co_RMBuf, desc->d_Fd, &conn->co_MemPool, &conn->co_BufPool);
    MBInit(&conn->co_ArtBuf, -1, &conn->co_MemPool, &conn->co_BufPool);

    if (conn->co_Desc->d_Type == THREAD_NNTP) {
	conn->co_ClientTotalByteCount = 0.0;
	conn->co_ClientTotalArticleCount = 0;
	conn->co_ClientGroupByteCount = 0.0;
	conn->co_ClientGroupArticleCount = 0;
	conn->co_ClientGroupCount = conn->co_ClientPostCount = 0;
    }

    return(conn);
}

void
DeleteConnection(Connection *conn)
{
    MemPool *mpool = conn->co_MemPool;

    if (conn->co_Desc->d_Type == THREAD_SPOOL) {
	conn->co_LastServerLog = 1;
	LogServerInfo(conn, TFd);
    } else if (conn->co_Desc->d_Type == THREAD_NNTP) {
	char statbuf[1024];
	char vsbuf[11];
	char hsbuf[31];

	snprintf(vsbuf, sizeof(vsbuf), "%s", conn->co_Auth.dr_VServerDef->vs_Name);

	snprintf(hsbuf, sizeof(hsbuf), "%s%s%s%s%s",
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host);
	RTStatusBase(conn->co_Desc->d_Slot, "CLSD %-10s %-30s", vsbuf, hsbuf);

	GroupStats(conn);

	snprintf(statbuf, sizeof(statbuf), "exit articles %lu groups %lu posts %lu bytes %.0f", conn->co_ClientTotalArticleCount, conn->co_ClientGroupCount, conn->co_ClientPostCount, conn->co_ClientTotalByteCount);
	LogCmd(conn, '$', statbuf);

	StatusUpdate(conn, "(closed)");
	freeReaderSlot(conn->co_Desc->d_Slot);
	--NumReaders;
	/*
	 * Inform the main server that we are done with the descriptor
	 * by writing the DnsRes structure back to it, so the main server
	 * can track who from where is connecting to what and when that
	 * connection terminates.
	 */
	conn->co_Auth.dr_ByteCount = conn->co_TMBuf.mh_TotalBytes;
	SendMsg(TFd, conn->co_Desc->d_Fd, &conn->co_Auth);
    }

    FreeControl(conn);
    freePool(&conn->co_BufPool);
    freePool(&mpool);		/* includes Connection structure itself */
}

void
NNTerminate(Connection *conn)
{
    conn->co_Func = NNTerminate;
    conn->co_State = "term";
    conn->co_RMBuf.mh_REof = 1;
    conn->co_TMBuf.mh_WEof = 1;

    /*
     * conn->co_SReq is only non-NULL for a client
     * connection.  Server use of the field will have
     * already NULL'd it out in NNServerTerminate().
     *
     * The problem we have is that the server may be actively
     * using the client connection's MBuf's and be in some 
     * intermediate state.  Therefore, we must change the SReq
     * to point to a sink-NULL client XXX.
     */
    if (conn->co_SReq) {
	if (conn->co_SReq->sr_CConn != conn)
	    fatal("NNTerminate(): server conn had non_NULL co_SReq");
	/* 
	 * Disconnect the co_SReq from the client.  This will cause
	 * the server operation-in-progress to abort, if possible.
	 */
	conn->co_SReq->sr_CConn = NULL;
	conn->co_SReq = NULL;
    }

    /*
     * If this connection was performing a list operation and had the
     * active cache locked, unlock it. If it was building the cache,
     * blast the cache because it's half-built.
     */
    if (conn->co_ListCacheMode == ACMODE_READ)
	ActiveCacheReadUnlock();
    if (conn->co_ListCacheMode == ACMODE_WRITE) {
	ActiveCacheFreeMain();
	ActiveCacheWriteUnlock();
    }
    MBFlush(conn, &conn->co_TMBuf);
    FD_SET(conn->co_Desc->d_Fd, &WFds);
}

void
ClientSetTCPOpts(Connection *conn)
{
#ifdef	IP_TOS
    int ipTOS;
#endif

#ifdef	IP_TOS
    if (((ipTOS = conn->co_Auth.dr_ReaderDef->rd_SetTOS)) >= 0)
	if (setsockopt((&conn->co_TMBuf)->mh_Fd, IPPROTO_IP, IP_TOS, (char *)&ipTOS, sizeof(ipTOS)) < 0) {
	    syslog(LOG_ERR, "setsockopt IP_TOS to %d on client fd %d: %m", ipTOS, (&conn->co_TMBuf)->mh_Fd);
	}
#endif

    if (conn->co_Auth.dr_ReaderDef->rd_RxBufSize)
	if (setsockopt((&conn->co_TMBuf)->mh_Fd, SOL_SOCKET, SO_RCVBUF, (void *)&conn->co_Auth.dr_ReaderDef->rd_RxBufSize, sizeof(int)) < 0) {
	    syslog(LOG_ERR, "setsockopt SO_RCVBUF to %d on client fd %d: %m", conn->co_Auth.dr_ReaderDef->rd_RxBufSize, (&conn->co_TMBuf)->mh_Fd);
	}
    if (conn->co_Auth.dr_ReaderDef->rd_TxBufSize)
	if (setsockopt((&conn->co_TMBuf)->mh_Fd, SOL_SOCKET, SO_SNDBUF, (void *)&conn->co_Auth.dr_ReaderDef->rd_TxBufSize, sizeof(int)) < 0) {
	    syslog(LOG_ERR, "setsockopt SO_SNDBUF to %d on client fd %d: %m", conn->co_Auth.dr_ReaderDef->rd_TxBufSize, (&conn->co_TMBuf)->mh_Fd);
	}
}

void
NNAuthDone(Connection *conn)
{
    conn->co_Auth.dr_ResultFlags = 0;
    if (conn->co_Auth.dr_Code == 1 && (conn->co_Auth.dr_Flags & DF_AUTHREQUIRED) == 0) {
	conn->co_Auth.dr_Flags &= ~DF_AUTHREQUIRED;
	ClientSetTCPOpts(conn);
	MBLogPrintf(conn, &conn->co_TMBuf, "281 Ok\r\n");
	logit(LOG_DEBUG,
		"Access granted to %s via %s", conn->co_Auth.dr_Host,
		conn->co_Auth.dr_ReaderName);
	NNCommand(conn);
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "502 Authentication error\r\n");
	NNTerminate(conn);
    }
}

char *
WelcomeString(char *base, Vserver *vs, const char *servertype)
{
    static char r[512];
    int l = 0;

    r[0] = 0;
    while (l < sizeof(r) && *base) {
	if (*base == '%') {
	    switch (*(++base)) {
		case 'a':
		    l += snprintf(&r[l], 512 - l, "%s", vs->vs_NewsAdm);
		    break;
		case 'c':
		    l += snprintf(&r[l], 512 - l, "%s", vs->vs_ClusterName);
		    break;
		case 'h':
		    l += snprintf(&r[l], 512 - l, "%s", vs->vs_HostName);
		    break;
		case 'o':
		    l += snprintf(&r[l], 512 - l, "%s", vs->vs_Org);
		    break;
		case 't':
		    l += snprintf(&r[l], 512 - l, "%s", servertype);
		    break;
		case 'v':
		    l += snprintf(&r[l], 512 - l, "Diablo %s-%s", VERS, SUBREV);
		    break;
	    }
	    base++;
	} else {
	    r[l++] = *base++;
	}
    }
    r[l] = '\0';
    return(r);
}

void
NNWriteHello(Connection *conn)
{
    const char *postingOk = "(no posting)";
    const char *noReading = "";
    const char *serverType = "NNRP";

    if (conn->co_Auth.dr_Flags & DF_POST)
	postingOk = "(posting ok)";
    if (conn->co_Flags & COF_SERVER) {
	serverType = "NNTP-FEED";
    } else if ((conn->co_Auth.dr_Flags & DF_READ) == 0) {
	noReading = "(no reading)";
    }
    if ((conn->co_Auth.dr_Flags & (DF_FEED|DF_READ|DF_POST|DF_AUTHREQUIRED)) == 0) {
	MBLogPrintf(conn,
	    &conn->co_TMBuf, 
	    "502 %s: Access denied to your node%s%s.\r\n",
	    conn->co_Auth.dr_VServerDef->vs_HostName,
	    *conn->co_Auth.dr_VServerDef->vs_NewsAdm ? " - " : "",
	    conn->co_Auth.dr_VServerDef->vs_NewsAdm
	);
	NNTerminate(conn);
	return;
    }

    ClientSetTCPOpts(conn);

    if (*conn->co_Auth.dr_VServerDef->vs_Welcome)
	MBLogPrintf(conn,
		&conn->co_TMBuf, 
		"%d %s %s%s\r\n",
		(conn->co_Auth.dr_Flags & DF_POST) ? 200 : 201,
		WelcomeString(conn->co_Auth.dr_VServerDef->vs_Welcome,
					conn->co_Auth.dr_VServerDef,
					serverType),
		postingOk,
		noReading
	);
    else
	MBLogPrintf(conn,
		&conn->co_TMBuf, 
		"%d %s %s Service Ready%s%s %s%s.\r\n",
		(conn->co_Auth.dr_Flags & DF_POST) ? 200 : 201,
		conn->co_Auth.dr_VServerDef->vs_HostName,
		serverType,
		*conn->co_Auth.dr_VServerDef->vs_NewsAdm ? " - " : "",
		conn->co_Auth.dr_VServerDef->vs_NewsAdm,
		postingOk,
		noReading
	);
    NNCommand(conn);
}

/*
 * NNCommand() - general command entry.  Attempt to flush output data
 *		 and then go onto the appropriate command set.
 */

#define CMDF_AUTH	0x00000001
#define CMDF_SERVER	0x00000002
#define CMDF_READER	0x00000004
#define CMDF_NOTFEEDONLY 0x00000008

typedef struct Command {
    const char *cmd_Name;
    int		cmd_Flags;
    void	(*cmd_Func)(Connection *conn, char **pptr);
    const char	*cmd_Help;
    int		cmd_DRBC_Type;
} Command;

Command Cmds[] = {
    { 
	"article",
	CMDF_AUTH|CMDF_SERVER|CMDF_READER|CMDF_NOTFEEDONLY,
	NNTPArticle,
	"[MessageID|Number]",
	DRBC_ARTICLE
    },
    { 
	"body",
	CMDF_AUTH|CMDF_SERVER|CMDF_READER|CMDF_NOTFEEDONLY,	
	NNTPBody,
	"[MessageID|Number]",
	DRBC_BODY
    },
    { 
	"date",		
	0	 |CMDF_SERVER|CMDF_READER,
	NNTPDate,
	"",
	DRBC_NONE
    },
    { 
	"head",	
	CMDF_AUTH|CMDF_SERVER|CMDF_READER|CMDF_NOTFEEDONLY,
	NNTPHead,
	"[MessageID|Number]",
	DRBC_HEAD
    },
    { 
	"help",		
	0	 |CMDF_SERVER|CMDF_READER,
	NNTPHelp,
	"",
	DRBC_NONE
    },
    { 
	"ihave",
	CMDF_AUTH|CMDF_SERVER|CMDF_READER,
	NNTPIHave,
	"",
	DRBC_NONE
    },
    { 
	"takethis",	
	CMDF_AUTH|CMDF_SERVER|0		 ,
	NNTPTakeThis,
	"MessageID",
	DRBC_NONE
    },
    { 
	"check",
	CMDF_AUTH|CMDF_SERVER|0		 ,
	NNTPCheck,
	"MessageID",
	DRBC_NONE
    },
    { 
	"mode",
	CMDF_AUTH|CMDF_READER,
	NNTPMode,
	"reader|stream",
	DRBC_NONE
    },
    { 
	"mode",
	CMDF_AUTH|CMDF_SERVER,
	NNTPMode ,
	"reader|stream|headfeed",
	DRBC_NONE
    },
    { 
	"slave",
	CMDF_AUTH|CMDF_SERVER|CMDF_READER,
	NNTPSlave,
	"",
	DRBC_NONE
    },
    { 
	"quit",
	0	 |CMDF_SERVER|CMDF_READER,
	NNTPQuit,
	"",
	DRBC_NONE
    },
    { 
	"group",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPGroup ,
	"newsgroup",
	DRBC_NONE
    },
    { 
	"last",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPLast,
	"",
	DRBC_NONE
    },
    { 
	"next",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPNext,
	"",
	DRBC_NONE
    },
    { 
	"list",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPList,
	"[active|active.times|newsgroups|extensions|distributions|distrib.pats|moderators|overview.fmt|subscriptions]",
	DRBC_LIST
    },
    { 
	"listgroup",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPListGroup,
	"newsgroup",
	DRBC_LIST
    },
    { 
	"newgroups",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPNewgroups,
	"yymmdd hhmmss [\"GMT\"] [<distributions>]",
	DRBC_LIST
    },
    { 
	"newnews",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPNewNews,
	"newsgroups yymmdd hhmmss [\"GMT\"] [<distribution>]",
	DRBC_LIST
    },
    { 
	"post",	
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPPost,
	"",
	DRBC_NONE
    },
    { 
	"stat",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPStat,
	"[MessageID|Number]",
	DRBC_NONE
    },
    { 
	"xgtitle",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXGTitle,
	"[group_pattern]",
	DRBC_LIST
    },
    { 
	"xhdr",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXHdr,
	"header [range|MessageID]",
	DRBC_XHDR
    },
    { 
	"hdr",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXHdr,
	"header [range|MessageID]",
	DRBC_XHDR
    },
    { 
	"over",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXOver,
	"[range]",
	DRBC_XOVER
    },
    { 
	"xover",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXOver,
	"[range]",
	DRBC_XOVER
    },
    { 
	"xzver",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXZver,
	"[range]",
	DRBC_XZVER
    },
    { 
	"xpat",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXPat,
	"header range|MessageID pat",
	DRBC_XHDR
    },
    { 
	"xpath",
	CMDF_AUTH|0	     |CMDF_READER,
	NNTPXPath,
	"MessageID",
	DRBC_XHDR
    },
    { 
	"xnumbering",
	CMDF_AUTH|CMDF_READER,
	NNTPXNumbering,
	"rfc3977|rfc977|window",
	DRBC_NONE
    },
    { 
	"authinfo",
	0	 |CMDF_SERVER|CMDF_READER,
	NNTPAuthInfo,
	"user Name|pass Password",
	DRBC_NONE
    }
};

void 
NNCommand(Connection *conn)
{

    /*
     * Check for changes to the access cache before every command
     */

    if (ReadAccessCache() == 1) {
	ScanThreads(THREAD_NNTP, UpdateAuthDetails);
	ScanThreads(THREAD_SPOOL, UpdateAuthDetails);
	ScanThreads(THREAD_READER, UpdateAuthDetails);
	ScanThreads(THREAD_SPOOL, UpdateAuthDetails);
	ScanThreads(THREAD_FEEDER, UpdateAuthDetails);
	ClearOldAccessMap();
    }

    MBFlush(conn, &conn->co_TMBuf);

    if (conn->co_Auth.dr_ReaderDef->rd_ByteLimit &&
	    conn->co_Auth.dr_ReaderDef->rd_ByteLimit <
	    conn->co_ClientTotalByteCount) {
	logit(LOG_INFO, "bytelimit session %s byte=%d/%d",
		conn->co_Auth.dr_Host,
		conn->co_ClientTotalByteCount,
		conn->co_Auth.dr_ReaderDef->rd_ByteLimit);
	MBLogPrintf(conn,
	    &conn->co_TMBuf,
	    "400 %s: Session byte limit reached.\r\n",
	    conn->co_Auth.dr_VServerDef->vs_HostName
	);
	NNTerminate(conn);
    }
    NNCommand2(conn);
    conn->co_LastActiveTime = CurTime.tv_sec;
}

void
NNCommand2(Connection *conn)
{
    char *ptr;
    char *cmd;
    char *buf;
    Command *scan;
    int len;

    conn->co_Func = NNCommand2;
    conn->co_State = "waitcmd";

    /*
     * we have to be careful in regards to recursive operation, nor do
     * we want one descriptor to hog the process.  We can't set RFds
     * because the next command may already be entirely loaded into an
     * MBuf so setting RFds may not unblock us.  Instead, we set WFds
     * which basically forces a wakeup at some point in the future.
     */

    if (conn->co_FCounter) {
	FD_SET(conn->co_Desc->d_Fd, &WFds);
	return;
    }
    ++conn->co_FCounter;

    /*
     * get command
     */

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) == 0) {
	StatusUpdate(conn, "(idle)");
	return;
    }

    conn->co_ByteCountType = DRBC_NONE;

    /*
     * check EOF
     */

    if (len < 0) {
	NNTerminate(conn);
	return;
    }

    /*
     * strip CR LF
     */

    ptr = buf;

    if (len > 1 && ptr[len-2] == '\r')
	ptr[len-2] = 0;

    if (DebugOpt)
	printf("command: %s\n", ptr);

    if (strncasecmp(ptr, "authinfo pass ", 14)) {
        LogCmd(conn, '<', ptr);
    } else {
        LogCmd(conn, '<', "authinfo pass **unlogged**");
    }

    if (conn->co_Auth.dr_Flags & DF_USEPROXIED) {
      struct sockaddr_in sin;
      char *pt = NULL;

      if (strncasecmp(ptr, "proxied ", 8) || ! ((pt = strrchr(ptr, ':')))) {
          MBLogPrintf(conn,
              &conn->co_TMBuf,
              "400 %s: Proxy authentication failure.\r\n",
              conn->co_Auth.dr_VServerDef->vs_HostName
          );
          NNTerminate(conn);
      }

      *pt++ = '\0';
      ptr += 8;

      bzero((void *)&sin, sizeof(&sin));
      sin.sin_family = AF_INET;
      sin.sin_port = htons(atoi(pt));
      sin.sin_addr.s_addr = inet_addr(ptr);
      bcopy(&sin, &conn->co_Auth.dr_Addr, sizeof(conn->co_Auth.dr_Addr));

      conn->co_Auth.dr_Flags &= ~DF_USEPROXIED;
      conn->co_Auth.dr_ResultFlags = DR_REQUIRE_DNS;
      return;
    }

    /*
     * extract command (note: StatusUpdate() will limit the line length)
     */

    StatusUpdate(conn, "%s", ptr);

    if ((cmd = parseword(&ptr, " \t")) == NULL) {
	NNCommand(conn);
	return;
    }
    {
	int i;

	for (i = 0; cmd[i]; ++i)
	    cmd[i] = tolower((int)(unsigned char)cmd[i]);
    }

    /*
     * Locate and execute command
     */

    for (scan = &Cmds[0]; scan < &Cmds[arysize(Cmds)]; ++scan) {
	if (strcmp(cmd, scan->cmd_Name) == 0) {
	    if (conn->co_Flags & COF_SERVER) {
		if (scan->cmd_Flags & CMDF_SERVER) {
		    if ((conn->co_Auth.dr_Flags & DF_FEEDONLY) == 0)
			break;
		    if ((scan->cmd_Flags & CMDF_NOTFEEDONLY) == 0)
			break;
		}
	    } else {
		if (scan->cmd_Flags & CMDF_READER)
		    break;
	    }
	}
    }
    if (scan < &Cmds[arysize(Cmds)]) {
	if ((scan->cmd_Flags & CMDF_AUTH) &&
	    (conn->co_Auth.dr_Flags & DF_AUTHREQUIRED)
	) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "480 Authentication required for command\r\n");
	    NNCommand(conn);
	} else {
	    conn->co_ByteCountType = scan->cmd_DRBC_Type;
	    scan->cmd_Func(conn, &ptr);
	}
    } else {
	NNUnknownCommand(conn);
    }
}

void
NNBadCommandUse(Connection *conn)
{
    MBLogPrintf(conn, &conn->co_TMBuf, "501 Bad command use\r\n");
    NNCommand(conn);
}

void
NNUnknownCommand(Connection *conn)
{
    MBLogPrintf(conn, &conn->co_TMBuf, "500 What?\r\n");
    NNCommand(conn);
}

/*
 * NNWaitThread() - wait for some other thread to finish
 *		    our request.  Howeve, we allow writes
 *		    to occur on our descriptor while we
 *		    are waiting in case the other thread
 *		    is shoving stuff out our connection.
 */

void
NNWaitThread(Connection *conn)
{
    conn->co_Func = NNWaitThread;
    conn->co_State = "waitrt";
    FD_CLR(conn->co_Desc->d_Fd, &RFds);
    /*FD_CLR(conn->co_Desc->d_Fd, &WFds);*/
}

void
NNTPHelp(Connection *conn, char **pptr)
{
    Command *scan;

    MBLogPrintf(conn, &conn->co_TMBuf, "100 Legal commands\r\n");
    for (scan = &Cmds[0]; scan < &Cmds[arysize(Cmds)]; ++scan) {
	int ok = 0;

	if (conn->co_Flags & COF_SERVER) {
	    if (scan->cmd_Flags & CMDF_SERVER) {
		ok = 1;
		if ((scan->cmd_Flags & CMDF_NOTFEEDONLY) &&
		    (conn->co_Auth.dr_Flags & DF_FEEDONLY)
		) {
		    ok = 0;
		}
	    }
	} else {
	    if (scan->cmd_Flags & CMDF_READER)
		ok = 1;
	    if (!conn->co_Auth.dr_ReaderDef->rd_AllowNewnews &&
				(strcmp(scan->cmd_Name, "newnews") == 0))
		ok = 0;
	}
	if (ok)
	    MBPrintf(&conn->co_TMBuf, "  %s %s\r\n", scan->cmd_Name, scan->cmd_Help);
    }
    MBPrintf(&conn->co_TMBuf, ".\r\n");
    NNCommand(conn);
}

void
StatusUpdate(Connection *conn, const char *ctl, ...)
{
    char buf[256];
    va_list va;

    snprintf(buf, sizeof(buf), "%c%c%c %-30s",
	((conn->co_Auth.dr_Flags & DF_FEED) ? 'f' : '-'),
	((conn->co_Auth.dr_Flags & DF_READ) ? 'r' : '-'),
	((conn->co_Auth.dr_Flags & DF_POST) ? 'p' : '-'),
	(conn->co_Auth.dr_Flags & DF_GROUPLOG) ?
	    (conn->co_GroupName ? conn->co_GroupName : "(none)") :
	    "(none)"
    );
    buf[34] = ' ';
    buf[35] = '\0';
    va_start(va, ctl);
    vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ctl, va);
    va_end(va);
    RTStatusUpdate(conn->co_Desc->d_Slot, "%s", buf);
}

int
makeReaderSlot(void)
{
    static int SlotNo;
    int i;

    if (ReaderSlotAry == NULL) {
	ReaderSlotAry = zalloc(&SysMemPool, sizeof(*ReaderSlotAry) * DOpts.ReaderThreads);
    }
    for (i = 0; i < DOpts.ReaderThreads; ++i) {
	SlotNo = (SlotNo + 1) % DOpts.ReaderThreads;
	if (ReaderSlotAry[SlotNo] == 0) {
	    ReaderSlotAry[SlotNo] = 1;
	    return(SlotNo);
	}
    }
    return(-1);
}

void
freeReaderSlot(int slot)
{
    if (slot >= 0) {
	ReaderSlotAry[slot] = 0;
    }
}

void
sigHup(int SigNo)
{
    TerminatePending++;
}

void
sigUsr1(int SigNo)
{
    DumpThreads();
    signal(SIGUSR1, sigUsr1);
}

void
sigSegVReader(int sigNo)
{
    int i;

    if (TFd >= 0)
	close(TFd);
    for (i = 0; i < MaxFds; ++i)
	if (FD_ISSET(i, &RFds) || FD_ISSET(i, &WFds))
	    close(i);

    nice(20);
    if (DOpts.ReaderCrashHandler == NULL ||
			strcasecmp(DOpts.ReaderCrashHandler, "none") == 0) {
	/* Chew spare CPU cycles and try to be noticed? :-) */
	for (;;)
	    ;
    } else {
	char cmdbuf[256];

	if (fork())
	   return;
	snprintf(cmdbuf, sizeof(cmdbuf), "%s %d", DOpts.ReaderCrashHandler, getpid());
	system(cmdbuf);

	for (;;)
	    sleep(60);
    }
}

