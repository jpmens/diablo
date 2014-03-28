
/*
 * UTIL/DEXPIREOVER.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 * 	In this incarnation, dexpireover cleans up overview information as
 *	specified in the 'x' fields in dexpire.ctl (see the sample dexpire.ctl)
 *
 *	This is still very rough.  I still need to add in a free-space-target
 *	option and have dexpireover adjust the expiration dynamically based on
 *	free space.  IT DOESN'T DO THIS YET!! You have to make sure your
 *	expiration ('x' option) in dexpire.ctl is not set too high.
 *
 *	I also need to have a remote-server scanning option to allow 
 *	dexpireover to adjust expirations based on remote server retentions.
 *	It does not do this yet either.
 *
 *	Modifications by Nickolai Zeldovich to allow spool-based expiration
 *	(ExpireBySpool and ExpireFromFile)
 *
 *	Specifying the -pN option will fork N dexpireover processes and
 *	perform the expiration process in parallel. This is useful to speed
 *	up ExpireBySpool where dhistory lookups take a long time.
 *
 *	LOCKING INFO
 *
 *	The data file will have an advisory lock at offset 4 from dreaderd
 *	when it has the file open (can't rewrite data)
 *	The info file will have an advisory lock at offset 4 from dreaderd
 *	when it has the file open (can't resize)
 *
 *	od_HFd = data. file
 *	ov_OFd = over. file
 *
 */

#include <dreaderd/defs.h>

#define GZ_MAGIC0       0x1F
#define GZ_MAGIC1       0x8B
#define GZ_EXTRA        0x04
#define GZ_ORIGNAME     0x08
#define GZ_COMMENT      0x10
#define GZ_HEADCRC      0x02


typedef struct Group {
    struct Group *gr_Next;
    int		gr_State;
    artno_t	gr_StartNo;
    artno_t	gr_EndNo;
    int		gr_CTS;
    int		gr_LMTS;
    int		gr_Iter;
    char	*gr_GroupName;
    char	*gr_Flags;
    char	*gr_Hash;
    int		gr_UpdateFlag;
} Group;

#define GRF_DESCRIPTION 0x00000001
#define GRF_STARTNO     0x00000002
#define GRF_ENDNO       0x00000004
#define GRF_FLAGS       0x00000008
#define GRF_FROMLOCAL   0x00000800
#define GRF_NEW         0x00001000
#define GRF_FROMREMOTE  0x00002000
#define GRF_MODIFIED    0x00008000
#define GRF_EDITEDBEG   0x00010000

#define GHSIZE		1024
#define GHMASK		(GHSIZE-1)

typedef struct FileCacheList {
    struct FileCacheList	*fc_Next;
    int				fc_Fd;
    artno_t			fc_ArtBase;
} FileCacheList;

#define DEXPOVER_READ_BUFFER_SIZE	4096
#define DEXPOVER_HASH_SIZE		32768

/* Since we only look at the first char of the first-level directory,
 * we do not support more than 16 forks.
 */
#define	MAX_PAR_COUNT			16

/*
 * These aren't really buckets, they're parts of a bucket
 */

typedef struct bucket_t {
    struct bucket_t *next;
    hash_t hash_item;
    short valid;
} bucket_t;

KPDB  *KDBActive;
Group *GHash[GHSIZE];

void ScanDirectories(void);
void scanDirectory(const char *dirpath, char *dirname, int *level);
void DeleteJunkFile(const char *dirPath, const char *name);
void ProcessOverviewFile(const char *dirPath, const char *name, int type);
int getFirstArtAge(Group *group, int fd, OverHead *oh);
char *allocTmpCopy(const char *buf, int bufLen);
Group *EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags);
Group *FindGroupByHash(char *Hash, int iter);
int SetField(char **pptr, const char *str);
void ExpireByDays(Group *group, int fd, OverHead *oh, int expireSecs);
void ExpireBySpool(Group *group, int fd, OverHead *oh);
void ExpireFromFile(Group *group, int fd, OverHead *oh, int expireSecs);
void RewriteData(Group *group, int fd, OverHead *oh, const char *dirPath, int oldDataEntries, int newDataEntries, int *fdl, int StoreGZDays);
void rewriteDataFile(Group *group, const char *cacheBase, int cacheSize, const OverArt *oa, OverArt *ob, const char *dirPath, int oldDataEntriesMask, int newDataEntriesMask, artno_t artNo);
void ResizeGroup(Group *group, int fd, OverHead *oh, int maxArts, int OldDataEntries, int *fdl);
int nearestPower(int n);
void ReadDExpOverList(void);
int expOverListCheckExpired(hash_t *hv);
int hexCharToInt(char c);
int getDataEntries(const char *path);
long long x64write(int d, const void *buf, long long nbytes);
long long x64read(int d, const void *buf, long long nbytes);
void dumpovmap(OverArt *oa, long long n, char *filename);
void dumpoh(OverHead *oh, char *filename);

int UpdateBegArtNoOpt = 0;
int UpdateCTSOpt = 0;
int RewriteDataOpt = 0;
int CheckDataEntries = 0;
int BadGroups = 0;
int ResizedGroups = 0;
int NoResizedGroups = 0;
int GoodGroups = 0;
int ActiveUpdated = 0;
int VerboseOpt = -1;
int ResizeOpt = -1;
int ForReal = 1;
int OldGroups = 0;
int UseExpireByDays = 0;
int UseExpireBySpool = 0;
int UseExpireFromFile = 0;
int ParallelCount = 0;
int FactorExpire = 0;
int LockWaitTime= 1;
int MustExit = 0;
char *Wild;
bucket_t *dexpover_msgid_hash;
int ParallelIdx = 0;
int ParallelPid[MAX_PAR_COUNT];

void
sigInt(int sigNo)
{
    printf("Exit signal caught - exiting\n");
    ++MustExit;
    if (MustExit > 3)
	exit(1);
}

/*
 * We use alarms to cancel a lock wait, so just ignore the alarm
 */
void
sigAlarm(int sigNo)
{
 ;;
}

