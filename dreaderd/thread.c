
/*
 * DREADERD/THREAD.C - select() support for threads
 *
 *	Thread module used to support non-blocking multi-threaded I/O
 *	(see also mbuf.c)
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype ForkDesc *AddThread(const char *id, int fd, pid_t pid, int type, int slot, int pri);
Prototype ForkDesc *FindThread(int fd, pid_t pid);
Prototype ForkDesc *FindLeastUsedThread(int type, int maxCount, int minPri, int *prand, int nopass, int (*callBack)(void *cbData, void *data), void *cbData);
Prototype ForkDesc *FindThreadId(int type, const char *id);
Prototype void ScanThreads(int type, void (*func)(ForkDesc *desc));
Prototype void DelThread(ForkDesc *desc);
Prototype void ResetThreads(void);
Prototype void DumpThreads(void);
Prototype void AddTimer(ForkDesc *desc, int ms, int flags);
Prototype void DelTimer(Timer *t);
Prototype void NextTimeout(struct timeval *tv, int maxMs);
Prototype int ScanTimers(int doRun, int maxMs);

Prototype fd_set SFds;
Prototype fd_set RFds;
Prototype fd_set WFds;
Prototype int MaxFds;
Prototype struct timeval CurTime;

fd_set SFds;
fd_set RFds;
fd_set WFds;
int MaxFds = 0;
struct timeval CurTime;
Timer  TimerBase = { &TimerBase, &TimerBase };

ForkDesc *FDes[MAXFDS] = { NULL };
MemPool	 *TMemPool = NULL;

ForkDesc *
AddThread(const char *id, int fd, pid_t pid, int type, int slot, int pri)
{
    ForkDesc *desc;

    if (fd < 0 || fd >= MAXFDS || (desc = FDes[fd]) != NULL) {
	logit(LOG_CRIT, "AddThread: descriptor %d already in use", fd);
	exit(1);
    }
    desc = FDes[fd] = zalloc(&TMemPool, sizeof(ForkDesc));

#if NONBLK_ACCEPT_BROKEN
    if (type != THREAD_LISTEN) 	/* see lib/config.h */
	fcntl(fd, F_SETFL, O_NONBLOCK);
#else
    fcntl(fd, F_SETFL, O_NONBLOCK);
#endif

    desc->d_Id = zallocStr(&TMemPool, id);
    desc->d_Slot = slot;
    desc->d_Pid = pid;
    desc->d_Fd = fd;
    desc->d_Type = type;
    desc->d_FdPend = -1;
    desc->d_Pri = pri;
    desc->d_LocalSpool = NULL ;
#ifdef	DREADER_CLIENT_TIMING
    desc->d_utime = 0;
    desc->d_stime = 0;
#endif
    FD_SET(fd, &SFds);
    if (MaxFds <= fd)
	MaxFds = fd + 1;
    return(desc);
}

ForkDesc *
FindThread(int fd, pid_t pid)
{
    int i;

    if (fd >= 0)
	return(FDes[fd]);
    for (i = 0; i < MaxFds; ++i) {
	if (FDes[i] && FDes[i]->d_Pid == pid)
	    return(FDes[i]);
    }
    return(NULL);
}

/*
 * FindLeastUsedThread()
 *
 *	Locate the least-used thread
 */

