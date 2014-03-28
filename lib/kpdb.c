
/*
 * LIB/KPDB.C	- Key-Pair database support
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 *	NOTE NOTE NOTE: when using these library calls, keep in mind that
 *	data returned by KPDBRead*() is only valid until the next library
 *	call and that the returned data is not zero-terminated!!!
 *
 *	The keypair database is a human-readable / machine-writable shared
 *	database implementation.  The database CAN be edited manually, but
 *	only if diablo is completely shutdown.  If your editor flocks files,
 *	you can determine that it is safe to edit if the editor does not 
 *	complain.  All open databases maintain a read-lock on offset 0.
 *
 *	Multiple processes may read & write the same database.  This library
 *	ensures that no conflicts occur.  Any modification that does not
 *	result in a change in the record size is done in place.  Any other
 *	modification marks the record as deleted and appends a new one.
 *	spaces, tabs, and newlines are not allowed in record data.
 *
 *	The database is sorted in place.  The actual records are not 
 *	rearranged.  Instead, the sorted offsets are stored in the ssssssss
 *	field of each record.  A binary search is used to locate a key.
 *	Newly appended records do not require an immediate resort.
 *
 *	The database is [re]sorted when the append-account (cccccccc) field
 *	exceeds a certain value or if, on initial open, the mmmmmmmm timestamp
 *	does not match the file mtime (indicating a manual edit).  The m* field
 *	is updated on database close.
 *
 *	The file starts with a fixed-format control line:
 *
 *	    $Vvv.vv aaaaaaaa mmmmmmmm cccccccc
 *
 *		Vvv.vv    - 00.00	(version 0)
 *		cccccccc - appends since last sort
 *		aaaaaaaa - append seq, hex, bumped whenver an append is made
 *		mmmmmmmm - last modify timestamp to detect manual editing
 *
 *	Each record is formatted as follows.  Either a space or tab may be
 *	used as a delimeter (this is to support offline manual editing). 
 *
 *	    +/-ssssssss.mmmm:key token=value token=value token=value...\n
 *		+/-	 - '+' means valid record, '-' means deleted record.
 *		ssssssss - sort offset field
 *		mmmm	 - modification counter in hex.
 */

#include "defs.h"

/*
 * USE_KP_RW_MAP is not yet supported due to the append we have to
 * do in some cases, which would cause a mix of memory-writes and
 * write() system calls.  This can cause consistancy problems with
 * some versions of UNIX, including FreeBSD.
 */

#undef USE_KP_RW_MAP
#define USE_KP_RW_MAP	0

Prototype KPDB *XKPDBOpen(int oflags, const char *ctl, ...);
Prototype KPDB *KPDBOpen(const char *fileName, int oflags);
Prototype void KPDBClose(KPDB *kpdb);
Prototype KPDB *KPDBReOpen(KPDB *kpdb);
Prototype void KPDBReSort(KPDB *kpdb);
Prototype const char *KPDBRead(KPDB *kpdb, const char *key, const char *tok, int lockMe, int *plen, const char *def);
Prototype const char *KPDBReadRecord(KPDB *kpdb, const char *key, int lockMe, int *preclen);
Prototype int KPDBReadRecordOff(KPDB *kpdb, const char *key, int lockMe, int *preclen);
Prototype const char *KPDBReadRecordAt(KPDB *kpdb, int offset, int lockMe, int *preclen);
Prototype int KPDBScanFirst(KPDB *kpdb, int lockMe, int *preclen);
Prototype int KPDBScanNext(KPDB *kpdb, int offset, int lockMe, int *preclen);
Prototype const char *KPDBGetField(const char *rec, int recLen, const char *tok, int *plen, const char *def);
Prototype const char *KPDBGetFieldDecode(const char *rec, int recLen, const char *tok, int *plen, const char *def);
Prototype int KPDBWriteEncode(KPDB *kpdb, const char *key, const char *tok, const char *data, int unlockMe);
Prototype int KPDBWrite(KPDB *kpdb, const char *key, const char *tok, const char *data, int unlockMe);
Prototype void KPDBLock(KPDB *kpdb, const char *rec);
Prototype void KPDBUnlock(KPDB *kpdb, const char *rec);
Prototype int KPDBDelete(KPDB *kpdb, const char *key);
Prototype int KPDBAppendCount(KPDB *kpdb);

void KPDBReSize(KPDB *kpdb);
int KPDBValidate(KPDB *kpdb);
int KPDBSort(KPDB *kpdb);
int KPDBLookup(KPDB *kpdb, const char *key, int lockMe, int forceCheck);

#define KEY_OFF		15
#define V_OFF		1
#define A_OFF		8
#define M_OFF		17
#define C_OFF		26

#define KPVERSION	"00.00"

#define RESORT_LIMIT	128U

