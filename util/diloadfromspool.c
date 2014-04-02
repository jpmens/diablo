
/*
 * DILOADFROMSPOOL.C	
 *
 *	Scan the specified spool directory or spool file, extract the
 *	Message-ID, offset, and article size, and create the appropriate
 *	entry in the dhistory file.  This command is typically used if 
 *	you have lost your history file entirely and need to regenerate it
 *	from the existing spool or to do a partial recovery from backup
 *	and regeneration the lost portion from the existing spool.
 *	
 *	diloadfromspool ... [-F dhistory] D.directory ... D.directory/B.file...
 *
 *	NOTE:  File specifications must be in the form of D.directory for
 *	a spool directory, or D.directory/B.file for a spool file in order
 *	for diloadfromspool to figure out the history file fields.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

#if defined(__GLIBC__) && !defined(_XOPEN_SOURCE)
  char *strptime(const char *s, const char *format, struct tm *tm);
#endif

int VerboseOpt;
int QuietOpt;
int LoadDupCount;
int LoadCount;
int ForReal = 1;
int RequeueOpt = 0;
int UnExpireOpt = 0;
int FileIdx = 0;
int FileMax = 2048;
char **FileAry;
int hisfd = -1;
char msgId[MAXMSGIDLEN];
char newsgroups[16384];
uint32 GmtStart = 0;
uint32 GmtEnd = 0;

void ScanSpoolObject(uint16 spoolobj);
void ScanSpool(uint16 spoolobj);
void ScanSpoolDirectory(char *dpath, int gmt, uint16 spoolobj);
void ScanSpoolFile(char *fpath, int gmt, int iter, uint16 spoolobj);
void ScanSpoolFileMap(const char *base, int bytes, int gmt, int iter, char *dpath, uint16 spoolobj, int fd);
void ScanSpoolFileMapOld(const char *base, int bytes, int gmt, int iter, char *dpath, uint16 spoolobj);
void DoArticle(History *h, const char *id, char *nglist, char *dist,
		char *npath, int headOnly, char *artType, char *cSize);
int strSort(const void *s1, const void *s2);

void
Usage(void)
{
    printf("This program scans the diablo spool and performs various tasks\n");
    printf("based on the articles found.\n\n");
    printf("diloadfromspool [-a] [-F dhistory-file] [-f] [-h hashtablesize]\n");
    printf("		    [-n] [-Q] [-q] [-S nn] [-tb TT] [-te TT]\n");
    printf("		    [-u] [-v] [spooldir/spoolfile]\n");
    printf("\t-a scan all the spool objects found in dspool.ctl\n");
    printf("\t-e unexpire all entries marked expired in dhistory\n");
    printf("\t-F specify the history file to update\n");
    printf("\t-f fast mode - lock history file\n");
    printf("\t-h specify the hash table size used when creating a new history\n");
    printf("\t-n prevents the program from adding new records to history\n");
    printf("\t-Q print articles in format suitable for drequeue\n");
    printf("\t-q quiet mode\n");
    printf("\t-S specify the spool object to scan\n");
    printf("\t-tb only scan spool directories since this time\n");
    printf("\t-te only scan spool directories until this time\n");
    printf("\t-u check for duplicates in history before adding\n");
    printf("\t-v verbose mode\n");
    printf("\n");
    printf("\t the TT value is specified as YYYYMMDDHHMM or D.NNNNNNNN\n");
    exit(1);
}

uint32
timeConv(char *tstr)
{
    if (tstr == NULL || (strlen(tstr) != 12 && strlen(tstr) != 10)) {
	fprintf(stderr, "Invalid time specification for -t option\n");
	fprintf(stderr, "Must be yyyymmddhhmm or D.nnnnnnnn\n");
	Usage();
    }
    if (strlen(tstr) == 10) {
	uint32 t;
	if (sscanf(tstr, "D.%08x", &t) != 1) {
	    fprintf(stderr, "Invalid time specification for -t option\n");
	    fprintf(stderr, "Must be yyyymmddhhmm or D.nnnnnnnn\n");
	    Usage();
	}
	return(t);
    } else {
	struct tm tm;
	time_t t;

	bzero(&tm, sizeof(tm));
	if (strptime(tstr, "%Y%m%d%H%M", &tm) == NULL) {
	    fprintf(stderr, "Invalid time specification for -t option\n");
	    fprintf(stderr, "Must be yyyymmddhhmm or D.nnnnnnnn\n");
	    Usage();
	}
	t = mktime(&tm);
	return((uint32)(t / 60));
    }
}

int
main(int ac, char **av)
{
    int flags = 0;
    int uflag = 0;
    int aflag = 0;
    uint16 spoolObj = (uint16)-1;
    char *historyFileName = NULL;

    LoadDiabloConfig(ac, av);

    FileAry = calloc(sizeof(char *), FileMax);

    {
	int i;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-') {
		if (FileIdx == FileMax) {
		    FileMax = FileMax * 2;
		    FileAry = realloc(FileAry, sizeof(char *) * FileMax);
		}
		FileAry[FileIdx++] = ptr;
		continue;
	    }
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'a':
		aflag = 1;
		break;
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'd':
		DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : strtol(av[++i], NULL, 0);
		break;
	    case 'e':
		UnExpireOpt = 1;
		break;
	    case 'F':
		historyFileName = (*ptr) ? ptr : av[++i];
		break;
	    case 'f':
		flags |= HGF_FAST | HGF_NOSEARCH;
		break;
	    case 'h':
		NewHSize = bsizetol((*ptr) ? ptr : av[++i]);
		if ((NewHSize ^ (NewHSize - 1)) != (NewHSize << 1) - 1) {
		    fprintf(stderr, "specified history size is not a power of 2\n");
		    exit(1);
		}
		break;
	    case 'n':
		ForReal = 0;
		break;
	    case 'Q':
		RequeueOpt = 1;
		break;
	    case 'q':
		QuietOpt = 1;
		break;
	    case 'S':
		spoolObj = (*ptr) ? strtol(ptr, NULL, 10) : strtol(av[++i], NULL, 10);
		break;
	    case 't':
		    if (*ptr == 'b') {
			ptr++;
			GmtStart = timeConv(*ptr ? ptr : av[++i]);
		    } else if (*ptr == 'e') {
			ptr++;
			GmtEnd = timeConv(*ptr ? ptr : av[++i]);
		    } else {
			fprintf(stderr, "Invalid option: %s\n", &ptr[-2]);
			Usage();
		    }
		break;
	    case 'u':
		uflag = 1;
		break;
	    case 'V':
		PrintVersion();
		break;
	    case 'v':
		VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
		break;
	    default:
		Usage();
	    }
	}
    }

    if (!UnExpireOpt && !aflag && FileIdx == 0 && spoolObj == (uint16)-1)
	Usage();
    if (flags & HGF_FAST || UnExpireOpt) {
	struct stat st;

	if (historyFileName == NULL) {
	    fprintf(stderr, "You cannot run fastmode/unexpire without specifying a filename!\n");
	    exit(1);
	}
	if (stat(historyFileName, &st) == 0 && uflag == 0) {
	    fprintf(stderr, "-f history files may not previously exist unless you also specify -u\n");
	    fprintf(stderr, "WARNING! -f -u is NOT suggested!\n");
	    exit(1);
	}
	if (uflag)
	    flags &= ~HGF_NOSEARCH;
    }

    if (VerboseOpt) {
	if (GmtStart != 0 && GmtEnd != 0)
	    printf("Scanning directories from D.%08x to D.%08x\n", GmtStart, GmtEnd);
	else if (GmtStart == 0 && GmtEnd != 0)
	    printf("Scanning directories from earliest to D.%08x\n", GmtEnd);
	else if (GmtStart != 0 && GmtEnd == 0)
	    printf("Scanning directories from D.%08x to latest\n", GmtStart);
    }
    if (UnExpireOpt) {
	hisfd = open(historyFileName, O_RDWR);
	if (hisfd == -1) {
	    fprintf(stderr, "Unable to open history (%s): %s\n",
					historyFileName, strerror(errno));
	    exit(1);
	}
    }

    LoadSpoolCtl(0, 1);

    if (RequeueOpt) {
	ForReal = 0;
	QuietOpt = 1;
    } else {

	/*
	 * historyFileName can be NULL and causes the default dhistory path
	 * to be used.
	 */

	HistoryOpen(historyFileName, flags);
    }

    {
	int i;

	for (i = 0; i < FileIdx; ++i) {
	    struct stat st;

	    if (VerboseOpt > 1)
		printf("Check: %s\n", FileAry[i]);

	    if (stat(FileAry[i], &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
		    int gmt;

		    if (sscanf(FileAry[i], "D.%x", &gmt) == 1) {
			ScanSpoolDirectory(FileAry[i], gmt, spoolObj);
		    } else {
			fprintf(stderr, "Illegal path format for dir: %s\n",
								FileAry[i]);
		    }
		} else {
		    int gmt;
		    int iter;

		    if (sscanf(FileAry[i], "D.%x/B.%x", &gmt, &iter) == 2) {
			ScanSpoolFile(FileAry[i], gmt, iter, spoolObj);
		    } else {
			fprintf(stderr, "Illegal path format for file: %s\n",
								FileAry[i]);
		    }
		}
	    } else {
		printf("Unable to stat: %s (%s)\n", FileAry[i], strerror(errno));
	    }
	}
    }
    if (aflag || spoolObj != (uint16)-1) {
	ScanSpoolObject(spoolObj);
    }
    printf("diload: %d/%d entries loaded (%d duplicate)\n", LoadCount,
					LoadCount + LoadDupCount, LoadDupCount);

    if (!RequeueOpt) 
    {
	int r = HistoryClose();

	if (r == RCOK)
	    return(0);
	else
	    return(1);
    }
    return(0);
    /* not reached */
}

