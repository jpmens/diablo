
/*
 * LIB/HISTORY.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Generally speaking, the history mechanism can be pictured from the
 * point of view of a reader or from the point of view of a writer.  From
 * the point of view of a reader, A base table lookup begins a hash chain
 * of history records that runs backwards through the history file.  More
 * recent entries are encountered first, resulting in a certain degree of
 * locality of reference based on age.
 *
 * A writer must scan the chain to ensure that the message-id does not
 * already exist.  The new history record is appended to the history file
 * and inserted at the base of the hash chain.  The file append requires
 * an exclusive lock to ensure atomic operation (O_APPEND is often not atomic
 * on heavily loaded systems, the exclusive lock is required).
 *
 * In a heavily loaded system, the exclusive lock and append may become a
 * bottleneck.
 *
 * WARNING!  offsets stored in history records / hash table index are signed
 * 32 bits but cast to unsigned in any lseek() operations.  The history file
 * is thus currently limited to 4GB even with 64 bit capable filesystems.
 * 2GB on linux, 4GB under FreeBSD (which is 64 bit capable).
 */

#include "defs.h"

Prototype int HistoryOpen(const char *fileName, int fastMode);
Prototype int HistoryClose(void);
Prototype int HistoryLookup(const char *msgid, History *h);
Prototype int HistoryLookupByHash(hash_t hv, History *h);
Prototype HistIndex HistoryPosLookupByHash(hash_t hv, History *h);
Prototype int HistoryAdd(const char *msgid, History *h);
Prototype int HistoryStore(History *h);
Prototype void HistoryStoreExp(History *h, HistIndex index);
Prototype int HistoryExpire(const char *msgid, History *h, int unexp);
Prototype void PrintHistory(History *h);

Prototype uint32 NewHSize;

#define HBLKINCR	16
#define HBLKSIZE	256

HistHead	HHead;
HistHead	*HHeadMap = NULL;
HistIndex 	*HAry;
int		HFd = -1;
int		LoggedDHistCorrupt;
int		HFlags;
uint32		HSize;
uint32		HMask;
off_t		HEntryOff;		/* Start of actual history records */
uint32		NewHSize = 0;
int		HBlkIncr = HBLKINCR;
int		HBlkGood;
char		HistoryFileName[PATH_MAX];
int		DoingReOpen = 0;