/*
 * KPDBOpen() - open a KP database.  WARNING! If you fork() after calling
 *		KPDBOpen(), you must call KPDBReOpen() to re-open the
 *		file descriptor so record-locking still works properly.
 */

KPDB *
KPDBOpen(const char *fileName, int oflags)
{
    KPDB *kpdb = zalloc(&SysMemPool, sizeof(KPDB) + strlen(fileName) + 1);

    kpdb->kp_FileName = (char *)(kpdb + 1);
    kpdb->kp_OFlags = oflags;
    kpdb->kp_ResortLimit = RESORT_LIMIT;
    strcpy(kpdb->kp_FileName, fileName);
    kpdb->kp_Fd = -1;
    return(KPDBReOpen(kpdb));
}

KPDB *
XKPDBOpen(int oflags, const char *ctl, ...)
{
    char path[1024];
    va_list va;

    va_start(va, ctl);
    vsnprintf(path, sizeof(path), ctl, va);
    va_end(va);
    return(KPDBOpen(path, oflags));
}

/*
 * KPDBClose() - close the specified KP database
 *
 */

void
KPDBClose(KPDB *kpdb)
{
    kpdb->kp_CloseMe = 1;
    (void)KPDBReOpen(kpdb);
}

void
KPDBReSize(KPDB *kpdb)
{
    kpdb->kp_ReSize = 1;
    if (KPDBReOpen(kpdb) == NULL) {
	logit(LOG_ERR, "resize %s failed", kpdb->kp_FileName);
	exit(1);
    }
    kpdb->kp_ReSize = 0;
}

/*
 * KPDBBReOpen() - close and re-open the KP database
 */

KPDB *
KPDBReOpen(KPDB *kpdb)
{
    int failed = 0;
    int dosort = (kpdb->kp_ReSize) ? 0 : -1;

    /*
     * free resources.  Write timestamp on close if modified prior to
     * releasing our lock.  We assume that nobody edited the file manually
     * while we had the lock.
     */

    if (kpdb->kp_ReSize == 0 && kpdb->kp_Fd >= 0) {
	if (kpdb->kp_CloseMe == 1 && 
	    kpdb->kp_Modified
	) {
	    char buf[9];

	    sprintf(buf, "%08x", (int)(uint32)time(NULL));
	    lseek(kpdb->kp_Fd, M_OFF, 0);
	    write(kpdb->kp_Fd, buf, 8);
	}
	hflock(kpdb->kp_Fd, 0, XLOCK_UN);
	close(kpdb->kp_Fd);
	kpdb->kp_Fd = -1;
	dosort = 0;		/* do not resort */
    }
    if (kpdb->kp_MapBase) {
	xunmap((char *)kpdb->kp_MapBase, kpdb->kp_MapSize);
	kpdb->kp_MapBase = NULL;
	kpdb->kp_MapSize = 0;
	kpdb->kp_Cache = NULL;
	kpdb->kp_CacheLen = 0;
    }
    if (kpdb->kp_CloseMe) {
	zfree(&SysMemPool, kpdb, sizeof(KPDB) + strlen(kpdb->kp_FileName) + 1);
	return(NULL);
    }

    /*
     * Preset CloseMe to 1, reset it to 0 if everything works out.
     *
     * Open, mmap, and resort the database if necessary.
     */
    kpdb->kp_CloseMe = 1;

    if (kpdb->kp_Fd < 0)
	kpdb->kp_Fd = open(kpdb->kp_FileName, kpdb->kp_OFlags, 0666);

    if (kpdb->kp_Fd >= 0) {
	struct stat st;

	/*
	 * locks
	 */
	if (kpdb->kp_Lock4 == 0)
	    hflock(kpdb->kp_Fd, 4, XLOCK_EX);	/* open sanity/sort */

	if (fstat(kpdb->kp_Fd, &st) == 0) {
	    if (hflock(kpdb->kp_Fd, 0, XLOCK_EX|XLOCK_NB) < 0) {
		dosort = 0;
	    } else {
		if (dosort == -1)
		    dosort = 2;		/* first locker, possible sort */
		hflock(kpdb->kp_Fd, 0, XLOCK_UN);
	    }
	    hflock(kpdb->kp_Fd, 0, XLOCK_SH);	/* leave read lock */

	    if (st.st_size == 0) {
		const char *s = "$V" KPVERSION " 00000000 00000000 0FFFFFFF\n";
		kpdb->kp_Modified = 1;
		if (write(kpdb->kp_Fd, s, strlen(s)) != strlen(s)) {
		    failed = 1;
		}
		if (fstat(kpdb->kp_Fd, &st) < 0) {
		    failed = 1;
		}
	    }

	    kpdb->kp_MapBase = xmap(
		NULL,
		st.st_size, 
		PROT_READ | (USE_KP_RW_MAP * PROT_WRITE),
		MAP_SHARED,
		kpdb->kp_Fd,
		0
	    );
	    if (failed == 0 && kpdb->kp_MapBase != NULL) {
		kpdb->kp_MapSize = (int)st.st_size;

		if (KPDBValidate(kpdb) == 0) {
		    if (dosort == 2) {
			int dt = (int32)strtol(kpdb->kp_MapBase + M_OFF, NULL, 16) - (int32)st.st_mtime;
			if (dt < -1) {
			    dosort = 1;
			}
			if (strtol(kpdb->kp_MapBase + C_OFF, NULL, 16) > kpdb->kp_ResortLimit) {
			    dosort = 1;
			}
		    }
		    if (dosort == 1)
			KPDBSort(kpdb);
		    kpdb->kp_CloseMe = 0;
		}
	    }
	}
	if (kpdb->kp_Lock4 == 0)
	    hflock(kpdb->kp_Fd, 4, XLOCK_UN);
    }
    if (kpdb->kp_CloseMe)
	kpdb = KPDBReOpen(kpdb);
    return(kpdb);
}

