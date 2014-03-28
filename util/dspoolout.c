/*
 * DSPOOLOUT.C
 *
 * Flush the outbound queue files and start up dnewslinks as appropriate
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

#define TYPE_NORMAL     1
#define TYPE_XREPLIC    2

#define SF_NOSTREAM	0x0001
#define SF_REALTIME	0x0002
#define SF_NOBATCH	0x0004
#define SF_HEADFEED	0x0008
#define SF_NOCHECK	0x0010
#define SF_GENLINES	0x0020
#define SF_PRESBYTES	0x0040
#define SF_ARTSTAT	0x0080
#define SF_NOTIFY	0x0100

#define MAXLOGARTS      80   /* maximal length of the logarts string */

int MaxRun = 2;
int MinFlushSecs = 0;	/* minimum time between flshs if queue not caught up */

typedef struct DNode {
    struct DNode *no_Next;
    char	*no_SpoolFile;
} DNode;

DNode *Base;

void processDnewsfeeds(const char *qFile);
void processDspoolout(const char *fname, char *qFile);
int spool_file(char *spoolFile, int pass, NewslinkInfo *li, int flags);
DNode *FindNode(char *name);
void AddNode(char *name);

int VerboseOpt;
int ForReal = 1;
int Quiet;
int ForceRealTime = 0;
int  TxBufSize = 0;
int  RxBufSize = 0;
int  TOS = 0;
char *OutboundIpStr = "-nop";

int
main(int ac, char **av)
{
    char *qFile = NULL;
    const char *cFile = NULL;
    int oldFormat = 0;

    OpenLog("dspoolout", (DebugOpt > 0 ? LOG_PERROR: 0) | LOG_NDELAY | LOG_PID);
    LoadDiabloConfig(ac, av);

    cFile = DNewsfeedsPat;

    /*
     * Maintain compatability with old format
     */
    {
	struct stat sb;
	if (stat(PatLibExpand(DNNTPSpoolCtlPat), &sb) == 0) {
	    cFile = DNNTPSpoolCtlPat;
	    oldFormat = 1;
	}
    }
    
    /*
     * Shift into the out.going directory
     */

    if (chdir(PatExpand(DQueueHomePat)) != 0) {
	fprintf(stderr, "unable to chdir to %s\n", PatExpand(DQueueHomePat));
	exit(1);
    }

    {
	int i;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];
	    if (*ptr == '-') {
		ptr += 2;
		switch(ptr[-1]) {
		case 'B':
		    if (*ptr == 0)
			ptr = av[++i];
		    OutboundIpStr = malloc(strlen(ptr) + 8);
		    sprintf(OutboundIpStr, "-B%s", ptr);
		    break;
		case 'C':
		    if (*ptr == 0)
			++i;
		    break;
		case 'd':
		    DebugOpt = 1;
		    if (*ptr)
			DebugOpt = strtol(ptr, NULL, 0);
		    break;
		case 'f':
		    cFile = (*ptr) ? ptr : av[++i];
		    break;
		case 'm':
		    MaxRun = strtol((*ptr ? ptr : av[++i]), NULL, 0);
		    break;
		case 'n':
		    ForReal = 0;
		    break;
		case 'p':
		    oldFormat = 1;
		    break;
		case 'q':
		    Quiet = 1;
		    break;
		case 'R':
		    RxBufSize = strtol((*ptr ? ptr : av[++i]), NULL, 0);
		    break;
		case 'Q':
		    TOS = strtol((*ptr ? ptr : av[++i]), NULL, 0);
		    break;
		case 'r':
		    ForceRealTime = 1;
		    break;
		case 's':
		    MinFlushSecs = strtol((*ptr ? ptr : av[++i]), NULL, 0) * 60;
		    break;
		case 'T':
		    TxBufSize = strtol((*ptr ? ptr : av[++i]), NULL, 0);
		    break;
		case 'V':
		    PrintVersion();
		    break;
		case 'v':
		    VerboseOpt = 1;
		    break;
		default:
		    break;
		}
	    } else {
		qFile = ptr;
	    }
	}
    }
    if (DebugOpt && qFile != NULL)
	printf("Processing label: %s\n", qFile);

    if (oldFormat || strstr(PatLibExpand(cFile), "dspoolout") != NULL) {
	    processDspoolout(PatLibExpand(cFile), qFile);
    } else {
	    DNewsfeedsPat = cFile;
	    processDnewsfeeds(qFile);
    }
    return(0);
}

