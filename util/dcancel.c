
/*
 * DCANCEL.C	- cancel articles and rewrite spool files.
 *
 * (c)Copyright 2003, Russell Vincent, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include <dreaderd/defs.h>

void CancelReader(char *msgid);
void AddToSpoolCancel(History *h);
void RewriteFiles(void);
void PrintCancelList(void);

typedef struct HistoryList {
    History h;
    struct HistoryList *nextent;
    struct HistoryList *nextspool;
    struct HistoryList *nextdir;
    struct HistoryList *nextfile;
} HistoryList;

int VerboseOpt = 1;
int ForReal = 1;
int HistoryCancel = 0;
int ReaderCancel = 0;
int SpoolCancel = 0;
HistoryList *CancelList = NULL;
int CancelCount = 0;
int RewriteCount = 0;
int RemoveCount = 0;
int ReaderCount = 0;
int UnExpire = 0;
KPDB *KDBActive;

void
Usage(void)
{
	printf("This program performs a cancel on a set of msgid's\n");
	printf("dcancel -h|-s [-u] [-v]\n");
	printf("\t-a\tstandard cancel (history and spool)\n");
	printf("\t-h\tcancel articles in feeder history\n");
	printf("\t-s\tcancel articles in feeder spool - not recommended\n");
	printf("\t-r\tcancel articles in reader header index (group:number)\n");
	printf("\t-u\tuncancel history entries\n");
	printf("\t-v\tbe more verbose\n");
	printf("\t-C file\tspecify diablo.config to use\n");
	printf("\t-d[n]\tset debug [with optional level]\n");
	printf("\t-V\tprint version and exit\n");
	printf("\n");
	printf("By default, the list of msgid's is read from stdin\n");
	exit(0);
}


int
main(int ac, char **av)
{
    int n;
    char buf[32768];
    char *msgid;
    FILE *inputFile = NULL;
    History h;

    DebugOpt = 1;
    LoadDiabloConfig(ac, av);

    for (n = 1; n < ac; ++n) {
	char *ptr = av[n];

	if (*ptr == '-') {
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'a':
		HistoryCancel = 1;
		SpoolCancel = 1;
		break;
	    case 'h':
		HistoryCancel = 1;
		break;
	    case 'n':
		ForReal = 0;
		break;
	    case 'r':
		ReaderCancel = 1;
		break;
	    case 's':
		SpoolCancel = 1;
		break;
	    case 'u':
		UnExpire = 1;
		break;
	    case 'v':
		VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
		break;
	    case 'C':		/* parsed by LoadDiabloConfig */
		if (*ptr == 0)
		    ++n;
		break;
	    case 'd':
		VerboseOpt++;
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
	    case 'V':
		PrintVersion();
		break;
	    default:
		fprintf(stderr, "Illegal option: %s\n", ptr - 2);
		exit(1);
	    }
	} else {
	    fprintf(stderr, "Illegal argument: %s\n", ptr);
	    exit(1);
	}
    }

    /*
     * this isn't an error, but a request to list 
     * valid arguments, then exit.
     */

    if (ac == 1)
	Usage();
    if (!HistoryCancel && !ReaderCancel && !SpoolCancel)
	Usage();
    if (HistoryCancel || SpoolCancel) {
	HistoryOpen(NULL, 0);
	LoadSpoolCtl(0, 1);
    }

    if (inputFile == NULL)
	inputFile = stdin;
    while (fgets(buf, sizeof(buf), inputFile) != NULL) {
	msgid = buf;
	while (isspace((int)*msgid))
	    msgid++;
	strtok(msgid, " \t\n");
	if (HistoryCancel) {
	    if (HistoryExpire(msgid, &h, UnExpire)) {
		if (h.iter == (uint16)-1) {
		    fprintf(stderr, "Already removed %s\n", msgid);
		    continue;
		}
		CancelCount++;
		if (VerboseOpt)
		    printf("%sCancelled history entry for: %s\n",
					UnExpire ? "Un" : "", msgid);
		if (SpoolCancel)
		    AddToSpoolCancel(&h);
	    } else {
		printf("No history entry for: %s\n", msgid);
	    }
	} else if (SpoolCancel) {
	    if (HistoryLookup(msgid, &h) == 0)
		AddToSpoolCancel(&h);

	}
	if (ReaderCancel)
	    CancelReader(msgid);
    }
    PrintCancelList();
    if (SpoolCancel)
	RewriteFiles();
    if (VerboseOpt)
	printf("Rewrote %d spool files with %d articles removed\n",
						RewriteCount, RemoveCount);
    exit(0);
}