void
ScanSpoolObject(uint16 spoolobj)
{
    char *path;
    uint16 spoolnum;
    int i;

    for (i = GetFirstSpool(&spoolnum, &path, NULL, NULL, NULL, NULL, NULL); i;
		i = GetNextSpool(&spoolnum, &path, NULL, NULL, NULL, NULL, NULL))  {
	if (spoolobj == (uint16)-1 || spoolobj == spoolnum) {

	    if (path == NULL || !*path)
		continue;
	    if (chdir(PatExpand(SpoolHomePat)) == -1 || chdir(path) == -1) {
		fprintf(stderr, "Unable to chdir(%s/%s): %s\n",
				PatExpand(SpoolHomePat), path, strerror(errno));
		exit(1);
	    }

	    ScanSpool(spoolnum);

	    /*
	     * Sort directories
	     */
	    if (FileIdx > 1)
		qsort(FileAry, FileIdx, sizeof(char *), strSort);

	    /*
	     * Process directories
	     */
	    {
		int i;

		for (i = 0; i < FileIdx; ++i) {
		    int gmt;
		    char *p;
	
		    p = strstr(FileAry[i], "D.");
		    if (p && sscanf(p, "D.%x", &gmt) == 1) {
			ScanSpoolDirectory(FileAry[i], gmt, spoolnum);
		    }
		}
	    }
	    FileIdx = 0;
	}
    }
}

