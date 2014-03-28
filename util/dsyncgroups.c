
/*
 * UTIL/DSYNCGROUPS.C
 *
 * dsyncgroups -h remotehost [-a] [-o] [-D] [-N[B,E,R]] [-g] [-i] [-m] [-w wildcard] [-p port] [-f/F dactive.kp]
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

typedef struct Group {
    struct Group *gr_Next;
    int		gr_State;
    artno_t	gr_StartNo;
    int		gr_StartDiff;
    artno_t	gr_EndNo;
    int		gr_EndDiff;
    artno_t	gr_SynNo;
    int		gr_SynDiff;
    int		gr_NoCTS;
    int		gr_NoLMTS;
    char	*gr_GroupName;
    char	*gr_Flags;
    char	*gr_Description;
} Group;

#define GRF_DESCRIPTION	0x0001
#define GRF_STARTNO	0x0002
#define GRF_ENDNO	0x0004
#define GRF_FLAGS	0x0008
#define GRF_SYNNO	0x0010
#define GRF_LMTS	0x0020	/* last modified time stamp */
#define GRF_CTS		0x0040	/* creation time stamp */
#define GRF_FROMLOCAL	0x0800
#define GRF_NEW		0x1000
#define GRF_FROMREMOTE	0x2000
#define GRF_MODIFIED	0x8000

#define GHSIZE		1024
#define GHMASK		(GHSIZE-1)

FILE *Fi;
FILE *Fo;
Group *GHash[GHSIZE];
KPDB  *KDBActive;

int SyncOverwriteOpt = 0; /* allow remote info to replace existing info	*/
int SyncDeleteOpt = 0;	/* delete local info not present on remote	*/
int SyncGroupsOpt = 0;	/* synchronize newsgroup names 			*/
int SyncDescrOpt = 0;	/* synchronize newsgroup descriptions		*/
int SyncModStatusOpt = 0; /* synchronize newsgroup moderation status	*/
int SyncArtNumbersBegOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersEndOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersSynOpt = 0; /* synchronize article numbers	*/
int SyncArtNumbersRange = 0;  /* relative sync beginning based on range	*/
int SyncArtNumbersAddition = 0;  /* add this when syncing ending article no. */
int SyncArtNumbersMaxPerc = 0;	/* max % value to increase upwards	*/
int VerboseOpt = 0;
int ForReal = 1;
char *SyncModStatusFlag = NULL;
char *BindHost = NULL;
char *BindPort = NULL;

void Usage(void);
void processLine(char *cmd, char *wild, int *count, int *ok);
char *allocTmpCopy(const char *buf, int bufLen);
Group *EnterGroup(int pass, const char *, artno_t, artno_t, artno_t, int, int, const char *, const char *);
int ConnectToHost(const char *host, int port);
int CommandResponse(const char *cmd, char **pres);
int hash(const char *str);
int SetField(char **pptr, const char *str);