void
KPDBReSort(KPDB *kpdb)
{
    hflock(kpdb->kp_Fd, 4, XLOCK_EX);	/* open sanity/sort */
    ++kpdb->kp_Lock4;
    KPDBLookup(kpdb, "", 0, 1);		/* force remap if size changed */
    KPDBSort(kpdb);
    hflock(kpdb->kp_Fd, 4, XLOCK_UN);	/* open sanity/sort */
    --kpdb->kp_Lock4;
}

/*
 * KPDBRead() - locate and return a record from the KP database.  Leave the
 *		record locked if asked.  If you do not lock the record, the
 *		content of the returned data (which is NOT zero-terminated) 
 *		may be changed out from under you, but the length of the 
 *		returned data will be consistent.
 *
 *		KPDBRead() may decide to do a resort/remap if necessary.
 *		This means that any data returned previously may be lost.
 *
 *		KPDBReadRecord() reads all tokens related to a record.  The
 *		individual fields can be retrieved via KPDBGetField()
 */

const char *
KPDBRead(KPDB *kpdb, const char *key, const char *tok, int lockMe, int *plen, const char *def)
{
    if (KPDBLookup(kpdb, key, lockMe, 0) == 0) {
	return(KPDBGetField(kpdb->kp_Cache, kpdb->kp_CacheLen, tok, plen,def));
    }
    return(def);
}

const char *
KPDBReadRecord(KPDB *kpdb, const char *key, int lockMe, int *preclen)
{
    if (KPDBLookup(kpdb, key, lockMe, 0) == 0) {
	*preclen = kpdb->kp_CacheLen;
	return(kpdb->kp_Cache);
    }
    *preclen = 0;
    return(NULL);
}

int
KPDBReadRecordOff(KPDB *kpdb, const char *key, int lockMe, int *preclen)
{
    if (KPDBLookup(kpdb, key, lockMe, 0) == 0) {
	*preclen = kpdb->kp_CacheLen;
	if (kpdb->kp_Cache)
	    return(kpdb->kp_Cache - kpdb->kp_MapBase);
    }
    *preclen = 0;
    return(0);
}

const char *
KPDBReadRecordAt(KPDB *kpdb, int offset, int lockMe, int *preclen)
{
    const char *rec = kpdb->kp_MapBase + offset;
    int sl;
    int sm = kpdb->kp_MapSize - offset;

    if (offset >= kpdb->kp_MapSize)
	return(NULL);

    for (sl = 0; sl < sm && rec[sl] != '\n'; ++sl)
	;
    if (sl < sm)
	++sl;
    if (lockMe)
	hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_EX);
    if (preclen)
	*preclen = sl;
    return(rec);
}

const char *
KPDBGetField(const char *rec, int recLen, const char *tok, int *plen, const char *def)
{
    int i = 0;
    int l;

    /*
     * if tok == NULL, return key field
     */

    if (tok == NULL) {
	i = KEY_OFF;
	for (l = i; rec[l] != ' ' && rec[l] != '\t' && rec[l] != '\n'; ++l)
	    ;
	if (plen)
	    *plen = l - i;
	return(rec + i);
    }

    /*
     * otherwise find the requested field
     */

    l = strlen(tok);
  
    while (i < recLen) {
	while (i < recLen && rec[i] != ' ' && rec[i] != '\t' && rec[i] != '\n')
	    ++i;
	if (++i < recLen) {
	    if (i + l < recLen &&
		strncmp(tok, rec + i, l) == 0 && 
		rec[i+l] == '='
	    ) {
		i += l + 1;
		l = i;
		while (
		    l < recLen && 
		    rec[l] != '\t' && 
		    rec[l] != ' ' &&
		    rec[l] != '\n'
		) {
		    ++l;
		}
		if (plen)
		    *plen = l - i;
		return(rec + i);
	    }
	}
    }
    if (def && plen)
	*plen = strlen(def);
    return(def);
}