void
ScanSpool(uint16 spoolobj)
{
    DIR *dir;

    if (VerboseOpt)
	printf("Scanning spool %02d\n", spoolobj);

    if ((dir = opendir(".")) != NULL) {
	den_t *den;

	while ((den = readdir(dir)) != NULL) {
	    int gmt;
	    int sd;

	    if (sscanf(den->d_name, "D.%x", &gmt) == 1) {
		if ((GmtStart != 0 && gmt < GmtStart) ||
					(GmtEnd != 0 && gmt > GmtEnd))
		    continue;
		if (FileIdx == FileMax) {
		    FileMax = FileMax * 2;
		    FileAry = realloc(FileAry, FileMax * sizeof(char *));
		}
		FileAry[FileIdx++] = strdup(den->d_name);
	    } else if (sscanf(den->d_name, "N.%x", &sd) == 1) {
		chdir(den->d_name);
		ScanSpool(spoolobj);
		chdir("..");
	    }
	}
	closedir(dir);
    }
}

void
ScanSpoolDirectory(char *dpath, int gmt, uint16 spoolobj)
{
    DIR *dir;

    if ((GmtStart != 0 && gmt < GmtStart) || (GmtEnd != 0 && gmt > GmtEnd))
	return;

    if (VerboseOpt)
	printf(" Scanning directory: %s\n", dpath);

    if ((dir = opendir(dpath)) != NULL) {
	den_t *den;
	char path[PATH_MAX];

	while ((den = readdir(dir)) != NULL) {
	    int iter;

	    if (gmt && sscanf(den->d_name, "B.%x", &iter) == 1) {
		snprintf(path, sizeof(path), "%s/%s", dpath, den->d_name);
		ScanSpoolFile(path, gmt, iter, spoolobj);
	    }
	}
	closedir(dir);
    }
}