/*
 * Create a linked list of spool objects, directories and filenames
 * so that we can work on all entries in one file at a time
 */
void
AddToSpoolCancel(History *h)
{
    HistoryList *hl;
    HistoryList *thl;
    int spool = H_SPOOL(h->exp);
    int dir = h->gmt;
    int f = h->iter;

    hl = (HistoryList *)malloc(sizeof(HistoryList));
    if (hl == NULL) {
	fprintf(stderr, "malloc error: %s\n", strerror(errno));
	exit(1);
    }
    bzero(hl, sizeof(HistoryList));
    hl->h = *h;
    if (CancelList == NULL) {
	CancelList = hl;
	return;
    }
    thl = CancelList;
    while (thl->nextspool != NULL && H_SPOOL(thl->h.exp) != spool)
	thl = thl->nextspool;
    if (H_SPOOL(thl->h.exp) != spool) {
	thl->nextspool = hl;
	return;
    }
    while (thl->nextdir != NULL && thl->h.gmt != dir)
	thl = thl->nextdir;
    if (thl->h.gmt != dir) {
	thl->nextdir = hl;
	return;
    }
    while (thl->nextfile != NULL && thl->h.iter != f)
	thl = thl->nextfile;
    if (thl->h.iter != f) {
	thl->nextfile = hl;
	return;
    }
    while (thl->nextent != NULL)
	thl = thl->nextent;
    thl->nextent = hl;
}

void
PrintCancelList(void)
{
    HistoryList *hl_sp;
    HistoryList *hl_dir;
    HistoryList *hl_file;
    HistoryList *hl;

    for (hl_sp = CancelList; hl_sp != NULL; hl_sp = hl_sp->nextspool) {
	printf("SPOOL: %d\n", H_SPOOL(hl_sp->h.exp));
	for (hl_dir = hl_sp; hl_dir != NULL; hl_dir = hl_dir->nextdir) {
	    printf(" DIR: D.%08x\n", hl_dir->h.gmt);
	    hl_file = hl_dir;
	    while (hl_file != NULL) {
		printf("   FILE: B.%04x\n", hl_file->h.iter);
		hl = hl_file;
		while (hl != NULL) {
		    PrintHistory(&hl->h);
		    hl = hl->nextent;
		}
		hl_file = hl_file->nextfile;
	    }
	}
    }
}

void
CancelReader(char *grpinfo)
{
	OverInfo *ov;
	char *group;
	artno_t artno;

	group = strchr(grpinfo, ':');
	if (group == NULL) {
		printf("Missing article number in %s\n", grpinfo);
		return;
	}
	*group++ = '\0';
	artno = atoll(group);
	group = grpinfo;
	ov = GetOverInfo(group);
	if (CancelOverArt(ov, artno)) {
		printf("%lld in %s cancelled\n", artno, group);
	} else {
		printf("Unable to cancel %lld in %s\n", artno, group);
	}
}

char *
getMsgId(int fd, char *buf, int hdrLen)
{
    static char msgid[MAXMSGIDLEN + 1];
    char *p;
    char *q;
    int n;

    bzero(msgid, sizeof(msgid));
    p = buf;
    while (p - buf < hdrLen - 15 && (*p != '\n' ||
				strncasecmp(p, "\nMessage-ID:", 12) != 0))
	p++;
    if (strncasecmp(p, "\nMessage-ID:", 12) != 0) {
	fprintf(stderr, "Cannot find Message-ID header!\n");
	return(NULL);
    }
    p += 12;
    while (isspace(*p))
	p++;
    if (*p != '<') {
	fprintf(stderr, "Invalid Message-ID header!\n");
	return(NULL);
    }
    q = p;
    while (q - buf < hdrLen && *q != '>')
	q++;
    if (*q != '>') {
	fprintf(stderr, "Invalid Message-ID header!\n");
	return(NULL);
    }
    n = q - p + 1;
    if (n >= sizeof(msgid))
	n = sizeof(msgid) - 1;
    strncpy(msgid, p, n);
    return(msgid);
}

