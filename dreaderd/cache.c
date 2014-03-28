
/*
 * OpenCache()
 *
 *	return +1	if a valid cache file was found, set *pcfd, *psize
 *	return 0	if a valid cache file was not found or if an
 *			empty file is found (uncacheable article)
 *
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int OpenCache(const char *msgid, int *pcfd, int *psize);
Prototype void CreateCache(Connection *conn);
Prototype void AbortCache(int fd, const char *msgid, int closefd);
Prototype void CommitCache(Connection *conn, int closefd);
Prototype void DumpArticleFromCache(Connection *conn, const char *map, int size, int grpIter, artno_t endNo);

Prototype void OpenCacheHits(void);

struct CacheHitEntry* FindCacheHitEntry(struct CacheHash_t *ch);
struct CacheHitEntry* CreateCacheHitEntry(struct CacheHash_t *ch, artno_t endNo);
struct CacheHitEntry* UpdateCacheHits(char *groupName, int grpIter, artno_t endNo, int cachehit);

int XPageMask=0;
int CacheHitsFD=-1;
char *CacheHits=NULL;
uint32 CacheHitsEnd=0;

unsigned int
cacheHashNum(char *st)
{
    hash_t h = hhash(st);
    return(abs(h.h1 + h.h2));
}

void
cacheFile(hash_t hv, char *path, int makedir)
{
    int blen = 0;
    char fstr[32];
    int i;

    sprintf(fstr, "%08x%08x", (int)hv.h1, (int)hv.h2);

    strcpy(path, PatExpand(CacheHomePat));
    for (i = 0; i < DOpts.ReaderCacheDirs.dt_dirlvl; i++) {
	int c = DOpts.ReaderCacheDirs.dt_dirinfo[i];
	int formsize = 0;
	char format[32];

	while (c > 0) {
	    formsize++;
	    c = (c - 1) / 16;
	}
	sprintf(format, "/%%0%dx", formsize);
	blen = strlen(path);
	sprintf(path + blen, format,
		cacheHashNum(&fstr[i]) % DOpts.ReaderCacheDirs.dt_dirinfo[i]);
	if (makedir) {
	    struct stat st;
	    if (stat(path, &st) < 0) {
		mkdir(path, 0755);
	    }
	}
    }
    blen = strlen(path);
    sprintf(path + blen, "/%08x.%08x", (int)hv.h1, (int)hv.h2);
}

int
OpenCache(const char *msgid, int *pcfd, int *psize)
{
    int fd;
    hash_t hv = hhash(msgid);
    char path[PATH_MAX];

    *pcfd = -1;
    *psize = 0;

    /*
     * open cache file
     */

    cacheFile(hv, path, 1);

    if ((fd = open(path, O_RDWR)) >= 0) {
	struct stat st;

	if (fstat(fd, &st) == 0) {
	    if (st.st_size == 0) {	/* uncachable */
		close(fd);
		return(0);
	    }
	    *pcfd = fd;			/* positively cached */
	    *psize = st.st_size;
#if MMAP_DOES_NOT_UPDATE_ATIME
	    {
		char t[1];
	    	read(fd, &t, 1);
	    }
#endif
	    return(1);
	}
	close(fd);			/* error	     */
	return(0);
    }
    return(0);
}