int
HistoryOpen(const char *fileName, int hflags)
{
    int fd;
    struct stat st;
    int openflags;

    if (fileName == NULL)
	fileName = strdup(PatDbExpand(DHistoryPat));

    strcpy(HistoryFileName, fileName);

    HFlags = hflags;

    if (NewHSize == 0)
	NewHSize = DOpts.HashSize;	/* which may also be 0 */

    /*
     * open the history file
     */

    if (HFlags & HGF_READONLY)
	openflags = O_RDONLY;
    else
	openflags = O_RDWR|O_CREAT;

    if (HFlags & HGF_EXCHECK && stat(fileName, &st) == 0) {
	fprintf(stderr, "%s in fast mode already exists - exiting\n",
						fileName);
	exit(1);
    }
    fd = open(fileName, openflags, 0644);

    if (fd < 0) {
	if (DoingReOpen)
	    return(-2);
	logit(LOG_ERR, "open %s failed (%s)", fileName, 
							strerror(errno));
	fprintf(stderr, "open %s failed (%s)", fileName,
							strerror(errno));
	exit(1);
    }

    if (fstat(fd, &st) < 0) {
	logit(LOG_ERR, "fstat %s failed", fileName);
	exit(1);
    }

    /*
     * initial history file creation, if necessary
     */

    bzero(&HHead, sizeof(HHead));

    if (st.st_size == 0 || 
	read(fd, &HHead, sizeof(HHead)) != sizeof(HHead) ||
	HHead.hmagic != HMAGIC
    ) {
	/*
	 * Is this history an old one and we need to wait for a new one?
	 */
	if (DoingReOpen && HHead.hmagic == HDEADMAGIC) {
	    close(fd);
	    return(-2);
	}

	/*
	 * lock after finding the history file to be invalid and recheck.
	 */

	hflock(fd, 0, XLOCK_EX);

	fstat(fd, &st);
	lseek(fd, 0L, 0);

	if (st.st_size == 0 || 
	    read(fd, &HHead, sizeof(HHead)) != sizeof(HHead) ||
	    HHead.hmagic != HMAGIC
	) {
	    uint32 n;
	    uint32 b;
	    char *z = calloc(8192, 1);

	    /*
	     * check for old version of history file
	     */

	    if (st.st_size) {
		logit(LOG_ERR, "Incompatible history file version or corrupted history file\n");
		exit(1);
	    }

	    if (HFlags & HGF_READONLY) {
		logit(LOG_ERR, "Unable to create %s - read-only mode\n",
							fileName);
		fprintf(stderr, "Unable to create %s - read-only mode\n",
							fileName);
		exit(1);
	    }

	    /*
	     * create new history file
	     */

	    logit(LOG_INFO, "Creating history file");

	    lseek(fd, 0L, 0);
	    ftruncate(fd, 0);
	    bzero(&HHead, sizeof(HHead));

	    HHead.hashSize = NewHSize;
	    HHead.version  = HVERSION;
	    HHead.henSize  = sizeof(History);
	    HHead.headSize = sizeof(HHead);

	    write(fd, &HHead, sizeof(HHead));

	    /*
	     * write out the hash table
	     */

	    n = 0;
	    b = HHead.hashSize * sizeof(HistIndex);

		printf("n=%u  b=%u HHead.hashSize=%u\n", n, b, HHead.hashSize);
	    while (n < b) {
		uint32 r = (b - n > 8192) ? 8192 : b - n;

		write(fd, z, r);
		n += r;
	    }
	    /*
	     * Write out a dummy history entry so we don't use zero offset
	     */
	    if (HHead.version > 1) {
		History h = { 0 };
		write(fd, &h, sizeof(h));
	    }

	    fsync(fd);

	    /*
	     * rewrite header with magic number
	     */

	    lseek(fd, 0L, 0);
	    HHead.hmagic = HMAGIC;
	    write(fd, &HHead, sizeof(HHead));

	    free(z);
	    logit(LOG_INFO, "History file creation complete");
	}

	hflock(fd, 0, XLOCK_UN);
    }

    if (HHead.version < 1) {
	logit(LOG_ERR, "DHistory version %d, expected %d, use the biweekly adm script to regenerate the history file",
	    HHead.version,
	    HVERSION
	);
	fprintf(stderr, "DHistory version %d, expected %d, use the biweekly adm script to regenerate the history file\n",
	    HHead.version,
	    HVERSION
	);
	exit(1);
    }
    if (HHead.version > HVERSION) {
	logit(LOG_ERR, "DHistory version %d, expected %d, YOU ARE RUNNING AN OLD DIABLO ON A NEW HISTORY FILE!",
	    HHead.version,
	    HVERSION
	);
	exit(1);
    }
    if (HHead.hmagic != HMAGIC) {
	logit(LOG_NOTICE, "DHistory magic mismatch");
	fprintf(stderr, "DHistory magic mismatch\n");
    }

    /*
     * Map the history header so that we can check it quickly
     */

    HHeadMap = xmap(NULL, HHead.headSize, PROT_READ, MAP_SHARED, fd, 0);
    if (HHeadMap == 0) {
	if (fd >= 0)
	    close(fd);
	logit(LOG_CRIT, "dhistory header mmap error: %s", strerror(errno));
	exit(1);
    }

    /*
     * Map history file
     */

    HSize = HHead.hashSize;
    HMask = HSize - 1;

    /*
     * In FAST mode we leave the history file locked in order to
     * cache the hash table array at the beginning, which in turn
     * allows us to essentially append new entries to the end of
     * the file without having to seek back and forth updating
     * the hash table.
     *
     * When we aren't in FAST mode, we memory-map the hash table
     * portion of the history file.
     */

    if (HFlags & HGF_FAST) {
	if (HFlags & HGF_READONLY) {
	    fprintf(stderr, "Ignoring fast mode for readonly history open\n");
	} else if (hflock(fd, 0, XLOCK_EX|XLOCK_NB) == -1) {
	    fprintf(stderr, "Unable to lock history file in fast mode (%s)\n",
							strerror(errno));
	    exit(1);
	}
    }

    if (HFlags & HGF_FAST) {
	HAry = calloc(HSize, sizeof(HistIndex));
	if (HAry == NULL) {
	    perror("calloc");
	    exit(1);
	}
	lseek(fd, HHead.headSize, 0);
	if (read(fd, HAry, (size_t)HSize * sizeof(HistIndex)) != (size_t)HSize * sizeof(HistIndex)) {
	    perror("read");
	    exit(1);
	}
    } else {
	int mapflags = PROT_READ;

	if ((HFlags & HGF_READONLY) == 0)
	    mapflags |= PROT_WRITE;
	HAry = xmap(NULL, (size_t)HSize * sizeof(HistIndex), mapflags, MAP_SHARED, fd, HHead.headSize);
	if (HFlags & HGF_MLOCK)
	    mlock(HAry, HSize * sizeof(HistIndex));
    }

    if (HAry == NULL || HAry == (HistIndex *)-1) {
	if (fd >= 0)
	    close(fd);
	logit(LOG_CRIT, "dhistory mmap error: %s", strerror(errno));
	exit(1);
    }
    HFd = fd;
    HEntryOff = HHead.headSize + HHead.hashSize * sizeof(HistIndex);
    return(0);
}