void Usage(char *progname)
{
    fprintf(stderr, "Expire the reader header database\n");
    fprintf(stderr, "dexpireover [-a] [-e] [-f active] [-l#] [-NB] [-n] [-O#] [-o] [-p#] [-R[R]] [-s] [-U] [-v#] [-w wildmat] [-x] [-y] [-C diablo.config] [-d[n]] [-V]\n");
    fprintf(stderr, "\t-a\t\tDo a standard header expire run (-NB -U -s -y)\n");
    fprintf(stderr, "\t-e\t\tExpire by checking a local spool\n");
    fprintf(stderr, "\t-f file\t\tSpecify the name of the active file\n");
    fprintf(stderr, "\t-l#\t\tWait N seconds for lock files\n");
    fprintf(stderr, "\t-NB\t\tUpdate the begining article number (NB) in active\n");
    fprintf(stderr, "\t-n\t\tDon't actually make any changes (dry run)\n");
    fprintf(stderr, "\t-O#\t\tRemove groups not used within # days\n");
    fprintf(stderr, "\t-o\t\tExpire from file of msgid hashes\n");
    fprintf(stderr, "\t-p#\t\tRun # parallel expire runs (speed up)\n");
    fprintf(stderr, "\t-R\t\tRewrite header data files\n\t\t\t(-RR to rewrite and remove corrupted entries)\n");
    fprintf(stderr, "\t-s\t\tResize group indexes\n");
    fprintf(stderr, "\t-U\t\tAdd CTS (group create time) in active if not present\n");
    fprintf(stderr, "\t-v[#]\t\tVerbose mode\n");
    fprintf(stderr, "\t-w wildmat\tSpecify a wildmat of groups to expire\n");
    fprintf(stderr, "\t-x\t\tUse factoring for expire (see man page)\n");
    fprintf(stderr, "\t-y\t\tActually expire headers based on dexpire.ctl\n");
    fprintf(stderr, "\t-C file\tspecify diablo.config to use\n");
    fprintf(stderr, "\t-d[n]\tset debug [with optional level]\n");
    fprintf(stderr, "\t-V\tprint version and exit\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int i;
    char *dbfile = NULL;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected argument: %s\n", ptr);
	    Usage(av[0]);
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    UseExpireByDays = 1;
	    UpdateBegArtNoOpt = 1;
	    UpdateCTSOpt = 1;
	    ResizeOpt = 1;
	    break;
	case 'e':
	    UseExpireBySpool = 1;
	    break;
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'l':
	    LockWaitTime = strtol((*ptr) ? ptr : av[++i], NULL, 0);
	    break;
	case 'N':
	    while (*ptr) {
		switch(*ptr) {
		case 'B':
		    UpdateBegArtNoOpt = 1;
		    break;
		default:
		    break;
		}
		++ptr;
	    }
	    break;
	case 'n':
	    ForReal = 0;
	    if (VerboseOpt < 0)
		VerboseOpt = 1;
	    break;
	case 'O':
	    if (*ptr)
		OldGroups = strtol(ptr, NULL, 0);
	    else
		OldGroups = 30 * 3;	/* 3 months by default */
	    break;
	case 'o':
	    UseExpireFromFile = 1;
	    break;
	case 'p':
	    if (*ptr)
		ParallelCount = strtol(ptr, NULL, 0);
	    else
		ParallelCount = 1;
	    if (ParallelCount > MAX_PAR_COUNT)
		ParallelCount = MAX_PAR_COUNT;
	    /* Note that a parcount of 1 doesn't do anything useful. */
	    break;
	case 'R':
	    RewriteDataOpt = 1;
	    if (*ptr == 'R') {
		CheckDataEntries = 1;
		ptr++;
	    }
	    break;
	case 's':
	    ResizeOpt = 1;
	    break;
	case 'U':
	    UpdateCTSOpt = 1;
	    break;
	case 'v':
	    VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'w':
	    Wild = (*ptr) ? ptr : av[++i];
	    break;
	case 'x':
	    FactorExpire = 1;
	    break;
	case 'y':
	    UseExpireByDays = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	/* Common options */
	case 'C':           /* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (!UseExpireByDays && ResizeOpt <= 0 && !RewriteDataOpt &&
			!UseExpireFromFile && !UseExpireBySpool && !OldGroups)
	Usage(av[0]);
    /*
     * Read in the list of expired msgid hashes, if we are using it
     */

    if (UseExpireFromFile)
	ReadDExpOverList();

    /*
     * fork off parallel copies of dexpireover at this point
     */

    if (ParallelCount) {
	char *stdout_buffer;

	for (ParallelIdx=0; ParallelIdx < ParallelCount; ParallelIdx++) {
	    int pid;

	    pid = fork();
	    if(pid == 0)
		break;
	    ParallelPid[ParallelIdx] = pid;
	}

	stdout_buffer = (char *)malloc(BUFSIZ);
	setvbuf(stdout, stdout_buffer, _IOLBF, BUFSIZ);

	if (ParallelIdx == ParallelCount) {
	    pid_t pid;
	    int remaining = ParallelCount;

	    while (remaining) {
		while (remaining && ((pid = wait3(NULL, 0, NULL)) > 0)) {
		    for (i=0; i<ParallelCount; i++)
			if(ParallelPid[i] == pid) {
			    ParallelPid[i] = 0;
			    --remaining;
			}
		}
	    }

	    printf("Parallelizing dexpireover (%d forks) finished.\n",
	       ParallelCount);
	    exit(0);
	}
    }

    /*
     * Open active file database
     */

    if (VerboseOpt)
	printf("Loading active file\n");
    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);
    }
    if (KDBActive == NULL) {
	fprintf(stderr, "Unable to open dactive.kp\n");
	exit(1);
    }
    if (OldGroups && Wild == NULL) {
	fprintf(stderr, "group wildcard must be specified if -O option used\n");
	Usage(av[0]);
    }

    LoadExpireCtl(1);

    /*
     * Open the history file if we are going to expire based on local spool
     */

    if (UseExpireBySpool)
	HistoryOpen(NULL, 0);

    /*
     * scan dactive.kp
     */

    if (VerboseOpt)
	printf("Hashing newsgroups\n");
    {
	int recLen;
	int recOff;
	int cts0 = (int)time(NULL);

	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	     recOff;
	     recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    int groupLen;
	    int flagsLen;
	    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	    const char *flags = KPDBGetField(rec, recLen, "S", &flagsLen, "y");
	    artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    int lmts = (int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
	    int cts = (int)strtoul(KPDBGetField(rec, recLen, "CTS", NULL, "0"), NULL, 16);
	    int iter = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 16);
	    Group *grp;

	    if (cts == 0)	/* enter non-zero cts only if group has no CTS field */
		cts = cts0;
	    else
		cts = 0;

	    if (group)
		group = allocTmpCopy(group, groupLen);
	    if (flags)
		flags = allocTmpCopy(flags, flagsLen);

	    /*
	     * ignore bad group or group that does not match the wildcard
	     */

	    if (group == NULL)
		continue;
	    if (Wild && WildCmp(Wild, group) != 0)
		continue;

	    grp = EnterGroup(
		group,
		begNo,
		endNo,
		lmts,
		cts,
		iter,
		flags
	    );
	    grp->gr_State &= ~(GRF_NEW|GRF_MODIFIED);
	}
    }

    rsignal(SIGINT, sigInt);
    rsignal(SIGHUP, sigInt);
    rsignal(SIGTERM, sigInt);
    rsignal(SIGALRM, SIG_IGN);

    /*
     * Scan /news/spool/group/ and do the actual expire, over resize
     * and data rewrite
     *
     */

    ScanDirectories();

    /*
     * Writeback active file
     */

    if (UpdateBegArtNoOpt || OldGroups || UpdateCTSOpt) {
	int t0 = (int)time(NULL);	/* int-sized for LMTS compare */
	int t;
	int i;
	int count = 0;

	t = t0 - OldGroups * (60 * 60 * 24);

	for (i = 0; i < GHSIZE; ++i) {
	    Group *group;

	    for (group = GHash[i]; group; group = group->gr_Next) {
		/*
		 * If we have a new group not previously in the database,
		 * we only add it if SyncGroupsOpt is set.
		 */
		int add = 0;

		if (OldGroups) {
		    if (group->gr_LMTS) {
			/*
			 * Existing LMTS
			 */
			if ((int)(t - group->gr_LMTS) > 0) {
			    if (ForReal)
				KPDBDelete(KDBActive, group->gr_GroupName);
			    if (VerboseOpt)
				printf("%s: stale group deleted\n", group->gr_GroupName);
			    add = 1;
			    group->gr_State &= ~GRF_MODIFIED;	/* prevent NB update */
			} 
		    } else {
			/*
			 * no LMTS in record, add one
			 */
			if (ForReal) {
			    char tsBuf[16];
			    sprintf(tsBuf, "%08x", (int)t0);
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "LMTS", tsBuf, 0);
			    add = 1;
			}
			if (VerboseOpt)
			    printf("%s: added missing LMTS\n", group->gr_GroupName);
		    }
		}

		if (UpdateCTSOpt) {
		    if (group->gr_CTS) {
			if (ForReal) {
			    char tsBuf[16];
			    sprintf(tsBuf, "%08x", group->gr_CTS);
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "CTS", tsBuf, 0);
			}
			if (VerboseOpt)
			    printf("%s: added missing CTS\n", group->gr_GroupName);
			add = 1;
		    }
		}

		if (ForReal && (group->gr_State & GRF_MODIFIED)) {
		    if (group->gr_State & GRF_EDITEDBEG) {
			char startBuf[20];
			snprintf(startBuf, sizeof(startBuf), "%010lld", group->gr_StartNo);
			KPDBWriteEncode(KDBActive, group->gr_GroupName, "NB", startBuf, 0);
			add = 1;
		    }
		}
		count += add;
	    }
	}
	printf("Updated article range in %d groups\n", count);
    }
    if (KDBActive)
	KPDBClose(KDBActive);

    /*
     * Close history if we had it open
     */
    if (UseExpireBySpool)
	HistoryClose();

    return(0);
}

/*
 * Scan /news/spool/group/ and do the actual expire, over resize
 * and data rewrite
 *
 * This works by scanning all files in the directories, mapping the
 * filename (hash) to a newsgroup name and performing the following,
 * depending on file type:
 *
 * over.* :
 * *.o.* :
 *		1) resize over (if requested)
 *		2) expire articles (by days, spool or file)
 *		3) rewrite data (if requested)
 *
 * data.*:
 * *.d.*:
 *		1) if the range of articles for the file falls below NB,
 *		   then delete the file
 *
 * If a newsgroup for a file cannot be found, the file is removed
 */

