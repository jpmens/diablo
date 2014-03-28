
/*
 * DHISEXPIRE.C	- Expire the history file with minimal downtime
 *
 * (c)Copyright 2002, Russell Vincent, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

#define	USE_DEADMAGIC

int VerboseOpt = 0;
int QuietOpt = 0;
int ShowProgress = 0;
int DonePause = 0;
int UseNewHistory = 0;
int DoRemember = 1;
int UnDead = 0;
int FileCopy = 0;
time_t MaxAge = -1;
int MustExit = 0;
char *FileName = NULL;
char NewFileName[PATH_MAX];
char OldFileName[PATH_MAX];
int HistoryVersion = 0;

void DoUnDead(int fd);
void DoExpire(int fd, int hsize, int rsize);
int ServerCmd(char *cmd);
void Fail(char *fname, char *errmsg);

void
Usage(void)
{
    printf("Expire old entries in the history file.\n\n");
    printf("dhisexpire [-a] [-p] [-r remember] [-T seconds] [-v] [-x]\n");
    printf("           [-C diablo.config] [-d[n]] [-V] dhistory-file [new-history]\n");
    printf("  where:\n");
    printf("\t-a\t- rename the new history to old history when finished\n");
    printf("\t-m\t- rename history files across filesystems (file copy)\n");
#ifndef	USE_DEADMAGIC
    printf("\t-P\t- don't pause diablo server\n");
#endif
    printf("\t-ofile\t- set path/name for backup of old history file\n");
    printf("\t-p\t- show progress on stdout\n");
    printf("\t-rN\t- set rememberdays\n");
    printf("\t-TN\t- don't dump articles older than N seconds\n");
    printf("\t-u\t- remove dead flag for a history file\n");
    printf("\t-v\t- be a little more verbose\n");
    printf("\t-x\t- keep records older than rememberdays old\n");
    printf("\t-Cfile\t- specify diablo.config to use\n");
    printf("\t-d[n]\t- set debug [with optional level]\n");
    printf("\t-V\t- print version and exit\n");
    exit(1);
}

void
sigInt(int sigNo)
{
    printf("Exit signal caught - exiting\n");
    ++MustExit;
    if (MustExit > 3)
	exit(1);
}

int
main(int ac, char **av)
{
    int fd;
    int i;
    int hsize = 1024 * 1024;
    int rsize = sizeof(History);
    struct stat st;

    NewFileName[0] = 0;
    OldFileName[0] = 0;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    if (FileName) {
		if (NewFileName[0] != 0) {
		    fprintf(stderr, "unexpected argument\n");
		    exit(1);
		}
		strncpy(NewFileName, ptr, sizeof(NewFileName) - 1);
		NewFileName[sizeof(NewFileName) - 1] = '\0';
		continue;
	    }
	    FileName = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'a':
	    UseNewHistory = 1; 
	    break;
	case 'm':
	    FileCopy = 1; 
	    break;
#ifndef	USE_DEADMAGIC
	case 'P':
	    DonePause = 1;
	    break;
#endif
	case 'o':
	    strncpy(OldFileName, ptr, sizeof(OldFileName) - 1);
	    OldFileName[sizeof(OldFileName) - 1] = '\0';
	    break;
	case 'p':
	    ShowProgress = 1;
	    break;
	case 'q':
	    QuietOpt = 1;
	    break;
	case 'r':
	    if (!*ptr)
		ptr = av[++i];
	    DOpts.RememberSecs = TimeSpec(ptr, "d");
	    if (DOpts.RememberSecs == -1)
		Usage();
	    break;
	case 'T':
	    MaxAge = btimetol(*ptr ? ptr : av[++i]);
	    break;
	case 'u':
	    UnDead = 1;
	    break;
	case 'v':
	    if (*ptr)
		VerboseOpt = strtol(ptr, NULL, 0);
	    else
		++VerboseOpt;
	    break;
	case 'x':
	    DoRemember = 0;
	    break;
	/* Common options */
	case 'C':		/* parsed by LoadDiabloConfig */
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = 1;
	    if (*ptr)
		DebugOpt = strtol(ptr, NULL, 0);
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    fprintf(stderr, "illegal option: %s\n", ptr - 2);
	    Usage();
	}
    }

    if (FileName == NULL) {
	Usage();
    }
    if (NewFileName[0] == 0)
	sprintf(NewFileName, "%s.new", FileName);
    if (OldFileName[0] == 0)
	sprintf(OldFileName, "%s.bak", FileName);
    if (strcmp(OldFileName, "0") == 0)
	OldFileName[0] = 0;

    if ((fd = open(FileName, O_RDWR)) >= 0 && fstat(fd, &st) == 0) {
	/*
	 * new style history file has a header
	 */

	HistHead hh;

	if (UnDead) {
	    DoUnDead(fd);
	    close(fd);
	    return(0);
	}
	if (read(fd, &hh, sizeof(hh)) != sizeof(hh)) {
	    perror("Corrupted history file");
	    exit(1);
	}
	if (hh.hmagic != HMAGIC) {
	    fprintf(stderr, "Corrupted history file - bad magic\n");
	    exit(1);
	}
	if (hh.version != HVERSION) {
	    fprintf(stderr, "WARNING! Version mismatch file V%d, expecting V%d\n", hh.version, HVERSION);
	    fprintf(stderr, "dump may be invalid\n");
	}
	rsize = hh.henSize;
	hsize = hh.hashSize;
	HistoryVersion = hh.version;

	lseek(fd, hh.headSize, 0);

	if (!QuietOpt)
	    printf("Hash table has %d entries, record size %d\n",
	    hsize,
	    rsize
	);

	DoExpire(fd, hsize, rsize);

	close(fd);
    } else {
	perror("History open failed");
	exit(1);
    }
    return(0);
}