const char *
KPDBGetFieldDecode(const char *rec, int recLen, const char *tok, int *plen, const char *def)
{
    int len = 0;
    const char *res = KPDBGetField(rec, recLen, tok, &len, NULL);
    static char *ResDC;
    static int ResDCLen = 0;

    if (res == NULL) {
	if (def && plen)
	    *plen = strlen(def);
	return(def);
    }
#ifdef NOTDEF
    /*
     * remove - we need to guarentee a \0 terminator
     */
    if (strchr(res, '%') == NULL) {
	if (plen)
	    *plen = len;
	return(res);
    }
#endif
    if (ResDCLen <= len) {
	if (ResDC)
	    zfree(&SysMemPool, ResDC, ResDCLen);
	ResDCLen = (len + (63 + 1)) & ~63;	/* +1 to include nul terminator? */
	ResDC = zalloc(&SysMemPool, ResDCLen);
    }
    {
	int i = 0;
	int j = 0;

	while (i < len) {
	    int c;

	    /*
	     * WARNING! do not call sscanf() on res.  sscanf() uses string
	     * functions which involve getting the length of the string, 
	     * which in this case is several megabytes even though we are
	     * only pulling 2 hex digits out of it, so copying the digits to
	     * a small 0-terminated tmp buf is a thousand times faster.
	     */

	    if ((c = res[i]) == '%') {
		char tmp[3];
		tmp[0] = res[++i];
		tmp[1] = res[++i];
		tmp[2] = 0;
		if ((c = strtol(tmp, NULL, 16)) == 0)
		    c = '?';
	    }
	    ++i;
	    ResDC[j++] = c;
	}
	ResDC[j] = 0;
	if (plen)
	    *plen = j;
    }
    return(ResDC);
}

void 
KPDBLock(KPDB *kpdb, const char *rec)
{
    hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_EX);
}

void 
KPDBUnlock(KPDB *kpdb, const char *rec)
{
    hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_UN);
}

/*
 * KPDBScanFirst() - Locate first record in database and return it
 * KPDBScanNext()  - Locate next record in database and return it
 *
 * XXX what if database grows ?
 */

int
KPDBScanFirst(KPDB *kpdb, int lockMe, int *preclen)
{
    const char *rec = kpdb->kp_MapBase + kpdb->kp_HeadLen;
    int sl;
    int sm = kpdb->kp_MapSize - (rec - kpdb->kp_MapBase);

    for (sl = 0; sl < sm && rec[sl] != '\n'; ++sl)
	;
    if (sl < sm)
	++sl;
    if (lockMe)
	hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_EX);

    *preclen = sl;

    if (rec[0] != '+') {
	return(KPDBScanNext(kpdb, rec - kpdb->kp_MapBase, lockMe, preclen));
    }
    return(rec - kpdb->kp_MapBase);
}

int
KPDBScanNext(KPDB *kpdb, int offset, int lockMe, int *preclen)
{
    if (lockMe) {
	hflock(kpdb->kp_Fd, offset, XLOCK_UN);
    }
    for (;;) {
	const char *rec = kpdb->kp_MapBase + offset;

	for (
	    rec = rec + *preclen; 
	    rec - kpdb->kp_MapBase < kpdb->kp_MapSize;
	    rec = rec + *preclen
	) {
	    int sl;
	    int sm = kpdb->kp_MapSize - (rec - kpdb->kp_MapBase);
	    for (sl = 0; sl < sm && rec[sl] != '\n'; ++sl)
		;
	    if (sl < sm)
		++sl;
	    *preclen = sl;
	    if (rec[0] == '+') {
		if (lockMe)
		    hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_EX);
		if (rec[0] == '+') {
		    return(rec - kpdb->kp_MapBase);
		}
		if (lockMe)
		    hflock(kpdb->kp_Fd, rec - kpdb->kp_MapBase, XLOCK_UN);
	    }
	}

	/*
	 * Convert back to offset, pointer may be invalid if database grows.
	 */

	offset = rec - kpdb->kp_MapBase;

	/*
	 * check if database grew and continue scan if appropriate
	 */
	{
	    int aval = strtol(kpdb->kp_MapBase + A_OFF, NULL, 16);
	    if (aval == kpdb->kp_AppendSeq)
		break;
	    kpdb->kp_AppendSeq = aval;
	    KPDBReSize(kpdb);   /* PRIOR DATA POINTERS BECOME INVALID */
	}
    }
    return(0);
}

