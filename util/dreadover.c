
/*
 * DREADOVER group:article
 *
 *	Read the overview record related to an article in a specific group.
 *	This command is typically used for debugging purposes only.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include <dreaderd/defs.h>

char * allocTmpCopy(const char *buf, int bufLen);
void ShowHeader(char *path);
void DReadOver(char *data);
void Usage(void);
void OverDump(char *doGroup);

int ForceOpt = 0;
int ViewData = 0;
int VerboseMode = 0;
int DumpHeader = 0;

char *
allocTmpCopy(const char *buf, int bufLen)
{
    static char *SaveAry[8];
    static int SaveCnt;
    char **pptr;

    SaveCnt = (SaveCnt + 1) % arysize(SaveAry);
    pptr = &SaveAry[SaveCnt];
    if (*pptr)
        free(*pptr);
    *pptr = malloc(bufLen + 1);
    memcpy(*pptr, buf, bufLen);
    (*pptr)[bufLen] = 0;
    return(*pptr);
}

int
main(int ac, char **av)
{
    int i;
    int Doneone = 0;

    LoadDiabloConfig(ac, av);

    if (ac < 2)
	Usage();

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-' || strcmp(ptr, "-") == 0) {
	    if (*ptr != '-') {
		if (DumpHeader) {
	    	    OverDump(ptr);
		} else {
		    DReadOver(ptr);
		}
	    } else {
		char buf[512];
		while (fgets(buf, sizeof(buf), stdin) != NULL)
		    DReadOver(buf);
	    }
	    Doneone = 1;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    OverDump(NULL);
	    Doneone = 1;
	    break;
	case 'C':
	    /* diablo.config path */
	    break;
	case 'd':
	    VerboseMode = 1;
	    break;
	case 'f':
	    ForceOpt = 1;
	    break;
	case 'H':
	    ShowHeader((*ptr) ? ptr : av[++i]);
	    exit(0);
	    break;
	case 'h':
	    DumpHeader = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    ViewData = 1;
	    break;
	default:
	    Usage();
	}
    }
    if (!Doneone)
	Usage();
    return(0);
}

void
Usage(void)
{
    fprintf(stderr, "Prints out overview info from the overview cache\n");
    fprintf(stderr, "  Usage: dreadover [-f] [-v] [-a] [group:artno[-artnoend]]\n");
    fprintf(stderr, "     -a       : List maxarts for all newsgroups\n");
    fprintf(stderr, "     -f       : Force retrieval if art no mismatch\n");
    fprintf(stderr, "     -h       : Print overview header details\n");
    fprintf(stderr, "     -v       : Also view the overview data\n");
    fprintf(stderr, "     group    : The newsgroup group name\n");
    fprintf(stderr, "     artno    : The starting article number\n");
    fprintf(stderr, "     artnoend : The ending article number\n");
    exit(1);
}

void
ShowHeader(char *path)
{
    int fd;
    OverHead oh;
    struct stat st;
    int maxarts;

	if ((fd = open(path, O_RDONLY)) < 0) {
	    fprintf(stderr, "Cannot open file: %s(%s)\n", path, strerror(errno));
	    return;
	}

	if (read(fd, &oh, sizeof(oh)) != sizeof(oh)) {
	    fprintf(stderr, "(read header fail)\n");
	    close(fd);
	    return;
	}

	if (oh.oh_Version > OH_VERSION) {
	    fprintf(stderr, "(wrong overview header version)\n");
	    close(fd);
	    return;
	}

	if (oh.oh_ByteOrder != OH_BYTEORDER) {
	    if (VerboseMode)
	    fprintf(stderr, "(wrong overview header byte order)\n");
	    close(fd);
	    return;
	}
	if (oh.oh_Version < 3)
	    oh.oh_DataEntries = OD_HARTS;

	fstat(fd, &st);
	maxarts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	printf("\tmaxarts: %d\n", maxarts);
	printf("\thead version: %d\n", oh.oh_Version);
	printf("\thead gname: %s\n", oh.oh_Gname);
	printf("\tdata entries: %d\n", oh.oh_DataEntries);
	close(fd);
}

