
/*
 * DEXPIRE.C	- diablo expire.
 *
 *	remove directories in time order until sufficient space is 
 *	available.
 *
 *	When sufficient space is available, scan remaining files and
 *	then scan history and set the expired flags as appropriate.
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Modification by Nickolai Zeldovich to store msgid hashes when
 * expiring articles to allow for better overview expiration.
 */

#include "defs.h"
#include <sys/param.h>
#ifndef _AIX
#include <sys/mount.h>
#endif
#ifdef _AIX
#include <sys/statfs.h>
#endif

#if USE_SYSV_STATFS
#include <sys/statfs.h>
#define f_bavail f_bfree
#endif

/*
 * The solaris statvfs is rather limited, but we suffer with reduced
 * capability (and hence take a possible performance hit).
 */
#if USE_SUN_STATVFS
#include <sys/statvfs.h>	/* god knows if this hack will work */
#define f_bsize	f_frsize	/* god knows if this hack will work */
#define fsid_t u_long
#define statfs statvfs
#endif

#if USE_SYS_VFS			/* this is mainly for linux	*/
#include <sys/vfs.h>
#endif

/*
 * For each directory (D.*) that contains files (B.*), we point to the
 * Partiton that contains this directory (P.* or N.*)
 *
 * The old spool layout will have Partiton = /news/spool/news
 *
 * The spooldir format will have Partiton = /news/spool/news/N.nn
 *
 * The new format will have Partition = /news/spool/news/P.nn
 * or Partition = /news/spool/news/P.nn/N.nn
 *
 */
typedef struct FileSystem {
    dev_t		dev;		/* The unique filesystem ID */
    double		fsfree;		/* Space free on filesystem  (MB) */
    long		fsfreefiles;	/* Inodes free on filesystem */
    struct FileSystem	*next;
} FileSystem;

/*
 * Linked list of all P.* and N.* directories with a pointer to the
 * underlying filesystem.
 */

typedef struct Partition {
    char		*partname;	/* The full directory path */
    double		minfree; 	/* Free Space target  (MB) */
    long		minfreefiles; 	/* Free Inodes target */
    int			spaceok;	/* Is space ok */
    double		partsize;	/* Space used in partition  (MB) */
    time_t		age;		/* Age of oldest dir in part (secs) */
    double		maxsize; 	/* Max size allowed */
    long		keeptime; 	/* How long to keep articles */
    int			expmethod; 	/* Sync or dirsize expire method */
    struct FileSystem	*filesys;	/* Which filesys does this lie on */
    struct Partition	*next;
} Partition;

/*
 * Entry for D.* directories with a pointer to the containing partition.
 * The partition size of updated when spool entires are removed and its
 * spaceok flag is set when further spool entries within the partition do
 * not need to be cleaned up.
 */

typedef struct SpoolEnt {
    char		*dirname;	/* The relative path to directory */
    double		dirsize;	/* Space used in directory */
    struct Partition	*partn;		/* Which spool partition */
    uint8		removed;	/* Has it been removed */
} SpoolEnt;

struct FileSystem *FileSystems = NULL;
struct Partition *Partitions = NULL;
struct SpoolEnt **SpoolEntries;
int		entryIdx = 0;
int		entryMax = 64;
double		FreeSpaceTargetList[100];
uint16		*SinglePart = NULL;
time_t		TimeNow;

int VerboseOpt = 1;
int NotForReal = 1;
int SoftUpdates = -1;
int SyncInterval = 0;
int UseDirSize = 0;
int MaxPass = 0;
int UnexpireOpt = 0;
int DoCleanup = 1;
int TestOpt = 0;
int UpdateHistory = 0;		/* Do we run the history update */
int HistoryUpdateOpt = -1;	/* Do we actually update the history entry 
				 *	0 = No
				 *	1 = Force
				 *	2 = Not entries
				 *	-1 = if changes
				 */
int WriteHashesToFileOpt = 0;
const char *HistoryFile = NULL;
off_t HistoryEnd;
int HistoryFd = -1;

int CleanAllSpools(void);
int spoolEntSort(const void *s1, const void *s2);
void addFreeSpaceTarget(char *which);
int cleanupDirectories(void);
long removeDirectory(char *partname, char *dirname, int *count, int *ccount, double *size);
void scanDirectory(Partition *scanpart);
double findSize(char *pathname, char *dirname);
void printPartition(Partition *p);
int updateHistory(void);
int findNode(const char *path, int createMe);
void freeNodes(void);
void dumpNodeTable(void);
time_t getAge(char *dirname);
double freeSpaceOn(char *path, int logit, long *freefiles);
FileSystem *findFileSys(char *path);
void DumpSpoolEntries(void);

