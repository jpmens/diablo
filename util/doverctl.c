
/*
 * UTIL/CONVOVER.C
 *
 * (c)Copyright 2001, Russell Vincent , All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 *	This program performs maintenance tasks on the dreaderd
 *	overview/header database.
 *
 *	WARNING: The server must be down when this is run
 */

#include <dreaderd/defs.h>

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

char *allocTmpCopy(const char *buf, int bufLen);
void ConvertHash(const char *group, GroupHashType *srcHash, GroupHashType *dstHash, int iter, artno_t begNo, artno_t endNo);
int SetField(char **pptr, const char *str);
Group *EnterGroup(const char *groupName, artno_t begNo, artno_t endNo, int lmts, int cts, int iter, const char *flags);
Group *FindGroupByHash(char *Hash, int iter);
void CleanSpool(char *cleanDir);
void scanDirectory(const char *dirpath, char *dirname, int *level);
void DeleteFile(const char *dirPath, const char *name);
void ProcessFile(const char *dirPath, const char *name);
int getDataEntries(const char *path);

#define	ACT_CONVERT	0x01
#define	ACT_CLEAN	0x02

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

int Action = 0;
GroupHashType SrcHash;
GroupHashType DstHash;
int VerboseOpt = 0;
int ForReal = 1;
int MustExit = 0;
char *GroupMatch = NULL;
char *CleanDir = NULL;
KPDB  *KDBActive;
Group *GHash[GHSIZE];
int TotalFiles = 0;
int BadGroups = 0;
int RemovedFiles = 0;
int FileCopy = 0;

void
sigInt(int sigNo)
{
    ++MustExit;
    if (MustExit > 3)
	exit(1);
}

void
Usage(char *progname)
{
    printf("Usage: %s [-f activefile][-m][-n][-w wild][-v] action params ...\n", progname);
    printf("    where:\n");
    printf("            -f file    set the active file to use\n");
    printf("            -w wild    limit the set of groups to work on\n");
    printf("            -n         don't actually make any changes (dummy run)\n");
    printf("            -m         move files with a file copy, instead of renaming them\n");
    printf("                          (needed when converting across filesystems)\n");
    printf("\n");
    printf("           action is one of\n");
    printf("\tclean				delete unused files\n");
    printf("\tconvert srchash dsthash		convert hash method\n");
    printf("\t    srchash and dsthash are one of\n");
    printf("\t\tcrc\n");
    printf("\t\tmd5-32/N[/N]\n");
    printf("\t\tmd5-64/N[/N]\n");
    printf("\t\thierarchy\n");
    printf("\t    WARNING: No other diablo processes must be running during\n");
    printf("\t\tthe conversion\n");
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
	    if (strcmp(ptr, "convert") == 0) {
		if (++i >= ac || SetGroupHashMethod(av[i], &SrcHash) != 0) {
		    printf("Missing or bad src hash method\n");
		    Usage(av[0]);
		}
		if (++i >= ac || SetGroupHashMethod(av[i], &DstHash) != 0) {
		    printf("Missing or bad dst hash method\n");
		    Usage(av[0]);
		}
		if (bcmp(&SrcHash, &DstHash, sizeof(SrcHash)) == 0) {
		    printf("Cannot convert to same method\n");
		    Usage(av[0]);
		}
		Action |= ACT_CONVERT;
	    } else if (strcmp(ptr, "clean") == 0) {
		i++;
		if (i < ac)
		    CleanDir = av[i];
		Action |= ACT_CLEAN;
	    }
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'C':           /* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'm':
	    FileCopy = 1;
	    break;
	case 'n':
	    ForReal = 0;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    VerboseOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'w':
	    GroupMatch = (*ptr) ? ptr : av[++i];
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (Action == 0)
	Usage(av[0]);

    /*
     * If we are converting, make sure that we set the default hash method
     * to the new one, so that we can do the correct type of clean otherwise
     * we are in serious trouble
     */
    if (Action & ACT_CONVERT) {
	char srcHashStr[64];
	char dstHashStr[64];
	bcopy(&DstHash, &DOpts.ReaderGroupHashMethod, sizeof(DstHash));
	printf("Converting from %s to %s\n", GetHash(&SrcHash, srcHashStr),
					GetHash(&DstHash, dstHashStr));
    }

    /*
     * Open active file database
     */

    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);
    }
    if (KDBActive == NULL) {
	fprintf(stderr, "Unable to open dactive.kp\n");
	exit(1);
    }

    rsignal(SIGINT, sigInt);
    rsignal(SIGHUP, sigInt);
    rsignal(SIGTERM, sigInt);
    rsignal(SIGALRM, SIG_IGN);

    /*
     * scan dactive.kp
     */

    {
	int recLen;
	int recOff;
	int count = 0;

	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	     recOff;
	     recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    int groupLen;
	    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	    artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    int iter = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 16);

	    if (group)
		group = allocTmpCopy(group, groupLen);

	    if (group == NULL)
		continue;

	    EnterGroup(group, begNo, endNo, 0, 0, iter, NULL);

	    if (GroupMatch && WildCmp(GroupMatch, group) != 0)
		continue;

	    if (Action & ACT_CONVERT) {
		ConvertHash(group, &SrcHash, &DstHash, iter, begNo, endNo);
		if (++count % 1000 == 0)
		    printf("Scanned %d groups\n", count);
	    }

	    if (MustExit)
		break;
	}
    }

    if (Action & ACT_CLEAN)
	CleanSpool(CleanDir);

    if (KDBActive)
	KPDBClose(KDBActive);

    if (Action & ACT_CONVERT)
	printf("Don't forget to set the new hash method in diablo.config\n");

    return(0);
}

