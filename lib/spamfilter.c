
/*
 * LIB/SPAMFILTER.C	- Spam filtering.  
 *
 *	The spam filter is really simple.  The NNTP-Posting-Host: header
 *	is placed in a relatively large cache with a 32 bit 'hit' counter.
 *	The counter is incremented for each hit, and decremented once a
 *	minute.  If the counter goes above 4, the entry is locked.  If
 *	the counter goes above 16, the posting source is filtered by 
 *	adding the message-id to the history file prior to the article
 *	commit.  The cache entry is not unlocked until the counter 
 *	returns to 0.
 *
 *	NOTE: We cannot open FilterFd in InitSpamFilter() because this
 *	occurs prior to any server forks and will cause the fcntl locks
 *	to be shared across the forks.  This means that if a child 
 *	creates a lock and is then killed, the lock will NOT automatically
 *	be removed.  Thus, we open FilterFd on the first call to SpamFilter()
 *	rather then in InitSpamFilter().
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

/*
 * TODO
 *
 */
#include "defs.h"

/*
 * Disable the SHM hash array for now. The mmap'ed version should
 * be fast enough and if we can get away from shared memory problems,
 * the world will be a better place.
 */
#undef	USE_SPAM_SHM

Prototype void SetSpamFilterTrip(int body, int nph);
Prototype void SetSpamFilterOpt(void);
Prototype void InitSpamFilter(void);
Prototype void TermSpamFilter(void);
Prototype int SpamFilter(time_t t, SpamInfo *spamInfo, int *phow);
Prototype void ClearSpamFilterEntry(int which, int entry);
Prototype void DumpSpamFilterCache(FILE *fo, int raw);
Prototype int BodyFilterFd;
Prototype int NphFilterFd;

#define F_B_HSIZE	65536
#define F_B_HMASK	(F_B_HSIZE - 1)
#define F_B_EXPIRE	(60 * 60)	/* one hour expire */
#define F_P_HSIZE	65536
#define F_P_HMASK	(F_B_HSIZE - 1)
#define F_P_EXPIRE	(60 * 60)	/* one hour expire */

typedef struct Filter {
    md5hash_t	f_Hash;		/* hash to check/store			*/
    hash_t	f_MHash;	/* message-id hash			*/
    int		f_Lines;	/* Line count				*/
    time_t	f_Time;		/* time demark				*/
    int32	f_HitCount;	/* incremented per hit, decremented per min */
    int32	f_FilterCount;	/* filtered postings			*/
} Filter;

typedef struct FilterInfo {
    Filter	*HashAry;	/* Shared memory hash			*/
    int		Fd;		/* fd of the disk image of hash		*/
    int		Trip;		/* spam after this many hits		*/
    int		Hsize;		/* number of entries in the memory hash	*/
    int		Hmask;		/* entry mask				*/
    int		Expire;		/* how long (in seconds) entries last	*/
} FilterInfo;

FilterInfo	BodyFilter = { NULL, -1, 0, F_B_HSIZE, F_B_HMASK, F_P_EXPIRE };
FilterInfo	NphFilter = { NULL, -1, 0, F_B_HSIZE, F_B_HMASK, F_P_EXPIRE };
int		BodyFilterFd = -1;
int		NphFilterFd = -1;
int		FilterLock = 4;
int		FilterMax  = 100;

void initSpamData(FilterInfo *f, const char *fname, int *fd);
void termSpamData(FilterInfo *filter);
int openFilterFile(FilterInfo *f, const char *fname);
int spamFilterTable(time_t t, FilterInfo *f, hash_t mhv, md5hash_t *hv, int lines);
void dumpSpamFilterMem(FILE *fo, FilterInfo *fi, char *stype, int raw);

void
SetSpamFilterTrip(int body, int nph)
{
    if (body >= 0)
	BodyFilter.Trip = body;
    if (nph >= 0)
	NphFilter.Trip = nph;
}