/*
 * Map an article into memory, using xmap() if it is not compressed or
 * allocating/reallocating a buffer for it if it is compresssed
 *
 * Returns:
 *	pointer	Success
 *	NULL	Failure
 */
char *
MapArticle(int fd, char *fname, off_t offset, uint32 size, int exp, int *artSize, int *compressed, FILE *logFo)
{
    static char *mmapBase = NULL;
    static off_t mmapLen = 0;

    if (mmapBase != NULL)
	xunmap(mmapBase, mmapLen);

    *artSize = 0;
    *compressed = 0;

    if (SpoolCompressed(H_SPOOL(exp))) {
#ifdef X_USE_ZLIB
	static char *base = NULL;
	static off_t baseLen = 0;

	gzFile *gzf;
	SpoolArtHdr tah = { 0 };
	char *p;

	lseek(fd, offset, 0);
	if (read(fd, &tah, sizeof(SpoolArtHdr)) != sizeof(SpoolArtHdr)) {
	    close(fd);
	    fprintf(logFo, "Unable to read article header (%s)\n",
							strerror(errno));
	    return(NULL);
	}
	if ((uint8)tah.Magic1 != STORE_MAGIC1 &&
					(uint8)tah.Magic2 != STORE_MAGIC2) {
	    lseek(fd, h->boffset, 0);
	    tah.Magic1 = STORE_MAGIC1;
	    tah.Magic2 = STORE_MAGIC2;
	    tah.HeadLen = sizeof(tah);
	    tah.ArtLen = h->bsize;
	    tah.ArtHdrLen = h->bsize;
	    tah.StoreLen = h->bsize;
	}
	gzf = gzdopen(fd, "r");
	if (gzf == NULL) {
	    fprintf(logFo, "Error opening compressed article\n");
	    return(NULL);
	}
	if (base == NULL || baseLen < tah.HeadLen + tah.ArtLen + 2) {
	    baseLen = tah.ArtLen + tah.HeadLen + 2;
	    base = (char *)realloc(base, baseLen);
	    if (base == NULL) {
		fprintf(logFo, "Unable to malloc %d bytes for article (%s)\n",
						baseLen, strerror(errno));
		gzclose(gzf);
		return(NULL);
	    }
	}
	p = base;
	*p++ = 0;
	bcopy(&tah, p, tah.HeadLen);
	p += tah.HeadLen;
	if (gzread(gzf, p, tah.ArtLen) != tah.ArtLen) {
	    fprintf(logFo, "Error uncompressing article\n");
	    return(NULL);
	}
	p[tah.ArtLen] = 0;
	*artSize = tah.ArtLen + tah.HeadLen;
	*compressed = 1;
	gzclose(gzf);
#else
        fprintf(logFo, "Compressed spools not yet supported with dcancel\n");
#endif
    } else {
	mmapLen = size;
	mmapBase = xmap(NULL, mmapLen, PROT_READ, MAP_SHARED, fd, offset);
	if (mmapBase != NULL) {
	    *artSize = size;
	    *compressed = 0;
	} else {
	    fprintf(logFo, "Unable to map file %s: %s (%llu,%u)\n",
					fname, strerror(errno), offset, size);
	 }
	return(mmapBase);
    }
    return(NULL);
}