void
ScanDirectories(void)
{
    DIR *dir;
    int level = 0;

    if (VerboseOpt)
	printf("Scanning group directories\n");

    chdir(PatExpand(GroupHomePat));
    if ((dir = opendir(".")) != NULL) {
	den_t *den;
	struct stat st;

	while ((den = readdir(dir)) != NULL) {
	    if (isalnum((int)den->d_name[0]) &&
	    	stat(den->d_name, &st) == 0 && S_ISDIR(st.st_mode) &&
		/*
		 * We explicitly use the first char, because overview
		 * sizes appear to be not evenly distributed wrt second
		 * char.
		 */
		(ParallelCount ? ((GFIndex(den->d_name[0]) % ParallelCount) ==
				      ParallelIdx) : 1)
	    )
		scanDirectory(PatExpand(GroupHomePat), den->d_name, &level);
	}
	closedir(dir);
    }

    printf("Scanned %d files, %d were bad, %d/%d indexes resized successfully\n",
	GoodGroups + BadGroups, 
	BadGroups, 
	ResizedGroups, 
	NoResizedGroups + ResizedGroups
    );

}

void
scanDirectory(const char *dirpath, char *dirname, int *level)
{
    DIR *dir2;
    char path[PATH_MAX];
    char origpath[PATH_MAX];

    sprintf(path, "%s/%s", dirpath, dirname);

    if (getcwd(origpath, sizeof(origpath)) == NULL) {
	printf("Unable to getcwd(%s): %s\n", dirname, strerror(errno));
	return;
    }
    if (chdir(dirname) != 0) {
	printf("Unable to chdir(%s)\n", dirname);
	return;
    }

    if ((dir2 = opendir(".")) != NULL) {
	den_t *den2;
	char tbuf[24];
	int tint;
	struct stat st;

	while ((den2 = readdir(dir2)) != NULL) {
	    if (strcmp(den2->d_name, ".") == 0 || strcmp(den2->d_name, "..") == 0)
		continue;
	    if (stat(den2->d_name, &st) == 0 && S_ISDIR(st.st_mode)) {
		if (*level <= 2)
		    scanDirectory(path, den2->d_name, level);
	    } else if (strncmp(den2->d_name, "over.", 5) == 0 ||
		strncmp(den2->d_name, "o.", 2) == 0 ||
		strstr(den2->d_name, ".o.") != NULL)
		ProcessOverviewFile(path, den2->d_name, 1);
	    if (MustExit)
		exit(1);
	}
	rewinddir(dir2);
	while ((den2 = readdir(dir2)) != NULL) {
	    /*
	     * delete junk files from previously interrupted
	     * dexpireover -R
	     */
	    if (strncmp(den2->d_name, ".data.", 6) == 0 ||
		strncmp(den2->d_name, ".d.", 3) == 0 ||
		sscanf(den2->d_name, ".%[^.]s.%d.d.", tbuf, &tint) == 2) {
		DeleteJunkFile(path, den2->d_name);
		continue;
	    }

	    /*
	     * process data. files
	     */
	    if (strncmp(den2->d_name, "data.", 5) == 0 ||
		strstr(den2->d_name, ".d.") != NULL)
		ProcessOverviewFile(path, den2->d_name, 2);
	    if (MustExit)
		exit(1);
	}
	closedir(dir2);
    }
    if (chdir(origpath) != 0) {
	printf("FATAL: Unable to return with chdir(%s)\n", dirname);
	exit(1);
    }
}

void
DeleteJunkFile(const char *dirPath, const char *name)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);
    if (VerboseOpt)
	printf("Removing old temp file: %s\n", path);
    if (ForReal)
	remove(path);
}

/*
 * ProcessOverviewFile() - process over. and data. files.  All over. files
 *			   are processed first. 
 *
 *	When processing over. files, we may resize the index array (-s)
 *	and/or cleanup the file (-R).
 *
 *	When processing data. files, we typically remove whole files. 
 *	If the -R option was used, however, we rewrite the files.  We can
 *	safely copy/rename-over data. files as long as we are able to
 *	lock the associated over. file.
 */

void
ProcessOverviewFile(const char *dirPath, const char *name, int type)
{
    artno_t artBase = -1;
    Group *group;
    char path[PATH_MAX];
    char Hash[PATH_MAX];
    int iter = 0;
    OverHead oh;

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (DebugOpt > 0)
	printf("ProcessOverviewFile(%s)\n", path);

    bzero(Hash, sizeof(Hash));

    if (ExtractGroupHashInfo(name, Hash, &artBase, &iter) == HASHGRP_NONE)
	return;

    if (DebugOpt > 2)
	printf("File: %s  type=%d  artBase=%lld  iter=%d  Hash=%s\n",
						name, type, artBase, iter, Hash);

    if ((group = FindGroupByHash(Hash, iter)) == NULL) {
	/*
	 * If we gave a wildcard, we can't remove stale groups because
	 * we do not have a full group list.
	 */

	if (Wild == NULL) {
	    ++BadGroups;
	    printf("Group for over file not found, removing %s\n", path);
	    if (ForReal)
		remove(path);
	}
	return;
    }
    ++GoodGroups;

    if (DebugOpt > 2)
	printf("Maps to group: %s\n", group->gr_GroupName);

    if (type == 1) {
	/*
	 * over. file	(fixed length file)
	 */
	int fd;

	if ((fd = open(path, O_RDWR)) >= 0) {
	    int r;
	    int OldDataEntries=0;
	    int fdLocked=0; /* lock cache, to avoid getting a lock twice */

	    r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	    if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
		r = 0;
	    if (r && oh.oh_Version > 1 &&
				strcmp(oh.oh_Gname, group->gr_GroupName) != 0)
		r = 0;
	    if (r) {
		if (oh.oh_Version < 3)
		    OldDataEntries = OD_HARTS;
		else {
		    OldDataEntries = oh.oh_DataEntries;
		    if (OldDataEntries <= 0 ||
			((OldDataEntries ^ (OldDataEntries - 1)) != (OldDataEntries << 1) - 1))
			OldDataEntries = OD_HARTS;
		}
	    }

	    if (r) {
		int expireSecs;
		OverExpire save;
		int recLen;
		const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
                                                        KP_LOCK, &recLen);

		/*
		 * Refresh the EndNo
		 */
		group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE",
							NULL, "-1"), NULL, 10);
		KPDBUnlock(KDBActive, rec);

		if (group->gr_EndNo - group->gr_StartNo >= oh.oh_MaxArts) {
		    if (VerboseOpt)
			printf("adjust %s startno %lld to %lld (endno=%lld  maxarts=%d)\n",
					group->gr_GroupName,
					group->gr_StartNo,
					group->gr_EndNo - oh.oh_MaxArts + 1,
					group->gr_EndNo,
					oh.oh_MaxArts);
		    group->gr_StartNo = group->gr_EndNo - oh.oh_MaxArts + 1;
		    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
		}

		/*
		 * Figure out expire values
		 */
		expireSecs = GetOverExpire(group->gr_GroupName, &save);

		/*
		 * Make sure we never have a dataEntries value of zero
		 */
		if (save.oe_DataSize <= 0 ||
			((save.oe_DataSize ^ (save.oe_DataSize - 1)) != (save.oe_DataSize << 1) - 1))
		    save.oe_DataSize = OldDataEntries;
		
		/*
		 * Force regeneration of over. file if RewriteDataOpt, else
		 * only regenerate if ResizeOpt and a size differential.  Valid
		 * overview index sizes run in steps if the nearest higher
		 * power of 2 divided by 3.
		 *
		 */
		if (ResizeOpt > 0 || RewriteDataOpt > 0) {
		    int numArts = group->gr_EndNo - group->gr_StartNo + 1;
		    int maxArts = oh.oh_MaxArts;
		    int artAge;

		    /*
		     * Find out the age of the first article in the
		     * group and calculate an age factor based on this
		     * and the expire value in dexpire.ctl.
		     *
		     * We don't let the age factor get too large
		     * if the age of the first articles is less than an
		     * hour because this can lead to some bloated files.
		     * We also adjust the age factor if there are a
		     * large number of articles in the group and the
		     * age factor is low as we don't need a huge
		     * number of spare slots.
		     */
		    artAge = getFirstArtAge(group, fd, &oh);
		    if (FactorExpire && artAge > 0 && expireSecs > 0) {
			float ageFactor;

			if (artAge <= 0)
			    artAge = 1;
			ageFactor = (expireSecs * 1.0) / (artAge * 1.0);
			if (ageFactor < 1.5)
			    ageFactor = 1.5;
			if (numArts > 20000 && ageFactor == 1.5)
			    ageFactor = 1.3;
			if (artAge < 3600 && ageFactor > 50.0)
			    ageFactor = 50.0;
			if (numArts > 500 && ageFactor > 20.0)
			    ageFactor = 20.0;
			if (numArts > 5000 && ageFactor > 5.0)
			    ageFactor = 5.0;
			if (ageFactor > 400.0)
			    ageFactor = 400.0;
			numArts = ageFactor * numArts;
		    } else if (numArts < maxArts / 2)
			numArts = maxArts - nearestPower(maxArts) / 3;
		    else if (numArts > maxArts * 2 / 3)
			numArts = maxArts + nearestPower(maxArts) / 3;
		    else
			numArts = maxArts;

		    /*
		     * The minimum is somewhat contrived, but if we can
		     * fit the index file into a fragment for unused groups
		     * we save a considerable amount of space.
		     */

		    if (save.oe_MaxArts > 0 && numArts > save.oe_MaxArts)
			numArts = save.oe_MaxArts;
		    if (save.oe_MinArts >= 0 && numArts < save.oe_MinArts)
			numArts = save.oe_MinArts;

		    {
			int needResize = RewriteDataOpt;
			int d = abs(maxArts - numArts);
			/*
			 * We only rewrite if there has been a radical
			 * change in the number of articles needed.
			 */
			if (numArts < 100 && d >= 10)
				needResize = 1;
			if (numArts >= 100 && numArts < 1000 && d > 20)
				needResize = 1;
			if (numArts >= 1000 && d > 200)
				needResize = 1;
			if (needResize)
			    ResizeGroup(group, fd, &oh, numArts, OldDataEntries, &fdLocked);
		    }
		}

		if (UseExpireBySpool)
		    ExpireBySpool(group, fd, &oh);
		else if (UseExpireFromFile)
		    ExpireFromFile(group, fd, &oh, expireSecs);
		else if (UseExpireByDays)
		    ExpireByDays(group, fd, &oh, expireSecs);

		/*
		 * Rewrite data files associated with over. file if -R.
		 */
		if (RewriteDataOpt > 0)
		    RewriteData(group, fd, &oh, dirPath, OldDataEntries, save.oe_DataSize, &fdLocked, save.oe_StoreGZDays);
	    } else {
		printf("group %s, file \"%s\" bad file header\n",
		    group->gr_GroupName,
		    path
		);
		if (oh.oh_Version > OH_VERSION)
		    printf("   expected version %d, got version %d\n",
						OH_VERSION, oh.oh_Version);
	    }
	    if (fdLocked) {
		hflock(fd, 4, XLOCK_UN);
		fdLocked = 0;
	    }
	    close(fd);
	}
    } else {
	/*
	 * data. file, modulo OD_HARTS.  OD_HARTS constant in second
	 * part of conditional is a fudge to make 100% sure we do not
	 * delete a brand new data file.
	 */
	int recLen;
	const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
                                                        KP_LOCK, &recLen);
	/*
	 * Refresh the EndNo
	 */
	group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE",
							NULL, "-1"), NULL, 10);
	KPDBUnlock(KDBActive, rec);

	oh.oh_DataEntries = getDataEntries(GFName(group->gr_GroupName,
					GRPFTYPE_OVER, 0, 2,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	if (oh.oh_DataEntries <= 0 ||
		((oh.oh_DataEntries ^ (oh.oh_DataEntries - 1)) != (oh.oh_DataEntries << 1) - 1))
	    oh.oh_DataEntries = OD_HARTS;

	/* data files have been rewritten if DataEntries has been changed */
	if (artBase + oh.oh_DataEntries <= group->gr_StartNo ||
	    artBase >= group->gr_EndNo + oh.oh_DataEntries ||
	    (artBase & (oh.oh_DataEntries-1))
	) {
	    if (DebugOpt)
		printf("%s artBase=%lld oh_DataEntries=%d\n",
				group->gr_GroupName,
				artBase, oh.oh_DataEntries);
	    if (VerboseOpt)
		printf("Deleting stale overview data for %s: %s (artBase=%lld  StartNo=%lld  EndNo=%lld\n",
				group->gr_GroupName, path,
				artBase, group->gr_StartNo, group->gr_EndNo
		);
	    if (ForReal)
		remove(path);
	} 
    }
}