void
ConvertHash(const char *group, GroupHashType *srcHash, GroupHashType *dstHash, int iter, artno_t begNo, artno_t endNo)
{
    char path1[PATH_MAX];
    char path2[PATH_MAX];
    char dir[PATH_MAX];
    struct stat st;
    artno_t artNo;
    artno_t artBase;
    artno_t prevArtBase;
    int dataEntriesMask;

    if (chdir(PatExpand(GroupHomePat)) != 0) {
	printf("Unable to chdir %s (%s)\n", PatExpand(GroupHomePat), strerror(errno));
	return;
    }

    strcpy(path1, GFName(group, GRPFTYPE_OVER, 0, 1, iter, srcHash));
    strcpy(path2, GFName(group, GRPFTYPE_OVER, 0, 1, iter, dstHash));
    strcpy(dir, path2);

    if (DebugOpt > 1)
	printf("Converting %s -> %s\n", path1, path2);
    if (stat(path1, &st) != 0) {
	if (VerboseOpt > 2)
	    printf("over file for %s not found - skipping conversion\n", group);
	return;
    }
    MakeGroupDirectory(dir);
    if (ForReal) {
	if (FileCopy) {
	    MoveFile(path1, path2);
	} else {
	    if (rename(path1, path2) != 0) {
		printf("Cannot rename %s -> %s (%s)\n", path1, path2,
							strerror(errno));
		return;
	    }
	}
    }
    dataEntriesMask = getDataEntries(path1) - 1;
    prevArtBase = -1;
    for (artNo = begNo; artNo <= endNo; artNo++) {
	artBase = artNo & ~dataEntriesMask;
	if (artBase == prevArtBase)
	    continue;
	prevArtBase = artBase;
	strcpy(path1, GFName(group, GRPFTYPE_DATA, artBase, 1, iter, srcHash));
	strcpy(path2, GFName(group, GRPFTYPE_DATA, artBase, 1, iter, dstHash));
	if (stat(path1, &st) != 0) {
	    if (VerboseOpt > 1)
		printf("data file for %s not found - ignoring\n", group);
	    continue;
	}
	if (DebugOpt > 1)
	    printf("Converting %s -> %s\n", path1, path2);
	if (ForReal) {
	    if (FileCopy) {
		MoveFile(path1, path2);
	    } else {
		if (rename(path1, path2) != 0) {
		    printf("Cannot rename %s -> %s (%s)\n", path1, path2,
							strerror(errno));
		    return;
		}
	    }
	}
    }
    if (VerboseOpt)
	printf("Converted %s\n", group);
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
	if (group->gr_EndNo != endNo) {
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

void
CleanSpool(char *cleanDir)
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
		isalnum((int)den->d_name[1]) &&
	    	stat(den->d_name, &st) == 0 && S_ISDIR(st.st_mode)
		/*
		 * We explicitly use the first char, because overview
		 * sizes appear to be not evenly distributed wrt second
		 * char.
		 */
	    )
		scanDirectory(PatExpand(GroupHomePat), den->d_name, &level);
		/*
		 * This is a bit of a hack, but we try to remove the
		 * directory in case it is empty. If it is not empty, the
		 * remove should fail silently - at leat we hope so!
		 */
		if (ForReal && remove(den->d_name) == 0) {
		    printf("Removed empty directory: %s/%s\n", PatExpand(GroupHomePat), den->d_name);
		}
	}
	closedir(dir);
    }
}