void
CreateCache(Connection *conn)
{
    int fd;
    hash_t hv;
    char path[PATH_MAX], tmp[PATH_MAX];

    /* do not create new cache files for old articles */
    if (conn->co_Desc->d_CacheableTime > 0 &&
	(conn->co_Desc->d_CacheableTime < (time(NULL) - conn->co_SReq->sr_TimeRcvd))) {
	return;
    }

    hv = hhash(conn->co_SReq->sr_MsgId);

    /*
     * open cache file
     */

    cacheFile(hv, path, 1);
    {
	struct stat st;
	if (!stat(path, &st)) { /* uncacheable article */
	    return;
	}
    }

    strcpy(tmp, path);
    strcat(tmp, ".tmp");

    if (conn->co_Desc->d_Cache == CACHE_LAZY) {
	/* 
	 * Lazy cache, if tmp file does not exit, we just create it and
	 * return
	 */
	if ((fd = open(tmp, O_RDWR, 0644)) < 0) {
	    close(creat(tmp, 0644));
	    return;
	}
    } else if (conn->co_Desc->d_Cache == CACHE_SCOREBOARD) {
	struct CacheHitEntry *che;
	int new;
	double read;
	/* Check cache hits ratio */
	che = UpdateCacheHits(conn->co_SReq->sr_Group, conn->co_SReq->sr_GrpIter, conn->co_SReq->sr_endNo, 0);
	if (che==NULL) return;
	read = che->che_ReadArt+che->che_Hits;
	new = che->che_NewArt+conn->co_SReq->sr_endNo-che->che_LastHi;
	if (new < 1) new=1;
	if ( (read / new) > conn->co_Desc->d_ReadNewRatio) {
	    if ((che->che_Hits/che->che_ReadArt) > conn->co_Desc->d_CacheReadRatio) {
		/* cache on */
		if ((fd = open(tmp, O_RDWR|O_CREAT, 0644)) < 0) {
		    return;	/* error	     */
		}
	    } else {
		/* partly cached */
		if ((fd = open(tmp, O_RDWR, 0644)) < 0) {
	    	    close(creat(tmp, 0644));
		    return;
		} else {
		    /* correcting stat */
		    che->che_ReadArt--;
		    che->che_Hits++;
		    /* lazy cache in scoring mode, no return */
		}
	    }
	} else {
	    return;
	}
    } else {
	if ((fd = open(tmp, O_RDWR|O_CREAT, 0644)) < 0) {
	    return;			/* error	     */
	}
    }
    if (hflock(fd, 0, XLOCK_EX|XLOCK_NB) < 0) {
	close(fd);			/* someone else owns it */
	return;
    }
    {
	struct stat st;
	struct stat st2;
	if (fstat(fd, &st) < 0 || st.st_nlink == 0) {
	    close(fd);			/* delete race		*/
	    return;
	}
	if (stat(tmp, &st2) < 0 || st.st_ino != st2.st_ino) {
	    close(fd);			/* rename race		*/
	    return;
	}
	if (st.st_size != 0)		/* truncate if partial left over */
	    ftruncate(fd, 0);
    }

    conn->co_SReq->sr_Cache = fdopen(fd, "w");
    if (!conn->co_SReq->sr_Cache) {
	close(fd);
    }
}

/*
 * AbortCache() - cache not successfully written, destroy
 */

void
AbortCache(int fd, const char *msgid, int closefd)
{
    char path[PATH_MAX];
    hash_t hv = hhash(msgid);

    cacheFile(hv, path, 0);
    strcat(path, ".tmp");
    remove(path);
    ftruncate(fd, 0);
    hflock(fd, 0, XLOCK_UN);
    if (closefd)
	close(fd);
}

/*
 * CommitCache() - cache successfully written, commit to disk
 */

void
CommitCache(Connection *conn, int closefd)
{
    char path1[PATH_MAX];
    char path2[PATH_MAX];
    char *msgid = conn->co_SReq->sr_MsgId;
    hash_t hv = hhash(msgid);
    int fd = fileno(conn->co_SReq->sr_Cache);

    cacheFile(hv, path2, 0);
    strcpy(path1, path2);
    strcat(path1, ".tmp");
    if (rename(path1, path2) < 0)
	remove(path1);
    {
	struct stat st;
	if (stat(path2, &st) == 0 && ((conn->co_Desc->d_CacheMax > 0 &&
				st.st_size > conn->co_Desc->d_CacheMax)
			|| (conn->co_Desc->d_CacheMin > 0 &&
				st.st_size < conn->co_Desc->d_CacheMin)))
	    ftruncate(fd, 0);
    }
    hflock(fd, 0, XLOCK_UN);
    if (closefd)
	close(fd);
}

/*
 * DUMPARTICLEFROMCACHE() - article buffer is passed as an argument.   The
 *			    buffer is already '.' escaped (but has no 
 *			    terminating '.\r\n'), and \r\n terminated.
 *
 *			    if (conn->co_ArtMode == COM_BODYNOSTAT), just
 *			    do the body.  Otherwise do the whole thing.
 */