void
DoUnDead(int fd)
{
    HistHead hh = { 0 };

    if (lseek(fd, 0, SEEK_SET) == -1) {
	perror("Unable to seek to pos 0 in history");
	return;
    }
    if (read(fd, &hh, sizeof(hh)) != sizeof(hh)) {
	perror("Unable to read history header");
	return;
    }
    if (hh.hmagic != HDEADMAGIC) {
	fprintf(stderr, "History file not marked as dead\n");
	return;
    }
    if (hh.version != HVERSION) {
	 fprintf(stderr, "ERROR! Version mismatch file V%d, expecting V%d\n", hh.version, HVERSION);
	return;
    }
    hh.hmagic = HMAGIC;
    if (lseek(fd, 0, SEEK_SET) == -1) {
	perror("Unable to seek to pos 0 in history");
	return;
    }
    if (write(fd, &hh, sizeof(hh)) != sizeof(hh)) {
	perror("Unable to write history header");
	return;
    }
    printf("History file %s no longer marked as dead\n", FileName);
}

void
DoExpire(int fd, int hsize, int rsize)
{
    char *hbuf;
    int hlen = rsize * 4096;
    int n;
    int rememberMins = DOpts.RememberSecs / 60;
    uint32 gmt = time(NULL) / 60;
    off_t seekpos = 0;
    uint32 totalentries = 0;
    uint32 okcount = 0;
    uint32 count = 0;
    uint32 failed = 0;
    uint32 ExpireDropCount = 0;
    uint32 ExpireKeepCount = 0;
    uint32 MaxAgeCount = 0;
    int r;
    History *h;
    int finished = 0;
    struct timeval tstart;
    struct timeval tend;
    double elapsed;

    hbuf = (char *)malloc(hlen);
    if (hbuf == NULL) {
	fprintf(stderr, "Unable to malloc %d bytes (%s)\n", hlen,
							strerror(errno));
	exit(1);
    }

    if (HistoryVersion > 1)
	seekpos = lseek(fd, hsize * sizeof(HistIndex) + rsize, 1);
    else
	seekpos = lseek(fd, hsize * sizeof(HistIndex), 1);

    {
	struct stat st;

	if (fstat(fd, &st) == -1) {
	    fprintf(stderr, "Unable to fstat history: %s\n", strerror(errno));
	    exit(1);
	}
	totalentries = (uint32)(((off_t)st.st_size - seekpos) / rsize);
	if (!QuietOpt)
	    printf("History entries start at offset %lld, %d records\n",
						seekpos, totalentries);
    }

    HistoryOpen(NewFileName, HGF_FAST|HGF_NOSEARCH|HGF_EXCHECK);

    gettimeofday(&tstart, NULL);

    while (!finished || MustExit) {
	int i;

	n = read(fd, hbuf, hlen) / rsize;

	if (n == 0) {
#ifdef USE_DEADMAGIC
	    if (DonePause > 0) {
		if (DonePause == 2) {
		    DonePause = 5;
		    HistoryClose();
		    if (UseNewHistory) {
			if (OldFileName[0])
			    remove(OldFileName);
			if (FileCopy) {
			    if (OldFileName[0] && MoveFile(FileName, OldFileName) == -1)
				Fail(NewFileName, "Unable to move history backup");
			    if (MoveFile(NewFileName, FileName) == -1)
				Fail(NewFileName, "Unable to move new history");
			} else {
			    if (OldFileName[0] && link(FileName, OldFileName) == -1)
				Fail(NewFileName, "Unable to link history backup");
			    if (rename(NewFileName, FileName) == -1)
				Fail(NewFileName, "Unable to rename new history");
			}
		    }
		}
		finished = 1;
	    } else {
		off_t seekpos = lseek(fd, 0, SEEK_CUR);
		HistHead hh = { 0 };
		if (lseek(fd, 0, SEEK_SET) == -1)
		    Fail(FileName, "Unable to seek to pos 0 in history");
		if (read(fd, &hh, sizeof(hh)) == -1)
		    Fail(FileName, "Unable to read history header");
		hh.hmagic = HDEADMAGIC;
		if (lseek(fd, 0, SEEK_SET) == -1)
		    Fail(FileName, "Unable to seek to pos 0 in history");
		if (write(fd, &hh, sizeof(hh)) == -1) {
		    perror("historyheadwrite");
		    Fail(FileName, "Unable to write history header");
		}
		if (lseek(fd, seekpos, SEEK_SET) == -1)
		    Fail(FileName, "Unable to seek in history");
		DonePause = 2;
	    }
#else
	    if (DonePause > 0) {
		if (DonePause == 2) {
		    rsignal(SIGINT, sigInt);
		    rsignal(SIGHUP, sigInt);
		    rsignal(SIGTERM, sigInt);
		    rsignal(SIGALRM, SIG_IGN);
		    DonePause = 3;
		    if (ServerCmd("pause") == 0)
			Fail(NewFileName, "Unable to pause diablo server");
		    DonePause = 4;
		    HistoryClose();
		    if (UseNewHistory) {
			if (OldFileName[0])
			    remove(OldFileName);
			if (FileCopy) {
			    if (OldFileName[0] && MoveFile(FileName, OldFileName) == -1)
				Fail(NewFileName, "Unable to move history backup");
			    if (MoveFile(NewFileName, FileName) == -1)
				Fail(NewFileName, "Unable to move new history");
			} else {
			    if (OldFileName[0] && link(FileName, OldFileName) == -1)
				Fail(NewFileName, "Unable to link history backup");
			    if (rename(NewFileName, FileName) == -1)
				Fail(NewFileName, "Unable to rename new history");
			}
		    }
		    if (ServerCmd("go") == 0)
			Fail(NewFileName, "Unable to resume diablo server");
		    DonePause = 5;
		}
		finished = 1;
	    } else {
		if (ServerCmd("flush") == 0)
		    Fail(NewFileName, "Unable to flush diablo server");
		if (ServerCmd("readonly") == 0)
		    Fail(NewFileName, "Unable to set readonly mode");
		if (ServerCmd("flush") == 0)
		    Fail(NewFileName, "Unable to flush diablo server");
		DonePause = 2;
		sleep(2);
	    }
#endif
	    continue;
	}

	gmt = time(NULL) / 60;

	for (i = 0; i < n; ++i) {
	    if (count++ > totalentries)
		totalentries = count;
	    if (ShowProgress && ((count % 32768) == 0)) {
		printf("%u/%u  (%d%%)   \r", count, totalentries,
				(int)((double)count * 100 / totalentries));
	    }

	    h = (History *)(hbuf + i * rsize);

	    /*
	     * If we specified a maximum age, then deal with it
	     */
	    if (MaxAge != -1) {
		int32 dgmt = (gmt - h->gmt) * 60;	/* Delta seconds */
		if (dgmt > MaxAge) {
		    MaxAgeCount++;
		    continue;
		}
	    }

	    if (DoRemember && H_EXPIRED(h->exp)) {
		int32 dgmt = gmt - h->gmt;	/* DELTA MINUTES */
		if (dgmt < -rememberMins || dgmt > rememberMins) {
		    ExpireDropCount++;
		    continue;
		} else {
		    ExpireKeepCount++;
		}
	    }

	    if ((r = HistoryAdd(NULL, h)) == 0)
		++okcount;
	    else if (r != RCTRYAGAIN)
		++failed;
	    else
		Fail(NewFileName, "HistoryAdd: write failed!");
	}
    }

    gettimeofday(&tend, NULL);

    if (DonePause == 3 && ServerCmd("go") == 0)		/* Got signal */
	Fail(NewFileName, "Unable to resume diablo server");
    if (ShowProgress && totalentries > 0)
	printf("%u/%u  (%d%%)   \n", count, totalentries,
				(int)((double)count * 100 / totalentries));
    if (!QuietOpt) {
	elapsed = (tend.tv_sec + tend.tv_usec / 1000000.0) -
				(tstart.tv_sec + tstart.tv_usec / 1000000.0);
	if (elapsed == 0)
	    elapsed = 1;
        printf("%.3f seconds\n", elapsed);
        printf("%u entries processed (%.0f per second)\n",
					count, (double)count / elapsed);
        printf("%u entries kept\n", okcount);
        printf("%u entries failed\n", failed);
        printf("%u expired entries kept\n", ExpireKeepCount);
        printf("%u expired entries dropped\n", ExpireDropCount);
        printf("%u dropped as beyond max age\n", MaxAgeCount);
    }
}