void
Usage(void)
{
	printf("This program performs an expire run on a diablo spool\n");
	printf("dexpire -a|n [-c0] [-f historyfile] [-h0|1] [-k] [-O nn] [-o] [-q] [-R s:n] [-S nn] [-s[n[:i]]] [-u] [-v] [-z] [-C diablo.config] [-d[n]] [-V]\n");
	printf("\t-a\tactually make changes (nothing is done without this option)\n");
	printf("\t-c0\tdon't perform file removal pass\n");
	printf("\t-f\tspecify the history file to use\n");
	printf("\t-h0\tdon't scan history file at all\n");
	printf("\t-h1\tforce scan of history file\n");
	printf("\t-k\tdon't update history entries\n");
	printf("\t-n\trun through process, but don't make any changes\n");
	printf("\t-O\tset number of dexpire iterations before exit\n");
	printf("\t-o\twrite ID hashes to a file\n");
	printf("\t-q\tbe relatively quiet\n");
	printf("\t-R\tset a freespacetarget for a spool (spoolnum:target)\n");
	printf("\t-S\tonly expire the specified spool number\n");
	printf("\t-s\tsync before disk free space checks (optional time:interval)\n");
	printf("\t-t\ttest expired status to spool status only\n");
	printf("\t-u\t'unexpire' all articles marked as expired in dhistory\n");
	printf("\t-v\tbe more verbose\n");
	printf("\t-z\texpire based on directory size, rather than disk space\n");
	printf("\t-C file\tspecify diablo.config to use\n");
	printf("\t-d[n]\tset debug [with optional level]\n");
	printf("\t-V\tprint version and exit\n");
	exit(0);
}