void
Usage(void)
{
    fprintf(stderr, "dsyncgroups -h remotehost | -G filename [-p port] [-B hostname] [-w wild] [-f/F file.kp] [-a] [-o] [-g] [-i] [-m] [-M c] [-D] [-N[B,E,X,R]] [-X[EX]] [-n#] [-v] [-z]\n");
    fprintf(stderr, "  -h remotehost = the host that we connect to\n");
    fprintf(stderr, "  -G filename   = specify an active file to use as input\n");
    fprintf(stderr, "  -p port       = port on remote host (default: 119)\n");
    fprintf(stderr, "  -B bindhost   = interface to bind to\n");
    fprintf(stderr, "  -w wild       = only update groups matching wildmat (default: *)\n");
    fprintf(stderr, "  -f file.kp    = specify the active to update (Default: dactive.kp)\n");
    fprintf(stderr, "  -F file.kp    = create active file if it doesn't exist\n");
    fprintf(stderr, "  -a            = shortcut for -o, -g, -i and -m\n");
    fprintf(stderr, "  -o            = overwrite description\n");
    fprintf(stderr, "  -g            = add missing groups\n");
    fprintf(stderr, "  -i            = sync newsgroup description\n");
    fprintf(stderr, "  -m            = update moderation status\n");
    fprintf(stderr, "  -M c          = update moderation status to the specified flag\n");
    fprintf(stderr, "  -D            = delete local groups in not on remote\n");
    fprintf(stderr, "  -N            = sync begin and end numbers\n");
    fprintf(stderr, "  -NB           = sync begin numbers\n");
    fprintf(stderr, "  -NE           = sync end numbers\n");
    fprintf(stderr, "  -NX           = sync end numbers but only upwards\n");
    fprintf(stderr, "  -u            = Log into remotehost using this password\n");
    fprintf(stderr, "  -U            = Log into remotehost using this username\n");
    fprintf(stderr, "  -XE           = sync NX field - feeder sync\n");
    fprintf(stderr, "  -XX           = sync NX but only upwards\n");
    fprintf(stderr, "  -n#           = if we sync end number then increase local value by remote+#\n");
    fprintf(stderr, "  -N#           = don't sync end number  more than #\n");
    fprintf(stderr, "  -v            = be more verbose about what is being done\n");
    fprintf(stderr, "  -z            = don't actually make changes\n");
    exit(1);
}
int
main(int ac, char **av)
{
    int i;
    int ocreate = 0;
    char *host = NULL;
    char *wild = NULL;
    char *dbfile = NULL;
    char *grpfile = NULL;
    char *username = NULL;
    char *password = NULL;
    int port = 119;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *p;
	char *ptr = av[i];

	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected argument: %s\n", ptr);
	    exit(1);
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    SyncOverwriteOpt = 1;
	    SyncGroupsOpt = 1;
	    SyncDescrOpt = 1;
	    SyncModStatusOpt = 1;
	    break;
	case 'B':
	    if (*ptr == 0)
		ptr = av[++i];
	    if ((p = strrchr(ptr, ':')) != NULL) {
		*p++ = 0;
		BindPort = p;
		BindHost = strdup(SanitiseAddr(ptr));
	    } else {  
		BindHost = strdup(SanitiseAddr(ptr));
	    }
	    break;
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'D':
	    SyncDeleteOpt = 1;
	    break;
	case 'd':
	    DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	case 'F':
	    ocreate = O_CREAT;
	    /* fall through */
	case 'f':
	    dbfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'G':
	    grpfile = (*ptr) ? ptr : av[++i];
	    break;
	case 'g':
	    SyncGroupsOpt = 1;
	    break;
	case 'i':
	    SyncDescrOpt = 1;
	    break;
	case 'h':
	    host = (*ptr) ? ptr : av[++i];
	    break;
	case 'm':
	    SyncModStatusOpt = 1;
	    break;
	case 'M':
	    SyncModStatusOpt = 1;
	    SyncModStatusFlag = (*ptr) ? ptr : av[++i];
	    break;
	case 'N':
	    if (*ptr == 0) {
		SyncArtNumbersBegOpt = 1;
		SyncArtNumbersEndOpt = 1;
	    }
	    while (*ptr) {
		if (*ptr == 'B')
		    SyncArtNumbersBegOpt = 1;
		if (*ptr == 'E')
		    SyncArtNumbersEndOpt = 1;
		if (*ptr == 'X')
		    SyncArtNumbersEndOpt = 2;
		if (*ptr == 'R')
		    SyncArtNumbersRange = 1;
		++ptr;
	    }
	    break;
	case 'n':
	    SyncArtNumbersAddition = strtol((*ptr) ? ptr : av[++i], NULL, 0);
	    break;
	case 'o':
	    SyncOverwriteOpt = 1;
	    break;
	case 'P':
	    SyncArtNumbersMaxPerc = strtol((*ptr) ? ptr : av[++i], NULL, 0);
	    break;
	case 'p':
	    ptr = (*ptr) ? ptr : av[++i];
	    port = strtol(ptr, NULL, 0);
	    break;
	case 'U':
	    username = (*ptr) ? ptr : av[++i];
	    break;
	case 'u':
	    password = (*ptr) ? ptr : av[++i];
	    break;
	case 'w':
	    wild = (*ptr) ? ptr : av[++i];
	    break;
	case 'X':
	    if (*ptr == 0) {
		SyncArtNumbersSynOpt = 1;
	    }
	    while (*ptr) {
		if (*ptr == 'E')
		    SyncArtNumbersSynOpt = 1;
		if (*ptr == 'X')
		    SyncArtNumbersSynOpt = 2;
		++ptr;
	    }
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    VerboseOpt = 1;
	    break;
	case 'z':
	    ForReal = 0;
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    break;
	}
    }
    if (host == NULL && grpfile == NULL) {
	if (ac == 1)
	    Usage();
	fprintf(stderr, "Missing host/filename option\n");
	exit(1);
    }
    if (port <= 0) {
	fprintf(stderr, "Illegal port option\n");
	exit(1);
    }
    if (wild && strlen(wild) > 128) {
	fprintf(stderr, "Wildcard too large\n");
	exit(1);
    }

    /*
     * open dactive.kp file
     */

    if (VerboseOpt)
	printf("%s local active file\n",
				ocreate ? "Loading/creating" : "Loading");

    if (dbfile) {
	KDBActive = KPDBOpen(dbfile, O_RDWR|ocreate);
    } else {
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR|ocreate);
    }
    if (KDBActive == NULL) {
	printf("Unable to open/create dactive.kp\n");
	exit(1);
    }

    /*
     * scan dactive.kp
     */

    {
	int recLen;
	int recOff;
	int saveSyncGroupsOpt = SyncGroupsOpt;

	SyncGroupsOpt = 1;	/* force new Group structures to be created */

	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
	     recOff;
	     recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    int groupLen;
	    int flagsLen;
	    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	    const char *flags = KPDBGetField(rec, recLen, "S", &flagsLen, "y");
	    const char *desc  = KPDBGetFieldDecode(rec, recLen, "GD", NULL, NULL);
	    artno_t begNo = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL, 10);
	    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    artno_t synNo = strtoll(KPDBGetField(rec, recLen, "NX", NULL, "-1"), NULL, 10);
	    int noCTS = !(int)strtoul(KPDBGetField(rec, recLen, "CTS", NULL, "0"), NULL, 16);
	    int noLMTS = !(int)strtoul(KPDBGetField(rec, recLen, "LMTS", NULL, "0"), NULL, 16);
	    Group *grp;

	    if (group)
		group = allocTmpCopy(group, groupLen);
	    if (flags)
		flags = allocTmpCopy(flags, flagsLen);
	    if (desc)
		desc = allocTmpCopy(desc, strlen(desc));

	    /*
	     * ignore bad group or group that does not match the wildcard
	     */

	    if (group == NULL)
		continue;
	    if (wild && WildCmp(wild, group) != 0)
		continue;

	    grp = EnterGroup(
		1,
		group,
		begNo,
		endNo,
		synNo,
		noCTS,
		noLMTS,
		flags,
		desc
	    );
	    grp->gr_State &= ~(GRF_NEW|GRF_MODIFIED);
	    grp->gr_State &= ~(GRF_CTS|GRF_LMTS);
	    grp->gr_State |= GRF_FROMLOCAL;
	}

	SyncGroupsOpt = saveSyncGroupsOpt;
    }

    /*
     * Connect to remote host
     */

    if (host != NULL)
    {
	int fd;

	if (VerboseOpt)
	    printf("Connecting to %s:%d\n", host, port);

	fd = ConnectToHost(host, port);

	if (fd < 0) {
	    fprintf(
		stderr, 
		"Unable to connect to %s:%d (%s)\n", 
		host, 
		port, 
		strerror(errno)
	    );
	    exit(1);
	}
	Fi = fdopen(fd, "r");
	Fo = fdopen(dup(fd), "w");
    } else {
	if (VerboseOpt)
	    printf("Syncing with %s\n", grpfile);
	Fi = fopen(grpfile, "r");
	if (Fi == NULL) {
	    printf("Unable to open %s\n", grpfile);
	    exit(1);
	}
	Fo = NULL;
    }

    /*
     * startup
     */

    if (host != NULL)
    {
	char *line;

	switch(CommandResponse(NULL, &line)) {
	case 200: case 201:
	    printf("Successful connection: %s\n", line);
	    break;
	default:
	    printf("Initial Response: %s\n", line);
	    exit(1);
	}
    }

    /*
     * login
     */

    if (host != NULL && username != NULL) {
	char *line;
	char cmd[4096];

	sprintf(cmd, "authinfo user %s",username);
	switch(CommandResponse(cmd, &line)) {
	    case 381:
		printf("authinfo accepted: %s\n", line);
		break;
	    default:
		printf("authinfo denied: %s\n", line);
		exit(1);
	}
    }

    if (host != NULL && password != NULL) {
	char *line;
	char cmd[4096];

	sprintf(cmd, "authinfo pass %s",password);
	switch(CommandResponse(cmd, &line)) {
	    case 281:
		printf("authinfo accepted: %s\n", line);
		break;
	    default:
		printf("authinfo denied: %s\n", line);
		exit(1);
	}
    }

    /*
     * mode reader
     */

    if (host)
    {
	char *line;

	switch(CommandResponse("mode reader", &line)) {
	case 200: case 201:
	    printf("Entering reader mode: %s\n", line);
	    break;
	default:
	    printf("Unable to switch to reader mode: %s\n", line);
	    printf("Going on anyway\n");
	    break;
	}
    }

    /*
     * list active [wild]
     */

    if (host)
    {
	char *line;
	char cmd[4096];
	int count = 0;
	int ok = 0;

	sprintf(cmd, "list active%s%s", ((wild) ? " " : ""), ((wild) ? wild : ""));
	switch(CommandResponse(cmd, &line)) {
	case 215:
	    while (fgets(cmd, sizeof(cmd), Fi) != NULL) {
		processLine(cmd, wild, &count, &ok);
		if (ok)
		    break;
	    }
	    break;
	default:
	    printf("list active command failed: %s\n", line);
	    break;
	}
	printf("%d groups returned by list active\n", count);
	if (ok == 0) {
	    printf("Did not receive . terminator to list active command\n");
	    exit(1);
	}
    } else {
	char cmd[4096];
	int count = 0;
	while (fgets(cmd, sizeof(cmd), Fi) != NULL)
	    processLine(cmd, wild, &count, NULL);
	printf("%d groups found\n", count);
    }

    /*
     * list newsgroups [wild]
     */

   if (host && SyncDescrOpt)
    {
	char *line;
	char cmd[4096];
	int count = 0;
	int ok = 0;

	sprintf(cmd, "list newsgroups%s%s", ((wild) ? " " : ""), ((wild) ? wild : ""));
	switch(CommandResponse(cmd, &line)) {
	case 215:
	    while (fgets(cmd, sizeof(cmd), Fi) != NULL) {
		char *group;
		char *desc;
		Group *grp;

		if (strcmp(cmd, ".\r\n") == 0) {
		    ok = 1;
		    break;
		}
		group = strtok(cmd, " \t\r\n");
		desc  = strtok(NULL, "\r\n");

		/*
		 * ignore group that does not match the wildcard, even though
		 * the remote end is supposed to do this for us.
		 */

		if (wild && WildCmp(wild, group) != 0)
		    continue;

		if (group)
		grp = EnterGroup(
		    2,
		    group, 
		    -1,
		    -1,
		    -1,
		    -1,
		    -1,
		    NULL,
		    desc
		); else grp = NULL;
		if (grp)
		    grp->gr_State |= GRF_FROMREMOTE;
		++count;
	    }
	    break;
	default:
	    printf("list newsgroups command failed: %s\n", line);
	    break;
	}
	printf("%d groups returned by list newsgroups\n", count);
	if (ok == 0) {
	    printf("Did not receive . terminator to list newsgroups command\n");
	    exit(1);
	}
    }

    /*
     * close remote connections
     */

    if (host)
    {
	char *line;

	printf("All remote commands completed, sending quit\n");
	switch(CommandResponse("quit", &line)) {
	case 205:
	    printf("quit response ok\n");
	    break;
	default:
	    printf("error sending quit command: %s\n", line);
	    exit(1);
	}
    }

    fclose(Fi);
    if (Fo)
	fclose(Fo);

    printf("Writing to .kp file\n");
    fflush(stdout);

    /*
     * Writeback
     */

    {
	int i;
	int nowt = (int)time(NULL);

	for (i = 0; i < GHSIZE; ++i) {
	    Group *group;

	    for (group = GHash[i]; group; group = group->gr_Next) {
		/*
		 * If we have a new group not previously in the database,
		 * we only add it if SyncGroupsOpt is set.
		 */
		if (group->gr_State & GRF_NEW) {
		    if (SyncGroupsOpt == 0)
			continue;
		}

		/*
		 * If group did not come from remote and SyncDeleteOpt is set,
		 * delete the group.
		 */
		if (!(group->gr_State & GRF_FROMREMOTE) &&
		    (group->gr_State & GRF_FROMLOCAL) &&
		    SyncDeleteOpt
		) {
		    printf("Deleting %s\n", group->gr_GroupName);
		    if (ForReal)
			KPDBDelete(KDBActive, group->gr_GroupName);
		} else if (group->gr_State & (GRF_MODIFIED | GRF_NEW)) {
		    if (group->gr_State & GRF_NEW) {
			printf("Creating %s", group->gr_GroupName);
		    } else {
			printf("Updating %s", group->gr_GroupName);
		    }
		    if (group->gr_State & GRF_DESCRIPTION) {
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "GD", group->gr_Description, 0);
			if (VerboseOpt)
			    printf(" desc='%s'", group->gr_Description);
		    }
		    if (group->gr_State & (GRF_STARTNO | GRF_NEW)) {
			char startBuf[20];
			sprintf(startBuf, "%010lld", group->gr_StartNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NB", startBuf, 0);
			if (VerboseOpt)
			    printf(" NB=%s(%+d)", startBuf, group->gr_StartDiff);
		    }
		    if (group->gr_State & (GRF_ENDNO | GRF_NEW)) {
			char endBuf[16];
			sprintf(endBuf, "%010lld", group->gr_EndNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NE", endBuf, 0);
			if (VerboseOpt)
			    printf(" NE=%s(%+d)", endBuf, group->gr_EndDiff);
		    }
		    if (group->gr_State & (GRF_SYNNO | GRF_NEW)) {
			char synBuf[16];
			sprintf(synBuf, "%010lld", group->gr_SynNo);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "NX", synBuf, 0);
			if (VerboseOpt)
			    printf(" NX=%s(%+d)", synBuf, group->gr_SynDiff);
		    }
		    if (group->gr_State & GRF_CTS) {
			char ctsBuf[16];
			sprintf(ctsBuf, "%08x", nowt);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "CTS", ctsBuf, 0);
			if (VerboseOpt)
			    printf(" CTS=%s", ctsBuf);
		    }
		    if (group->gr_State & GRF_LMTS) {
			char lmtsBuf[16];
			sprintf(lmtsBuf, "%08x", nowt);
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "LMTS", lmtsBuf, 0);
			if (VerboseOpt)
			    printf(" LMTS=%s", lmtsBuf);
		    }
		    if (group->gr_Flags && group->gr_State & GRF_FLAGS) {
			if (ForReal)
			    KPDBWriteEncode(KDBActive, group->gr_GroupName, "S", group->gr_Flags, 0);
			if (VerboseOpt)
			    printf(" S=%s",
				(group->gr_Flags ? group->gr_Flags : "y"));
		    }
		    printf("\n");
		}
	    }
	}
    }

    return(0);
}