int
ServerCmd(char *cmd)
{
    FILE *fi;
    FILE *fo;
    char buf[256];
    int r = 0;

    /*
     * UNIX domain socket
     */

    {
	struct sockaddr_un soun;
	int ufd;

	memset(&soun, 0, sizeof(soun));

	if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	    perror("udom-socket");
	    return(r);
	}
	soun.sun_family = AF_UNIX;
	sprintf(soun.sun_path, "%s", PatRunExpand(DiabloSocketPat));
	if (connect(ufd, (struct sockaddr *)&soun, offsetof(struct sockaddr_un, sun_path[strlen(soun.sun_path)+1])) < 0) {
	    perror("udom-connect");
	    return(r);
	}
	fo = fdopen(dup(ufd), "w");
	fi = fdopen(ufd, "r");
    }

    fprintf(fo, "%s\n", cmd);
    fprintf(fo, "quit\n");
    fflush(fo);
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	if (VerboseOpt)
	    printf("%s", buf);
	if (strncmp(buf, "200", 3) == 0)
	    r = 1;
	if (strncmp(buf, "211 Flushing feeds", 18) == 0)
	    r = 1;
	if (strcmp(buf, ".\n") == 0)
	    break;
    }
    fclose(fo);
    fclose(fi);
    return(r);
}

void
Fail(char *fname, char *errmsg)
{
    printf("%s\n", errmsg);
    printf("History rebuild is not complete - keeping old history\n");
    HistoryClose();
    if (fname != NULL)
	remove(fname);
    exit(1);
}