/*
 * On close, we have to commit the hash table if we were in
 * FAST mode, otherwise we need only unmap the file before
 * closing it.
 */

int
HistoryClose(void)
{
    int r = RCOK;

    if (HFd >= 0 && !(HFlags & HGF_READONLY)) {
	if (HFlags & HGF_FAST) {
	    lseek(HFd, HHead.headSize, 0);
	    if (write(HFd, HAry, HSize * sizeof(HistIndex)) != HSize * sizeof(HistIndex)) {
		r = RCTRYAGAIN;
	    } else {
		free(HAry);
		HAry = NULL;
	    }
	} else {
	    if (HAry && HAry != (HistIndex *)-1) {
		if (HFlags & HGF_MLOCK)
		    munlock(HAry, HSize * sizeof(HistIndex));
		xunmap((void *)HAry, HSize * sizeof(HistIndex));
		HAry = NULL;
	    }
	}
    }
    if (r == RCOK) {
	if (HAry && HAry != (HistIndex *)-1) {
	    if (HFlags & HGF_MLOCK)
		munlock(HAry, HSize * sizeof(HistIndex));
	    xunmap((void *)HAry, HSize * sizeof(HistIndex));
	    HAry = NULL;
	}
	if (HHeadMap != NULL) {
	    xunmap((void *)HHeadMap, HHead.headSize);
	    HHeadMap = NULL;
	}
	if (HFd >= 0) {
	    if (HFlags & HGF_FAST)
		hflock(HFd, 0, XLOCK_UN);
	    close(HFd);
	}
	HFd = -1;
    }
    return(r);
}

void
historyReOpen(void)
{
    int count = 0;

    DoingReOpen = 1;
    HistoryClose();
    while (HistoryOpen(HistoryFileName, HFlags) == -2 && count++ < 360)
	sleep(1);
    if (count++ >= 360) {
	fprintf(stderr, "Unable to reopen history file due to old magic\n");
	logit(LOG_ERR, "Unable to reopen history file due to old magic");
	exit(1);
    }
    DoingReOpen = 0;
}

void
PrintHistory(History *h)
{
    printf(" [hv=%08x.%08x gm=%d ex=%d off=%d len=%d F=%s]\n",
		h->hv.h1,
		h->hv.h2,
		(int)h->gmt,
		(int)h->exp,
		(int)h->boffset,
		(int)h->bsize,
		((h->exp & EXPF_HEADONLY) ? "H" : "")
    );
}