void
SetSpamFilterOpt(void)
{
    if (DOpts.SpamFilterOpt != NULL) {
	char *ptr = DOpts.SpamFilterOpt;
	char *oldopts = DOpts.SpamFilterOpt;
	int n;
	int enabled = 0;
	int nntpPostDisabled = 0;
	int ftype = 0;

	TermSpamFilter();
	DOpts.SpamFilterOpt = NULL;
	while (*ptr) {
	    switch (*ptr) {
		case ' ':
		    ++ptr;
		    ftype = 0;
		    break;
		case 'B':
		    ++ptr;
		    if (!isdigit((int)*ptr)) {
			logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			free(oldopts);
			return;
		    }
		    n = strtol(ptr, NULL, 0);
		    SetSpamFilterTrip(n, -1);
		    while (isdigit((int)*ptr))
			++ptr;
		    if (n > 0)
			enabled = 1;
		    ftype = 1;
		    break;
		case 'D':
		    ++ptr;
		    SetSpamFilterTrip(-1, 0);
		    nntpPostDisabled = 1;
		    ftype = 0;
		    break;
		case 'e':
		    ++ptr;
		    if (!enabled || !isdigit((int)*ptr)) {
			logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			free(oldopts);
			return;
		    }
		    n = strtol(ptr, NULL, 0);
		    switch (ftype) {
			case 1:
			    BodyFilter.Expire = n;
			    break;
			case 2:
			    NphFilter.Expire = n;
			    break;
			default:
			    logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			free(oldopts);
			    return;
		    }
		    while (isdigit((int)*ptr))
			++ptr;
		    break;
		case 'N':
		    ++ptr;
		    if (!isdigit((int)*ptr)) {
			logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			free(oldopts);
			return;
		    }
		    n = strtol(ptr, NULL, 0);
		    SetSpamFilterTrip(-1, n);
		    while (isdigit((int)*ptr))
			++ptr;
		    if (n > 0)
			enabled = 1;
		    ftype = 2;
		    break;
		case 's':
		    ++ptr;
		    if (!enabled || !isdigit((int)*ptr)) {
			logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			free(oldopts);
			return;
		    }
		    n = strtol(ptr, NULL, 0);
		    if ((n ^ (n - 1)) != (n << 1) - 1) {
			logit(LOG_ERR, "spam hash size option (%d) not a power of 2\n",
							n);
			free(oldopts);
			return;
		    }
		    switch (ftype) {
			case 1:
			    if (BodyFilter.HashAry != NULL)
				break;
			    BodyFilter.Hsize = n;
			    BodyFilter.Hmask = n - 1;
			    break;
			case 2:
			    if (NphFilter.HashAry != NULL)
				break;
			    NphFilter.Hsize = n;
			    NphFilter.Hmask = n - 1;
			    break;
			default:
			    logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
			    free(oldopts);
			    return;
		    }
		    while (isdigit((int)*ptr))
			++ptr;
		    break;
		default:
		    logit(LOG_ERR, "Invalid spam option: %s\n", oldopts);
		    free(oldopts);
		    return;
	    }
	}
	if (enabled) {
	    DOpts.SpamFilterOpt = oldopts;
	} else {
	    free(oldopts);
	}
    }

}

/*
 * InitSpamFilter() - called by master diablo server to initialize the 
 *		      shared memory segment for the spam filter.
 *
 *		      we allocate, map, then remove the shared memory id
 *		      so it is not persistant after the last diablo process
 *		      goes away.  This is necessary because there is really
 *		      no way to reserve a permanent id without possibly 
 *		      stomping on someone else in the system using shared
 *		      memory.
 *
 *		      NOTE: we can open FilterFd here even though fork() will
 *		      share the lseek position because we do not use lseek
 *		      if USE_SPAM_SHM is set.  We also read any preexisting
 *		      spam cache into the shared memory segment.
 */