int
KPDBWriteEncode(KPDB *kpdb, const char *key, const char *tok, const char *data, int lockMe)
{
    int tmpLen = strlen(data) * 3 + 1;
    char *tmp = zalloc(&SysMemPool, tmpLen);
    int i;
    int j;
    int c;

    for (i = j = 0; (c = (int)(uint8)data[i]) != 0; ++i) {
	if ((c >= 'a' && c <= 'z') ||
	    (c >= 'A' && c <= 'Z') ||
	    (c >= '0' && c <= '9') ||
	    c == '.'
	) {
	    tmp[j++] = c;
	    continue;
	}
	sprintf(tmp + j, "%%%02x", c);
	j += 3;
    }
    tmp[j] = 0;
    c = KPDBWrite(kpdb, key, tok, tmp, lockMe);
    zfree(&SysMemPool, tmp, tmpLen);
    return(c);
}

/*
 * KPDBWrite()- overwrite, replace, or create a KP record.  If a record
 *		was read with lockMe, it must be written with unlockMe
 *		set.  A NULL token may be specified to simply unlock a
 *		record without writing.  A NULL token will create a
 *		new record if the key does not exist.
 *
 *		KPDBWrite() may decide to do a resort/remap if necessary.
 *		This means that any data returned previously may be lost.
 *		However, if KPDWrite() does a resort, it will copy the data
 *		into a private buffer so the data CAN be directly passed to
 *		KPDBWrite() from the last KPDBRead().
 *
 *		lockMe = 0		obtain temporary lock, do not hold
 *
 *		lockMe = KP_LOCK	obtain lock, hold on return
 *
 *		lockMe = KP_LOCK_CONTINUE already have lock, maintain it
 *
 *		lockMe = KP_UNLOCK	release previously obtained locked
 *					after write complete.
 */