int
HistoryLookup(const char *msgid, History *nh)
{
    hash_t hv;
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    History h = { 0 };
    static int HLAlt = 0;
    int r = -1;
    int counter = 0;
    int statfailed = 0;

    if (HHeadMap->hmagic != HMAGIC)
	historyReOpen();

    hv = hhash(msgid);
    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	lseek(HFd, off, 0);
	if (read(HFd, &h, sizeof(h)) != sizeof(h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup @ %d->%d chain %d  offset %ld  msgid %s  counter=%d",
					pindex, index,
					(int)((hv.h1 ^ hv.h2) & HMask),
					off, msgid, counter);
		sleep(1);
	    }
	    break;
	}
	if (h.hv.h1 == hv.h1 && h.hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h.next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d (%s)",
					(int)pindex, (int)index,
					(int)((hv.h1 ^ hv.h2) & HMask),
					msgid);
	    index = 0;
	}
    }
    if (index != 0)
	r = 0;
    /*
     * On failure, try alternate hash method (for lookup only)
     */
    if (r < 0 && DOpts.CompatHashMethod >= 0 && HLAlt == 0 && !statfailed) {
	int save = DOpts.HashMethod;

	DOpts.HashMethod = DOpts.CompatHashMethod;
	HLAlt = 1;
	r = HistoryLookup(msgid, nh);
	DOpts.HashMethod = save;
	HLAlt = 0;
    }

    if (r == 0 && nh != NULL)
	memcpy(nh, &h, sizeof(h));

    return(r);
}

int
HistoryLookupByHash(hash_t hv, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    int counter = 0;

    if (HHeadMap->hmagic != HMAGIC)
	historyReOpen();

    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	lseek(HFd, off, 0);
	if (read(HFd, h, sizeof(*h)) != sizeof(*h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup");
		sleep(1);
	    }
	    break;
	}
	if (h->hv.h1 == hv.h1 && h->hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h->next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d",
			(int)pindex, (int)index, (int)((hv.h1 ^ hv.h2) & HMask));
	    index = 0;
	}
    }
    if (index != 0) {
	return(0);
    }
    return(-1);
}

HistIndex
HistoryPosLookupByHash(hash_t hv, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    int counter = 0;

    hi = (hv.h1 ^ hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    index = HAry[hi];

    while (index) {
	if (HHead.version > 1)
	    off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	else 
	    off = index;
	lseek(HFd, off, 0);
	if (read(HFd, h, sizeof(*h)) != sizeof(*h)) {
	    if ((LoggedDHistCorrupt & 1) == 0 || DebugOpt) {
		LoggedDHistCorrupt |= 1;
		logit(LOG_ERR, "dhistory file corrupted on lookup");
		sleep(1);
	    }
	    break;
	}
	if (h->hv.h1 == hv.h1 && h->hv.h2 == hv.h2)
	    break;
	pindex = index;
	index = h->next;
	if (counter++ > 5000) {
	    logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d",
			(int)pindex, (int)index, (int)((hv.h1 ^ hv.h2) & HMask));
	    index = 0;
	}
    }
    if (index != 0) {
	return(index);
    }
    return(-1);
}

/*
 * This is a new and simplifed HistoryAdd that locks an append lock
 * (pos 4) and just appends the single history record and unlocks.
 * It then updates the hash chain (already locked) by adding the
 * new history entry at the beginning as usual.
 */