int
openFilterFile(FilterInfo *fi, const char *fname)
{
    struct stat st;

    if ((fi->Fd = open(PatDbExpand(fname), O_RDWR|O_CREAT, 0644)) < 0)
	logit(LOG_ERR, "Unable open spam cache file: %s %s)\n",
					PatDbExpand(fname), strerror(errno));

    if (fi->HashAry == NULL && fi->Fd >= 0 && fstat(fi->Fd, &st) == 0) {
	if (st.st_size != fi->Hsize * sizeof(Filter)) {
	    int i;
	    Filter f = {{ 0 }};
	    ftruncate(fi->Fd, 0);
	    for (i = 0; i < fi->Hsize; i++)
		write(fi->Fd, &f, sizeof(f));
	    lseek(fi->Fd, 0, SEEK_SET);
	}
	fi->HashAry = xmap(
		NULL, 
		fi->Hsize * sizeof(Filter),
		PROT_READ | (USE_SPAM_RW_MAP * PROT_WRITE),
		MAP_SHARED,
		fi->Fd,
		0
	    );
    }
    if (fi->HashAry == NULL && fi->Fd >= 0) {
	close(fi->Fd);
	fi->Fd = -1;
    }
    return(fi->Fd);
}

void
initSpamData(FilterInfo *f, const char *fname, int *fd)
{
#if USE_SPAM_SHM
    int sid;
    struct shmid_ds ds;
#endif

    if (f->HashAry != NULL)
	return;
#if USE_SPAM_SHM
    sid = shmget(IPC_PRIVATE, f->Hsize * sizeof(Filter), SHM_R|SHM_W);

    if (sid < 0) {
        logit(LOG_ERR, "sysv shared memory alloc failed, is your machine configured with a high enough maximum segment size?");
        exit(1);
    } else if (DebugOpt > 1) {
	printf("Allocated spam shm segment\n");
    }

    f->HashAry = (Filter *)shmat(sid, NULL, SHM_R|SHM_W);

    if (shmctl(sid, IPC_STAT, &ds) < 0 || shmctl(sid, IPC_RMID, &ds) < 0) {
        logit(LOG_ERR, "sysv shmctl stat/rmid failed");
        exit(1);
    }

    if (f->HashAry == (Filter *)-1) {
        f->HashAry = NULL;
        logit(LOG_ERR, "sysv shared memory map failed");
        exit(1);
    }
    bzero(f->HashAry, f->Hsize * sizeof(Filter));

    if ((f->Fd = open(PatDbExpand(fname), O_RDWR, 0644)) >= 0) {
	struct stat sb;
	if (fstat(f->Fd, &sb) == 0 && sb.st_size != f->Hsize * sizeof(Filter))
	    ftruncate(f->Fd, 0);
	else
	    read(f->Fd, f->HashAry, f->Hsize * sizeof(Filter));
	*fd = f->Fd;
    } else {
	logit(LOG_ERR, "Unable to open %s (%s)", PatDbExpand(fname),
							strerror(errno));
	termSpamData(f);
	*fd = -1;
    }
#else
    if (*fd == -1)
	*fd = openFilterFile(f, fname);
#endif
}

void
InitSpamFilter(void)
{
    if (BodyFilter.Trip) {
	initSpamData(&BodyFilter, SpamBodyCachePat, &BodyFilterFd);
	logit(LOG_INFO, "Initialiased internal (body) spamfilter (trip=%d size=%d expire=%d)", 
			BodyFilter.Trip, BodyFilter.Hsize, BodyFilter.Expire);
    }
    if (NphFilter.Trip) {
	initSpamData(&NphFilter, SpamNphCachePat, &NphFilterFd);
	logit(LOG_INFO, "Initialiased internal (nph) spamfilter (trip=%d size=%d expire=%d)", 
			NphFilter.Trip, NphFilter.Hsize, NphFilter.Expire);
    }
}

void
termSpamData(FilterInfo *fi)
{
#if USE_SPAM_SHM
    if (fi->Fd >= 0 && fi->HashAry != NULL) {
	lseek(fi->Fd, 0L, 0);
	write(fi->Fd, fi->HashAry, fi->Hsize * sizeof(Filter));
	ftruncate(fi->Fd, fi->Hsize * sizeof(Filter));
    }
    if (fi->HashAry != NULL) {
	if (shmdt((void *)fi->HashAry) != 0)
	    logit(LOG_ERR, "shmdt error: %s\n", strerror(errno));
	fi->HashAry = NULL;
    }
    if (fi->Fd >= 0) {
	close(fi->Fd);
	fi->Fd = -1;
    }
#else
    if (fi->Fd >= 0) {
	if (fi->HashAry != NULL)
	    xunmap(fi->HashAry, fi->Hsize * sizeof(Filter));
	close(fi->Fd);
	fi->Fd = -1;
	fi->HashAry = NULL;
    }
#endif
}