int
KPDBWrite(KPDB *kpdb, const char *key, const char *tok, const char *data, int lockMe)
{
    /*
     * Best case - simple replacement of existing data, no append required
     */
    int dataLen = strlen(data);
    int tlock = KP_LOCK;
    int lockoff = 0;

    /*
     * If record already locked, do not double-lock it
     */

    if (lockMe == KP_LOCK_CONTINUE || lockMe == KP_UNLOCK)
	tlock = 0;

    /*
     * Obtain record.  Record is locked on valid return
     */

    if (KPDBLookup(kpdb, key, tlock, 0) == 0) {
	/*
	 * found existing record.  Lock is held at this
	 * point (either from previous read or new lock
	 * generated).
	 */
	const char *odata = NULL;
	int odataLen = 0;

	lockoff = kpdb->kp_Cache - kpdb->kp_MapBase;

	odata = KPDBGetField(
	    kpdb->kp_Cache,
	    kpdb->kp_CacheLen,
	    tok,
	    &odataLen,
	    NULL
	);
	if (dataLen == odataLen) {
	    kpdb->kp_Modified = 1;
#if USE_KP_RW_MAP
	    bcopy(data, (char *)odata, dataLen);
#else
	    lseek(kpdb->kp_Fd, odata - kpdb->kp_MapBase, 0);
	    write(kpdb->kp_Fd, data, dataLen);
#endif
	    /*
	     * unlock record if lockMe is 0 or KP_UNLOCK, else
	     * leave locked (if KP_LOCK_CONTINUE or KP_LOCK)
	     */
	    if (lockMe != KP_LOCK_CONTINUE && lockMe != KP_LOCK)
		hflock(kpdb->kp_Fd, lockoff, XLOCK_UN);
	    return(0);
	}
	/*
	 * leave record locked through replace
	 */
#ifdef NOTDEF
	if (unlockMe == 0)
	    hflock(kpdb->kp_Fd, kpdb->kp_Cache - kpdb->kp_MapBase, XLOCK_UN);
#endif
    }

    /*
     * Append new record, bump aaaaaaaa, bump cccccccc, resort if necessary,
     * delete original record.  We have to be exclusively locked through our
     * lookup.
     *
     * (if prior read was locked, it is held through)
     */

    {
	int copyOld = 0;
	int aval = 0;
	int cval = 0;
	char astr[9];
	char cstr[9];
	int nlockoff;

	hflock(kpdb->kp_Fd, 4, XLOCK_EX);	/* append lock		*/
	kpdb->kp_Lock4 = 1;			/* locked thru remap	*/

	/*
	 * Bump aaaaaaaa and cccccccc, resort if necessary (we are already
	 * holding the exclusive lock for the resort).
	 *
	 * If we are going to resort, we must resize our map if the database
	 * has grown so force a check in KPDBLookup().
	 */

	aval = strtol(kpdb->kp_MapBase + A_OFF, NULL, 16);
	cval = strtol(kpdb->kp_MapBase + C_OFF, NULL, 16);

	if (KPDBLookup(kpdb, key, 0, (cval > kpdb->kp_ResortLimit)) == 0) {
	    copyOld = 1;
	}

	if ((uint32)cval > kpdb->kp_ResortLimit) {
	    KPDBSort(kpdb);
	    cval = 0;
	}

	sprintf(astr, "%08x", (int)(uint32)(aval + 1));
	sprintf(cstr, "%08x", (int)(uint32)(cval + 1));

	kpdb->kp_Modified = 1;

#if USE_KP_RW_MAP
	bcopy(astr, (char *)kpdb->kp_MapBase + A_OFF, 8);
	bcopy(cstr, (char *)kpdb->kp_MapBase + C_OFF, 8);
#else
	lseek(kpdb->kp_Fd, A_OFF, 0);
	write(kpdb->kp_Fd, astr, 8);
	lseek(kpdb->kp_Fd, C_OFF, 0);
	write(kpdb->kp_Fd, cstr, 8);
#endif

	/*
	 * Append new record, possibly merging from the old record
	 *
	 * To use USE_KP_RW_MAP, we have to ftruncate the file to 
	 * extend it, make a temporary mmap of that area, and write
	 * the data.  Bleh.
	 *
	 * Lock new record
	 */

	nlockoff = lseek(kpdb->kp_Fd, 0L, 2);
	if (hflock(kpdb->kp_Fd, nlockoff, XLOCK_EX|XLOCK_NB) < 0) {
	    logit(LOG_ERR, "unable to lock new %s record offset %d", kpdb->kp_FileName, nlockoff);
	}

	if (copyOld) {
	    int odataLen;
	    const char *odata;
	    int l = kpdb->kp_CacheLen + strlen(tok) + strlen(data) + 8;
	    char *p = zalloc(&SysMemPool, l);

	    bcopy("+00000000", p, 9);
	    bcopy(kpdb->kp_Cache + 9, p + 9, kpdb->kp_CacheLen - 9);

	    odata = KPDBGetField(
		kpdb->kp_Cache,
		kpdb->kp_CacheLen,
		tok,
		&odataLen,
		NULL
	    );
	    if (odata) {
		int dlen = strlen(data);
		int poff = (odata - kpdb->kp_Cache);

		bcopy(data, p + poff, dlen);
		poff += dlen;

		dlen = kpdb->kp_CacheLen - (odata + odataLen - kpdb->kp_Cache);
		bcopy(odata + odataLen, p + poff, dlen);
		p[poff+dlen] = 0;
	    } else {
		sprintf(p + kpdb->kp_CacheLen - 1, " %s=%s\n", tok, data);
	    }
	    write(kpdb->kp_Fd, p, strlen(p));
	    zfree(&SysMemPool, p, l);

	    /*
	     * delete original record
	     */

	    lseek(kpdb->kp_Fd, kpdb->kp_Cache - kpdb->kp_MapBase, 0);
	    write(kpdb->kp_Fd, "-", 1);
	} else {
	    int l = 20 + strlen(key) + strlen(tok) + dataLen;
	    char *p = zalloc(&SysMemPool, l);
	    sprintf(p, "+00000000.0000:%s %s=%s\n", key, tok, data);
	    write(kpdb->kp_Fd, p, strlen(p));
	    zfree(&SysMemPool, p, l);
	}
	kpdb->kp_Lock4 = 0;
	hflock(kpdb->kp_Fd, 4, XLOCK_UN);

	/*
	 * Unlock original record
	 */
	if (lockoff != 0)
	    hflock(kpdb->kp_Fd, lockoff, XLOCK_UN);

	/*
	 * Unlock new record if lockMe is KP_UNLOCK or 0, else
	 * leave it locked on return (if KP_LOCK or KP_LOCK_CONTINUE)
	 */
	if (lockMe == KP_UNLOCK || lockMe == 0)
	    hflock(kpdb->kp_Fd, nlockoff, XLOCK_UN);
    }
    return(0);
}

/*
 * KPDBDelete() - delete a KP record
 */

int
KPDBDelete(KPDB *kpdb, const char *key)
{
    if (KPDBLookup(kpdb, key, KP_LOCK, 0) == 0) {
	kpdb->kp_Modified = 1;
#if USE_KP_RW_MAP
	*(char *)kpdb->kp_Cache = '-';
#else
	lseek(kpdb->kp_Fd, kpdb->kp_Cache - kpdb->kp_MapBase, 0);
	write(kpdb->kp_Fd, "-", 1);
#endif
	hflock(kpdb->kp_Fd, kpdb->kp_Cache - kpdb->kp_MapBase, XLOCK_UN);
    }
    return(-1);
}

int
KPDBValidate(KPDB *kpdb)
{
    int i;

    for (i = 0; i < kpdb->kp_MapSize; ++i) {
	if (kpdb->kp_MapBase[i] == '\n') {
	    kpdb->kp_HeadLen = i + 1;	/* XXX 35? */
	    return(0);
	}
    }
    return(-1);
}