void
processDnewsfeeds(const char *qFile)
{
    int sfCount = 200;
    int *sfAry = malloc(sfCount * sizeof(int));
    int pass;
    int hour;

    {
	struct tm *tp;
	time_t t;

	t=time(0);
	tp=localtime(&t);
	hour=tp->tm_hour;
    }

    if (DebugOpt > 1)
	printf("Using dnewsfeeds for feed config\n");

    LoadNewsFeed(0, 1, NULL);

    /*
     * Get list of spoolfile names and NNTP hosts, one pair per line,
     * blank lines and lines beginning with '#' excepted.
     */

    for (pass = 1; pass <= 2; ++pass) {
	LabelList *ll;
        NewslinkInfo *li;
	int count = 0;

	for (ll = FeedLinkLabelList(); ll != NULL && ll->label != NULL;
							ll = ll->next) {
	    int flags = 0;

	    li = FeedLinkInfo(ll->label);
	    if (li == NULL)
		continue;
	    if (qFile != NULL && strcmp(ll->label, qFile) != 0)
		continue;

	    if (li->li_Hours != NULL) {
		int found = 0;
		int hourbegin;
		int hourend;
		char *hourinterval;
		char *minus;

		if ((hourinterval=strtok(li->li_Hours,",")) != NULL) {
		    while (hourinterval != NULL) {
			hourbegin=strtol(hourinterval,NULL,0);
			if ((minus=strstr(hourinterval,"-")) != NULL)
			    hourend=strtol(minus+1,NULL,0);
			else
			    hourend=hourbegin;
			if (hourbegin <= hourend) {
			    if (hour >= hourbegin && hour <= hourend)
				found=1;
			} else {
			    if (hour >= hourbegin || hour <= hourend)
				found=1;
			}
			hourinterval=strtok(NULL,",");
		    }
		}
		if (found == 0)
		    continue;
	    }
	    flags = flags | (li->li_NoStream ? SF_NOSTREAM : 0);
	    flags = flags | (li->li_RealTime ? SF_REALTIME : 0);
	    flags = flags | (li->li_Notify ? SF_NOTIFY : 0);
	    flags = flags | (li->li_HeadFeed ? SF_HEADFEED : 0);
	    flags = flags | (li->li_Check ? 0 : SF_NOCHECK);
	    flags = flags | (li->li_GenLines ? SF_GENLINES : 0);
	    flags = flags | (li->li_PreserveBytes ? SF_PRESBYTES : 0);
	    flags = flags | (li->li_ArticleStat ? SF_ARTSTAT : 0);
	    flags = flags | (li->li_NoBatch ? SF_NOBATCH : 0);

	    if (flags & SF_NOTIFY && !(flags & SF_REALTIME))
		flags &= ~SF_NOTIFY;

	    /*
	     * run spool file, with flags modifier from pass 1.  Pass1
	     * may clear the SF_REALTIME flag.
	     */
	    if (count >= sfCount) {
		sfCount += 200;
		sfAry = realloc(sfAry, sfCount * sizeof(int));
		memset(sfAry+count, 0, (sfCount - count) * sizeof(int));
	    }
	    if (pass == 2) {
		flags = (flags & ~SF_REALTIME) | 
			(sfAry[count] & SF_REALTIME);
	    }

	    flags = spool_file(ll->label, pass, li, flags);

	    if (pass == 1)
		sfAry[count] = flags;
	    ++count;
	}
	if (pass == 1) {
	    char sysline[256];

	    snprintf(sysline, sizeof(sysline), "%s/dbin/dicmd flush", NewsHome);
	    if (ForReal)
		system(sysline);
	}
    }
}