void
TermSpamFilter(void)
{
    int done = 0;

    if (BodyFilter.Fd != -1 || BodyFilter.HashAry != NULL) {
	termSpamData(&BodyFilter);
	done = 1;
    }
    if (NphFilter.Fd != -1 || NphFilter.HashAry != NULL) {
	termSpamData(&NphFilter);
	done = 1;
    }
    if (done)
	logit(LOG_INFO, "Terminated internal spamfilter");
}

/*
 * SpamFilter() - run spam filter on message-id hash, optional
 * nntpPostingHost.  If nntpPostingHost is not provided, it must
 * be "".  This filter will rate-filter based on nntpPostingHost
 */

int
SpamFilter(time_t t, SpamInfo *spamInfo, int *phow)
{
    int r = 0;

    *phow = 0;

    if (spamInfo == NULL)
	return(r);

    if (NphFilter.Trip && NphFilter.HashAry != NULL &&
				spamInfo->PostingHost != NULL && r == 0) {
	r = spamFilterTable(t, &NphFilter, spamInfo->MsgIdHash,
				&spamInfo->PostingHostHash, spamInfo->Lines);
	if (r != 0)
	    *phow = 1;
    }

    if (BodyFilter.Trip && BodyFilter.HashAry != NULL && r == 0) {
	r = spamFilterTable(t, &BodyFilter, spamInfo->MsgIdHash,
				&spamInfo->BodyHash,
				spamInfo->Lines);
	if (r != 0)
	    *phow = 2;
    }
    return(r);
}

/*
 * Execute spam filter on hash code
 */

int
spamFilterTable(time_t t, FilterInfo *fi, hash_t mhv, md5hash_t *hv, int lines)
{
    int r = 0;
    int i = (hv->h1 ^ hv->h2) & fi->Hmask;	/* hash index */
    int off = i * sizeof(Filter);	/* map offset */
    Filter *f = &fi->HashAry[i];		/* structural pointer	*/
    time_t t0;
    int dhits = 0;
    int isdup = 1;

    hflock(fi->Fd, off, XLOCK_EX);

    t0 = f->f_Time;

    /*
     * calculate delta hits
     */
    {
	int32 dt = (int)(t - t0);

	if (t0 == 0 || dt < -10 || dt > fi->Expire) {
	    /*
	     * Slot is garbaged or long-expired, reset
	     * it.  Set dhits to force override.  Reset
	     * t0.
	     */
	    dhits = -f->f_HitCount;
	    t0 = t;
	    isdup = 0;
	} else {
	    /*
	     * Slot ok (but may or may not match hash code).
	     *
	     * calculate per-minute rate, adjust dhits as if hash
	     * code were ok.
	     */
	    while (dt >= 60 && f->f_HitCount + dhits > 0) {
		--dhits;
		t0 += 60;
		dt -= 60;
	    }
	    /*
	     * Check for duplicate message-id.  If not a duplicate,
	     * enable write-back and bump dhits.
	     */
	    if (f->f_MHash.h1 != mhv.h1 || f->f_MHash.h2 != mhv.h2) {
		++dhits;
		isdup = 0;
	    }
	}
    }

    /*
     * cache hit / miss
     */

    if (f->f_Hash.h1 == hv->h1 &&
	f->f_Hash.h2 == hv->h2 &&
	(lines == 0 || (f->f_Lines == lines))
    ) {
	/*
	 * same-slot, valid.
	 */
	Filter copy;

	copy = *f;

	copy.f_Time = t0;
	copy.f_HitCount += dhits;
	copy.f_MHash = mhv;

	if (copy.f_HitCount <= 0) {	/* handle garbage  */
	    copy.f_HitCount = 0;
	    copy.f_Time = t;
	}
	if (copy.f_HitCount >= FilterMax)	/* handle garbage  */
	    copy.f_HitCount = FilterMax;

	if (copy.f_HitCount >= fi->Trip) {
	    if (isdup == 0)
		++copy.f_FilterCount;
	    r = -copy.f_FilterCount;
	    if (r >= 0)		/* make sure r is negative */
		r = -1;
	}
	if (isdup == 0) {
#if USE_SPAM_RW_MAP
	    *f = copy;
#else
	    lseek(fd, off, 0);		/* seek & lock	*/
	    write(fd, &copy, sizeof(Filter));
#endif
	}
    } else if (f->f_HitCount + dhits < FilterLock) {
	/*
	 * reset slot
	 */
	Filter copy = { { 0 } };

	copy.f_Hash = *hv;
	copy.f_MHash = mhv;
	copy.f_Time = t;
	copy.f_HitCount = 1;
	copy.f_FilterCount = 0;
	copy.f_Lines = lines;

#if USE_SPAM_RW_MAP
	*f = copy;
#else
	lseek(fd, off, 0);		/* seek & lock	*/
	write(fd, &copy, sizeof(Filter));
#endif
    } else {
	logit(LOG_INFO, "SpamFilter, slot %d in use: dt=%d, %d + %d\n",
	    i, 
	    (int)(t - f->f_Time),
	    f->f_HitCount, 
	    dhits
	);
    }
    hflock(fi->Fd, off, XLOCK_UN);

    return(r);
}