int KPDBLookupBinary(KPDB *kpdb, const char *key, int l, int b, int e);

int
KPDBLookup(KPDB *kpdb, const char *key, int lockMe, int forceCheck)
{
    int l = strlen(key);
    int found = -1;

    /*
     * Optimize cache hit
     */

    if (kpdb->kp_Cache) {
	if (l < kpdb->kp_CacheLen - KEY_OFF &&		/* not too short */
	    kpdb->kp_Cache[0] == '+' &&			/* not deleted	*/
	    strncmp(key, kpdb->kp_Cache + KEY_OFF, l) == 0 &&	/* key match */
	    ( kpdb->kp_Cache[l + KEY_OFF] == ' ' ||	/* key terminator */
	      kpdb->kp_Cache[l + KEY_OFF] == '\t' ||
	      kpdb->kp_Cache[l + KEY_OFF] == '\n'
	    )
	) {
	    found = 0;
	}
    }

    /*
     * Binary search
     */

    if (found < 0) {
	found = KPDBLookupBinary(
	    kpdb, 
	    key,
	    l,
	    kpdb->kp_HeadLen,
	    kpdb->kp_MapSize
	);
    }

    /*
     * Sequential search from end (unsorted records only)
     */

    if (found < 0) {
	const char *p = kpdb->kp_MapBase;
	int i = kpdb->kp_MapSize;

	while (found < 0) {
	    int rl = 1;

	    if (i <= kpdb->kp_HeadLen)
		break;
	    --i;
	    if (p[i] != '\n')
		break;

	    while (i > kpdb->kp_HeadLen && p[i-1] != '\n') {
		--i;
		++rl;
	    }

	    /*
	     * beginning of record, scan 00000000 (unsorted) records only
	     */

	    if (strtol(p + i + 1, NULL, 16) != 0)
		break;

	    if (l < rl - KEY_OFF &&
		p[i] == '+' &&
		strncmp(key, p + i + KEY_OFF, l) == 0 &&
		( p[i + l + KEY_OFF] == ' ' ||	/* key terminator */
		  p[i + l + KEY_OFF] == '\t' ||
		  p[i + l + KEY_OFF] == '\n'
		)
	    ) {
		found = 0;
		kpdb->kp_Cache = p + i;
		kpdb->kp_CacheLen = rl;
	    }
	}
    }

    /*
     * Did database get larger?
     */

    if ((forceCheck >= 0 && found < 0) || forceCheck > 0) {
	int aval = strtol(kpdb->kp_MapBase + A_OFF, NULL, 16);
	if (aval != kpdb->kp_AppendSeq) {
	    kpdb->kp_AppendSeq = aval;
	    KPDBReSize(kpdb);	/* PRIOR DATA POINTERS BECOME INVALID */
	    return(KPDBLookup(kpdb, key, lockMe, -1));
	}
    }

    if (found == 0 && lockMe == KP_LOCK) {
	hflock(kpdb->kp_Fd, kpdb->kp_Cache - kpdb->kp_MapBase, XLOCK_EX);
    }
    return(found);
}