int
HistoryAdd(const char *msgid, History *h)
{
    HistIndex hi;
    HistIndex pindex;
    HistIndex index;
    off_t off;
    off_t chainlock;
    int r = RCOK;

    if (HHeadMap->hmagic != HMAGIC)
	historyReOpen();

    /*
     * This should not happen, but if it does, log it and pretend we did it
     */
    if (h->gmt == 0) {
	logit(LOG_ERR, "Not adding history entry with gmt=0");
	return(RCOK);
    }
    /*
     * record lock, search for message id
     *
     */

    hi = (h->hv.h1 ^ h->hv.h2) & HMask;
    pindex = HHead.headSize + hi * sizeof(HistIndex);
    chainlock = (off_t)pindex;

    if ((HFlags & HGF_FAST) == 0)	/* lock hash chain */
	hflock(HFd, chainlock, XLOCK_EX);

    /*
     * make sure message-id is not already in hash table
     */
    if ((HFlags & HGF_NOSEARCH) == 0) {
	int counter = 0;

	index = HAry[hi];
	while (index) {
	    static History ht;

	    if (HHead.version > 1)
		off = (off_t)HEntryOff + (off_t)index * sizeof(History);
	    else 
		off = index;
	    lseek(HFd, off, 0);
	    if (read(HFd, &ht, sizeof(ht)) != sizeof(ht)) {
		if ((LoggedDHistCorrupt & 2) == 0 || DebugOpt) {
		    LoggedDHistCorrupt |= 2;
		    logit(LOG_ERR, "dhistory file corrupted @ %u (%s)",
						index, strerror(errno));
		    sleep(1);
		}
		break;
	    }
	    if (ht.hv.h1 == h->hv.h1 && ht.hv.h2 == h->hv.h2) {
		r = RCALREADY;
		break;
	    }
	    pindex = index;
	    index = ht.next;
	    if (counter++ > 5000) {
		logit(LOG_ERR, "dhistory file chain loop @ %d->%d chain %d (%s)",
					(int)pindex, (int)index,
					(int)((ht.hv.h1 ^ ht.hv.h2) & HMask),
					msgid ? msgid : "no message-id");
		index = 0;
	    }
	}
    }

    while (r == RCOK) {
	off_t writePos;
	int n = 0;

	h->next = HAry[hi];

	if ((HFlags & HGF_FAST) == 0)	/* append/scan lock */
	    hflock(HFd, 4, XLOCK_EX);

	if ((writePos = lseek(HFd, 0L, 2)) == -1) {
	    r = RCTRYAGAIN;
	    break;
	}

	n = write(HFd, h, sizeof(History));

	if (n != sizeof(History)) {
	    logit(LOG_ERR, "Error writing to history: %s", strerror(errno));
	    lseek(HFd, writePos, 0);
	    ftruncate(HFd, writePos);
	}

	if ((HFlags & HGF_FAST) == 0)	/* append/scan lock */
	    hflock(HFd, 4, XLOCK_UN);

	if (HHead.version > 1)
	    index = (HistIndex)((writePos - HEntryOff) / sizeof(History));
	else
	    index = (HistIndex)writePos;
	if (n == sizeof(History)) {
	    if ((HFlags & HGF_FAST) == 0) {
		lseek(HFd, HHead.headSize + hi * sizeof(HistIndex), 0);
		if (write(HFd, &index, sizeof(index)) != sizeof(index)) {
		    logit(LOG_ERR, "Error writing to history: %s", strerror(errno));
		    r = RCTRYAGAIN;
		}
	    } else {
		HAry[hi] = index;
	    }
	} else {
	    r = RCTRYAGAIN;
	}
	break;
    }

    if ((HFlags & HGF_FAST) == 0)	/* unlock hash chain */
	hflock(HFd, chainlock, XLOCK_UN);

    return(r);
}

int
HistoryStore(History *h)
{
    HistIndex index;
    History th;

    index = HistoryPosLookupByHash(h->hv, &th);
    if (index == (HistIndex)-1)
	return(0);
    if (HHead.version > 1)
	lseek(HFd, (off_t)HEntryOff + (off_t)index * sizeof(History), 0);
    else
	lseek(HFd, index, 0);
    write(HFd, h, sizeof(History));
    return(1);
}

void
HistoryStoreExp(History *h, HistIndex index)
{
    if (HHead.version > 1)
	lseek(HFd, (off_t)HEntryOff + (off_t)index * sizeof(History) +
						offsetof(History, exp), 0);
    else
	lseek(HFd, index + offsetof(History, exp), 0);
    write(HFd, &h->exp, sizeof(h->exp));
}

/*
 * Cancel/expire article in history
 */
int
HistoryExpire(const char *msgid, History *h, int unexp)
{
    HistIndex index;
    hash_t hv;

    if (msgid == NULL)
	hv = h->hv;
    else
	hv = hhash(msgid);
    index = HistoryPosLookupByHash(hv, h);
    if (index == (HistIndex)-1)
	return(0);
    if (unexp && H_EXPIRED(h->exp)) {
	h->exp &= ~EXPF_EXPIRED;
	HistoryStoreExp(h, index);
    } else if (!H_EXPIRED(h->exp)) {
	h->exp |= EXPF_EXPIRED;
	HistoryStoreExp(h, index);
    }
    return(1);
}