void
rewriteFile(HistoryList *hl_file)
{
    int oldf;
    int newf = -1;
    static char *artBuf = NULL;
    int n;
    int eof = 0;
    char opath[PATH_MAX];
    char npath[PATH_MAX];
    SpoolArtHdr artHdr;
    off_t filepos;
    int opened = 0;
    HistoryList *hl;
    History h;
    int copyArt;
    char *msgid;
    int artSize;
    int compressed;
    hash_t hv;
    
    /*
     * Get the article filename and open it
     */
    ArticleFileName(opath, sizeof(opath), &hl_file->h, ARTFILE_FILE_REL);

    printf("Rewriting: %s\n", opath);

    if ((oldf = open(opath, O_RDONLY)) == -1) {
	fprintf(stderr, "Cannot open %s : %s\n", opath, strerror(errno));
	return;
    }

    /*
     * Find a new spool filename
     */
    n = 0;
    while (!opened) {
	hl_file->h.iter++;
	hl_file->h.iter &= 0x7FFF;
	ArticleFileName(npath, sizeof(npath), &hl_file->h, ARTFILE_FILE_REL);
	if ((newf = open(npath, O_RDWR|O_CREAT|O_EXCL, 0644)) == -1) {
	    if (++n > 200) {
		fprintf(stderr, "Cannot create new spool file %s : %s\n",
							npath, strerror(errno));
		return;
	    }
	    continue;
	}
	opened = 1;
    }

    /*
     * Copy all the articles except the ones at the specified locations
     */
    filepos = 0;
    while (!eof) {
	lseek(oldf, filepos, SEEK_SET);
	/*
	 * Read the article's spool header
	 */
	n = read(oldf, &artHdr, sizeof(artHdr));
	if (n == 0) {
	    eof = 1;
	    break;
	}
	if (n != sizeof(artHdr)) {
	    fprintf(stderr, "Error reading article header at %lld in %s\n",
							filepos, opath);
	    break;
	}
	if (artHdr.Magic1 != STORE_MAGIC1 || artHdr.Magic2 != STORE_MAGIC2) {
	    fprintf(stderr, "Invalid article header at %lld (%x.%x)in %s\n",
				filepos, artHdr.Magic1, artHdr.Magic2, opath);
	    break;
	}

	/*
	 * Map the article into memory (including the artHdr)
	 */
	artBuf = MapArticle(oldf, opath, filepos,
				artHdr.StoreLen,
				SpoolCompressed(H_SPOOL(hl_file->h.exp)),
				&artSize, &compressed, stderr);
	if (artBuf == NULL)
	    break;

	if (compressed) {
	    fprintf(stderr, "Compressed spools are currently not supported\n");
	    break;
	}

	/*
	 * Extract the Message-ID from the article headers
	 */
	msgid = getMsgId(oldf, artBuf, artHdr.ArtHdrLen);
	if (msgid == NULL)
	    break;
	hv = hhash(msgid);

	/*
	 * Lookup the msgid in history for later modification
	 */
	if (HistoryCancel && HistoryLookup(msgid, &h) != 0) {
	    fprintf(stderr, "Cannot find %s in history - skipping\n",
								msgid);
	    filepos += artSize;
	    continue;
	}


	/*
	 * Copy the article unless the offset matches the history entry
	 * for the skipped article(s)
	 */
	copyArt = 1;
	for (hl = hl_file; hl != NULL; hl = hl->nextent) {
	    if (hl->h.hv.h1 == hv.h1 && hl->h.hv.h2 == hv.h2)
		copyArt = 0;
	}
	if (copyArt) {
	    h.boffset = lseek(newf, 0, 1);
	    printf("Copying article at offset %lld to offset %d\n",
							filepos, h.boffset);
	    if (compressed) {
		lseek(oldf, filepos, SEEK_SET);
		read(oldf, artBuf, artHdr.StoreLen);
		if (write(newf, &artBuf, artHdr.StoreLen) != artHdr.StoreLen) {
		    fprintf(stderr, "Error writing article in %s (%s)\n",
							npath, strerror(errno));
		    break;
		}
	    } else {
		if (write(newf, &artBuf, artSize) != artSize) {
		    fprintf(stderr, "Error writing article in %s (%s)\n",
							npath, strerror(errno));
		    break;
		}
	    }
	    filepos += artSize;
	} else {
	    printf("Skipping article at offset %lld\n", filepos);
	    filepos += artHdr.StoreLen;
	    lseek(oldf, filepos, SEEK_SET);
	    h.iter = (uint16)-1;
	    h.boffset = 0;
	    h.bsize = 0;
	    h.gmt = 0;
	    h.exp |= EXPF_EXPIRED;
	    RemoveCount++;
	}

	/*
	 * Update the history entry with the new location or cancel
	 */
	if (HistoryCancel && ForReal)
	    HistoryStore(&h);
    }
    close(oldf);
    close(newf);
    if (eof == 1) {
	RewriteCount++;
	if (ForReal) {
	    struct stat sb;
	    if (stat(npath, &sb) == 0 && sb.st_size == 0) {
		remove(opath);
		remove(npath);
	    } else {
		rename(npath, opath);
	    }
	}
    } else {
	if (ForReal)
	    remove(npath);
    }
}