void
processDspoolout(const char *fname, char *qFile)
{
    int sfCount = 200;
    int *sfAry = malloc(sfCount * sizeof(int));
    int pass;
    int hour;
    FILE *fi;
    char buf[256];

    {
	struct tm *tp;
	time_t t;

	t=time(0);
	tp=localtime(&t);
	hour=tp->tm_hour;
    }

    if (*fname != '/') {
	char *p = malloc(strlen(fname) + strlen(NewsHome) + 3);
	sprintf(p, "%s/%s", NewsHome, fname);
	fname = p;
    }

    printf("Using legacy dnntpspool.ctl format from %s\n", fname);

    /*
     * Get list of spoolfile names and NNTP hosts, one pair per line,
     * blank lines and lines beginning with '#' excepted.
     */

    for (pass = 1; pass <= 2; ++pass) {
	if ((fi = fopen(fname, "r")) != NULL) {
	    int count = 0;

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		char *spoolFile;
		char *hostName;

		if (buf[0] == '\n' || buf[0] == '#')
		    continue;
		if ((spoolFile = strtok(buf, " \t\n")) != NULL &&
		    (hostName = strtok(NULL, " \t\n")) != NULL &&
		    (qFile == NULL || strcmp(spoolFile, qFile) == 0)
		) {
		    int maxq = -1;
		    int port = -1;
		    int maxr = MaxRun;
		    int type = TYPE_NORMAL;
		    int delay = 0;
		    int flags = 0;
		    int qskip = 0;
		    int compress = 0;
		    int txbufsiz = TxBufSize;
		    int rxbufsiz = RxBufSize;
		    int tos = TOS;
		    int maxstream = 0;
		    char *obip = NULL;
		    char *hours = NULL;
		    char *logarts = NULL;

		    {
			char *maxqStr;

			while ((maxqStr = strtok(NULL, " \t\n")) != NULL) {
			    if (strtol(maxqStr, NULL, 0) > 0) {
				maxq = strtol(maxqStr, NULL, 0);
			    } else if (strcmp(maxqStr, "xreplic") == 0) {
				type = TYPE_XREPLIC;
			    } else if (strcmp(maxqStr, "nostream") == 0) {
				flags |= SF_NOSTREAM;
			    } else if (strcmp(maxqStr, "headfeed") == 0) {
				flags |= SF_HEADFEED;
			    } else if (strcmp(maxqStr, "preservebytes") == 0) {
				flags |= SF_PRESBYTES;
			    } else if (strcmp(maxqStr, "ignorebytes") == 0) {
				flags |= SF_PRESBYTES;
			    } else if (strcmp(maxqStr, "nocheck") == 0) {
				flags |= SF_NOCHECK;
			    } else if (strcmp(maxqStr, "realtime") == 0) {
				flags |= SF_REALTIME;
			    } else if (strcmp(maxqStr, "nobatch") == 0) {
				flags |= SF_NOBATCH;
			    } else if (strcmp(maxqStr, "genlines") == 0) {
				flags |= SF_GENLINES;
			    } else if (strcmp(maxqStr, "notify") == 0) {
				flags |= SF_NOTIFY;
			    } else if (strcmp(maxqStr, "compress") == 0) {
				compress = 1;
			    } else if (strcmp(maxqStr, "logarts") == 0) {
				logarts = "all";
			    } else if (strncmp(maxqStr, "logarts=", 8) == 0) {
			        /* we should check for validity of the parameter here */
				logarts = maxqStr + 8;
			    } else if (strncmp(maxqStr, "bind=", 5) == 0) {
				obip = maxqStr + 5;
			    } else if (strncmp(maxqStr, "hours=", 6) == 0) {
				hours = maxqStr + 6;
			    } else if (maxqStr[0] == 'q') {
				qskip = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'n') {
				maxr = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'p') {
				port = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'd') {
				delay = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'T') {
				txbufsiz = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'R') {
				rxbufsiz = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'Q') {
				tos = strtol(maxqStr + 1, NULL, 0);
			    } else if (maxqStr[0] == 'M') {
				maxstream = strtol(maxqStr + 1, NULL, 0);
			    } else {
				if (pass == 1)
				    fprintf(stderr, "bad keyword in control file: %s\n", maxqStr);
			    }
			}
		    }

		    if (hours != NULL) {
			int found = 0;
			int hourbegin;
			int hourend;
			char *hourinterval;
			char *minus;

			if ((hourinterval=strtok(hours,",")) != NULL) {
			    while (hourinterval != NULL) {
				hourbegin=strtol(hourinterval,NULL,0);
				if ((minus=strstr(hourinterval,"-")) != NULL)
				    hourend=strtol(minus+1,NULL,0);
				else
				    hourend=hourbegin;
				if (hourbegin <= hourend) {
				    if (hour >= hourbegin && hour <= hourend)
					found=1;
				} else {
				    if (hour >= hourbegin || hour <= hourend)
					found=1;
				}
				hourinterval=strtok(NULL,",");
			    }
			}
			if (found == 0)
			    continue;
		    }
			
		    /*
		     * run spool file, with flags modifier from pass 1.  Pass1
		     * may clear the SF_REALTIME flag.
		     */
		    if (count >= sfCount) {
			sfCount += 200;
			sfAry = realloc(sfAry, sfCount * sizeof(int));
			memset(sfAry+count, 0, (sfCount - count) * sizeof(int));
		    }
		    if (pass == 2) {
			flags = (flags & ~SF_REALTIME) | 
				(sfAry[count] & SF_REALTIME);
		    }

		    {
			NewslinkInfo li;

			memset((char *)&li, 0, sizeof(li));
			li.li_HostName = hostName;
			li.li_MaxQueueFile = maxq;
			li.li_MaxParallel = maxr;
			li.li_Port = port;
			li.li_StartDelay = delay;
			li.li_TransmitBuf = txbufsiz;
			li.li_ReceiveBuf = rxbufsiz;
			li.li_TOS = tos;
			li.li_LogArts = logarts;
			li.li_BindAddress = obip;
			li.li_MaxStream = 0;
			li.li_Compress = compress;
			li.li_QueueSkip = qskip;

			flags = spool_file(spoolFile, pass, &li, flags);

/*
		    flags = spool_file(
			spoolFile, 
			hostName,
			maxq,
			maxr,
			qskip,
			port,
			delay,
			type, 
			flags,
			obip,
			txbufsiz,
			rxbufsiz,
			logarts,
			pass,
			maxstream
		    );
*/
		    }

		    if (pass == 1)
			sfAry[count] = flags;
		    ++count;
		}
	    }
	    fclose(fi);
	} else if (pass == 1) {
	    fprintf(stderr, "Unable to open %s\n", fname);
	}
	if (pass == 1) {
	    char sysline[256];

	    snprintf(sysline, sizeof(sysline), "%s/dbin/dicmd flush", NewsHome);
	    system(sysline);
	}
    }
}