int
getFirstArtAge(Group *group, int fd, OverHead *oh)
{
    const OverArt *oaBase;
    struct stat st;
    long long n;
    time_t t = time(NULL);
    int dt = 0;

    if (fstat(fd, &st) != 0)
	return(-1);

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd,
			oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in getFirstArtAge (%s)\n",
			group->gr_GroupName, n, strerror(errno));
	return(-1);
    }

    {
        long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % oh->oh_MaxArts;
	const OverArt *oa = &oaBase[i];

	/*
	 * Get the age of the first valid article
	 */
	if (oa && i <= n)
	    dt = (int)(t - oa->oa_TimeRcvd);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
    return(dt);
}

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

Group *
EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags)
{
    hash_t hv = hhash(GFHash(groupName, &DOpts.ReaderGroupHashMethod));
    Group **pgroup = &GHash[hv.h1 & GHMASK];
    Group *group;

    while ((group = *pgroup) != NULL) {
	if (strcmp(groupName, group->gr_GroupName) == 0)
	    break;
	pgroup = &group->gr_Next;
    }
    if (group == NULL) {
	*pgroup = group = calloc(sizeof(Group) + strlen(groupName) + 1, 1);
	group->gr_State = GRF_NEW;
	group->gr_GroupName = (char *)(group + 1);
	group->gr_Hash = strdup(GFHash(groupName, &DOpts.ReaderGroupHashMethod));
	group->gr_Iter = iter;
	strcpy(group->gr_GroupName, groupName);
    }

    /*
     * update fields
     */
    if (begNo >= 0) {
	group->gr_State |= GRF_STARTNO;
	if (group->gr_StartNo != begNo) {
	    group->gr_State |= GRF_MODIFIED;
	    group->gr_StartNo = begNo;
	}
    }
    if (endNo >= 0) {
	group->gr_State |= GRF_ENDNO;
	if (endNo < group->gr_EndNo) {
	    printf("*** Would be adjusting NE down - not doing it ***\n");
	    printf("adjust %s NE from %lld to %lld\n", group->gr_GroupName,
				endNo, group->gr_EndNo);
	} else if (group->gr_EndNo != endNo) {
	    group->gr_EndNo = endNo;
	    group->gr_State |= GRF_MODIFIED;
	}
    }
    group->gr_LMTS = lmts;

    if (cts) {
	group->gr_CTS = cts;
    }

    if (flags) {
	group->gr_State |= GRF_FLAGS;
	if (SetField(&group->gr_Flags, flags))
	    group->gr_State |= GRF_MODIFIED;
    }
    return(group);
}

Group *
FindGroupByHash(char *Hash, int iter)
{
    Group *group;
    hash_t hv = hhash(Hash);

    for (group = GHash[hv.h1 & GHMASK]; group; group = group->gr_Next) {
	if (strcmp(group->gr_Hash, Hash) == 0 && group->gr_Iter == iter)
	    break;
    }
    return(group);
}

int
SetField(char **pptr, const char *str)
{
    if (*pptr && strcmp(*pptr, str) == 0)
	return(0);
    if (*pptr)
	free(*pptr);
    *pptr = strcpy(malloc(strlen(str) + 1), str);
    return(1);
}

/*
 * Scan overview records from beginning article to ending article
 */