void
DumpArticleFromCache(Connection *conn, const char *map, int size, int grpIter, artno_t endNo)
{
    int b = 0;
    int inHeader = 1;
    int nonl = 0;
    char line[8192];
    char *vserver;
    char *buf;
    char ch;

    if (conn->co_Auth.dr_VServerDef)
	vserver = conn->co_Auth.dr_VServerDef->vs_ClusterName;
    else
	vserver = "";

    while (b < size) {
	int i;
	int yes = 0;

	for (i = b; i < size && map[i] != '\n'; ++i)
	    ;
	if (i < size) 
	    ++i;
	else
	    nonl = 1;
	switch(conn->co_ArtMode) {
	case COM_STAT:
	    break;
	case COM_HEAD:
	    if (inHeader == 1) {
		yes = 1;
		if (i == b + 2 && map[b] == '\r' && map[b+1] == '\n')
		    yes = 0;
	    }
	    break;
	case COM_ARTICLEWVF:
	    {
		const char *ovdata;
		int ovlen;

		if ((ovdata = NNRetrieveHead(conn, &ovlen, NULL, NULL, NULL, NULL)) != NULL) {
		    DumpOVHeaders(conn, ovdata, ovlen);
		    MBPrintf(&conn->co_TMBuf, "\r\n");
		    conn->co_ArtMode = COM_BODYNOSTAT;
		} else {
		    yes = 1;
		}
	    }
	    break;
	case COM_ARTICLE:
	    yes = 1;
	    break;
	case COM_BODY:
	case COM_BODYWVF:
	case COM_BODYNOSTAT:
	    if (inHeader == 0)
		yes = 1;
	    break;
	}
	/*
	 * Do some header rewriting for virtual servers
	 */

	*line = '\0';
	buf = (char *)&map[b];
	ch = tolower(*buf);

	if (inHeader && *vserver &&
		!conn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate &&
		ch == 'x' && strncasecmp(buf, "Xref:", 5) == 0) {
	    char *ptr;

	    ptr = (char *)buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    while (!isspace((int)*ptr))
		ptr++;
	    while (isspace((int)*ptr))
		ptr++;
	    /* ptr should point to first group name */
	    if (*ptr) {
		int len;
		int e = i - 1;
		while (map[e] == '\r' || map[e] == '\n')
		    e--;
		len = (e - b + 1) - (ptr - buf);
		if (len > sizeof(line) - 100)
		    len = sizeof(line) - 100;
		sprintf(line, "Xref: %s ", vserver); 
		e = strlen(line);
		memcpy(&line[e], ptr, len);
		line[e + len] = '\0';
		strcat(line, "\r\n");
            }

	}
	if (inHeader && *vserver && ch == 'p' &&
		!conn->co_Auth.dr_VServerDef->vs_NoReadPath &&
				strncasecmp(buf, "Path:", 5) == 0) {
	    char *ptr;
	    int vsl = strlen(vserver);

	    ptr = (char *)buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    if (ptr && *ptr && (strncmp(vserver, ptr, vsl) ||
				((ptr[vsl] != '\0') &&
				 (ptr[vsl] != '!')))) {
		int len;
		int e = i - 1;
		while (map[e] == '\r' || map[e] == '\n')
		    e--;
		len = (e - b + 1) - (ptr - buf);
		if (len > sizeof(line) - 100)
		    len = sizeof(line) - 100;
		sprintf(line, "Path: %s!", vserver); 
		e = strlen(line);
		memcpy(&line[e], ptr, len);
		line[e + len] = '\0';
		strcat(line, "\r\n");
            }
	}

	if (yes) {
	    if (*line)
		MBWrite(&conn->co_TMBuf, line, strlen(line));
	    else
		MBWrite(&conn->co_TMBuf, map + b, i - b);
	}

	if (inHeader && i == b + 2 && map[b] == '\r' && map[b+1] == '\n')
	    inHeader = 0;

	b = i;
    }
    if (nonl && conn->co_ArtMode != COM_STAT)
	MBPrintf(&conn->co_TMBuf, "\r\n", 2);

    /* update the cache hits */
    if (CacheHits) {
	UpdateCacheHits(conn->co_GroupName, grpIter, endNo, 1); 
    }
}