void
ClearSpamFilterEntry(int which, int entry)
{
    FilterInfo *f;

    switch (which) {
	case 1:
		f = &BodyFilter;
		if (entry >= f->Hsize)
		    return;
		break;
	case 2:
		f = &NphFilter;
		if (entry >= f->Hsize)
		    return;
		break;
	default:
		return;
    }
    if (entry < 0)
	bzero(f->HashAry, f->Hsize * sizeof(Filter));
    else
	bzero(&f->HashAry[entry], sizeof(Filter));
}

void
dumpSpamFilterMem(FILE *fo, FilterInfo *fi, char *stype, int raw)
{
    time_t t = time(NULL);
    char buf[64];

    if (fi->HashAry != NULL) {
	int i;
	int anyinfo = 0;

	for (i = 0; i < fi->Hsize; ++i) {
	    Filter *f = &fi->HashAry[i];
	    int32 dt = t - f->f_Time;
	    int32 odt = dt;
	    int hits = f->f_HitCount;

	    if (f->f_Time == 0)
		continue;
	    if (!raw && dt >= fi->Expire)
		continue;
	    while (hits > 0 && dt >= 60) {
		--hits;
		dt -= 60;
	    }
	    if (raw || hits >= 0) {
		if (!anyinfo)
		    fprintf(fo, "Internal spamfilter %s hits\n", stype);
		    fprintf(fo, "%5s %16s %-15s %8s %-8s   %5s %5s %5s %5s\n",
				"entry",
				stype,
				"hash",
				"msgid",
				"hash",
				"lines",
				"dtime",
				"hits",
				"filtered"
		    );
		anyinfo = 1;
		fprintf(fo, "%05x %32s %08x.%08x   %5d %5d %5d %5d\n",
				i,
				md5hashstr(&f->f_Hash, buf),
				(int)f->f_MHash.h1,
				(int)f->f_MHash.h2,
				(int)f->f_Lines,
				(int)odt,
				(int)hits,
				(int)f->f_FilterCount
		);
	    }
	}
	if (!anyinfo)
	    fprintf(fo, "No spam %s hits\n", stype);
	fprintf(fo, "-------------------------------------------------\n");
    }
}

void
DumpSpamFilterCache(FILE *fo, int raw)
{
    int dummyHow;

    SpamFilter(time(NULL), NULL, &dummyHow);

    dumpSpamFilterMem(fo, &BodyFilter, "body", raw);
    dumpSpamFilterMem(fo, &NphFilter, "nph", raw);
}