void
ExpireByDays(Group *group, int fd, OverHead *oh, int expireSecs)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;
    time_t t = time(NULL);

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireByDays (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];
	    int expiredart = 0;
	    int artno = oa->oa_ArtNo;

	    if (VerboseOpt > 3) {
		printf("entry %lld artNo %d\n", i, artno);
	    }
	    if (OA_ARTVALID(oa)) {
		int dt = (int)(t - oa->oa_TimeRcvd);

		expiredart = 0;
		if (expireSecs > 0 && 
		    (dt > expireSecs || dt < -(60 * 60 * 24))
		) {
		    OverArt copy = *oa;

		    expiredart = 1;
		    copy.oa_ArtNo = -2;		/* EXPIRED */
		    if (ForReal) {
			lseek(fd, (off_t)oh->oh_HeadSize + i * sizeof(OverArt), 0);
			write(fd, &copy, sizeof(OverArt));
		    }
		    ++count;
		}

		if (VerboseOpt > 2)
		    printf("DT %d/%d %08lx %08lx %s\n", dt, expireSecs, (long)t, (long)oa->oa_TimeRcvd, expiredart ? "(expired)" : "(kept)");

	    }
	}
    }

    {
	/*
	 * Update active file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires in %6.2f days, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    ((expireSecs>0) ? (double)expireSecs / (60.0 * 60.0 * 24.0) :-1.0),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Scan overview records from beginning article to ending article
 *
 * Expire by checking the history file for the expired bit
 */

void
ExpireBySpool(Group *group, int fd, OverHead *oh)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireBySpool (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];

	    if (oa && OA_ARTVALID(oa)) {
		hash_t dh = oa->oa_MsgHash;
		History dh_lookup;

		/*
		 * Make sure that the history entry exists. It's possible
		 * that dexpire already removed the article, and dhistory
		 * was cleaned, so the msgID doesn't exist.
		 *
		 * If the article does not exist or is expired, then expire
		 * its overview entry as well.
		 */
		if ((HistoryLookupByHash(dh, &dh_lookup) == -1) ||
						H_EXPIRED(dh_lookup.exp)) {
		    OverArt copy = *oa;

		    copy.oa_ArtNo = -2;		/* EXPIRED */
		    if (ForReal) {
			lseek(fd, oh->oh_HeadSize + i * sizeof(OverArt), 0);
			write(fd, &copy, sizeof(OverArt));
		    }
		    ++count;
		}
	    }
	}
    }

    {
	/*
	 * Update history file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires by spool, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Similar to ExpireBySpool but uses a file generated by dexpire as a list
 * of msgid hashes of messages which are expired.
 * 
 * The 'x' parameter in dexpire.ctl is also checked, to punt articles which
 * have been laying in overview for a long time and somehow escaped being
 * written to the dexpover.dat file.
 */

void
ExpireFromFile(Group *group, int fd, OverHead *oh, int expireSecs)
{
    const OverArt *oaBase;
    struct stat st;
    int count = 0;
    int jumped = 0;
    int expired = 0;
    int canceled = 0;
    int stale = 0;
    long long n;
    time_t t = time(NULL);

    if (fstat(fd, &st) != 0)
	return;

    /*
     * Calculate number of overview records
     */

    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in ExpireFromFile (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    /*
     * Delete expired overview
     */

    {
	long long i;

	for (i = 0; i < n; ++i) {
	    const OverArt *oa = &oaBase[i];

	    if (oa && OA_ARTVALID(oa)) {
		int dt = (int)(t - oa->oa_TimeRcvd);
		hash_t *hv = (hash_t *)(&(oa->oa_MsgHash));

		if (VerboseOpt > 2)
		    printf("DT %d/%d %08lx %08lx\n", dt, expireSecs, (long)t, (long)oa->oa_TimeRcvd);

		if ((expOverListCheckExpired(hv) == 0) ||
		    ((expireSecs > 0) && 
		     (dt > expireSecs || dt < -(60 * 60 * 24)))
		) {
		    OverArt copy = *oa;

		    copy.oa_ArtNo = -2;		/* EXPIRED */
		    if (ForReal) {
			lseek(fd, oh->oh_HeadSize + i * sizeof(OverArt), 0);
			write(fd, &copy, sizeof(OverArt));
		    }
		    ++count;
		}
	    }
	}
    }

    {
	/*
	 * Update history file begin sequence number
	 */
	while (group->gr_StartNo <= group->gr_EndNo) {
	    long long i = (group->gr_StartNo & 0x7FFFFFFFFFFFFFFFLL) % n;
	    const OverArt *oa = &oaBase[i];

	    if (VerboseOpt > 2)
		printf("test %d vs %lld (i = %lld)\n", oa->oa_ArtNo, group->gr_StartNo, i);
	    if (OA_ARTNOEQ(group->gr_StartNo, oa->oa_ArtNo))
		break;
	    ++group->gr_StartNo;
	    switch(oa->oa_ArtNo) {
	    case -2:
		++expired;
		break;
	    case -1:
		++canceled;
		break;
	    default:
		++stale;
		break;
	    }
	    ++jumped;
	}
	if (jumped)
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
    }
    if (VerboseOpt && (jumped || count)) {
	printf("expired %-4d NB += %-4d (%3d can, %3d stale, %3d exprd) left %-4d expires in %6.2f days, grp=%s\n", 
	    count,
	    jumped,
	    canceled, stale, expired,
	    (int)(group->gr_EndNo - group->gr_StartNo + 1),
	    ((expireSecs>0) ? (double)expireSecs / (60.0 * 60.0 * 24.0) :-1.0),
	    group->gr_GroupName
	);
    }
    xunmap((void *)oaBase, n * sizeof(OverArt));
}

/*
 * Rewrite the data.* files associated with an over. file
 */
void
RewriteData(Group *group, int fd, OverHead *oh, const char *dirPath, int oldDataEntries, int newDataEntries, int *fdl, int StoreGZDays)
{
    const OverArt *oaBase;
    OverArt *oaBaseNew;
    struct stat st;
    artno_t startNo;
    artno_t endNo;
    artno_t artNo;
    int rblt;
    long long n;
    int ok = 1;
    int tmpfd, gzflags;
    unsigned char *zmap, *tmpmap, *zpos;
    char *tmpfile;
    unsigned long zlen;
    z_stream z;
    int code;

    /*
     * If not for real, do not do anything
     */

    if (ForReal == 0)
	return;

    /*
     * We need an exclusive lock for this to work, and if the
     * number of links has fallen to zero we are in a race with
     * another expireover.
     */

    if (*fdl==0) {
	nrsignal(SIGALRM, sigAlarm);
	alarm(LockWaitTime);
	if (hflock(fd, 4, XLOCK_EX) < 0) {
	    nrsignal(SIGALRM, SIG_IGN);
	    if (VerboseOpt > 2)
		printf("unable to rewrite data for grp=%s - file in use\n",
							group->gr_GroupName);
	    return;
	}
	nrsignal(SIGALRM, SIG_IGN);
        *fdl=1;
    }

    if (fstat(fd, &st) != 0 || st.st_nlink == 0) {
        hflock(fd, 4, XLOCK_UN);
        return;
    }

    /*
     * Calculate number of overview records
     */
    n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);

    if ((sizeof(size_t) < sizeof(long long)) && (n > 0x7FFFFFFF)) {
	fprintf(stderr, "Sanity check: dangerous number of records on non-64-bit system\n");
	return;
    }

    /*
     * Map the over map into mem
     */

    oaBase = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
    if (oaBase == NULL) {
	fprintf(stderr, "Unable to xmap over.* file for group %s (%lld) in RewriteData (%s)\n", group->gr_GroupName, n, strerror(errno));
	return;
    }

    oaBaseNew = calloc((size_t)n, sizeof(OverArt));
    if (oaBaseNew == NULL) {
	fprintf(stderr, "Unable to calloc new over.* file space for group %s (%lld) in RewriteData (%s)\n", group->gr_GroupName, n, strerror(errno));
        xunmap((void *)oaBase, n * sizeof(OverArt));
	return;
    }


    /*
     * Get new copies of group start and end numbers in case they moved
     */
    {
	const char *rec;
	int recLen;

	rec = KPDBReadRecord(KDBActive, group->gr_GroupName, KP_LOCK, &recLen);
	startNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	KPDBUnlock(KDBActive, rec);
    }
    if (group->gr_State & GRF_MODIFIED)
	startNo = group->gr_StartNo;

    /*
     * Scan entire overview index, building replacement data.* files on the
     * fly.  We have already cleaned it up, so there should not be any
     * garbarge in the index.
     */

    {
	const char *cacheBase = NULL;
	int cacheSize = 0;
	artno_t cacheArtBase = -1;

	for (artNo = startNo; artNo <= endNo; ++artNo) {
	    long long i = ((artNo & 0x7FFFFFFFFFFFFFFFLL) % n);
	    const OverArt *oa = &oaBase[i];

	    if (! OA_ARTVALID(oa))
		continue;

	    if (! OA_ARTNOEQ(artNo, oa->oa_ArtNo)) {
		printf("artNo != oa_ArtNo, panic XXXXXXXXXXXX\n");
		exit(1);
	    }

	    if (cacheArtBase == -1 ||
		artNo < cacheArtBase ||
		artNo >= (cacheArtBase + oldDataEntries)
	    ) {
		char path[PATH_MAX];
		struct stat st;
		int cfd;

		if (cacheBase != NULL) {
		    xunmap((void *)cacheBase, cacheSize);
		    cacheSize = 0;
		    cacheBase = NULL;
		}
		cacheArtBase = artNo & ~(oldDataEntries - 1);
		snprintf(path, sizeof(path), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, cacheArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		if (DebugOpt > 1)
		    printf("Opening: %s\n", path);
		if ((cfd = open(path, O_RDONLY)) >= 0 &&
		    fstat(cfd, &st) == 0
		) {
		    cacheBase = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, cfd, 0);
		    cacheSize = st.st_size;
		    xadvise(cacheBase, st.st_size, XADV_WILLNEED);
		} else {
		    if (cfd >= 0) {
			close(cfd);
		    }

		    snprintf(path, sizeof(path), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, cacheArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		    if ((cfd = open(path, O_RDONLY)) >= 0) {
			tmpfile = tmpnam(NULL);
			tmpfd = xopen(O_RDWR|O_CREAT, 0644, tmpfile);

			if (! (tmpfd < 0)) {
			    st.st_size = 0;
			    fstat(cfd, &st);

			    zmap = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, cfd, 0);
			    if (! zmap) {
		    		printf("Unable to xmap for gzcat %s: %s\n",
					path, strerror(errno));
			    } else {
				zpos = zmap + st.st_size - 4;
				zlen = ((unsigned)zpos[0] & 0xff) |
				       ((unsigned)zpos[1] & 0xff) << 8 |
				       ((unsigned)zpos[2] & 0xff) << 16 |
				       ((unsigned)zpos[3] & 0xff) << 24;

				if (zlen < 256 || zlen > (64 * 1048576)) {
				    printf("Bad zlen %d for gzcat %s\n",
						(int)zlen, path);
				    xunmap((void *)zmap, st.st_size);
				    close(tmpfd);
				} else {
				    ftruncate(tmpfd, zlen);
				    tmpmap = xmap(NULL, zlen, PROT_READ|PROT_WRITE, MAP_SHARED, tmpfd, 0);
				    if (! tmpmap) {
					printf("Unable to xmap for gztmp %s: %s\n", tmpfile, strerror(errno));
					xunmap((void *)zmap, st.st_size);
					close(tmpfd);
				    } else {
					// handle gzip headers
					zpos = zmap;

					if (zpos[0] != GZ_MAGIC0 || zpos[1] != GZ_MAGIC1 || zpos[2] != Z_DEFLATED) {
					    printf("gzip header error (%d, %d, %d) for gzcat %s: %s\n",
						zpos[0], zpos[1], zpos[2],
						path, z.msg);
					}
					zpos += 3;
					gzflags = *zpos;
					zpos += 7;

					if ((gzflags & GZ_EXTRA)) {
					    zpos += zpos[0] + (zpos[1] << 8);
					}
					if ((gzflags & GZ_ORIGNAME)) {
					    for ( ; *zpos; zpos++) {
					    }
					    zpos++;
					}
					if ((gzflags & GZ_COMMENT)) {
					    for ( ; *zpos; zpos++) {
					    }
					    zpos++;
					}
					if ((gzflags & GZ_HEADCRC)) {
					    zpos += 2;
					}

					// begin uncompress
					bzero(&z, sizeof(z));

					z.next_in = zpos;
					z.avail_in = st.st_size - (zpos - zmap);

					z.next_out = tmpmap;
					z.avail_out = zlen;

					inflateInit2(&z, -MAX_WBITS);
					code = inflate(&z, Z_FINISH);
					inflateEnd(&z);

					if (code != Z_STREAM_END) {
					    printf("inflate error (%i) for gzcat %s: %s\n",
					     	code, path, z.msg);
					    xunmap((void *)zmap, st.st_size);
					    xunmap((void *)tmpmap, zlen);
					    close(tmpfd);
					} else {
					    xunmap((void *)zmap, st.st_size);

					    cacheBase = (char *)tmpmap;
					    cacheSize = zlen;
					}
				    }
				}
			    }
			    if (unlink(tmpfile) < 0) {
				printf("Unable to remove gztmp %s: %s\n",
					tmpfile, strerror(errno));
			    }
			} else {
			    printf("Unable to open/create gztmp %s: %s\n",
				tmpfile, strerror(errno));
			}
		    }
		}
		if (cfd >= 0)
		    close(cfd);
	    }
	    rewriteDataFile(group, cacheBase, cacheSize, oa,
					&oaBaseNew[i], dirPath,
					oldDataEntries - 1, newDataEntries - 1, artNo);
	}
	rewriteDataFile(group, (char*)NULL, 0, (OverArt*)NULL, (OverArt*)NULL, (char*)NULL, 0, 0, (artno_t)0);
	if (cacheBase != NULL) {
	    xunmap((void *)cacheBase, cacheSize);
	    cacheSize = 0;
	    cacheBase = NULL;
	}
    }

    /*
     * Remove original data.* files so we can replace the over. file
     * safely.  If we are broken after this point, portions or all of the
     * overview information relating to this group will be lost.
     */
    {
	artno_t artNo;
	for (artNo = (startNo & ~(oldDataEntries-1));artNo <= endNo;
						artNo += oldDataEntries) {
	    char path1[PATH_MAX];
	    snprintf(path1, sizeof(path1), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Remove: %s\n", path1);
	    if (ForReal)
		remove(path1);

	    snprintf(path1, sizeof(path1), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Remove: %s\n", path1);
	    if (ForReal)
		remove(path1);
	}
    }

    /*
     * Replace the over. file.  If an error occurs we pretty much have to
     * blow the file away because we already deleted the data. files.
     */

    {
	char path1[PATH_MAX];
	char path2[PATH_MAX];
	int ovFd;

	snprintf(path1, sizeof(path1), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_OVER, 0, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	snprintf(path2, sizeof(path2), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_OVER, 0, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));

	/* Make sure we are using the latest versions */
	if (oh->oh_Version < OH_VERSION) {
	    printf("Upgrading overinfo header version %d->%d in %s\n",
						oh->oh_Version, OH_VERSION,
						group->gr_GroupName);
	    oh->oh_Version = OH_VERSION;
	    strncpy(oh->oh_Gname, group->gr_GroupName,
						sizeof(oh->oh_Gname) - 1);
	    oh->oh_Gname[sizeof(oh->oh_Gname) - 1] = '\0';
	    oh->oh_HeadSize = sizeof(OverHead);
	}
	oh->oh_DataEntries = newDataEntries;
	if (DebugOpt)
	    printf("Setting data size to %d\n", oh->oh_DataEntries);
	ovFd = open(path1, O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (ovFd >= 0) {
	    if (write(ovFd, oh, oh->oh_HeadSize) != oh->oh_HeadSize)
		ok = 0;
	    if (x64write(ovFd, oaBaseNew, n*sizeof(OverArt)) != n*sizeof(OverArt))
		ok = 0;
	    close(ovFd);
	    if (ok) {
		if (DebugOpt > 1)
		    printf("Renaming over: %s -> %s\n", path1, path2);
		if (ForReal && rename(path1, path2) < 0) {
		    printf("Error renaming %s -> %s (%s)\n", path1, path2,
							strerror(errno));
		    ok = 0;
		}
	    }
	} else {
	    ok = 0;
	}
	if (ok == 0) {
	    printf("Rewrite of %s over. file failed, removing\n",
							group->gr_GroupName);
	    if (ForReal)
		remove(path1);
	}
    }

    /*
     * Rename the temporary .data files to the real ones.
     */
    rblt = 0;
    if (startNo<=endNo) {
	for (artNo = (startNo & ~(newDataEntries-1)); artNo <= endNo;
						artNo += newDataEntries) {
	    char path1[PATH_MAX];
	    char path2[PATH_MAX];

	    snprintf(path1, sizeof(path1), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    snprintf(path2, sizeof(path2), "%s/%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    if (DebugOpt > 1)
		printf("Renaming data: %s -> %s\n", path1, path2);
	    if (ForReal && rename(path1, path2) < 0) {
		remove(path1);

		/*
		 * For gzip support.  Doing it this way is more I/O
		 * intensive, but doesn't require us to remember what
		 * we chose to compress.
		 */

	    	snprintf(path1, sizeof(path1), "%s/.%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	    	snprintf(path2, sizeof(path2), "%s/%s.gz", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, artNo, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
		if (rename(path1, path2) < 0) {
		    remove(path1);
		    printf("Error renaming %s -> %s (%s)\n", path1, path2,
							strerror(errno));
		}
	    }
	    rblt++;
	}
    }

    if (VerboseOpt)
	printf("Rebuilt %d data file(s) for %s\n", rblt, group->gr_GroupName);

    /*
     * cleanup
     */
    xunmap((void *)oaBase, n * sizeof(OverArt));
    free(oaBaseNew);
    return;
}

/*
 * rewriteDataFile() - rewrite the file from cacheFd/oa-params to
 *		       ob, maintaining the FileCacheList list.
 */

void
rewriteDataFile(Group *group, const char *cacheBase, int cacheSize, const OverArt *oa, OverArt *ob, const char *dirPath, int oldDataEntriesMask, int newDataEntriesMask, artno_t artNo)
{
    artno_t oldArtBase, newArtBase;
    static FileCacheList *fc = NULL;

    if (oa==NULL || ob==NULL) { /* end of for, closing fd */
	if (VerboseOpt > 2)
	    printf("End of rewrite: %s\n", group->gr_GroupName);
	if (fc != NULL && fc->fc_Fd != -1) {
	    close(fc->fc_Fd);
	    fc->fc_Fd = -1;
	    fc->fc_ArtBase = -1;
	}
	return;
    }

    oldArtBase = artNo & ~oldDataEntriesMask;
    newArtBase = artNo & ~newDataEntriesMask;

    if (VerboseOpt > 2)
	printf("Rewriting: %s:%lld:%lld\n", group->gr_GroupName, oldArtBase, newArtBase);

    if (fc == NULL) {
	fc = calloc(1, sizeof(FileCacheList));
	fc->fc_Fd = -1;
	fc->fc_ArtBase = -1;
    }

    if (fc->fc_ArtBase != newArtBase || fc->fc_Fd == -1) {
	char path[PATH_MAX];

	if (fc->fc_Fd != -1)
	    close(fc->fc_Fd);

	snprintf(path, sizeof(path), "%s/.%s", dirPath,
					GFName(group->gr_GroupName,
					GRPFTYPE_DATA, newArtBase, 0,
					group->gr_Iter,
					&DOpts.ReaderGroupHashMethod));
	if (DebugOpt > 1)
            printf("Creating: %s\n", path);
	fc->fc_Fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fc->fc_Fd == -1)
	    printf("Error opening %s: %s\n", path, strerror(errno));
	fc->fc_ArtBase = newArtBase;
    }

    /*
     * copy data from cacheFd/oa-params to rf->fc_Fd and fill in ob.  It is
     * possible for cacheBase to be NULL if there is no valid data source
     * for the file.
     */
    if (
	cacheBase != NULL && 
	fc->fc_Fd >= 0 && 
	oa->oa_SeekPos >= 0 &&
	oa->oa_Bytes > 0 &&
	oa->oa_SeekPos + oa->oa_Bytes < cacheSize &&
	cacheBase[oa->oa_SeekPos + oa->oa_Bytes] == 0 && 	  /* guard */
	(oa->oa_SeekPos == 0 || cacheBase[oa->oa_SeekPos-1] == 0) /* guard */

    ) {
	*ob = *oa;
	ob->oa_SeekPos = lseek(fc->fc_Fd, 0L, 1);
	if (write(fc->fc_Fd, cacheBase + oa->oa_SeekPos, oa->oa_Bytes + 1) != oa->oa_Bytes + 1) {
	    lseek(fc->fc_Fd, ob->oa_SeekPos, 0);
	    ftruncate(fc->fc_Fd, ob->oa_SeekPos);
	    ob->oa_ArtNo = -2;
	    ob->oa_SeekPos = 0;
	    ob->oa_Bytes = 0;
	    printf("copy failed %s:%d, write error\n", group->gr_GroupName, oa->oa_ArtNo);
	}
    } else if (oa->oa_SeekPos == -1) {
	; /* do nothing */
    } else if (CheckDataEntries) {
	*ob = *oa;
	printf("entry removed %s:%d, %s\n",
	    group->gr_GroupName,
	    oa->oa_ArtNo,
	    ((cacheBase == NULL) ? "source-missing" :
	    (fc->fc_Fd < 0) ? "dest-failure" :
	    (oa->oa_Bytes <= 0) ? "source-bounds1" :
	    (oa->oa_SeekPos + oa->oa_Bytes >= cacheSize) ? "source-bounds2" :
	    "source-corrupt")
	);
	ob->oa_ArtNo = -2;
	ob->oa_SeekPos = 0;
	ob->oa_Bytes = 0;
    } else {
	printf("copy failed %s:%d, %s\n",
	    group->gr_GroupName,
	    oa->oa_ArtNo,
	    ((cacheBase == NULL) ? "source-missing" :
	    (fc->fc_Fd < 0) ? "dest-failure" :
	    (oa->oa_Bytes <= 0) ? "source-bounds1" :
	    (oa->oa_SeekPos + oa->oa_Bytes >= cacheSize) ? "source-bounds2" :
	    "source-corrupt")
	);
    }
}

/*
 * Resize a newsgroup's over.* index file, if possible.  If called via the -s
 * option, only groups that need resizing are rebuilt.  If called via the -R
 * option, the group is always rebuild AND the associated data files are 
 * rebuilt.
 */

void
ResizeGroup(Group *group, int fd, OverHead *oh, int newSize, int OldDataEntries, int *fdl)
{
    int oldSize = oh->oh_MaxArts;
    struct stat st;

    if (*fdl==0) {
	nrsignal(SIGALRM, sigAlarm);
	alarm(LockWaitTime);
	if (hflock(fd, 4, XLOCK_EX) < 0) {
	    nrsignal(SIGALRM, SIG_IGN);
	    ++NoResizedGroups;
	    if (VerboseOpt)
		printf("resize maxArts from %d to %d failed, file in use grp=%s\n",
					oldSize, newSize, group->gr_GroupName);
	    return;
	}
	nrsignal(SIGALRM, SIG_IGN);
	*fdl=1;
    }

    if (fstat(fd, &st) < 0 || st.st_nlink == 0) {
        hflock(fd, 4, XLOCK_UN);
        ++NoResizedGroups;
        if (VerboseOpt) {
    	    printf("resize maxArts from %d to %d failed, file in use grp=%s\n",
				oldSize, newSize, group->gr_GroupName);
        }
        return;
    }

    /*
     * Resize a group.  We 'own' the overview index file (because other 
     * processes must get a shared lock on offset 4 and we got the exclusive
     * lock).  We can do anything we want with it, but we must rewrite the
     * file in-place to maintain lock consistancy.
     *
     * Resize the group by copying the existing data into an array, validating
     * it based on known information, then putting it back.
     */

    {
#ifdef	READOA
	OverArt *oa = calloc(oldSize, sizeof(OverArt));
#else
	OverArt *oa = NULL;
#endif
	OverArt *ob = calloc(newSize, sizeof(OverArt));
	long long n;
	long long i;
	int recLen;
	const char *rec = KPDBReadRecord(KDBActive, group->gr_GroupName,
							KP_LOCK, &recLen);

#ifndef	READOA
        if (fstat(fd, &st) != 0)
	    return;

        /*
         * Calculate number of overview records
         */

        n = (st.st_size - oh->oh_HeadSize) / sizeof(OverArt);
	oa = xmap(NULL, n * sizeof(OverArt), PROT_READ, MAP_SHARED, fd, oh->oh_HeadSize);
#endif

	if (! oa || ! ob) {
	    fprintf(stderr, "Unable to calloc space for over.* file for group %s\n", group->gr_GroupName);
	    exit(1);
	}

#ifdef	READOA
	lseek(fd, oh->oh_HeadSize, 0);
	if (((n = x64read(fd, oa, oldSize * sizeof(OverArt)))) < 0) {
	    fprintf(stderr, "Unable to read over.* file for group %s: %s\n",
	    			group->gr_GroupName,
				strerror(errno));
	    exit(1);
	}
	if (n != oldSize * sizeof(OverArt)) {
	    fprintf(stderr, "Read of group over.* file smaller than expected (%s %d %d)\n", 
				group->gr_GroupName,
				n,
				oldSize * sizeof(OverArt));
	}
	n = n / sizeof(OverArt);
#endif

	/*
	 * We need to get the NE field again now that we have locked the
	 * over.* file because it could have been adjusted by dreaderd
	 * since we originally got it.
	 */
	group->gr_EndNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"),
								NULL, 10);
	KPDBUnlock(KDBActive, rec);

	if (group->gr_EndNo - group->gr_StartNo >= newSize) {
	    if (VerboseOpt)
		printf("adjust %s startno %lld to %lld (endno=%lld  newsize=%d)\n",
				group->gr_GroupName,
				group->gr_StartNo,
				group->gr_EndNo - newSize + 1,
				group->gr_EndNo,
				newSize);
	    group->gr_StartNo = group->gr_EndNo - newSize + 1;
	    group->gr_State |= GRF_EDITEDBEG | GRF_MODIFIED;
	}

	/*
	 * Run through and copy validated entries
	 */

	for (i = group->gr_StartNo; i <= group->gr_EndNo; ++i) {
	    OverArt *op = &oa[(i & 0x7FFFFFFFFFFFFFFFLL) % oh->oh_MaxArts];

	    if (OA_ARTNOEQ(i, op->oa_ArtNo)) {
		ob[(i & 0x7FFFFFFFFFFFFFFFLL) % newSize] = *op;
		if (VerboseOpt > 2)
		    printf("resize %s copying ArtNo at %lld (got %d wanted %d)\n", 
				group->gr_GroupName,
				i,
				op->oa_ArtNo,
				OA_ARTNOSET(i));
	    } else {
		if (VerboseOpt > 2 || (VerboseOpt && op->oa_ArtNo))
		    printf("resize %s mismatched ArtNo at %lld (got %d wanted %d)\n", 
				group->gr_GroupName,
				i,
				op->oa_ArtNo,
				OA_ARTNOSET(i));
	    }
	}

	/*
	 * Rewrite.  Only if ForReal.  Note that oh_MaxArts isn't updated 
	 * (for use in other parts of dexpireover) if not for real.
	 */

	if (ForReal) {
	    long long obsize;

	    /* Take this opportunity to set the correct version */
	    if (oh->oh_Version < OH_VERSION) {
		printf("Upgrading overinfo header version %d->%d\n",
						oh->oh_Version, OH_VERSION);
		oh->oh_Version = OH_VERSION;
		strncpy(oh->oh_Gname, group->gr_GroupName,
						sizeof(oh->oh_Gname) - 1);
		oh->oh_Gname[sizeof(oh->oh_Gname) - 1] = '\0';
		oh->oh_HeadSize = sizeof(OverHead);
		oh->oh_DataEntries = OldDataEntries;
	    }
	    lseek(fd, 0L, 0);
	    if (ftruncate(fd, oh->oh_HeadSize + newSize * sizeof(OverArt)) < 0) {
	        fprintf(stderr, "ftruncate of new group over.* file failed (%s): %s\n", 
						group->gr_GroupName,
						strerror(errno));
	    }
	    oh->oh_MaxArts = newSize;
	    if (x64write(fd, oh, sizeof(OverHead)) != sizeof(OverHead)) {
		fprintf(stderr, "write of new group over.* OverHead failed (%s): %s\n",
						group->gr_GroupName,
						strerror(errno));
	    }

	    /*
	     * This is no longer an atomic write.  We do hold
	     * a write lock, in theory this should be OK.  Have
	     * to write chunks because write can't handle > 32bit
	     */

	    obsize = newSize * sizeof(OverArt);
	    if (x64write(fd, ob, obsize) != obsize) {
		fprintf(stderr, "write of new group over.* data failed (%s, %lld): %s\n",
						group->gr_GroupName,
						obsize,
						strerror(errno));
	    }
	}

	free(ob);
#ifdef	READOA
	free(oa);
#else
        xunmap((void *)oa, n * sizeof(OverArt));
#endif
    }
    ++ResizedGroups;

    if (VerboseOpt && oldSize != newSize) {
	printf("resized maxArts from %d to %d grp=%s\n", oldSize, newSize, group->gr_GroupName);
    }
    return;
}

int
nearestPower(int n)
{
    int i;

    for (i = 1; i < n; i <<= 1)
	;
    return(i);
}

void
ReadDExpOverList()
{
    FILE *DExpOverList;
    hash_t read_buffer[DEXPOVER_READ_BUFFER_SIZE];
    int i, n;
    char path[128];

    dexpover_msgid_hash =
	(struct bucket_t *)malloc(DEXPOVER_HASH_SIZE * sizeof(struct bucket_t));
    for(i=0; i<DEXPOVER_HASH_SIZE; i++) {
	dexpover_msgid_hash[i].valid=0;
	dexpover_msgid_hash[i].next=NULL;
    }

    snprintf(path, 128, "%s.bak", PatDbExpand(DExpireOverListPat));
    rename(PatDbExpand(DExpireOverListPat), path);
    DExpOverList = fopen(path, "r");

    if(DExpOverList == NULL) return;

    while((n = fread(read_buffer, sizeof(hash_t),
		     DEXPOVER_READ_BUFFER_SIZE, DExpOverList))) {
	for(i=0; i<n; i++) {
	    int hashval;
	    struct bucket_t *chain;

	    hashval = (read_buffer[i].h1)&(DEXPOVER_HASH_SIZE-1);
	    chain = &dexpover_msgid_hash[hashval];

	    while((chain->valid == 1) && (chain->next != NULL))
		chain = chain->next;

	    if(chain->valid == 1) {
		chain->next = (struct bucket_t *)malloc(sizeof(struct bucket_t));
		chain = chain->next;
	    }

	    chain->valid = 1;
	    chain->hash_item = read_buffer[i];
	    chain->next = NULL;
	}
    }

    fclose(DExpOverList);
}

int
hexCharToInt(char c)
{
    return
	(c == '0') ? 0 :
	(c == '1') ? 1 :
	(c == '2') ? 2 :
	(c == '3') ? 3 :
	(c == '4') ? 4 :
	(c == '5') ? 5 :
	(c == '6') ? 6 :
	(c == '7') ? 7 :
	(c == '8') ? 8 :
	(c == '9') ? 9 :

	(c == 'a') ? 10 :
	(c == 'b') ? 11 :
	(c == 'c') ? 12 :
	(c == 'd') ? 13 :
	(c == 'e') ? 14 :
	(c == 'f') ? 15 :

	(c == 'A') ? 10 :
	(c == 'B') ? 11 :
	(c == 'C') ? 12 :
	(c == 'D') ? 13 :
	(c == 'E') ? 14 :
	(c == 'F') ? 15 :

	-1;
}

int
expOverListCheckExpired(hash_t *hv)
{
    int hashval;
    bucket_t *chain;

    hashval = (hv->h1)&(DEXPOVER_HASH_SIZE-1);
    chain = &dexpover_msgid_hash[hashval];

    while(chain && chain->valid) {
	if((chain->hash_item.h1 == hv->h1) &&
	   (chain->hash_item.h2 == hv->h2)) {
	    return 0;
	}
	chain = chain->next;
    }

    return -1;
}

int
getDataEntries(const char *path)
{
    /*
     * over. file	(fixed length file)
     */
    int fd;
    OverHead oh;
    int de = OD_HARTS;

    if ((fd = open(path, O_RDONLY)) >= 0) {
	int r;
	r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
	    r = 0;
	if (r == 1 && oh.oh_Version >= 3)
	    de = oh.oh_DataEntries;
	close(fd);
    }
    return(de);
}

long long
x64write(int d, const void *buf, long long nbytes)
{
    long long nwrite, owrite = 0LL;
    int rval;

    while ((nwrite = nbytes)) {
	if (nwrite > 1024 * 1024 * 1024LL) {
	    nwrite = 1024 * 1024 * 1024LL;
	}
	if ((rval = write(d, buf + owrite, (int)nwrite)) < 0) {
		return(-1LL);
	}
	nbytes -= rval;
	owrite += rval;
    }
    return(owrite);
}

long long
x64read(int d, const void *buf, long long nbytes)
{
    long long nread, oread = 0LL;
    int rval;

    while ((nread = nbytes)) {
	if (nread > 1024 * 1024 * 1024LL) {
	    nread = 1024 * 1024 * 1024LL;
	}
	if ((rval = read(d, buf + oread, (int)nread)) < 0) {
		return(-1LL);
	}
	nbytes -= rval;
	oread += rval;
    }
    return(oread);
}

void dumpoh(OverHead *oh, char *filename)
{
    FILE *fp;
    if (! ((fp = fopen(filename, "w")))) {
	fprintf(stderr, "failed to open %s\n", filename);
	return;
    }
    fprintf(fp, "OverHead version %d, byteorder %08x\n", oh->oh_Version, oh->oh_ByteOrder);
    fprintf(fp, "	HeadSize	%d\n	MaxArts		%d\nDataEntries	%d\nGroup		%s\n\n", oh->oh_HeadSize, oh->oh_MaxArts, oh->oh_DataEntries, oh->oh_Gname);
    fclose(fp);
}



void dumpovmap(OverArt *oa, long long n, char *filename)
{
    long long i;
    int nulrun = 0;
    FILE *fp;

    if (! ((fp = fopen(filename, "w")))) {
	fprintf(stderr, "failed to open %s\n", filename);
	return;
    }
    for (i = 0; i < n; i++) {
	if (oa[i].oa_ArtNo == 0 && nulrun != 0) {
	    continue;
	}
	fprintf(fp, "%012lld a=%010d o=%07d b=%07d h=%08x s=%08d t=%08d %s\n", i, oa[i].oa_ArtNo, oa[i].oa_SeekPos, oa[i].oa_Bytes, oa[i].oa_MsgHash.h1, oa[i].oa_ArtSize, oa[i].oa_TimeRcvd, OA_ARTVALID(&oa[i]) ? "(valid)" : "(BAD)");
	if (oa[i].oa_ArtNo == 0 && oa[i].oa_TimeRcvd == 0) {
	    nulrun = 1;
	    fprintf(fp, "	null entries\n");
	} 
    }
    fclose(fp);
}