struct CacheHitEntry*
FindCacheHitEntry(struct CacheHash_t *ch) {
    uint32 *i=NULL;
    uint32 max=CacheHitsEnd-sizeof(struct CacheHitEntry);
    struct CacheHitEntry *che=NULL;
    struct CacheHitHead *chh=(struct CacheHitHead *)CacheHits;

    i = (uint32*) (CacheHits+sizeof(struct CacheHitHead)+((ch->h1^ch->h2)%chh->chh_hashSize)*sizeof(uint32));

    while ((*i != 0) && (*i < max)) {
	che = (struct CacheHitEntry*) (CacheHits + (*i));
	if (memcmp(ch, &(che->che_hash), sizeof(struct CacheHash_t))==0) {
	    return che;
	}
	i = &(che->che_Next);
    }
    return NULL;
}

/*** Any call to CreateCacheHitEntry may change CacheHits value ***
 *
 * There is no hurry to re-mmap CacheHitsFD, we may only check just before
 * creating a new entry
 */
struct CacheHitEntry*
CreateCacheHitEntry(struct CacheHash_t *ch, artno_t endNo) {
    uint32 *i=NULL;
    uint32 ne=0;
    struct CacheHitEntry *che=NULL;
    struct CacheHitHead *chh=(struct CacheHitHead *)CacheHits;

    /* Get an exclusive lock */
    hflock(CacheHitsFD, 0, XLOCK_EX);

    /* has CacheHitsFD size changed ? */
    if (chh->chh_end != CacheHitsEnd) {
	int end;
	logit(LOG_ERR, "CreateCacheHitEntry - cache.hits size has changed");
	end = CacheHitsEnd;
	CacheHitsEnd = chh->chh_end;
	munmap(CacheHits, end);
    	CacheHits = mmap(NULL, CacheHitsEnd, PROT_READ|PROT_WRITE, MAP_SHARED, CacheHitsFD, 0);
	if (CacheHits == NULL) {
    	    CacheHitsEnd = 0;
	    logit(LOG_ERR, "Error on cache hits remaping (%s)", strerror(errno));
    	    hflock(CacheHitsFD, 0, XLOCK_UN);
	    close(CacheHitsFD);
	    return NULL;
	}
	chh=(struct CacheHitHead *)CacheHits;
    }

    /* check if someone else have created the entry while waiting for the lock */
    i = (uint32*) (CacheHits+sizeof(struct CacheHitHead)+((ch->h1^ch->h2)%chh->chh_hashSize)*sizeof(uint32));

    while ((*i) != 0) {
	che = (struct CacheHitEntry*) (CacheHits + (*i));
	if (memcmp(ch, &(che->che_hash), sizeof(struct CacheHash_t))==0) {
    	    hflock(CacheHitsFD, 0, XLOCK_UN);
	    return che;
	}
	i = &(che->che_Next);
    }

    /* Adding an entry */
    (*i) = ne = chh->chh_newEntry;
    chh->chh_newEntry += sizeof(struct CacheHitEntry);
    if (chh->chh_newEntry > chh->chh_end) {
	char buf[512];
	int len;
	uint32 j, end;

	j = chh->chh_end;
	lseek(CacheHitsFD, chh->chh_end, SEEK_SET);
	end = (chh->chh_newEntry + XPageMask) & ~XPageMask;
	logit(LOG_ERR, "CreateCacheHitEntry - cache.hits is being increased (%x/%x)", j, end);

	bzero(buf, sizeof(buf));
	len = sizeof(buf);
	while(j<end) {
	    if (j+len>end) len=end-j;
	    write(CacheHitsFD, buf, len);
	    j += len;
	}
	fsync(CacheHitsFD);

	chh->chh_end = end;
	munmap(CacheHits, CacheHitsEnd);
    	CacheHitsEnd = end;
    	CacheHits = mmap(NULL, end, PROT_READ|PROT_WRITE, MAP_SHARED, CacheHitsFD, 0);
	if (CacheHits == NULL) {
    	    CacheHitsEnd = 0;
	    logit(LOG_ERR, "Error on cache hits remaping (%s)", strerror(errno));
    	    hflock(CacheHitsFD, 0, XLOCK_UN);
	    close(CacheHitsFD);
	    return NULL;
	}
	chh=(struct CacheHitHead *)CacheHits;
    }
    /* beware, values may have changed */
    che = (struct CacheHitEntry*) (CacheHits + ne);

    memcpy(&(che->che_hash) , ch, sizeof(struct CacheHash_t));
    che->che_Next = 0;
    che->che_ReadArt = 0;
    che->che_Hits = 0;
    che->che_LastHi = endNo;
    che->che_NewArt = 0;

    hflock(CacheHitsFD, 0, XLOCK_UN);
    return che;
}