/*
 * SPOOL_FILE() - pass 1 - rename spool files
 *
 *		  pass 2 - start dnewslink processes
 */

int
spool_file(char *spoolFile, int pass, NewslinkInfo *li, int flags)
{
    char seqFile[256];
    char newFile[256];
    char obIpBuf[256];
    char *outboundIpStr = OutboundIpStr;
    int begSeq = 0;
    int endSeq = 0;
    long newTime = 0;
    char portBuf[32];
    char logartsStr[MAXLOGARTS+2];
    int fd;
    struct stat st;
    char txBufSizeStr[32];
    char rxBufSizeStr[32];
    char TOSStr[32];
    char maxStreamStr[32];
    char compressStr[32];
    char delayStr[32];

    if (VerboseOpt)
	printf("spool_file(%s, %d, %s, %d)\n",
			spoolFile, pass, li->li_HostName, flags);
    /*
     * Initialize rx/txBufSizeStr
     */

    sprintf(txBufSizeStr, "-nop");
    sprintf(rxBufSizeStr, "-nop");
    sprintf(TOSStr, "-nop");
    sprintf(maxStreamStr, "-nop");
    sprintf(compressStr, "-nop");
    sprintf(delayStr, "-nop");

    if (li->li_TransmitBuf > 0 && li->li_TransmitBuf < 16 * 1024 * 1024)
	sprintf(txBufSizeStr, "-T%d", li->li_TransmitBuf);
    if (li->li_ReceiveBuf > 0 && li->li_ReceiveBuf < 16 * 1024 * 1024)
	sprintf(rxBufSizeStr, "-R%d", li->li_ReceiveBuf);
    if (li->li_TOS > 0 && li->li_TOS <= 0xff )
	sprintf(TOSStr, "-Q%d", li->li_TOS);
    if (li->li_MaxStream > 0)
	sprintf(maxStreamStr, "-M%d", li->li_MaxStream);
    if (li->li_Compress > 0)
	sprintf(compressStr, "-Z%d", li->li_Compress);
    if (li->li_DelayFeed > 0)
	sprintf(delayStr, "-W%d", li->li_DelayFeed);

    /*
     * Outbound article logging.
     */
    sprintf(logartsStr, "-nop");
    if (li->li_LogArts && *li->li_LogArts) {
	strcpy(logartsStr, "-A");
	StrnCpyNull(logartsStr+2, li->li_LogArts, MAXLOGARTS);
    }

    /*
     * Override outbound IP
     */

    if (li->li_BindAddress != NULL &&
			strlen(li->li_BindAddress) < sizeof(obIpBuf) - 8) {
	sprintf(obIpBuf, "-B%s", li->li_BindAddress);
	outboundIpStr = obIpBuf;
    }

    if (li->li_Port <= 0)
	li->li_Port = 119;
    sprintf(portBuf, "%d", li->li_Port);

    sprintf(seqFile, ".%s.seq", spoolFile);

    /*
     * Get beginning and ending sequence numbers
     */

    fd = open(seqFile, O_RDWR|O_CREAT, 0600);

    if (fd >= 0) {
	char buf[64];

	xflock(fd, XLOCK_EX);		/* lock and leave locked */
	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf) - 1);
	sscanf(buf, "%d %d %lx", &begSeq, &endSeq, &newTime);
    }

    /*
     * Discard beginning sequence numbers that are now deleted, or
     * delete queue files that are too old.
     */

    while (begSeq < endSeq) {
	sprintf(newFile, "%s.S%05d", spoolFile, begSeq);
	if (li->li_MaxQueueFile > 0 && begSeq < endSeq - li->li_MaxQueueFile)
	    remove(newFile);
	if (stat(newFile, &st) == 0)
	    break;
	++begSeq;
    }

    /*
     * If primary spool file exists, shift to primary
     * queue
     */

    if (pass == 1) {
	{
	    int tries = 100;
	    int32 dt;
	    struct stat st;

	    while (tries > 0) {
		sprintf(newFile, "%s.S%05d", spoolFile, endSeq);
		if (stat(newFile, &st) < 0)
		    break;
		--tries;
		++endSeq;
	    }

	    /*
	     * Only create a new spool file if:
	     *
	     * (1) endSeq == begSeq + 1, or
	     * (2) current time larger then newTime + MinFlushSecs
	     * (3) there is time weirdness
	     */

	    dt = time(NULL) - ((int32)newTime + MinFlushSecs);

	    if (VerboseOpt)
		printf("PASS %d: dt=%d tries=%d begSeq=%d endSeq=%d\n",
			pass, dt, tries, begSeq, endSeq);

	    if (endSeq <= begSeq + 4 || dt > 0 || dt < -MinFlushSecs) {
		/*
		 * If a dnewslink has a lock on the unsequenced spool file,
		 * clear the SF_REALTIME flag... a realtime dnewslink is
		 * already running so there is no sense trying to start 
		 * another one.
		 */
		if (flags & SF_REALTIME) {
		    int tfd;

		    if ((tfd = open(spoolFile, O_RDWR)) >= 0) {
			if (xflock(tfd, XLOCK_EX|XLOCK_NB) < 0) {
			    if (VerboseOpt)
				printf("%s: realtime already\n", spoolFile);
			    flags &= ~SF_REALTIME;
			}
			close(tfd);
		    }
		}

		if (stat(spoolFile, &st) == 0 && st.st_size > 0 &&
					rename(spoolFile, newFile) == 0) {
		    ++endSeq;
		    if (!Quiet)
			printf("dspoolout: add %s\n", newFile);
		} else {
		    if (VerboseOpt && !Quiet)
			printf("dspoolout: nofile %s\n", newFile);
		    if (endSeq == begSeq)
			AddNode(spoolFile);
		}
		if (flags & SF_REALTIME) {
		    int fd = xopen(O_RDWR|O_CREAT, 0644, spoolFile);
		    if (fd != -1) {
			close(fd);
		    }
		}
		newTime = time(NULL);
	    } else {
		AddNode(spoolFile);
		if (VerboseOpt && !Quiet)
		    printf("dspoolout: wait %s\n", spoolFile);
	    }
	}

	/*
	 * Update sequence number file
	 */

	if (fd >= 0) {
	    char buf[64];

	    sprintf(buf, "%5d %5d %08lx\n", begSeq, endSeq, newTime);
	    lseek(fd, 0L, 0);
	    write(fd, buf, strlen(buf));
	    ftruncate(fd, lseek(fd, 0L, 1));
	}
    }

    if (fd >= 0) {
	xflock(fd, XLOCK_UN);
	close(fd);
    }

    /*
     * Run up to N newslink's/innxmit's, usually set at 2.
     *
     * Note that we spool out the queue as a FIFO.  This is important as
     * it reduces article jumble.
     */

    if (pass == 2 && FindNode(spoolFile) == NULL) {
	int tries = li->li_MaxParallel;
	int look;

	/*
	 * Start up to MaxParallel dnewslinks.  If the nobatch
	 * flag is set, do not start any.
	 */

	if (flags & SF_NOBATCH)
	    tries = 0;

	for (look = 0; look < 32 && tries &&
			begSeq + look < endSeq - li->li_QueueSkip; ++look) {
	    int use = begSeq + look;
	    int fd;
	    char numBuf[32];

	    {
		int left = endSeq - use - li->li_QueueSkip;
		if (left > 32)
		    left = 32;
		if (left < 3 && li->li_QueueSkip == 0)
		    left = 3;
		else if (left < 1)
		    left = 1;
		sprintf(numBuf, "%d", left);
	    if (VerboseOpt)
		printf("PASS %d: look=%d tries=%d begSeq=%d endSeq=%d use=%d left=%d\n",
			pass, look, tries, begSeq, endSeq, use, left);
	    }


	    sprintf(newFile, "%s.S%05d", spoolFile, use);

	    if ((fd = open(newFile, O_RDWR)) >= 0) {
		if (xflock(fd, XLOCK_EX|XLOCK_NB) == 0) {
		    char seqName[64];
		    char templateFile[256];

		    xflock(fd, XLOCK_UN);

		    if (!Quiet)
			printf("dspoolout: run %s.S%05d\n", spoolFile, use);

		    sprintf(templateFile, "%s.S%%05d", spoolFile);
		    sprintf(seqName, "%d", use);
		    if (VerboseOpt)
			    printf("%s %s%s %s%s %s%s %s%s %s%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
				"dnewslink",
				"-h", li->li_HostName,
				"-b", templateFile,
				"-S", seqName,
				"-N", numBuf,
				"-P", portBuf,
				"-D",
				((flags & SF_NOSTREAM) ? "-i" : "-nop"),
				((flags & SF_HEADFEED) ?
				    ((flags & SF_PRESBYTES) ? "-HB" : "-H") :
				    "-nop"),
				((flags & SF_NOCHECK) ? "-I" : "-nop"),
				((flags & SF_GENLINES) ? "-L" : "-nop"),
				((flags & SF_ARTSTAT) ? "-x" : "-nop"),
				compressStr,
				logartsStr,
				outboundIpStr,
				txBufSizeStr,
				rxBufSizeStr,
				TOSStr,
				delayStr,
				maxStreamStr
			    );
		    if (ForReal && fork() == 0) {
			char binPath[128];

			if (li->li_StartDelay > 0) {
			    if (xflock(fd, XLOCK_EX|XLOCK_NB) == 0) {
				sleep(li->li_StartDelay);
				xflock(fd, XLOCK_UN);
			    } else {
				if (DebugOpt)
				    printf(" qfile already locked for delay\n");
				exit(0);
			    }
			}

			if (li->li_Priority > 0)
			    nice(li->li_Priority);

			snprintf(binPath, sizeof(binPath), "%s/dbin/dnewslink", NewsHome);

			    execl(binPath, "dnewslink",
				"-h", li->li_HostName,
				"-b", templateFile,
				"-S", seqName,
				"-N", numBuf,
				"-P", portBuf,
				"-D",
				((flags & SF_NOSTREAM) ? "-i" : "-nop"),
				((flags & SF_HEADFEED) ?
				    ((flags & SF_PRESBYTES) ? "-HB" : "-H") :
				    "-nop"),
				((flags & SF_NOCHECK) ? "-I" : "-nop"),
				((flags & SF_GENLINES) ? "-L" : "-nop"),
				((flags & SF_ARTSTAT) ? "-x" : "-nop"),
				compressStr,
				logartsStr,
				outboundIpStr,
				txBufSizeStr,
				rxBufSizeStr,
				TOSStr,
				delayStr,
				maxStreamStr,
				NULL
			    );
			exit(0);
		    }
		}
		--tries;	/* even if lock fails */
		close(fd);
	    }
	}

	/*
	 * Deal with the realtime dnewslink, but only if:
	 *	The realtime flag is set
	 *	We have not gotten behind
	 *	We did not detect another realtime process
	 */

	if (VerboseOpt)
	    printf("%s: flags %02x nseq %d\n", spoolFile, flags & SF_REALTIME, endSeq-begSeq);

	if ((flags & SF_REALTIME) &&
				(ForceRealTime || (endSeq - begSeq <= 2))) {
	    int fd;

	    if ((fd = open(spoolFile, O_RDWR|O_CREAT, 0600)) >= 0) {
		if (xflock(fd, XLOCK_EX|XLOCK_NB) == 0) {
		    xflock(fd, XLOCK_UN);

		    if (!Quiet)
			printf("dspoolout: run realtime %s\n", spoolFile);

		    if (VerboseOpt)
			printf("%s %s%s %s%s %s%s %s%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
				"dnewslink",
				"-h", li->li_HostName,
				"-b", spoolFile,
				"-P", portBuf,
				"-N", "100",	/* 1000 spool cycles	*/
				((li->li_StartDelay < 0) ? "-r-1" : "-r"),
				((flags & SF_NOTIFY) ? "-o" : "-nop"),
				((flags & SF_NOTIFY) ? spoolFile : "-nop"),
				((flags & SF_NOSTREAM) ? "-i" : "-nop"),
				((flags & SF_HEADFEED) ?
				    ((flags & SF_PRESBYTES) ? "-HB" : "-H") :
				    "-nop"),
				(((flags & SF_NOCHECK)||(li->li_Check==2)) ? "-I" : "-nop"),
				((flags & SF_GENLINES) ? "-L" : "-nop"),
				((flags & SF_ARTSTAT) ? "-x" : "-nop"),
				compressStr,
				logartsStr,
				outboundIpStr,
				txBufSizeStr,
				rxBufSizeStr,
				delayStr,
				maxStreamStr
			    );
		    if (ForReal && fork() == 0) {
			char binPath[128];

			if (li->li_StartDelay > 0)
			    sleep(li->li_StartDelay);

			if (li->li_Priority > 0)
			    nice(li->li_Priority);

			snprintf(binPath, sizeof(binPath), "%s/dbin/dnewslink", NewsHome);

			    /*
			     * realtime option is '-r'.  If a delay of -1
			     * is specified, we pass -r-1 to dnewslink which
			     * reduces the tail latency from 1 second to 1ms.
			     */
			    execl(binPath, "dnewslink", 
				"-h", li->li_HostName,
				"-b", spoolFile,
				"-P", portBuf,
				"-N", "100",	/* 1000 spool cycles	*/
				((li->li_StartDelay < 0) ? "-r-1" : "-r"),
				((flags & SF_NOTIFY) ? "-o" : "-nop"),
				((flags & SF_NOTIFY) ? spoolFile : "-nop"),
				((flags & SF_NOSTREAM) ? "-i" : "-nop"),
				((flags & SF_HEADFEED) ?
				    ((flags & SF_PRESBYTES) ? "-HB" : "-H") :
				    "-nop"),
				(((flags & SF_NOCHECK)||(li->li_Check==2)) ? "-I" : "-nop"),
				((flags & SF_GENLINES) ? "-L" : "-nop"),
				((flags & SF_ARTSTAT) ? "-x" : "-nop"),
				compressStr,
				logartsStr,
				outboundIpStr,
				txBufSizeStr,
				rxBufSizeStr,
				delayStr,
				maxStreamStr,
				NULL
			    );
			exit(0);
		    }
		}
		close(fd);
	    }
	}
    }
    return(flags);
}

DNode *
FindNode(char *name)
{
    DNode *node;

    for (node = Base; node; node = node->no_Next) {
	if (strcmp(name, node->no_SpoolFile) == 0)
	    break;
    }
    return(node);
}

void
AddNode(char *name)
{
    DNode *node = malloc(sizeof(Node) + strlen(name) + 1);

    if (!node) {
      fprintf(stderr, "unable to malloc in AddNode\n");
      exit(1);
    }
    node->no_Next = Base;
    node->no_SpoolFile = (char *)(node + 1);
    Base = node;
    strcpy(node->no_SpoolFile, name);
}