void
RewriteFiles(void)
{
    HistoryList *hl_spool;
    HistoryList *hl_dir;
    HistoryList *hl_file;
    HistoryList *hl;

    if (chdir(PatExpand(SpoolHomePat)) == -1) {
	fprintf(stderr, "Unable to chdir(%s): %s\n",
				PatExpand(SpoolHomePat), strerror(errno));
	exit(1);
    }
    if (VerboseOpt)
	printf("Rewriting spool file(s) for cancelled articles\n");

    hl_spool = CancelList;
    while (hl_spool != NULL) {
	printf("SPOOL: %d\n", H_SPOOL(hl_spool->h.exp));
	hl_dir = hl_spool;
	while (hl_dir != NULL) {
	    printf(" DIR: D.%08x\n", hl_spool->h.gmt);
	    hl_file = hl_dir;
	    while (hl_file != NULL) {
		printf("   FILE: B.%04x\n", hl_spool->h.iter);
		hl = hl_file;
		while (hl != NULL) {
		    PrintHistory(&hl->h);
		    hl = hl->nextent;
		}
		rewriteFile(hl_file);
		hl_file = hl_file->nextfile;
	    }
	    hl_dir = hl_dir->nextdir;
	}
	hl_spool = hl_spool->nextspool;
    }
}

OverInfo *
GetOverInfo(const char *group)
{
    OverInfo *ov = NULL;

    if (ov == NULL) {
	struct stat st;
	char *path;
	int iter = 0;
	artno_t endNo = 0;

	bzero(&st, sizeof(st));

	ov = zalloc(&SysMemPool, sizeof(OverInfo));
	ov->ov_Group = zallocStr(&SysMemPool, group);

	{
	    const char *rec;
	    int recLen;
	    if ((KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR)) == NULL) {
		fprintf(stderr, "Unable to open active file (%s)\n", strerror(errno));
		exit(1);
	    }
	    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
		return(NULL);
	    }
	    iter = strtol(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 10);
	    endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    KPDBUnlock(KDBActive, rec);
	}

	path = zalloc(&SysMemPool, strlen(PatExpand(GroupHomePat)) + 48);