void
ScanSpoolFile(char *fpath, int gmt, int iter, uint16 spoolobj)
{
    int fd;
    char *base;
    int bytes;
    struct stat st;

    if (VerboseOpt)
	printf("  Scanning file: %s\n", fpath);

    errno = 0;
    if ((fd = open(fpath, O_RDONLY)) < 0) {
	printf("    %s\t%s\n", fpath, strerror(errno));
	return;
    }
    if (fstat(fd, &st) < 0) {
	printf("    %s\t%s\n", fpath, strerror(errno));
	close(fd);
	return;
    }
    bytes = st.st_size;

    base = xmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (base == NULL) {
	printf("    %s\t%s\n", fpath, strerror(errno));
	close(fd);
	return;
    }

    if (!QuietOpt)
	printf("    %s: ", fpath);

    if (bytes > 2 && (uint8)*base == (uint8)STORE_MAGIC1 &&
				(uint8)*(base + 1) == (uint8)STORE_MAGIC2)
	ScanSpoolFileMap(base, bytes, gmt, iter, fpath, spoolobj, fd);
    else
	ScanSpoolFileMapOld(base, bytes, gmt, iter, fpath, spoolobj);

    if (!QuietOpt)
	printf("\n");
    xunmap(base, bytes);
    close(fd);
}

void
ScanSpoolFileMap(const char *base, int bytes, int gmt, int iter, char *dpath, uint16 spoolobj, int fd)
{
    int count = 0;
    int b = 0;
    int arthdrlen;
    char *artbase = NULL;
    char *artpos;
    SpoolArtHdr ah;
    char cSize[64];
    int headOnly;

    while (b < bytes) {
	bcopy(base + b, &ah, sizeof(ah));
	if ((uint8)ah.Magic1 != STORE_MAGIC1 ||
					(uint8)ah.Magic2 != STORE_MAGIC2) {
	    printf("\tFailed at offset %d: invalid header magic (%d:%d)\n", b,
						ah.Magic1, ah.Magic2);
	    ScanSpoolFileMapOld(base + b, bytes - b, gmt, iter, dpath, spoolobj);
	    return;
	}
	arthdrlen = ah.ArtHdrLen;
	if (ah.StoreType & STORETYPE_GZIP) {
#ifdef USE_ZLIB
	    gzFile *gzf;
	    long len = ah.ArtLen;

	    artbase = (char *)malloc(ah.ArtLen + 2);
	    bzero(artbase, ah.ArtLen + 2);
	    lseek(fd, b + ah.HeadLen, 0);
	    if ((gzf = gzdopen(dup(fd), "r")) != NULL) {
		if (gzread(gzf, artbase, len) != len)
		    arthdrlen = 0;
		gzclose(gzf);
	    } else {
		arthdrlen = 0;
	    }
#else
	    printf("\tCompressed file detected and compression support not enabled\n");
	    arthdrlen = 0;
#endif
	} else {
	    artbase = (char *)base + b + ah.HeadLen;
	}
	artpos = artbase;
	msgId[0] = 0;
	newsgroups[0] = 0;
	while (arthdrlen > 11) {
	    int l;

	    /*
	     * Scan line
	     */

	    for (l = 0; l < arthdrlen; l++) {
		if (artpos[l] == '\n') {
		    l++;
		    if (l < arthdrlen && artpos[l] != ' ' && artpos[l] != '\t')
			break;
		    l--;
		}
	    }

	    if (strncasecmp(artpos, "Message-ID:", 11) == 0) {
		diablo_strlcpynl(msgId, artpos + 11, l - 11, sizeof(msgId));
	    } else if (strncasecmp(artpos, "Newsgroups:", 11) == 0) {
		diablo_strlcpynl2(newsgroups, ',', artpos + 11, l - 11, sizeof(newsgroups));
	    }
	    arthdrlen -= l;
	    artpos += l;
	}
	if (msgId[0]) {
	    const char *id = MsgId(msgId, NULL);
	    History h = { 0 };

	    h.hv = hhash(id);
	    h.iter = iter;
	    h.gmt = gmt;
	    h.exp = 100 + spoolobj;
	    h.boffset = b;
	    h.bsize = ah.StoreLen - 1;
	    headOnly = 0;
	    if (ah.ArtHdrLen == ah.ArtLen) {
		h.exp |= EXPF_HEADONLY;
		headOnly = 1;
	    }
	    cSize[0] = 0;
	    if (ah.StoreType & STORETYPE_GZIP) {
		h.bsize = ah.ArtLen + ah.HeadLen;
		sprintf(cSize, "%d", ah.StoreLen);
	    }
	    DoArticle(&h, id, newsgroups, " ", " ", headOnly, "0", cSize);
	    count++;
	} else {
	    if (VerboseOpt)
		printf("No Message-ID for %d,%d\n", b, ah.StoreLen - 1);
	}
	b += ah.StoreLen;
	if (ah.StoreType & STORETYPE_GZIP) {
	    free(artbase);
	    b++;
	}
    }
    if (!QuietOpt)
	printf("%d entries", count);
}