void
DReadOver(char *data)
{
    const char *gfname;
    const char *slash;
    char *ptr, *p;
    artno_t artNo = 0;
    artno_t artNoStart = 0;
    artno_t artNoEnd = 0;
    int fd1;
    gzFile zfd2;
    OverHead oh;
    struct stat st;
    int maxarts;
    char path[1024];

    if ((ptr = strrchr(data, ':')) != NULL) {
	*ptr++ = 0;
	artNo = strtoll(ptr, NULL, 10);
	artNoStart = artNo;
	artNoEnd = artNoStart;
	if ((p = strrchr(ptr, '-')) != NULL)
		artNoEnd = strtoll(p + 1, NULL, 10);
    }

    /*
     * Cycle through the overview entries
     */
    for (artNo = artNoStart; artNo <= artNoEnd; artNo++) {
	off_t pos;
	OverArt oa;

	/* Open the overview info file */

	gfname = GFName(data, GRPFTYPE_OVER, 0, 1, 0, &DOpts.ReaderGroupHashMethod);
	slash = strchr(gfname, '/');

	snprintf(path, sizeof(path), "%s/%s", PatExpand(GroupHomePat), gfname);
	printf("GROUPINFO:%s:%lld\t%s", data, artNo, gfname);

	if ((fd1 = open(path, O_RDONLY)) < 0) {
	    fprintf(stderr, "\t(array open fail)\n");
	    return;
	}

	if (read(fd1, &oh, sizeof(oh)) != sizeof(oh)) {
	    fprintf(stderr, "\t(read overview header fail)\n");
	    close(fd1);
	    return;
	}

	if (oh.oh_Version > OH_VERSION) {
		fprintf(stderr, "\t(wrong overview header version)\n");
		close(fd1);
		return;
	}
	if (oh.oh_ByteOrder != OH_BYTEORDER) {
		fprintf(stderr, "\t(wrong overview byte order)\n");
		close(fd1);
		return;
	}
	if (oh.oh_Version < 3)
	    oh.oh_DataEntries = OD_HARTS;

	fstat(fd1, &st);
	maxarts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	printf("\tmaxarts=%d", maxarts);

	pos = oh.oh_HeadSize + ((artNo & 0x7FFFFFFFFFFFFFFFLL) % maxarts) *
						sizeof(OverArt);
	printf("\tpos=%lld", (long long)pos);
	lseek(fd1, pos, 0);
	if (read(fd1, &oa, sizeof(oa)) != sizeof(oa)) {
		printf("\t(unable to read OverArt)");
		close(fd1);
		return;
	}
	if (oa.oa_ArtNo <= 0) {
	    if (oa.oa_ArtNo == -1)
		printf("\t(Article cancelled)(%lld)\n", artNo);
	    else if (oa.oa_ArtNo == -2)
		printf("\t(Article expired)(%lld)\n", artNo);
	    else
		printf("\t(Article not found)(%lld)\n", artNo);
	    close(fd1);
	    continue;
	}

	if (!OA_ARTNOEQ(artNo, oa.oa_ArtNo)) {
	    printf("\tartNoMismatch(got=%d  wanted=%d)\n", oa.oa_ArtNo,
		OA_ARTNOSET(artNo));
	    if (!ForceOpt) {
		close(fd1);
		continue;
	    }
	}

        /*
	 * Open the overview data file
	 */

	gfname = GFName(data, GRPFTYPE_DATA, artNo & ~(oh.oh_DataEntries - 1), 1, 0,
						&DOpts.ReaderGroupHashMethod);
	snprintf(path, sizeof(path), "%s/%s",
				PatExpand(GroupHomePat), gfname);
	printf("\t%s", gfname);
	if (! ((zfd2 = gzopen(path, "r")))) {

	    snprintf(path, sizeof(path), "%s/%s.gz",
				    PatExpand(GroupHomePat), gfname);
	    zfd2 = gzopen(path, "r");
	    if (! (zfd2)) {
		printf("\t(data open fail)\n");
	        close(fd1);
	        return;
	    }
	}

	printf("\tdataFilePos=%d\tsize=%d\treceived=%d\tbytes=%d\thash=%x.%x\n",
			oa.oa_SeekPos, oa.oa_Bytes, oa.oa_TimeRcvd,
			oa.oa_ArtSize, oa.oa_MsgHash.h1, oa.oa_MsgHash.h2);

	if (ViewData) {
	    static char *buf = NULL;
	    int buflen = 0;
	    int n = oa.oa_Bytes + 1;
	    int r;

	    if (n > buflen)
		buflen = n;
		buf = (char *)realloc(buf, buflen);
	    gzseek(zfd2, oa.oa_SeekPos, 0);
	    while ((r = gzread(zfd2, buf, n)) > 0) {
		printf("%s", buf);
		n -= r;
	    }
	    gzclose(zfd2);
	}
	close(fd1);
	if (artNo <= artNoEnd)
	    printf("--------------------------------------------------------------------\n");
    }
    printf("\n");
}
/*
 * Dump the contents of the overview info files for all newsgroups in
 * active file.
 */