again:
	{
	    const char *gfname = GFName(group, GRPFTYPE_OVER, 0, 1, iter,
						&DOpts.ReaderGroupHashMethod);

	    sprintf(path, "%s/%s", PatExpand(GroupHomePat), gfname);
	    ov->ov_OFd = -1;
	    if (MakeGroupDirectory(path) == -1)
		logit(LOG_ERR, "Error on overview dir open/create: %s (%s)",
						path, strerror(errno));
	    else
		ov->ov_OFd = xopen(O_RDWR|O_CREAT, 0644, "%s", path);
	}
	if (ov->ov_OFd < 0) {
	    logit(LOG_ERR, "Error on overview open/create for group %s: %s (%s)",
						group, path, strerror(errno));
	    ov = NULL;
	} else {
	    OverHead oh;
	    int r;

	    /*
	     * Leave a shared lock on the over.* file so expireover knows when
	     * it is OK to resize the file.  If the file was renamed-over,
	     * we have to re-open it.
	     */

	    hflock(ov->ov_OFd, 4, XLOCK_SH);

	    if (fstat(ov->ov_OFd, &st) < 0 || st.st_nlink == 0) {
		hflock(ov->ov_OFd, 4, XLOCK_UN);
		close(ov->ov_OFd);
		ov->ov_OFd = -1;
		goto again;
	    }

	    /*
	     * check if new overview file or illegal overview file and size 
	     * accordingly
	     */
	    r = (read(ov->ov_OFd, &oh, sizeof(oh)) != sizeof(oh));
	    if (r == 0 && oh.oh_ByteOrder != OH_BYTEORDER) {
		logit(LOG_CRIT, "Incorrect overview byte order for %s (%s)",
								group, path);
		r = -1;
	    }
	    if (r == 0 && oh.oh_Version > OH_VERSION) {
		logit(LOG_CRIT, "Incorrect overview version for %s (%s)",
								group, path);
		r = -1;
	    }
	    if (r != 0) {
		hflock(ov->ov_OFd, 0, XLOCK_EX);
		/*
		 * we have to test again after we got the lock in case
		 * another process had a lock and adjusted the file.
		 */
		lseek(ov->ov_OFd, 0L, 0);
		if (read(ov->ov_OFd, &oh, sizeof(oh)) != sizeof(oh) ||
		    oh.oh_ByteOrder != OH_BYTEORDER
		) {
		    OverExpire save;

		    GetOverExpire(group, &save);

		    /*
		     * If 'aInitArts' option not given in expireCtl
		     */

		    if (save.oe_InitArts == 0)
			save.oe_InitArts = DEFARTSINGROUP;

		    ftruncate(ov->ov_OFd, 0);
		    st.st_size = sizeof(oh) + sizeof(OverArt) * save.oe_InitArts;
		    ftruncate(ov->ov_OFd, st.st_size);
		    fsync(ov->ov_OFd);
		    lseek(ov->ov_OFd, 0L, 0);
		    bzero(&oh, sizeof(oh));
		    oh.oh_Version = OH_VERSION;
		    oh.oh_ByteOrder = OH_BYTEORDER;
		    oh.oh_HeadSize = sizeof(oh);
		    oh.oh_MaxArts = save.oe_InitArts;
		    strncpy(oh.oh_Gname, group, sizeof(oh.oh_Gname) - 1);
		    oh.oh_DataEntries = save.oe_DataSize;
		    write(ov->ov_OFd, &oh, sizeof(oh));
		    fsync(ov->ov_OFd);
		}
		hflock(ov->ov_OFd, 0, XLOCK_UN);
	    }
	    if (oh.oh_Version < 3)
		oh.oh_DataEntries = OD_HARTS;
	    if (oh.oh_Version > 1 && strcmp(oh.oh_Gname, group) != 0) {
		hflock(ov->ov_OFd, 4, XLOCK_UN);
		close(ov->ov_OFd);
		ov->ov_OFd = -1;
		iter++;
		goto again;
	    }
	    if (iter > 0) {
		char tsBuf[64];
		sprintf(tsBuf,"%06d", iter);
		KPDBWrite(KDBActive, group, "ITER", tsBuf, 0);   
	    }
	    ov->ov_Iter = iter;
	    ov->ov_endNo = endNo;
	    ov->ov_Size = st.st_size;
	    ov->ov_MaxArts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	    ov->ov_DataEntryMask = oh.oh_DataEntries - 1;
	    ov->ov_Head = xmap(NULL, ov->ov_Size, PROT_READ, MAP_SHARED, ov->ov_OFd, 0);
	    if (ov->ov_Head == NULL) {
		logit(LOG_ERR, "Error on overview mmap for group %s (%s)",
						group, strerror(errno));
		ov = NULL;
	    }
	}
	zfree(&SysMemPool, path, strlen(PatExpand(GroupHomePat)) + 48);
    }
    if (ov)
	++ov->ov_Refs;
    return(ov);
}

const OverArt *
GetOverArt(OverInfo *ov, artno_t artno, off_t *ppos)
{
    const OverArt *oa;
    off_t ovpos = ov->ov_Head->oh_HeadSize + 
	    ((artno & 0x7FFFFFFFFFFFFFFFLL) % ov->ov_MaxArts) * sizeof(OverArt);

    /*
     * memory map the overview data.  Check overview record to
     * see if we actually have the requested information.
     */

    oa = (const OverArt *)((const char *)ov->ov_Head + ovpos);

    if (ppos)
	*ppos = ovpos;

    if (DebugOpt > 2)
	printf("OA %08lx %d,%lld %s\n", (long)oa, oa->oa_ArtNo, artno, OA_ARTNOEQ(artno, oa->oa_ArtNo) ? "(match)" : "(MISMATCH)");
    return(oa);
}

/*
 * CancelOverMsgId() - cancel overview by message-id given article number.
 */

int 
CancelOverArt(OverInfo *ov, artno_t artNo)
{
    int r = 0;

    if (ov != NULL) { 
	off_t ovpos = 0;
	const OverArt *oa;

	hflock(ov->ov_OFd, 0, XLOCK_EX);
	oa = GetOverArt(ov, artNo, &ovpos);
	if (OA_ARTNOEQ(artNo, oa->oa_ArtNo)) {
	    OverArt copy = *oa;

	    copy.oa_ArtNo = -1;		/* CANCELED! */
	    lseek(ov->ov_OFd, ovpos, 0);
	    write(ov->ov_OFd, &copy, sizeof(copy));
	    r = 1;
	}
	hflock(ov->ov_OFd, 0, XLOCK_UN);
    }
    return(r);
}