int
main(int ac, char **av)
{
    int n;
    int allOk = 0;
    int count;

    LoadDiabloConfig(ac, av);

    for (n = 1; n < ac; ++n) {
	char *ptr = av[n];

	if (*ptr == '-') {
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'a':
		NotForReal = 0;
		break;
	    case 'c':
		DoCleanup = strtol(ptr, NULL, 0);;
		break;
	    case 'f':
		HistoryFile = *ptr ? ptr : av[++n];
		break;
	    case 'h':
		if (*ptr == '0')
		    HistoryUpdateOpt = 0;
		else
		    HistoryUpdateOpt = 1;
		break;
	    case 'k':
		HistoryUpdateOpt = 2;
		break;
	    case 'n':
		NotForReal = 1;
		break;
	    case 'O':
		if (*ptr)
		    MaxPass = strtol(ptr, NULL, 0);
		else
		    MaxPass = 1;
		break;
	    case 'o':
		WriteHashesToFileOpt = 1;
		break;
	    case 'q':
		VerboseOpt = 0;
		break;
	    case 'R':
		addFreeSpaceTarget(*ptr ? ptr : av[++n]);
		break;
	    case 'S':
		SinglePart = (uint16 *)malloc(sizeof(uint16));
		*SinglePart = (uint16)strtol((*ptr ? ptr : av[++n]), NULL, 0);
		break;
	    case 's':
		if (*ptr) {
		    char *p;
		    if ((p = strchr(ptr, ':')) != NULL) {
			SoftUpdates = strtol(ptr, NULL, 0);
			SyncInterval = strtol(p + 1, NULL, 0); 
		    } else {
			SoftUpdates = strtol(ptr, NULL, 0);
		    }
		} else {
		    SoftUpdates = 1;
		}
		break;
	    case 't':
		TestOpt = 1;
		VerboseOpt = 1;
		break;
	    case 'u':
		UnexpireOpt = 1;
		break;
	    case 'v':
		VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
		break;
	    case 'z':
		if (*ptr)
		    UseDirSize = strtol(ptr, NULL, 0);
		else
		    UseDirSize = 1;
		break;
	    /* Common options */
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

    if (ac == 1) {
	Usage();
    }

    if (NotForReal)
	MaxPass = 1;
    if (NotForReal && HistoryUpdateOpt == 1)
	HistoryUpdateOpt = 2;
    count = 1;
    if (HistoryFile == NULL)
	HistoryFile = strdup(PatDbExpand(DHistoryPat));
    TimeNow = time(NULL);
    while (!allOk) {
	if (VerboseOpt)
	    printf("Spool scan pass %d\n", count);
	allOk = CleanAllSpools();
	if (allOk || (MaxPass > 0 && --MaxPass == 0))
	    break;
	count++;
	sleep(10);
    }
    return(0);
}

int
CleanAllSpools(void)
{
    int n;
    int allOk = 1;

    UpdateHistory = (HistoryUpdateOpt > 0);
    FileSystems = NULL;
    Partitions = NULL;

    /*
     * Removal Scan
     */

    SpoolEntries = malloc(entryMax * sizeof(SpoolEnt *));
    entryIdx = 0;
     
    LoadSpoolCtl(0, 1);

    /*
     * Open and get a copy of the history file size before we scan the spools
     * because we shouldn't check history entries added beyond this point
     */
    if (HistoryUpdateOpt != 0 || UnexpireOpt) {
	struct stat sb;

	HistoryFd = open(HistoryFile, O_RDWR, 0644);
	if (HistoryFd == -1) {
	    fprintf(stderr, "Unable to open history(%s): %s\n",
						HistoryFile, strerror(errno));
	    exit(1);
	}
	if (fstat(HistoryFd, &sb) != 0) {
	    fprintf(stderr, "Unable to stat history(%s): %s\n",
						HistoryFile, strerror(errno));
	    exit(1);
	}
	HistoryEnd = sb.st_size;
    }

    /*
     * Get into spool directory
     */
    if (chdir(PatExpand(SpoolHomePat)) != 0) {
	fprintf(stderr, "Unable to chdir(%s)\n", PatExpand(SpoolHomePat));
	exit(1);
    }

    /*
     * Scan through each of the spool objects
     */
    if (!UnexpireOpt) {
	int i;
	uint16 spoolnum;
	char *path;
	double maxsize = 0.0;
	double minfree = 0.0;
	long minfreefiles = 0;
	long keeptime = 0;
	int expmethod = EXM_SYNC;
	
	for (i = GetFirstSpool(&spoolnum, &path, &maxsize, &minfree, &minfreefiles, &keeptime, &expmethod); i;
		i = GetNextSpool(&spoolnum, &path, &maxsize, &minfree, &minfreefiles, &keeptime, &expmethod))  {
	    if (SinglePart && *SinglePart != spoolnum)
		continue;
	    {
		Partition *p = (Partition *)malloc(sizeof(Partition));;
	
		if (VerboseOpt)
		    printf("Spool Object: %02d\n", spoolnum);
		p->partname = (char *)malloc(strlen(path) + 2);
		strcpy(p->partname, path);
		if (path[strlen(path) - 1] != '/')
		    strcat(p->partname, "/");
		p->partsize = 0.0;
		p->age = 0;
		p->minfree = minfree;
		p->minfreefiles = minfreefiles;
		p->maxsize = maxsize;
		p->keeptime = keeptime;
		if (UseDirSize)
		    p->expmethod = EXM_DIRSIZE;
		else
		    p->expmethod = expmethod;
		p->spaceok = 0;
		p->filesys = findFileSys(p->partname);
		p->next = Partitions;
		Partitions = p;
		if (FreeSpaceTargetList[spoolnum] > 0.0)
		    p->minfree = FreeSpaceTargetList[spoolnum];
		scanDirectory(p);
	    }
	}
    }

    if (chdir(PatExpand(SpoolHomePat)) != 0) {
	fprintf(stderr, "Unable to chdir(%s)\n", PatExpand(SpoolHomePat));
	exit(1);
    }

    /*
     * Sort directory	'0' <= 'a' so we can safely sort numerically
     *			with strcmp.
     */


    if (entryIdx) {
	qsort(SpoolEntries, entryIdx, sizeof(SpoolEnt *), spoolEntSort);
	if (DoCleanup)
	    allOk = cleanupDirectories();
    }

    /*
     * History file update scan
     */

    if (chdir(PatExpand(SpoolHomePat)) != 0) {
	fprintf(stderr, "Unable to chdir(%s)\n", PatExpand(SpoolHomePat));
	exit(1);
    }

    if (UpdateHistory || UnexpireOpt) {
	int n;
	printf("DExpire updating history file\n");
	n = updateHistory();
	printf("DExpire history file update complete, %d articles %smarked %sexpired\n",
					n,
					NotForReal ? "would be " : "",
					UnexpireOpt ? "un" : "");
    } else {
	printf("DExpire history file will not be updated\n");
    }

    if (HistoryUpdateOpt != 0 || UnexpireOpt)
	close(HistoryFd);

    /*
     * Cleanup all the allocated space
     */
    for (n = 0; n < entryIdx; ++n) {
	free(SpoolEntries[n]->dirname);
	free(SpoolEntries[n]);
    }
    free(SpoolEntries);

    while (Partitions) {
	Partition *next;

	next=Partitions->next;
	free(Partitions);
	Partitions=next;        
    }

    while (FileSystems) {
	FileSystem *next;

	next=FileSystems->next;
	free(FileSystems);
	FileSystems=next;        
    }
    return(allOk);
}

int
spoolEntSort(const void *s1, const void *s2)
{
    struct SpoolEnt *ent1 = *(struct SpoolEnt **)s1;
    struct SpoolEnt *ent2 = *(struct SpoolEnt **)s2;

    return(strcmp(ent1->dirname, ent2->dirname));
}

void
addFreeSpaceTarget(char *which)
{
    char *p;
    int part;

    p = strchr(which, ':');
    if (p == NULL) {
	fprintf(stderr, "-R parameter needs 'n=n'\n");
	exit(1);
    }
    *p = '\0';
    part = atoi(which);
    if (part < 0 || part >= 100) {
	fprintf(stderr, "Invalid spool number for -R (%s)\n", which);
	exit(1);
    }
    FreeSpaceTargetList[part] = strtod(++p, NULL);
    if (FreeSpaceTargetList[part] < 0.0) {
	fprintf(stderr, "Invalid free space target in -R (%d)\n", part);
	exit(1);
    }
}

long
removeDirectory(char *partname, char *dirname, int *count, int *ccount, double *size)
{
    DIR *dir;
    char pwd[PATH_MAX];
    char tdir[PATH_MAX];
    char *ptr;
    int c = 0;

    if (getcwd(pwd, sizeof(pwd)) == NULL) {
	fprintf(stderr, "Unable to determine current directory\n");
	return(c);
    }

    sprintf(tdir,"%s/%s", partname, dirname);
    if (chdir(tdir) != 0) {
	fprintf(stderr, "Unable to chdir(%s)\n", tdir);
	return(c);
    }

    if (VerboseOpt) {
	printf("    -%s%s ... ", partname, dirname);
	fflush(stdout);
    }

    if ((dir = opendir(".")) != NULL) {
	den_t *den;
	struct stat sb;

	while ((den = readdir(dir)) != NULL) {
	    if ((den->d_name[0] == 'B' && den->d_name[1]=='.') ||
		    (strlen(den->d_name) > 8 && den->d_name[8] =='.')
	    ) {
		if (size != NULL && stat(den->d_name, &sb) == 0)
		    *size += sb.st_size;
		if (!NotForReal)
		    remove(den->d_name);
		if (count)
		    ++*count;
		c++;
	    } else if (WildCmp("*.core", den->d_name) == 0 ||
				WildCmp("core.*", den->d_name) == 0 ||
				strcmp(den->d_name, "core") == 0
	    ) {
		if (size != NULL && stat(den->d_name, &sb) == 0)
		    *size += sb.st_size;
		if (!NotForReal)
		    remove(den->d_name);
		if (ccount)
		    ++*ccount;
		c++;
	    }
	}
	closedir(dir);
    }
    if (chdir("..") != 0) {
	fprintf(stderr, "Unable to chdir(..)\n");
	return(c);
    }
    strcpy(tdir, dirname);
    if ((ptr = strchr(tdir, 'D')))
	*ptr = 'A';
    if (!NotForReal && rename(dirname, tdir) < 0) {
	printf("unable to rename directory: %s\n", dirname);
    } else {
	if (VerboseOpt)
	    printf("done.\n");
	errno = 0;
	if (!NotForReal && rmdir(tdir) < 0)
	    fprintf(stderr, "Unable to rmdir(\"%s\"): %s\n",
					tdir, strerror(errno));
	else
	    c++;
    }
    chdir(pwd);
    return(c);
}

/*
 * cleanupDirectories() - work through each partition and cleanup
 * space on it until enough space is free
 */
int
cleanupDirectories(void)
{
    int i;
    int dircount = 0;
    int count = 0;
    int ccount = 0;
    double tremsize = 0.0;
    int allclean = 1;

    if (entryIdx)
	printf("%d directories (%s - %s)\n", entryIdx, SpoolEntries[0]->dirname,
				SpoolEntries[entryIdx-1]->dirname);

    if (VerboseOpt)
	printf("Cleaning up directories\n");
    /*
     * Remove files a directory at a time.  The spool directories are
     * named A.* or D.*.  We remove a directory by renaming it from D.
     * to A., removing the files, then removing the directory.  The
     * rename is required to prevent Diablo from recreating files in
     * the directory (and thus potentially corrupting articles by 
     * reusing history keys).
     */

    for (i = 0; i < entryIdx; ++i) {
	int spaceok = 1;
	int filesok = 1;
	int sizeok = 1;
	int ageok = 1;
	Partition *part;
	double spacefree;
	long freefiles;
	double remsize = 0.0;
	long c;

	part = SpoolEntries[i]->partn;
	if (part->spaceok)
	    continue;

	spacefree = part->filesys->fsfree;
	freefiles = part->filesys->fsfreefiles;
#if 0
	/*
	 * XXX We don't need to do this because it has already been done
	 * as part of the directory scan and we do it after removing
	 * each directory
	 */
	if (part->expmethod == EXM_SYNC && part->minfree) {
	    spacefree = freeSpaceOn(part->partname, 1, &freefiles);
	}
#endif
	if (VerboseOpt) {
	    printf("Checking %s on %s\n", SpoolEntries[i]->dirname,
							part->partname);
	    if (part->maxsize)
		printf("\tsize=%s/%s\n", ftos(part->partsize),
						ftos(part->maxsize));
	    if (part->keeptime)
		printf("\tkeeptime=%u/%lu secs\n", (int)part->age, part->keeptime);
	    if (part->minfree)
		printf("\tfreespace=%s/%s\n", ftos(spacefree),
						ftos(part->minfree));
	    if (part->minfreefiles)
		printf("\tfreefiles=%ld/%ld\n", freefiles, part->minfreefiles);
	    printf("\texpire method %s",
			(part->expmethod == EXM_SYNC) ? "sync" : "dirsize");

	}
	if (part->minfree && spacefree < part->minfree)
	    spaceok = 0;
	if (part->minfreefiles && freefiles < part->minfreefiles)
	    filesok = 0;
	if (part->maxsize && part->partsize > part->maxsize)
	    sizeok = 0;
	if (part->keeptime && part->age > part->keeptime)
	    ageok = 0;
	if (spaceok && sizeok && ageok && filesok) {
	    part->spaceok = 1;
	    if (VerboseOpt)
		printf("\t\t: ok\n");
	    continue;
	}
	if (VerboseOpt) {
	    printf("\t\t: need attention ");
	    if (!spaceok)
		printf("(freespace < minfree) ");
	    if (!filesok)
		printf("(files < minfiles) ");
	    if (!sizeok)
		printf("(size > maxsize) ");
	    if (!ageok)
		printf("(age > keeptime) ");
	    printf("\n");
	}
	if (DebugOpt > 1)
	    printPartition(part);

	/*
	 * XXX We should probably include the size of the dir in remsize
	 */
	c = removeDirectory(part->partname, SpoolEntries[i]->dirname,
						&count, &ccount, &remsize);
	tremsize += remsize;
	++dircount;
	allclean = 0;
	SpoolEntries[i]->removed = 1;
	if (HistoryUpdateOpt != 0)
	    UpdateHistory = 1;
	part->filesys->fsfree += remsize;
	part->filesys->fsfreefiles += c;
	if (part->expmethod == EXM_DIRSIZE || part->maxsize) {
	    SpoolEntries[i]->dirsize = remsize;
	    part->partsize -= remsize;
	} else if (!NotForReal && (part->minfree || part->minfreefiles))
	    part->filesys->fsfree = freeSpaceOn(part->partname, 1,
					&part->filesys->fsfreefiles);
	if (i + 1 < entryIdx)
	    part->age = TimeNow - getAge(SpoolEntries[i + 1]->dirname);
	if (VerboseOpt) {
	    if (part->expmethod == EXM_DIRSIZE || part->maxsize)
		printf("     Removed %s (used: %s free: %s age: %s)\n",
					ftos(remsize),
					ftos(part->partsize),
					ftos(part->filesys->fsfree),
					dtlenstr(part->age));
	    else if (part->minfree)
		printf("     Removed %s (free: %s age: %s)\n",
					ftos(remsize),
					ftos(part->filesys->fsfree),
					dtlenstr(part->age));
	    else if (part->minfreefiles)
		printf("     Removed %ld (freefiles: %ld age: %s)\n",
					c,
					part->filesys->fsfreefiles,
					dtlenstr(part->age));
	    else 
		printf("     Removed %s (age: %s)\n",
					ftos(remsize),
					dtlenstr(part->age));
	}
    }
    printf("%d files %s (%d directories) removed", count,
						ftos(tremsize), dircount);
    if (ccount)
	printf(", and %d core files removed!", ccount);
    printf("\n");
    fflush(stdout);
    return(allclean);
}

void
printPartition(Partition *p)
{
    printf("partname: %s\n", p->partname);
    printf("size: %s\n", ftos(p->partsize));
    printf("minfree: %s\n", ftos(p->minfree));
    printf("spaceok: %d\n", p->spaceok);
    printf("maxsize: %s\n", ftos(p->maxsize));
    printf("keeptime: %s\n", dtlenstr(p->keeptime));
    printf("age: %s\n", dtlenstr(p->age));
    printf("free: %s\n", ftos(p->filesys->fsfree));
    printf("freefiles: %ld\n", p->filesys->fsfreefiles);
}

/*
 * scanDirectory(): Scan a Diablo spool directory and act according to
 *    the type of partition/directory/file found:
 *
 *	P.01 		= A spool partition (dexpire.ctl Sn option)
 *	N.00 		= A spool partition (DiabloSpoolDirs)
 *	D.00f49e76	= A spool directory (gets renamed to A.*)
 *	A.00f49e76	= A spool directory that can be removed
 *	B.051b		= A spool file
 *
 * All spool directories on partitions that need clearing are stored
 * in the array "SpoolEntries[]"
 */
void
scanDirectory(Partition *scanpart)
{
    DIR *dir;
    den_t *den;
 
    /*
     * Check to see if this directory/partition actually needs
     * to be cleaned up. If we aren't updating history, we don't
     * bother checking the partition.
     *
     */

    if (DebugOpt > 1)
	printf("Scanning directory: %s\n", scanpart->partname);

    if ((dir = opendir(scanpart->partname)) == NULL) {
	fprintf(stderr, "Unable to scan directory: %s (%s)\n",
				scanpart->partname, strerror(errno));
	return;
    }
    {
	while ((den = readdir(dir)) != NULL) {
	    if (den->d_name[1] != '.' || (strcmp(den->d_name, "..") == 0))
		continue;
	    switch (den->d_name[0]) {
	    case 'N':
		{
		Partition *p = malloc(sizeof(Partition));
		
		    p->partname = (char *)malloc(strlen(scanpart->partname) +
						strlen(den->d_name) + 2);
		    sprintf(p->partname, "%s%s/", scanpart->partname,
						den->d_name);
		    p->partsize = 0;
		    p->age = 0;
		    p->spaceok = 0;
		    p->keeptime = scanpart->keeptime;
		    p->maxsize = scanpart->maxsize;
		    p->minfree = scanpart->minfree;
		    p->minfreefiles = scanpart->minfreefiles;
		    p->filesys = findFileSys(p->partname);
		    p->expmethod = scanpart->expmethod;
		    p->next = scanpart->next;
		    scanpart->next = p;
		    scanDirectory(p);
		    scanpart->partsize += p->partsize;
		    if (p->age > scanpart->age)
			scanpart->age = p->age;
		    break;
		}
	    case 'B':
		fprintf(stderr, "Skipping unexpected spool file %s%s\n",
					scanpart->partname, den->d_name);
		break;
	    case 'A':
		removeDirectory(scanpart->partname, den->d_name, NULL, NULL, NULL);
		break;
	    case 'D':
		if (entryIdx == entryMax) {
		    entryMax = entryMax * 2;
		    SpoolEntries = realloc(SpoolEntries, entryMax * sizeof(SpoolEnt *));
		    if (!SpoolEntries) {
			fprintf(stderr, "unable to realloc in scan for queue dirs\n");
			exit(1);
		    }
		}
		SpoolEntries[entryIdx] = malloc(sizeof(SpoolEnt));
		if (!SpoolEntries[entryIdx]) {
		    fprintf(stderr, "unable to malloc in scan for queue dirs\n");
		    exit(1);
		}

		SpoolEntries[entryIdx]->partn = scanpart;
		SpoolEntries[entryIdx]->removed = 0;
		SpoolEntries[entryIdx]->dirsize = 0.0;
		SpoolEntries[entryIdx]->dirname = strdup(den->d_name);

		if (scanpart->expmethod == EXM_DIRSIZE || scanpart->maxsize) {
		    SpoolEntries[entryIdx]->dirsize =
				findSize(scanpart->partname, den->d_name);
		    scanpart->partsize += SpoolEntries[entryIdx]->dirsize;
		}
		{
		    time_t t;
		    t = getAge(den->d_name);
		    if ((TimeNow - t) > scanpart->age)
			scanpart->age = TimeNow - t;
		}
		if (DebugOpt > 2)
		    printf("ADD DIR %s%s dirsize=%s age=%u partsize=%s\n",
				scanpart->partname,
				den->d_name,
				ftos(SpoolEntries[entryIdx]->dirsize),
				(int)scanpart->age,
				ftos(scanpart->partsize));
		++entryIdx;
		break;
	    case 'P':
		/* Skip spool object names */
		break;
	    default:
		printf("Skipping unknown file/dir: %s%s\n",
					scanpart->partname, den->d_name);
	    }
	}

    }
    closedir(dir);
}

/*
 * Find the size of a spool directory (in MB)
 */
double
findSize(char *pathname, char *dirname)
{
    char p[PATH_MAX];
    char f[PATH_MAX];
    DIR *dir;
    den_t *den;
    double size = 0;
    struct stat sb;

    strcpy(p, pathname);
    strcat(p, dirname);
    if ((dir = opendir(p)) == NULL) {
	fprintf(stderr, "Unable to find size of directory: %s (%s)\n",
					p, strerror(errno));
	return(0);
    }
    strcat(p, "/");
    while ((den = readdir(dir)) != NULL) {
	if (strcmp(den->d_name, "..") == 0)
	    continue;
	strcpy(f, p);
	strcat(f, den->d_name);
	if (stat(f, &sb) == 0)
	    size += sb.st_size;
    }
    closedir(dir);
    return(size);
}

int
updateHistory(void)
{
    uint32 countExp = 0;
    uint32 numEnt = 0;
    off_t countEnt = 0;
    off_t countTestSpool = 0;
    off_t countTestHistory = 0;
    off_t countTestExpired = 0;
    off_t countTestValid = 0;
    int lastPerc = 0;
    FILE *DExpOverList = NULL;
    char path[PATH_MAX];
    int i;
    char spoolHome[512];
    int spoolHomeLen;
    off_t npos = -1;
    off_t bpos = -1;
    History *h;

    /*
     * Write expired article msgid hashes to a file if requested.
     */

    if (WriteHashesToFileOpt == 1) {
	const char *filename = PatDbExpand(DExpireOverListPat);

	DExpOverList = fopen(filename, "a");
	if (DExpOverList == NULL)
	    fprintf(stderr, "Error opening %s: %s\n", filename,
						      strerror(errno));
    }

    /*
     * scan all directories in the spool.   Expire history records by
     * directory.  We can't expire history records by file anymore 
     * because 'reader mode' expire may create new files with 'old' gmt
     * times.
     */

    if (VerboseOpt) {
	printf("Scanning history with %d directory entries ....\n", entryIdx);
	fflush(stdout);
    }

    snprintf(spoolHome, sizeof(spoolHome), "%s", PatExpand(SpoolHomePat));
    spoolHomeLen = strlen(spoolHome);

    for (i = 0; i < entryIdx; ++i) {
	int partnameLen;

	if (SpoolEntries[i]->removed)
	    continue;

	partnameLen = strlen(SpoolEntries[i]->partn->partname);

	/* If partname has a "/news/spool/news/" prefix, strip it off */
	if (partnameLen > spoolHomeLen &&
	    strncmp(SpoolEntries[i]->partn->partname, spoolHome,
		    spoolHomeLen) == 0 &&
	    SpoolEntries[i]->partn->partname[spoolHomeLen] == '/'
	    ) {
	    snprintf(path, sizeof(path), "%s%s",
		     SpoolEntries[i]->partn->partname + spoolHomeLen + 1,
		     SpoolEntries[i]->dirname);
	} else {
	    /* No "/news/spool/news/" prefix; go for absolute path */
	    snprintf(path, sizeof(path), "%s%s",
					SpoolEntries[i]->partn->partname,
					SpoolEntries[i]->dirname);
	}

	if (DebugOpt > 1)
	    printf("NODEPATH: %s\n", path);
	(void)findNode(path, 1);
    }

    /*
     * Scan history file and update the expiration
     *
     * The history file was opened before the spool scan to make sure
     * we get the right history file at this point and that we know
     * where the end of the file is before the spool entry hash is built
     */

    {
	int n;
	HistHead hh;
	History hist[65536];

	if ((n = read(HistoryFd, &hh, sizeof(hh))) != sizeof(hh)) {
	    if (n == -1)
		fprintf(stderr, "Unable to read history header from %s (%s)\n",
						HistoryFile, strerror(errno));
	    else 
		fprintf(stderr, "Read %d bytes from history %s, expected %d\n",
					n, HistoryFile, (int)sizeof(hh));

	    exit(1);
	}
	if (hh.hmagic != HMAGIC) {
	    fprintf(stderr, "corrupted history file or old version of history file: %x : %x\n", hh.hmagic, HMAGIC);
	    exit(1);
	}
	if (hh.version > HVERSION) {
	    fprintf(stderr, "wrong dhistory file version (%d), expected %d\n",
		hh.version,
		HVERSION
	    );
	    exit(1);
	}

	lseek(HistoryFd, hh.headSize + sizeof(HistIndex) * hh.hashSize, 0);

	numEnt = (HistoryEnd -
			(hh.headSize + sizeof(HistIndex) * hh.hashSize)) /
					sizeof(History);
	if (VerboseOpt) {
	    printf("History contains %d entries ....\n", numEnt);
	    fflush(stdout);
	}

	npos = lseek(HistoryFd, 0L, 1);
	while ((n = read(HistoryFd, hist, sizeof(hist))) > 0) {
	    int i;
	    int changed = 0;

	    bpos = npos;
	    npos += n;
	    n /= sizeof(History);

	    for (i = 0; i < n; ++i) {
		/*
		 * Don't scan beyond the stored history position
		 */
		if (bpos + i * sizeof(History) >= HistoryEnd)
		    break;

		countEnt++;
		if (VerboseOpt && numEnt > 0 &&
				countEnt * 100 / numEnt >= lastPerc + 10) {
		    lastPerc = countEnt * 100 / numEnt;
		    printf("\t%-10lld of %-10d (%d%%) complete  %lld  %u   \n",
					(long long)countEnt, numEnt, lastPerc,
					(long long)bpos, countExp);
		    fflush(stdout);
		}

		h = &hist[i];
		path[0] = 0;

		if (TestOpt) {
		    int res;

		    ArticleFileName(path, sizeof(path), h, ARTFILE_DIR_REL);
		    res = findNode(path, 0);

		    if (res == 0) {
			if (H_EXPIRED(h->exp)) {
			    countTestSpool++;
			    printf("%08x.%08x expired @%lld (index=%lld) but on spool (%s)\n",
				h->hv.h1, h->hv.h2,
				(long long)(npos + i * sizeof(History)),
				(long long)countEnt, path);
			    findNode(path, 0);
			} else {
			    countTestValid++;
			}
		    } else {
			if (H_EXPIRED(h->exp)) {
			    countTestExpired++;
			} else {
			    countTestHistory++;
			    printf("%08x.%08x not expired @%lld (index=%llu) and not on spool (%s)\n",
				h->hv.h1, h->hv.h2,
				(long long)(npos + i * sizeof(History)),
				(long long)countEnt, path);
			}
		    }
		    continue;
		}

		/*
		 * skip if the article has already expired or if it
		 * is a new article that we may not have scanned, or
		 * if it is an expansion slot.
		 */

		if (SinglePart != NULL && *SinglePart != H_SPOOL(h->exp))
		    continue;
		if (!UnexpireOpt && H_EXPIRED(h->exp))
		    continue;
		if (UnexpireOpt && !H_EXPIRED(h->exp))
		    continue;
		if (h->hv.h1 == 0 && h->hv.h2 == 0)
		    continue;

		if (!UnexpireOpt)
		    ArticleFileName(path, sizeof(path), h, ARTFILE_DIR_REL);

		if (UnexpireOpt || findNode(path, 0) < 0) {
		    if (!UnexpireOpt && VerboseOpt > 1) {
			printf("Unable to find path %s (%08x.%08x), %s history record\n",
			    path,
			    h->hv.h1, h->hv.h2,
			    ((HistoryUpdateOpt != 2) ? "expiring" : "would expire")
			);
		    }
		    if (UnexpireOpt || HistoryUpdateOpt != 2) {
			changed = 1;
			if (UnexpireOpt)
			    h->exp &= ~EXPF_EXPIRED;
			else
			    h->exp |= EXPF_EXPIRED;
			lseek(
			    HistoryFd,
			    bpos + sizeof(History) * i + offsetof(History, exp),
			    0
			);
			if (!NotForReal)
			    write(HistoryFd, &h->exp, sizeof(h->exp));

			if (WriteHashesToFileOpt == 1)
			    fwrite(&h->hv, sizeof(hash_t), 1, DExpOverList);
		    }
		    ++countExp;
		}
	    }
	    if (changed)
		lseek(HistoryFd, npos, 0);
	}
    }

    if (WriteHashesToFileOpt == 1)
	fclose(DExpOverList);

    if (TestOpt) {
	printf("Total entries scanned      : %12lld\n",
				(long long)countEnt);
	printf("Total !expired + on spool  : %12lld\n",
				(long long)countTestValid);
	printf("Total expired + ! on spool : %12lld\n",
				(long long)countTestExpired);
	printf("Total expired + on spool   : %12lld\n",
				(long long)countTestSpool);
	printf("Total !expired + ! on spool: %12lld\n",
				(long long)countTestHistory);
    }
    return(countExp);
}

typedef struct ENode {
    struct ENode *no_Next;
    char	*no_Path;
} ENode;

#define EHSIZE	16384
#define EHMASK	(EHSIZE - 1)

ENode	*NodeAry[EHSIZE];

/*
 * Find a path in the node array
 * returns:	0 for found
 *		1 for created
 *		-1 for not found
 */
int
findNode(const char *path, int createMe)
{
    unsigned int hv = 0xA4FC3244;
    int i;
    ENode **pnode;
    ENode *node;
    unsigned int index;

    for (i = 0; path[i]; ++i)
	hv = (hv << 5) ^ path[i] ^ (hv >> 23);

    index = (hv ^ (hv >> 16)) & EHMASK;
    for (pnode = &NodeAry[index]; 
	(node = *pnode) != NULL; 
	pnode = &node->no_Next
    ) {
	if (strcmp(path, node->no_Path) == 0)
	    return(0);
    }
    if (createMe) {
	node = malloc(sizeof(ENode) + strlen(path) + 1);
	if (!node) {
	  fprintf(stderr, "unable to malloc in findNode\n");
	  exit(1);
	}
	node->no_Next = NULL;
	node->no_Path = (char *)(node + 1);
	*pnode = node;
	strcpy(node->no_Path, path);
	return(1);
    }
    return(-1);
}

void
freeNodes(void)
{
    int i;

    for (i = 0; i < EHSIZE; ++i) {
	ENode *node;

	while ((node = NodeAry[i]) != NULL) {
	    NodeAry[i] = node->no_Next;
	    free(node);
	}
    }
}

void
dumpNodeTable(void)
{
    int i;

    for (i = 0; i < EHSIZE; ++i) {
	ENode *node;

	node = NodeAry[i];
	while (node != NULL) {
	    printf("NODEent: %d : %s\n", i, node->no_Path);
	    node = node->no_Next;
	}
    }
}

FileSystem *
findFileSys(char *path)
{
    FileSystem *f;
    FileSystem *pf;
    struct stat sb;

    if (stat(path, &sb) == -1) {
	fprintf(stderr, "Unable to stat %s\n", path);
	exit(1);
    }

    for (f = FileSystems, pf = NULL; f != NULL; pf = f, f = f->next)
	if (sb.st_dev == f->dev)
	    return(f);

    f = (FileSystem *)malloc(sizeof(FileSystem));
    if (pf == NULL)
	FileSystems = f;
    else
	pf->next = f;

    f->dev = sb.st_dev;
    f->fsfree = freeSpaceOn(path, 0, &f->fsfreefiles);
    f->next = 0;
    if (DebugOpt > 1)
	printf("ADD FS: %s\n", path);
    return(f);
}

/*
 * Return a time value (in secs) of a directory name in hex
 * specified as D.xxxxxxxxx, where 'xxxxxxxxx' is (time / 60) in hex
 */
time_t
getAge(char *dirname)
{
    unsigned long long t = 0;

    sscanf(dirname, "D.%llx", &t);
    if (t)
	t = t * 60;
    return(t);
}

/*
 * freeSpaceOn() - return the space free (MB) in a directory/partition
 *
 */

double
freeSpaceOn(char *path, int syncit, long *freefiles)
{
    struct statfs stmp;
    double avail;
    static int synccount = 1;

    if (DebugOpt)
	printf("Check space on %s\n", path);

    /*
     * This code does not significantly slow dexpire down, but it does give
     * the system sync a chance to update the bitmaps so statfs returns a
     * more accurate value.  Certain filesystems such as FreeBSD and BSDI
     * w/ softupdates are so decoupled and so fast that dexpire might remove 
     * 80% of the spool before statfs() realizes that sufficient free space 
     * remains.
     */
    if (syncit && SoftUpdates > 0 && synccount++ >= SyncInterval) {
	sync();
	sync();
	sleep(SoftUpdates);
	sync();
	synccount = 0;
    }

#if USE_SYSV_STATFS
    if (statfs(path, &stmp, sizeof(stmp), 0) != 0) {
#else
    if (statfs(path, &stmp) != 0) {
#endif
	fprintf(stderr, "dexpire: unable to statfs %s (%s)\n", path,
						strerror(errno));
	return(1);
    }
    avail = stmp.f_bavail * 1.0 * stmp.f_bsize;
    if (freefiles != NULL)
	*freefiles = stmp.f_ffree;
    if (DebugOpt)
	printf("Tested fs avail: %s (%ld inodes)\n", ftos(avail), (long)stmp.f_ffree);
    return(avail);
}

void
DumpSpoolEntries(void)
{
    char path[PATH_MAX];
    int i;
    char spoolHome[PATH_MAX];
    int spoolHomeLen;

    snprintf(spoolHome, sizeof(spoolHome), "%s", PatExpand(SpoolHomePat));
    spoolHomeLen = strlen(spoolHome);
    printf("List of directories on spool:\n");
    for (i = 0; i < entryIdx; ++i) {
	int partnameLen;

	if (SpoolEntries[i]->removed)
	    continue;

	partnameLen = strlen(SpoolEntries[i]->partn->partname);

	/* If partname has a "/news/spool/news/" prefix, strip it off */
	if (partnameLen > spoolHomeLen &&
	    strncmp(SpoolEntries[i]->partn->partname, spoolHome,
		    spoolHomeLen) == 0 &&
	    SpoolEntries[i]->partn->partname[spoolHomeLen] == '/'
	    ) {
	    snprintf(path, sizeof(path), "%s%s",
		     SpoolEntries[i]->partn->partname + spoolHomeLen + 1,
		     SpoolEntries[i]->dirname);
	} else {
	    /* No "/news/spool/news/" prefix; go for absolute path */
	    snprintf(path, sizeof(path), "%s%s",
					SpoolEntries[i]->partn->partname,
					SpoolEntries[i]->dirname);
	}

	printf("path= %s  dirname=%s  partition=%s  removed=%d  found=%d\n",
		path,
                SpoolEntries[i]->dirname,
                SpoolEntries[i]->partn->partname,
                SpoolEntries[i]->removed,
		findNode(path, 0));
    }
}