ForkDesc *
FindLeastUsedThread(int type, int maxCount, int minPri, int *prand, int nopass, int (*callBack)(void *cbData, void *data), void *cbData)
{
    int i = -1;
    int j;
    ForkDesc *best = NULL;

    /*
     * 'random' start (actually sequential), if requested
     */

    if (prand) {
	i = *prand;
	if (i < 0 || i >= MaxFds)
	    i = -1;
    }

    if (nopass >= MaxFds)
	nopass = MaxFds - 1;

    /*
     * Scan through all available descriptors starting at position i.
     * Locate the best descriptor that meets our requirements.
     */

    for (j = 0; j < MaxFds; ++j) {
	ForkDesc *desc;

	i  = (i + 1) % MaxFds;

	/*
	 * nopass is used when scanning servers at the same priority, so we
	 * know when we've looped.
	 */

	if (i == nopass)
	    break;

	if ((desc = FDes[i]) != NULL && 
	    desc->d_Type == type &&
	    desc->d_Count < maxCount &&
	    (callBack == NULL || callBack(cbData, desc->d_Data)) &&
	    ((nopass < 0 && desc->d_Pri >= minPri) || desc->d_Pri == minPri)
	) {
	    if (best == NULL) {
		/*
		 * found at least one
		 */
		best = desc;
		if (prand != NULL)
		    *prand = i;
	    } else if (desc->d_Pri < best->d_Pri) {
		/*
		 * found one with a better (lower) priority
		 */
		best = desc;
		if (prand != NULL)
		    *prand = i;
	    } else if (desc->d_Pri == best->d_Pri) {
		/*
		 * found one with a better queue count
		 */
		if (desc->d_Pri == best->d_Pri &&
		    desc->d_Count < best->d_Count
		) {
		    best = desc;
		    if (prand != NULL)
			*prand = i;
		}
	    }
	}
    }
    return(best);
}

ForkDesc *
FindThreadId(int type, const char *id)
{
    int i;

    for (i = 0; i < MaxFds; ++i) {
	ForkDesc *desc;

	if ((desc = FDes[i]) != NULL && 
	    desc->d_Type == type &&
	    strcmp(id, desc->d_Id) == 0
	) {
	    return(desc);
	}
    }
    return(NULL);
}

void
DumpThreads(void)
{
    int i;

    printf("**** DUMP THREADS %d ****\n", (int)getpid());
    sleep(1);

    for (i = 0; i < MAXFDS; ++i) {
	ForkDesc *desc;

	if ((desc = FDes[i]) != NULL) {
	    if (i >= MaxFds)
		printf("DESC > MAXFDS (%d,%d)\n", i, MaxFds);
	    printf("DESC %s type=%d count=%d\n", desc->d_Id, desc->d_Type, desc->d_Count);
	}
    }
}

void
ScanThreads(int type, void (*func)(ForkDesc *desc))
{
    int i;

    for (i = 0; i < MaxFds; ++i) {
	ForkDesc *desc;

	if ((desc = FDes[i]) != NULL && desc->d_Type == type)
	    func(desc);
    }
}

void
DelThread(ForkDesc *desc)
{
    int fd = desc->d_Fd;

    if (desc->d_Timer) {
	DelTimer(desc->d_Timer);
	desc->d_Timer = NULL;	/* XXX not necessary */
    }

    if (fd >= 0) {
	FDes[fd] = NULL;
	close(fd);
	desc->d_Fd = -1;

	FD_CLR(fd, &SFds);
	FD_CLR(fd, &RFds);
	FD_CLR(fd, &WFds);

	if (fd + 1 == MaxFds) {
	    while (fd >= 0) {
		if (FDes[fd] != NULL)
		    break;
		--fd;
	    }
	    MaxFds = fd + 1;
	}
    }
    zfreeStr(&TMemPool, &desc->d_Id);
    zfree(&TMemPool, desc, sizeof(ForkDesc));
}

void
ResetThreads(void)
{
    int i;

    for (i = 0; i < MaxFds; ++i) {
	ForkDesc *desc;
	if ((desc = FDes[i]) != NULL) {
	    if (desc->d_Timer) {
		DelTimer(desc->d_Timer);
		desc->d_Timer = NULL;
	    }
	    if (desc->d_Fd >= 0) {
		close(desc->d_Fd);
		desc->d_Fd = -1;
	    }
	    if (desc->d_FdPend >= 0) {
		close(desc->d_FdPend);
		desc->d_FdPend = -1;
	    }
	}
    }
    FD_ZERO(&SFds);
    FD_ZERO(&RFds);
    FD_ZERO(&WFds);
    bzero(FDes, sizeof(FDes));
    freePool(&TMemPool);
    MaxFds = 0;
}