int
KPDBLookupBinary(
    KPDB *kpdb,
    const char *key,
    int l,
    int b,
    int e
) {
    int c;
    int cl;
    int r = -1;
    int s = 0;
    int doLeft = 0;
    int doRight = 0;
    const char *p = kpdb->kp_MapBase;

    c = (b + e) >> 1;	/* rounds down */

    while (c >= b && p[c] != '\n')
	--c;
    ++c;

    /*
     * cl may be zero if the database is empty
     */

    for (cl = c; cl < e && p[cl] != '\n'; ++cl)
        ;
    cl = cl - c + 1;

    if (DebugOpt > 8)
	printf("LookupBinary %d (%d) %d %*.*s\n", b, c, e, cl, cl, &p[c]);

    if (cl && c < kpdb->kp_MapSize)
	s = strtol(p + c + 1, NULL, 16);  /* offset of sorted record or 0 */

    if (s < kpdb->kp_HeadLen || s >= kpdb->kp_MapSize || p[s-1] != '\n') {
	/*
	 * If we can't access the record, we have to search both branches
	 */
	if (DebugOpt > 8)
	    printf("A\n");
	doLeft = 1;
	doRight = 1;
    } else {
	int sl;

	for (sl = s; sl < kpdb->kp_MapSize && p[sl] != '\n'; ++sl)
	   ;
	if (DebugOpt > 8)
	    printf("LookupBinary USE %*.*s\n", sl - s, sl - s, p + s);
	if (sl == kpdb->kp_MapSize || sl - s < KEY_OFF) {
	    /*
	     * newline past end of map, same problem
	     */
	    doLeft = 1;
	    doRight = 1;
	    if (DebugOpt > 8)
		printf("B\n");
	} else {
	    /*
	     * compare key
	     */
	    int i = 0;

	    ++sl;	/* include newline */

	    p += s + KEY_OFF;
	    sl -= s + KEY_OFF;

	    if (DebugOpt > 8)
		printf("C\n");

	    while (i < sl && i < l) {
		if (key[i] < p[i]) {
		    doLeft = 1;
		    if (DebugOpt > 8)
			printf("D\n");
		    break;
		}
		if (key[i] > p[i]) {
		    doRight = 1;
		    if (DebugOpt > 8)
			printf("E\n");
		    break;
		}
		++i;
	    }
	    if (DebugOpt > 8)
		printf("i = %d %d %d\n", i, sl, l);
	    if (doLeft == 0 && doRight == 0) {
		if (i < l) {	/* i < l && i == sl */
		    if (DebugOpt > 8)
			printf("F\n");
		    doRight = 1;
		} else if (i < sl && p[i] != ' ' && p[i] != '\t' && p[i] != '\n') { /* i < sl && i == l */
		    if (DebugOpt > 8)
			printf("G\n");
		    doLeft = 1;
		} else if (p[-KEY_OFF] == '-') {	/* deleted record */
		    doLeft = 1;
		    doRight = 1;
		} else {
		    if (DebugOpt > 8)
			printf("H\n");
		    r = 0;
		    kpdb->kp_Cache = p - KEY_OFF;
		    kpdb->kp_CacheLen = sl + KEY_OFF;
		}
	    }
	    /* do not restore p and sl */
	}
    }
    if (r < 0 && doLeft && b != c)
	r = KPDBLookupBinary(kpdb, key, l, b, c);
    if (r < 0 && doRight && c + cl < e)
	r = KPDBLookupBinary(kpdb, key, l, c + cl, e);
    return(r);
}

/*
 * Called with the database locked and fully mapped
 */

int
kpdqsort(const void *s1, const void *s2)
{
    const char *p1 = *(char **)s1 + KEY_OFF;
    const char *p2 = *(char **)s2 + KEY_OFF;
    int r = 0;

    for (;;) {
	if (*p1 == ' ' || *p1 == '\t' || *p1 == '\n') {
	    if (*p2 == ' ' || *p2 == '\t' || *p2 == '\n')
		break;
	    return(-1);
	}
	if (*p2 == ' ' || *p2 == '\t' || *p2 == '\n') {
	    return(1);
	}
	if (*p1 < *p2)
	    return(-1);
	if (*p1 > *p2)
	    return(1);
	++p1;
	++p2;
    }
    return(r);
}

int
KPDBSort(KPDB *kpdb)
{
    int i;
    int c;
    int nc;
    int t;
    const char *p = kpdb->kp_MapBase;
    const char **ary;

    if (DebugOpt > 8)
	printf("KPDBSort(): sorting %s\n", kpdb->kp_FileName);

    for ((c = 0), (i = kpdb->kp_HeadLen); i < kpdb->kp_MapSize; ++i) {
	if (p[i] == '\n')
	   ++c;
    }
    nc = c;
    ary = pagealloc(&t, nc * sizeof(char *));

    for ((c = 0), (i = kpdb->kp_HeadLen); c < nc; ++c) {
	ary[c] = p + i;

	while (i < kpdb->kp_MapSize && p[i] != '\n')
	    ++i;
	if (i == kpdb->kp_MapSize)
	    break;
	++i;
    }

    qsort(ary, nc, sizeof(char *), kpdqsort);

    for ((c = 0), (i = kpdb->kp_HeadLen); c < nc; ++c) {
	char buf[9];
	int al = strchr(ary[c], '\n') - ary[c];

	if (DebugOpt > 8)
	    printf("ary[%d] = %*.*s\n", c, al, al, ary[c]);

	sprintf(buf, "%08x", (unsigned int)(ary[c] - kpdb->kp_MapBase));
	lseek(kpdb->kp_Fd, i + 1, 0);
	write(kpdb->kp_Fd, buf, 8);

	while (i < kpdb->kp_MapSize && p[i] != '\n')
	    ++i;
	if (i == kpdb->kp_MapSize)
	    break;
	++i;
    }
    lseek(kpdb->kp_Fd, C_OFF, 0);
    write(kpdb->kp_Fd, "00000000", 8);
    {
	char buf[10];
	sprintf(buf, "%08x", (int)(uint32)time(NULL));
	lseek(kpdb->kp_Fd, M_OFF, 0);
	write(kpdb->kp_Fd, buf, 8);
    }
    pagefree(ary, nc * sizeof(char *));
    kpdb->kp_Modified = 1;
    return(0);
}

int
KPDBAppendCount(KPDB *kpdb)
{
    return strtol(kpdb->kp_MapBase + A_OFF, NULL, 16);
}