void
ScanSpoolFileMapOld(const char *base, int bytes, int gmt, int iter, char *dpath, uint16 spoolobj)
{
    int b = 0;
    int count = 0;

    /*
     * scan file
     */

    printf(" (old format) ");
    while (b < bytes) {
	int i = b;
	int inHeader = 1;
	int linesLeft = -1;
	int numLines = 0;

	msgId[0] = 0;
	newsgroups[0] = 0;

	/*
	 * scan article
	 */

	while (i < bytes && linesLeft && base[i] != 0) {
	    int l;

	    /*
	     * Scan line
	     */

	    for (l = i; l < bytes; l++) {
		if (base[l] == '\n') {
		    l++;
		    if (l == i + 1 || !inHeader)
			break;
		    if (l < bytes && base[l] != ' ' && base[l] != '\t')
			break;
		    l--;
		}
	    }

	    if (inHeader) {
		if (l - i == 1) {
		    inHeader = 0;
		} else if (strncasecmp(base + i, "Lines:", 6) == 0) {
		    linesLeft = strtol(base + i + 6, NULL, 0);
		} else if (strncasecmp(base + i, "Message-ID:", 11) == 0) {
		    diablo_strlcpynl(msgId, base + i + 11, l - i - 11, sizeof(msgId));
		} else if (strncasecmp(base + i, "Newsgroups:", 11) == 0) {
		    diablo_strlcpynl2(newsgroups, ',', base + i + 11, l - i - 11, sizeof(newsgroups));
		}
	    } else {
		--linesLeft;
		++numLines;
	    }
	    i = l;
	}
	if (i < bytes && base[i] == 0) {
	    const char *id = MsgId(msgId, NULL);
	    History h = { 0 };

	    h.hv = hhash(id);
	    h.iter = iter;
	    h.gmt = gmt;
	    h.exp = 100 + spoolobj;
	    h.boffset = b;
	    h.bsize = i - b;
	    if (numLines == 0)
		h.exp |= EXPF_HEADONLY;
	    DoArticle(&h, id, newsgroups, " ", " ", 0, " ", " ");
	    count++;
	    ++i;
	} else {
	    if (!QuietOpt) {
		printf("\tFailed %d,%d %s\n", b, i - b, MsgId(msgId, NULL));
		fflush(stdout);
	    }
	    /*
	    write(1, base + b, i - b);
	    write(1, "*", 1);
	    printf("(%d)\n", base[i]);
	    */

	    while (i < bytes && base[i] != 0)
		++i;
	    if (i < bytes)
		++i;
	}
	b = i;
    }
    if (!QuietOpt)
	printf("%d entries", count);
}

void
DoArticle(History *h, const char *id, char *nglist, char *dist,
		char *npath, int headOnly, char *artType, char *cSize)
{
    int r = 0;
    if (RequeueOpt) {
	char path[PATH_MAX];
	ArticleFileName(path, (int)sizeof(path), h, ARTFILE_FILE_REL);
	printf("SOUT\t%s\t%lld,%ld\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
	    path, (long long)h->boffset, (long)h->bsize, id, nglist, dist,
	    npath, headOnly, artType, cSize
	);
	++LoadCount;
    } else {
	History htmp;

	if ((r = HistoryLookupByHash(h->hv, &htmp)) == 0) {
	    if (UnExpireOpt) {
		uint32 pos = HistoryPosLookupByHash(h->hv, &htmp);
		htmp.exp &= ~EXPF_EXPIRED;
		if (pos != -1 && ForReal && htmp.iter == h->iter &&
						htmp.boffset == h->boffset)
		    HistoryStoreExp(&htmp, (HistIndex)pos);
	    }
	    ++LoadDupCount;
	} else {
	    if (ForReal && !UnExpireOpt)
		HistoryAdd(id, h);
	    ++LoadCount;
	}
    }
    if (VerboseOpt > 1 || (VerboseOpt && r != 0))
	printf("\tMessage %d,%d %s %s\n", h->boffset, h->bsize,
				((r == 0) ? "dup" : "add"), id);
}

int     
strSort(const void *s1, const void *s2)
{ 
    char *str1 = *(char **)s1;
    char *str2 = *(char **)s2;

    str1 = strstr(str1, "D.");
    str2 = strstr(str2, "D.");
    return(strcmp(str1, str2));
}  