void
scanDirectory(const char *dirpath, char *dirname, int *level)
{
    DIR *dir2;
    char path[PATH_MAX];

    sprintf(path, "%s/%s", dirpath, dirname);

    if (chdir(dirname) != 0) {
	printf("Unable to chdir(%s)\n", dirname);
	return;
    }

    if ((dir2 = opendir(".")) != NULL) {
	den_t *den2;
	struct stat st;

	while ((den2 = readdir(dir2)) != NULL) {
	    if (strcmp(den2->d_name, ".") == 0 || strcmp(den2->d_name, "..") == 0)
		continue;
	    if (stat(den2->d_name, &st) != 0) {
		printf("Unable to stat(%s/%s)\n", dirname, den2->d_name);
		continue;
	    }
	    if (S_ISDIR(st.st_mode)) {
		if (*level <= 2)
		    scanDirectory(path, den2->d_name, level);
		/*
		 * This is a bit of a hack, but we try to remove the
		 * directory in case it is empty. If it is not empty, the
		 * remove should fail silently - at leat we hope so!
		 */
		if (ForReal && remove(den2->d_name) == 0) {
		    printf("Removed empty directory: %s/%s\n", dirpath, den2->d_name);
		}
	    } else if (S_ISREG(st.st_mode)) {
		ProcessFile(path, den2->d_name);
	    } else {
		if (VerboseOpt)
		    printf("Removing unknown file: %s/%s\n", dirname, den2->d_name);
		DeleteFile(dirname, den2->d_name);
	    }
	    if (MustExit)
		exit(1);
	}
	closedir(dir2);
    }
    chdir("..");
}

void
DeleteFile(const char *dirPath, const char *name)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);
    if (ForReal)
	remove(path);
    ++RemovedFiles;
}


/*
 * ProcessFile() - process over. and data. files.  All over. files
 *			   are processed first. 
 *
 */

void
ProcessFile(const char *dirPath, const char *name)
{
    artno_t artBase = -1;
    Group *group;
    char path[PATH_MAX];
    char Hash[PATH_MAX];
    int iter = 0;
    int type = 0;
    int dataEntries;
    const char *ovpath;

    snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (DebugOpt > 0)
	printf("ProcessOverviewFile(%s)\n", path);

    ++TotalFiles;

    bzero(Hash, sizeof(Hash));

    if ((type = ExtractGroupHashInfo(name, Hash, &artBase, &iter)) == HASHGRP_NONE) {
	printf("Deleting unknown/stale file: %s/%s\n", dirPath, name);
	DeleteFile(dirPath, name);
	return;
    }

    if (DebugOpt > 2)
	printf("File: %s  type=%d  artBase=%lld  iter=%d  Hash=%s\n",
					name, type, artBase, iter, Hash);

    if ((group = FindGroupByHash(Hash, iter)) == NULL) {
	/*
	 * If we gave a wildcard, we can't remove stale groups because
	 * we do not have a full group list.
	 */

	++BadGroups;
	printf("Group for over file not found, removing %s\n", path);
	DeleteFile(dirPath, name);
	return;
    }

    if (DebugOpt > 2)
	printf("Maps to group: %s\n", group->gr_GroupName);

    if (type & HASHGRPTYPE_OVER) {
	/*
	 * over. file	(fixed length file)
	 */
	int fd;
	OverHead oh;

	if ((fd = open(path, O_RDWR)) >= 0) {
	    int r;
	    r = (read(fd, &oh, sizeof(oh)) == sizeof(oh));
	    if (oh.oh_Version > OH_VERSION ||
		oh.oh_ByteOrder != OH_BYTEORDER)
		r = 0;
	    if (r && oh.oh_Version > 1 &&
				strcmp(oh.oh_Gname, group->gr_GroupName) != 0)
		r = 0;
	    if (r == 0) {
		printf("group %s, file \"%s\" bad file header - removing\n",
		    group->gr_GroupName,
		    path
		);
		if (oh.oh_Version > OH_VERSION)
		    printf("   expected version %d, got version %d\n",
						OH_VERSION, oh.oh_Version);
		DeleteFile(dirPath, name);
	    }
	    close(fd);
	}
    } else if (type & HASHGRPTYPE_DATA) {
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

    	ovpath = GFName(group->gr_GroupName, GRPFTYPE_OVER, 0, 0, iter,
						&DOpts.ReaderGroupHashMethod);
	dataEntries = getDataEntries(ovpath);
	if (artBase + dataEntries <= group->gr_StartNo ||
	    artBase >= group->gr_EndNo + dataEntries
	) {
	    printf("Deleting stale overview data %s: %s\n",
		group->gr_GroupName,
		path
	    );
	    if (DebugOpt)
		printf("artBase=%lld, dataEntries=%d group->gr_StartNo=%lld group->gr_EndNo=%lld\n",
				artBase, dataEntries, group->gr_StartNo, group->gr_EndNo);
	    DeleteFile(dirPath, name);
	} 
    } else {
	printf("Bad Error: Unknown HASHGRPTYPE\n");
    }
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

    if ((fd = open(path, O_RDWR)) >= 0) {
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