void
OverDump(char *doGroup)
{
    char path[PATH_MAX];
    const char *gfname;
    char *slash;
    char *groupname;
    int recLen;
    int recOff;
    int fd1;
    OverHead oh;
    struct stat st;
    int maxarts;
    KPDB *KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);

    if (KDBActive == NULL) {
	printf("Unable to open active file\n");
	exit(1);
    }
    for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
        recOff;
        recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
    ) {
        int groupLen; 
        const char *rec;
        const char *group;
	artno_t begno;

        rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
        group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);

        if (!group)
	    continue;

	groupname = allocTmpCopy(group, groupLen);

	if (doGroup != NULL && strcmp(doGroup, groupname) != 0)
	    continue;

	begno = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
	gfname = GFName(groupname, GRPFTYPE_OVER, 0, 1, 0,
						&DOpts.ReaderGroupHashMethod);
	slash = strchr(gfname, '/');

	snprintf(path, sizeof(path), "%s/%s", PatExpand(GroupHomePat), gfname);

	if ((fd1 = open(path, O_RDONLY)) < 0) {
	    fprintf(stderr, "%s (array open fail)\n", groupname);
	    continue;
	}

	if (read(fd1, &oh, sizeof(oh)) != sizeof(oh)) {
	    fprintf(stderr, "%s (read header fail)\n", groupname);
	    close(fd1);
	    continue;
	}

	if (oh.oh_Version > OH_VERSION) {
	    fprintf(stderr, "%s (wrong overview header version)\n", groupname);
	    close(fd1);
	    continue;
	}

	if (oh.oh_ByteOrder != OH_BYTEORDER) {
	    if (VerboseMode)
	    fprintf(stderr, "%s (wrong overview header byte order)\n", groupname);
	    close(fd1);
	    continue;
	}
	if (oh.oh_Version < 3)
	    oh.oh_DataEntries = OD_HARTS;

	fstat(fd1, &st);
	maxarts = (st.st_size - oh.oh_HeadSize) / sizeof(OverArt);
	if (doGroup == NULL) {
	    printf("%s:%d\n", groupname, maxarts);
	} else {
	    printf("%s:\n", groupname);
	    printf("\tNB: %lld\n", begno);
	    printf("\tmaxarts: %d\n", maxarts);
	    printf("\thead version: %d\n", oh.oh_Version);
	    printf("\thead gname: %s\n", oh.oh_Gname);
	    printf("\tdata entries: %d\n", oh.oh_DataEntries);
	}
	close(fd1);
    }
}