struct CacheHitEntry*
UpdateCacheHits(char *groupName, int grpIter, artno_t endNo, int cachehit) {
    struct CacheHash_t ch;
    struct CacheHitEntry *che=NULL;

    if ((groupName==NULL) || (grpIter<0)) {
	return NULL;
    }
    SetCacheHash(&ch, groupName, grpIter, &DOpts.ReaderGroupHashMethod);

    che = FindCacheHitEntry(&ch);
    if (che==NULL) {
	che = CreateCacheHitEntry(&ch, endNo);
	if (che==NULL) {
	    return NULL;
	}
    }

    if (cachehit) {
	che->che_Hits++;
    } else {
	che->che_ReadArt++;
    }
    return che;
}

void
OpenCacheHits(void) {
    struct CacheHitHead chh;
    int r;

    if (CacheHitsFD >= 0) {
	return;
    } 
    if (XPageMask==0) {
	XPageMask = getpagesize()-1;
    }
    CacheHitsFD = open(PatDbExpand(CacheHitsPat), O_RDWR|O_CREAT, 0644);
    if (CacheHitsFD<0) {
	logit(LOG_ERR, "Can not open cache hits file (%s)", PatDbExpand(CacheHitsPat));
	return;
    }

    r = read(CacheHitsFD, &chh, sizeof(struct CacheHitHead));
    if ( (r < sizeof(struct CacheHitHead)) || (chh.chh_magic != CHMAGIC) || (chh.chh_version != CHVERSION) ) {
	/* unusable cache, recreating it */
	hflock(CacheHitsFD, 0, XLOCK_EX);
	lseek(CacheHitsFD, 0L, 0);
    	r = read(CacheHitsFD, &chh, sizeof(struct CacheHitHead));
    	if ( (r < sizeof(struct CacheHitHead)) || (chh.chh_magic != CHMAGIC) || (chh.chh_version != CHVERSION) ) {
	    /* clean the cache only if no one had the lock before */
	    char buf[512];
	    int i,len;

	    ftruncate(CacheHitsFD, sizeof(struct CacheHitHead));
	    fsync(CacheHitsFD);
	    bzero(&chh, sizeof(struct CacheHitHead));
	    chh.chh_magic = CHMAGIC;
	    chh.chh_version = CHVERSION;
	    chh.chh_hashSize = DOpts.ReaderCacheHashSize;
	    chh.chh_newEntry = sizeof(struct CacheHitHead)+DOpts.ReaderCacheHashSize*sizeof(uint32);
	    chh.chh_end = (chh.chh_newEntry+XPageMask) & ~XPageMask;
	    time(&(chh.chh_lastExpired));

	    lseek(CacheHitsFD, 0L, 0);
	    write(CacheHitsFD, &chh, sizeof(struct CacheHitHead));
	    i = sizeof(struct CacheHitHead);
	    bzero(buf, sizeof(buf));
	    len = sizeof(buf);
	    while(i<chh.chh_end) {
		if (i+len>chh.chh_end) len=chh.chh_end-i;
		write(CacheHitsFD, buf, len);
		i += len;
	    }
	    fsync(CacheHitsFD);
	}
	hflock(CacheHitsFD, 0, XLOCK_UN);
    }
    CacheHitsEnd = chh.chh_end;
    CacheHits = mmap(NULL, chh.chh_end, PROT_READ|PROT_WRITE, MAP_SHARED, CacheHitsFD, 0);
    if (CacheHits == NULL) {
    	CacheHitsEnd = 0;
	logit(LOG_ERR, "Error on cache hits mmap (%s)", strerror(errno));
	close(CacheHitsFD);
	return;
    }
}