void
AddTimer(ForkDesc *desc, int ms, int flags)
{
    Timer *ti = zalloc(&TMemPool, sizeof(Timer));
    Timer *scan;

    if (desc->d_Timer) {
	DelTimer(desc->d_Timer);
    }

    ti->ti_To.tv_sec = ms / 1000;
    ti->ti_To.tv_usec = (ms - (ti->ti_To.tv_sec * 1000)) * 1000;
    ti->ti_Tv.tv_usec = CurTime.tv_usec + ti->ti_To.tv_usec;
    ti->ti_Tv.tv_sec = CurTime.tv_sec + ti->ti_To.tv_sec;
    if (ti->ti_Tv.tv_usec >= 1000000) {
	ti->ti_Tv.tv_usec -= 1000000;
	++ti->ti_Tv.tv_sec;
    }

    /*
     * Attach to descriptor, clear select bits
     * on descriptor for timer function.
     */

    desc->d_Timer = ti;
    ti->ti_Desc = desc;
    ti->ti_Flags = flags;

    if (desc->d_Fd >= 0) {
	if (flags & TIF_READ)
	    FD_CLR(desc->d_Fd, &RFds);
	if (flags & TIF_WRITE)
	    FD_CLR(desc->d_Fd, &WFds);
    }

    /*
     * find insert-before point
     */
    for (scan = TimerBase.ti_Next; scan != &TimerBase; scan = scan->ti_Next) {
	if (ti->ti_Tv.tv_sec < scan->ti_Tv.tv_sec ||
	    (ti->ti_Tv.tv_sec == scan->ti_Tv.tv_sec &&
	    ti->ti_Tv.tv_usec < scan->ti_Tv.tv_usec)
	) {
	    break;
	}
    }
    ti->ti_Next = scan;
    ti->ti_Prev = scan->ti_Prev;
    ti->ti_Prev->ti_Next = ti;
    ti->ti_Next->ti_Prev = ti;
}

void
DelTimer(Timer *t)
{
    t->ti_Next->ti_Prev = t->ti_Prev;
    t->ti_Prev->ti_Next = t->ti_Next;
    if (t->ti_Desc != NULL) {
	t->ti_Desc->d_Timer = NULL;
	t->ti_Desc = NULL;
    }
    zfree(&TMemPool, t, sizeof(Timer));
}

void
NextTimeout(struct timeval *tv, int maxMs)
{
    int nms = ScanTimers(0, maxMs);

    if (nms > maxMs)
	nms = maxMs;
    if (nms < 1)
	nms = 1;
    tv->tv_sec = nms / 1000;
    tv->tv_usec = (nms - (tv->tv_sec * 1000)) * 1000;
}

int
ScanTimers(int doRun, int maxMs)
{
    int dt = maxMs;
    Timer *scan;

    while ((scan = TimerBase.ti_Next) != &TimerBase) {
	if (scan->ti_Tv.tv_sec < CurTime.tv_sec ||
	    (scan->ti_Tv.tv_sec == CurTime.tv_sec &&
	    scan->ti_Tv.tv_usec <= CurTime.tv_usec)
	) {
	    if (doRun) {
		int fd;

		if ((fd = scan->ti_Desc->d_Fd) >= 0) {
		    if (scan->ti_Flags & TIF_READ)
			FD_SET(fd, &RFds);
		    if (scan->ti_Flags & TIF_WRITE)
			FD_SET(fd, &WFds);
		}
		DelTimer(scan);
		continue;
	    }
	    dt = 0;
	} else {
	    dt = (scan->ti_Tv.tv_sec - 1 - CurTime.tv_sec) * 1000 +
		(scan->ti_Tv.tv_usec + 1000000 - CurTime.tv_usec) / 1000;
	}
	break;
    }
    return(dt);
}