void
processLine(char *cmd, char *wild, int *count, int *ok)
{
    char *group;
	char *gbeg;
	char *gend;
	char *flags;
	Group *grp;

	if (strcmp(cmd, ".\r\n") == 0) {
	    if (ok)
		*ok = 1;
	    return;
	}
	group = strtok(cmd, " \r\n");
	gend  = strtok(NULL, " \r\n");
	gbeg  = strtok(NULL, " \r\n");
	flags = strtok(NULL, " \r\n");

	/*
	 * ignore group that does not match the wildcard, even though
	 * the remote end is supposed to do this for us.
	 */

	if (wild && WildCmp(wild, group) != 0)
	    return;

	if (*gend == '-' || *gbeg == '-') {
	    printf("Invalid group begin or end number - skipping : %s\n", group);
	    return;
	}

	grp = EnterGroup(
	    2,
	    group, 
	    strtol(gbeg, NULL, 10), 
	    strtol(gend, NULL, 10), 
	    strtol(gend, NULL, 10), /* end repeated */
	    -1,
	    -1,
	    flags, 
	    NULL
	);

	/*
	 * mark group as coming from remote
	 */
	if (grp)
	    grp->gr_State |= GRF_FROMREMOTE;
	++*count;
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
EnterGroup(int pass, const char *groupName, artno_t begNo, artno_t endNo, artno_t synNo, int noCTS, int noLMTS, const char *flags, const char *desc)
{
    Group **pgroup = &GHash[hash(groupName)];
    Group *group;

    while ((group = *pgroup) != NULL) {
	if (strcmp(groupName, group->gr_GroupName) == 0)
	    break;
	pgroup = &group->gr_Next;
    }
    if (group == NULL) {
	/*
	 * ignore groups not already in the dactive.kp file if SyncGroupsOpt is not
	 * set.
	 */
	if (SyncGroupsOpt == 0)
	    return(NULL);
	*pgroup = group = calloc(sizeof(Group) + strlen(groupName) + 1, 1);
	group->gr_State = GRF_NEW | GRF_LMTS | GRF_CTS;
	group->gr_GroupName = (char *)(group + 1);
	group->gr_NoCTS = 1;
	group->gr_NoLMTS = 1;
	group->gr_StartNo = 1;
	strcpy(group->gr_GroupName, groupName);
    }

    /*
     * update fields
     */

    if (synNo >= 0) {
	if (pass == 1) {
	    group->gr_SynNo = synNo;
	} else if (SyncArtNumbersSynOpt) {
	    group->gr_State |= GRF_SYNNO;
	    if (group->gr_SynNo != synNo) {
		if (SyncArtNumbersSynOpt != 2 || synNo > group->gr_SynNo) {
		    int perc;
		    if (group->gr_SynNo == 0)
			group->gr_SynNo = 1;
		    group->gr_SynDiff = (synNo + SyncArtNumbersAddition) -
							group->gr_SynNo;
		    perc = group->gr_SynDiff * 100 / group->gr_SynNo;
		    if (SyncArtNumbersMaxPerc && group->gr_SynNo &&
				(group->gr_SynDiff > 0) &&
				(perc > SyncArtNumbersMaxPerc))
			group->gr_SynDiff = SyncArtNumbersMaxPerc *
							group->gr_SynNo / 100;
		    if (group->gr_SynDiff == 0)
			group->gr_SynDiff = 1;
		    group->gr_SynNo += group->gr_SynDiff;
		    group->gr_State |= GRF_MODIFIED;
		}
	    }
	}
    }

    if (endNo >= 0) {
	if (pass == 1) {
	    group->gr_EndNo = endNo;
	} else if (SyncArtNumbersEndOpt) {
	    group->gr_State |= GRF_ENDNO;
	    if (group->gr_EndNo != endNo) {
		if (SyncArtNumbersEndOpt != 2 || endNo > group->gr_EndNo) {
		    int perc;
		    if (group->gr_EndNo == 0)
			group->gr_EndNo = 1;
		    group->gr_EndDiff = (synNo + SyncArtNumbersAddition) -
							group->gr_EndNo;
		    perc = group->gr_EndDiff * 100 / group->gr_EndNo;
		    if (SyncArtNumbersMaxPerc && group->gr_EndNo &&
				(group->gr_EndDiff > 0) &&
				(perc > SyncArtNumbersMaxPerc))
			group->gr_EndDiff = SyncArtNumbersMaxPerc *
							group->gr_EndNo / 100;
		    if (group->gr_EndDiff == 0)
			group->gr_EndDiff = 1;
		    group->gr_EndNo += group->gr_EndDiff;
		    group->gr_NoLMTS = 1;
		    group->gr_State |= GRF_MODIFIED;
		}
	    }
	}
    }

    if (begNo >= 0) {
	if (pass == 1) {
	    group->gr_StartNo = begNo;
	} else if (SyncArtNumbersBegOpt ||
	    (SyncArtNumbersRange && (group->gr_State & GRF_ENDNO) && endNo > 0)
	) {
	    if (SyncArtNumbersRange) {
		/*
		 * Set beginning based on ending minus the difference
		 * between the remote ending and beginning.
		 */
		begNo = group->gr_EndNo - (endNo - begNo);
		if (begNo < group->gr_StartNo)
		    begNo = group->gr_StartNo;
		if (begNo > group->gr_EndNo)
		    begNo = group->gr_EndNo;
	    }
	    if (group->gr_StartNo != begNo) {
		group->gr_State |= GRF_STARTNO;
		group->gr_State |= GRF_MODIFIED;
	    }
	    group->gr_StartDiff = begNo - group->gr_StartNo;
	    group->gr_StartNo = begNo;
	}
    }

    if (flags) {
	if (pass == 1) {
	    SetField(&group->gr_Flags, flags);
	} else if ((SyncOverwriteOpt && SyncModStatusOpt) ||
					(group->gr_State & GRF_NEW)) {
	    group->gr_State |= GRF_FLAGS;
	    if (SetField(&group->gr_Flags,
				SyncModStatusFlag ? SyncModStatusFlag : flags))
		group->gr_State |= GRF_MODIFIED;
	}
    }
    if (desc) {
	if (pass == 1) {
	    SetField(&group->gr_Description, desc);
	} else if ((SyncOverwriteOpt && SyncDescrOpt) ||
			(SyncDescrOpt && (group->gr_State & GRF_NEW))) {
	    group->gr_State |= GRF_DESCRIPTION;
	    if (SetField(&group->gr_Description, desc))
		group->gr_State |= GRF_MODIFIED;
	}
    }

    if (noCTS >= 0 && group->gr_NoCTS != noCTS) {
	group->gr_NoCTS = noCTS;
	if (pass > 1)
	    group->gr_State |= GRF_MODIFIED | GRF_CTS;
    }

    if (noLMTS >= 0 && group->gr_NoLMTS != noLMTS) {
	group->gr_NoLMTS = noLMTS;
	if (pass > 1)
	    group->gr_State |= GRF_MODIFIED | GRF_LMTS;
    }

    return(group);
}

int
ConnectToHost(const char *host, int port)
{
    int fd;
    struct sockaddr_in lsin;
    struct sockaddr_in rsin;

    bzero(&lsin, sizeof(lsin));
    bzero(&rsin, sizeof(rsin));
    lsin.sin_port = 0;
    if (BindPort != NULL) {
	struct servent *sen;
	int port;
	if ((port = strtol(BindPort, NULL, 0)) != 0) {
	    lsin.sin_port = htons(port);
	} else { 
	    if ((sen = getservbyname(BindPort, "tcp")) != NULL) {
		lsin.sin_port = sen->s_port;
	    } else {
		fprintf(stderr, "Unknown service: %s\n", BindPort);
		exit(1);
	    }
	}
    }
    lsin.sin_family = AF_INET;
    if (BindHost == NULL) {
	lsin.sin_addr.s_addr = INADDR_ANY;
    } else {
	if (strtol(BindHost, NULL, 0) > 0) {
	    lsin.sin_addr.s_addr = inet_addr(BindHost);
	} else {
	    struct hostent *he;
                
	    if ((he = gethostbyname(BindHost)) != NULL) {
		lsin.sin_addr = *(struct in_addr *)he->h_addr;
	    } else { 
		fprintf(stderr, "Unknown bind host: %s\n", BindHost);
		exit(1);
	    }
	}
    }

    errno = 0;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return(-1);

    if (bind(fd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
	close(fd);
	return(-1);
    }

    rsin.sin_port = htons(port);
    rsin.sin_family = AF_INET;

    {
	struct hostent *he;
	if ((he = gethostbyname(host)) != NULL) {
	    memcpy(&rsin.sin_addr, he->h_addr, he->h_length);
	} else {
	    rsin.sin_addr.s_addr = inet_addr(host);
	    if (rsin.sin_addr.s_addr == INADDR_NONE) {
		printf("host %s lookup failure\n", host);
		close(fd);
		return(-1);
	    }
        }
    }

    if (DebugOpt) {
	fprintf(stderr, "Connecting to %s (%s)\n", host, inet_ntoa(rsin.sin_addr));
	fflush(stderr);
    }

    if (connect(fd, (struct sockaddr *)&rsin, sizeof(rsin)) < 0) {
	if (DebugOpt) {
	    fprintf(stderr, "connection failed: %s\n", strerror(errno));
	    fflush(stderr);
	}
	close(fd);
	return(-1);
    }
    if (DebugOpt) {
	fprintf(stderr, "connection success\n");
	fflush(stderr);
    }
    return(fd);
}

int
CommandResponse(const char *cmd, char **pres)
{
    static char buf[4096];

    if (DebugOpt && cmd) {
	fprintf(stderr, ">> %s\n", cmd);
	fflush(stderr);
    }

    if (cmd)
	fprintf(Fo, "%s\r\n", cmd);
    fflush(Fo);
    if (fgets(buf, sizeof(buf), Fi) != NULL) {
	if (DebugOpt) {
	    fprintf(stderr, "<< %s", buf);
	    fflush(stderr);
	}
	if (pres)
	    *pres = buf;
	return(strtol(buf, NULL, 10));
    } else if (DebugOpt) {
	fprintf(stderr, "<< (EOF)\n");
	fflush(stderr);
    }
    return(0);
}

int
hash(const char *str)
{
    unsigned int hv = 0xA432BCDD;

    while (*str) {
	hv = (hv << 5) ^ *str ^ (hv >> 23);
	++str;
    }
    hv ^= (hv >> 16);
    return(hv & GHMASK);
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
