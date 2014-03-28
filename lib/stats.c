
/*
 * LIB/STATS.C
 *
 * (c)Copyright 2000, Russell Vincent, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 *
 * This file maintains access to shared stats structures for:
 *    outgoing feeds
 */

#include "defs.h"

#define	FS_VERSION	1

Prototype FeedStats *FeedStatsFindSlot(char *hostname);
Prototype void LockFeedRegion(FeedStats *Stats, int locktype, int stype);
Prototype void FeedStatsClear(FILE *fo, char *hostname, int stype);
Prototype void FeedStatsDump(FILE *fo, char *hostname, int raw, int stype);
Prototype void FeedStatsSnapShot(FILE *fo, char *hostname, char *ext);

void dumpInStats(FILE *fo, char *hostname, FeedStats *fs, int raw);
void dumpInDetStats(FILE *fo, char *hostname, FeedStats *fs, int raw);
void dumpOutStats(FILE *fo, char *hostname, FeedStats *fs, int raw);
void dumpSpoolStats(FILE *fo, char *hostname, FeedStats *fs, int raw);
void dumpSpoolDetStats(FILE *fo, char *hostname, FeedStats *fs, int raw);

int FSSFd = -1;
int FRSFd = -1;

/**********************************************************************
 * Outgoing feed stats routines
 **********************************************************************/
/*
 * FeedStatsFindSlot - locate a free area of a mmaped file that we use
 * use to store some outgoing feed stats. The file will be created
 * if it doesn't exist.
 */
FeedStats *
FeedStatsFindSlot(char *hostname)
{
    FeedStats *Stats;
    FeedStats fs;
    int count;
    int found;

    if (FSSFd < 0) {
	FSSFd = open(PatDbExpand(DFeedStatsPat), O_RDWR|O_CREAT, 0644);
	if (FSSFd == -1) {
	    logit(LOG_ERR, "Unable to create feeder stats file: %s (%s)",
			PatDbExpand(DFeedStatsPat), strerror(errno));
	    Stats = (FeedStats *)malloc(sizeof(FeedStats));
	    if (Stats == NULL) {
		logit(LOG_CRIT, "Unable to alloc memory for stats struct");
		exit(1);
	    }
	    bzero(Stats, sizeof(FeedStats));
	    return(Stats);
	}
    } else {
	lseek(FSSFd, 0, 0);
    }

    hflock(FSSFd, 0, XLOCK_EX);

    /*
     * The first entry is a dummy entry
     */
    if (read(FSSFd, &fs, sizeof(fs)) == 0) {
	bzero(&fs, sizeof(fs));
	strcpy(fs.hostname, "Outgoing Feeder Stats");
	fs.SentStats.TimeStart = time(NULL);
	fs.RecStats.TimeStart = time(NULL);
	fs.SpoolStats.TimeStart = time(NULL);
	fs.version = FS_VERSION;
	write(FSSFd, &fs, sizeof(fs));
    }
    if (fs.version != FS_VERSION) {
	logit(LOG_CRIT, "Feed stats db '%s' has wrong version - please delete",
					PatDbExpand(DFeedStatsPat));
	fprintf(stderr, "Feed stats db '%s' has wrong version - please delete\n",
					PatDbExpand(DFeedStatsPat));
	exit(1);
    }
    count = 1;
    found = 0;
    while (read(FSSFd, &fs, sizeof(fs)) > 0) {
	if (strcmp(fs.hostname, hostname) == 0) {
	    found = 1;
	    break;
	}
	count++;
    }
    if (!found) {
	bzero(&fs, sizeof(fs));
	fs.region = count;
	strcpy(fs.hostname, hostname);
	lseek(FSSFd, count * sizeof(FeedStats), SEEK_SET);
	write(FSSFd, &fs, sizeof(fs));
    }
    Stats = xmap(NULL, sizeof(FeedStats), PROT_READ|PROT_WRITE,
			MAP_SHARED, FSSFd, count * sizeof(FeedStats));
    hflock(FSSFd, 0, XLOCK_UN);
    return(Stats);
}

/*
 * LockFeedRegion - lock a region of the mmaped file so that we
 *	can safely update the values
 */
void
LockFeedRegion(FeedStats *Stats, int locktype, int stype)
{
    if (Stats->region > 0) {
	switch (stype) {
	case FSTATS_IN:
	case FSTATS_INDETAIL:
	    hflock(FSSFd, (int)&Stats->RecStats - (int)Stats, locktype);
	    break;
	case FSTATS_OUT:
	    hflock(FSSFd, (int)&Stats->SentStats - (int)Stats, locktype);
	    break;
	case FSTATS_SPOOL:
	case FSTATS_SPOOLDETAIL:
	    hflock(FSSFd, (int)&Stats->SpoolStats - (int)Stats, locktype);
	    break;
	}
    }
}

/*
 * FeedStatsClear - zero the stats
 */
void
FeedStatsClear(FILE *fo, char *hostname, int stype)
{
    FeedStats *Stats;
    FeedStats *fs;
    struct stat st;
    int count;
    int cleared = 0;

    FSSFd = open(PatDbExpand(DFeedStatsPat), O_RDWR, 0644);
    if (FSSFd == -1) {
	fprintf(fo, "Unable to open feeder stats file: %s (%s)\n",
			PatDbExpand(DFeedStatsPat), strerror(errno));
	return;
    }
    if (fstat(FSSFd, &st) != 0)
	return;
    fs = Stats = xmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, FSSFd, 0);
    if (Stats == NULL) {
	fprintf(fo, "Unable to mmap stats file: %s\n", strerror(errno));
	return;
    }
    if (fs->version != FS_VERSION) {
	fprintf(fo, "Feed stats db '%s' has wrong version - please delete\n",
					PatDbExpand(DFeedStatsPat));
	fflush(fo);
	exit(1);
    }
    count = st.st_size / sizeof(FeedStats);
    while (--count > 0) {
	fs++;
	if (hostname != NULL && strcmp(hostname, fs->hostname) != 0)
	    continue;
	LockFeedRegion(fs, XLOCK_EX, stype);
	switch(stype) {
	case FSTATS_IN:
	case FSTATS_INDETAIL:
	    if (fs->RecStats.TimeStart != 0) {
		bzero(&fs->RecStats, sizeof(fs->RecStats));
		fs->RecStats.TimeStart = time(NULL);
	    }
	    cleared++;
	    break;
	case FSTATS_OUT:
	    if (fs->SentStats.TimeStart != 0) {
		bzero(&fs->SentStats, sizeof(fs->SentStats));
		fs->SentStats.TimeStart = time(NULL);
	    }
	    cleared++;
	    break;
	case FSTATS_SPOOL:
	case FSTATS_SPOOLDETAIL:
	    if (fs->SpoolStats.TimeStart != 0) {
		bzero(&fs->SpoolStats, sizeof(fs->SpoolStats));
		fs->SpoolStats.TimeStart = time(NULL);
	    }
	    cleared++;
	    break;
	}
	LockFeedRegion(fs, XLOCK_UN, stype);
    }
    xunmap(Stats, st.st_size);
    close(FSSFd);
    fprintf(fo, "Cleared %d records of type ", cleared);
    switch (stype) {
    case FSTATS_IN:
    case FSTATS_INDETAIL:
	printf("incoming\n");
	break;
	break;
    case FSTATS_OUT:
	printf("outgoing\n");
	break;
    case FSTATS_SPOOL:
    case FSTATS_SPOOLDETAIL:
	printf("spool\n");
	break;
    }
}

/*
 * FeedStatsDump - print the feed stats
 */
void
FeedStatsDump(FILE *fo, char *hostname, int raw, int stype)
{
    FeedStats *Stats;
    FeedStats *fs;
    struct stat st;
    int count;
    int totalonly = 0;
    FeedStats ts;
    int i;

    if (hostname != NULL && strcmp(hostname, "TOTAL") == 0)
	totalonly = 1;
    FSSFd = open(PatDbExpand(DFeedStatsPat), O_RDONLY, 0644);
    if (FSSFd == -1) {
	fprintf(fo, "Unable to open feeder stats file: %s (%s)\n",
			PatDbExpand(DFeedStatsPat), strerror(errno));
	return;
    }
    if (fstat(FSSFd, &st) != 0)
	return;
    fs = Stats = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, FSSFd, 0);
    if (Stats == NULL) {
	fprintf(fo, "Unable to mmap stats file: %s\n", strerror(errno));
	return;
    }
    if (fs->version != FS_VERSION) {
	fprintf(fo, "Feed stats db '%s' has wrong version - please delete\n",
					PatDbExpand(DFeedStatsPat));
	fflush(fo);
	exit(1);
    }
    count = st.st_size / sizeof(FeedStats);
    bzero(&ts, sizeof(ts));
    ts.RecStats.TimeStart = time(NULL);
    ts.SentStats.TimeStart = time(NULL);
    ts.SpoolStats.TimeStart = time(NULL);
    while (--count > 0) {
	fs++;
	if (fs->hostname[0] == 0)
	    continue;
	if (hostname != NULL && !totalonly &&
					strcmp(hostname, fs->hostname) != 0)
	    continue;
	if (fs->RecStats.TimeStart > 0 &&
			fs->RecStats.TimeStart < ts.RecStats.TimeStart)
	    ts.RecStats.TimeStart = fs->RecStats.TimeStart;
	ts.RecStats.ConnectCnt += fs->RecStats.ConnectCnt;
	for (i = 0; i < STATS_NSLOTS; i++)
	    ts.RecStats.Stats[i] += fs->RecStats.Stats[i];
	ts.RecStats.ReceivedBytes += fs->RecStats.ReceivedBytes;
	ts.RecStats.AcceptedBytes += fs->RecStats.AcceptedBytes;
	ts.RecStats.RejectedBytes += fs->RecStats.RejectedBytes;
	if (fs->SentStats.TimeStart > 0 &&
			fs->SentStats.TimeStart < ts.SentStats.TimeStart)
	    ts.SentStats.TimeStart = fs->SentStats.TimeStart;
	ts.SentStats.ConnectCnt += fs->SentStats.ConnectCnt;
	ts.SentStats.OfferedCnt += fs->SentStats.OfferedCnt;
	ts.SentStats.AcceptedCnt += fs->SentStats.AcceptedCnt;
	ts.SentStats.RefusedCnt += fs->SentStats.RefusedCnt;
	ts.SentStats.RejectedCnt += fs->SentStats.RejectedCnt;
	ts.SentStats.DeferredFailCnt += fs->SentStats.DeferredFailCnt;
	ts.SentStats.AcceptedBytes += fs->SentStats.AcceptedBytes;
	ts.SentStats.RejectedBytes += fs->SentStats.RejectedBytes;
	if (fs->SpoolStats.TimeStart > 0 &&
			fs->SpoolStats.TimeStart < ts.SpoolStats.TimeStart)
	    ts.SpoolStats.TimeStart = fs->SpoolStats.TimeStart;
	ts.SpoolStats.ConnectCnt += fs->SpoolStats.ConnectCnt;
	for (i = 0; i < STATS_S_NSLOTS; i++)
	    ts.SpoolStats.Arts[i] += fs->SpoolStats.Arts[i];
	ts.SpoolStats.ArtsBytesSent += fs->SpoolStats.ArtsBytesSent;
	if (!totalonly) {
	    switch (stype) {
	    case FSTATS_IN:
		dumpInStats(fo, fs->hostname, fs, raw);
		break;
	    case FSTATS_INDETAIL:
		dumpInDetStats(fo, fs->hostname, fs, raw);
		break;
	    case FSTATS_OUT:
		dumpOutStats(fo, fs->hostname, fs, raw);
		break;
	    case FSTATS_SPOOL:
		dumpSpoolStats(fo, fs->hostname, fs, raw);
		break;
	    case FSTATS_SPOOLDETAIL:
		dumpSpoolDetStats(fo, fs->hostname, fs, raw);
		break;
	    }
	}
    }
    xunmap(Stats, st.st_size);
    close(FSSFd);
    switch (stype) {
    case FSTATS_IN:
	dumpInStats(fo, "TOTAL", &ts, raw);
	break;
    case FSTATS_INDETAIL:
	dumpInDetStats(fo, "TOTAL", &ts, raw);
	break;
    case FSTATS_OUT:
	dumpOutStats(fo, "TOTAL", &ts, raw);
	break;
    case FSTATS_SPOOL:
	dumpSpoolStats(fo, "TOTAL", &ts, raw);
	break;
    case FSTATS_SPOOLDETAIL:
	dumpSpoolDetStats(fo, "TOTAL", &ts, raw);
	break;
    }
}

void
dumpInStats(FILE *fo, char *hostname, FeedStats *fs, int raw)
{
    if (fs->RecStats.TimeStart == 0)
	return;
    if (raw)
	fprintf(fo, "INFEED %-20s secs=%u con=%u off=%u rec=%u acc=%u ref=%u rej=%u recbytes=%.0f accbytes=%.0f rejbytes=%.0f\n",
				hostname,
				(int)(time(NULL) - fs->RecStats.TimeStart),
				fs->RecStats.ConnectCnt,
				fs->RecStats.Stats[STATS_OFFERED],
				fs->RecStats.Stats[STATS_RECEIVED],
				fs->RecStats.Stats[STATS_ACCEPTED],
				fs->RecStats.Stats[STATS_REFUSED],
				fs->RecStats.Stats[STATS_REJECTED],
				fs->RecStats.ReceivedBytes,
				fs->RecStats.AcceptedBytes,
				fs->RecStats.RejectedBytes
	);
    else
	fprintf(fo, "INFEED %-20s secs=%u con=%u off=%u rec=%u acc=%u ref=%u rej=%u recbytes=%s accbytes=%s rejbytes=%s\n",
				hostname,
				(int)(time(NULL) - fs->RecStats.TimeStart),
				fs->RecStats.ConnectCnt,
				fs->RecStats.Stats[STATS_OFFERED],
				fs->RecStats.Stats[STATS_RECEIVED],
				fs->RecStats.Stats[STATS_ACCEPTED],
				fs->RecStats.Stats[STATS_REFUSED],
				fs->RecStats.Stats[STATS_REJECTED],
				ftos(fs->RecStats.ReceivedBytes),
				ftos(fs->RecStats.AcceptedBytes),
				ftos(fs->RecStats.RejectedBytes)
	);
}

void
dumpInDetStats(FILE *fo, char *hostname, FeedStats *fs, int raw)
{
    if (fs->RecStats.TimeStart == 0)
	return;
    if (raw)
	fprintf(fo, "INFEEDDETAIL %-20s secs=%u ihave=%d chk=%d takethis=%d rec=%d acc=%d ref=%d precom=%d postcom=%d his=%d badmsgid=%d ifilthash=%d rej=%d ctl=%d failsafe=%d misshdrs=%d tooold=%d grpfilt=%d intspamfilt=%d extspamfilt=%d incfilter=%d nospool=%d ioerr=%d notinactv=%d pathtab=%d ngtab=%d posdup=%d hdrerr=%d toosmall=%d incompl=%d nul=%d nobytes=%d proto=%d msgidmis=%d err=%d toobig=%d recbytes=%.0f accbytes=%.0f rejbytes=%.0f\n",
				hostname,
				(int)(time(NULL) - fs->RecStats.TimeStart),
				fs->RecStats.Stats[STATS_IHAVE],
				fs->RecStats.Stats[STATS_CHECK],
				fs->RecStats.Stats[STATS_TAKETHIS],
				fs->RecStats.Stats[STATS_RECEIVED],
				fs->RecStats.Stats[STATS_ACCEPTED],
				fs->RecStats.Stats[STATS_REFUSED],
				fs->RecStats.Stats[STATS_REF_PRECOMMIT],
				fs->RecStats.Stats[STATS_REF_POSTCOMMIT],
				fs->RecStats.Stats[STATS_REF_HISTORY],
				fs->RecStats.Stats[STATS_REF_BADMSGID],
				fs->RecStats.Stats[STATS_REF_IFILTHASH],
				fs->RecStats.Stats[STATS_REJECTED],
				fs->RecStats.Stats[STATS_CONTROL],
				fs->RecStats.Stats[STATS_REJ_FAILSAFE],
				fs->RecStats.Stats[STATS_REJ_MISSHDRS],
				fs->RecStats.Stats[STATS_REJ_TOOOLD],
				fs->RecStats.Stats[STATS_REJ_GRPFILTER],
				fs->RecStats.Stats[STATS_REJ_INTSPAMFILTER],
				fs->RecStats.Stats[STATS_REJ_EXTSPAMFILTER],
				fs->RecStats.Stats[STATS_REJ_INCFILTER],
				fs->RecStats.Stats[STATS_REJ_NOSPOOL],
				fs->RecStats.Stats[STATS_REJ_IOERROR],
				fs->RecStats.Stats[STATS_REJ_NOTINACTV],
				fs->RecStats.Stats[STATS_REJ_PATHTAB],
				fs->RecStats.Stats[STATS_REJ_NGTAB],
				fs->RecStats.Stats[STATS_REJ_POSDUP],
				fs->RecStats.Stats[STATS_REJ_HDRERROR],
				fs->RecStats.Stats[STATS_REJ_TOOSMALL],
				fs->RecStats.Stats[STATS_REJ_ARTINCOMPL],
				fs->RecStats.Stats[STATS_REJ_ARTNUL],
				fs->RecStats.Stats[STATS_REJ_NOBYTES],
				fs->RecStats.Stats[STATS_REJ_PROTOERR],
				fs->RecStats.Stats[STATS_REJ_MSGIDMIS],
				fs->RecStats.Stats[STATS_REJ_ERR],
				fs->RecStats.Stats[STATS_REJ_TOOBIG],
				fs->RecStats.ReceivedBytes,
				fs->RecStats.AcceptedBytes,
				fs->RecStats.RejectedBytes
	);
    else
	fprintf(fo, "INFEEDDETAIL %-20s secs=%u ihave=%d chk=%d takethis=%d rec=%d acc=%d ref=%d precom=%d postcom=%d his=%d badmsgid=%d ifilthash=%d rej=%d ctl=%d failsafe=%d misshdrs=%d tooold=%d grpfilt=%d intspamfilt=%d extspamfilt=%d incfilter=%d nospool=%d ioerr=%d notinactv=%d pathtab=%d ngtab=%d posdup=%d hdrerr=%d toosmall=%d incompl=%d nul=%d nobytes=%d proto=%d msgidmis=%d err=%d toobig=%d recbytes=%s accbytes=%s rejbytes=%s\n",
				hostname,
				(int)(time(NULL) - fs->RecStats.TimeStart),
				fs->RecStats.Stats[STATS_IHAVE],
				fs->RecStats.Stats[STATS_CHECK],
				fs->RecStats.Stats[STATS_TAKETHIS],
				fs->RecStats.Stats[STATS_RECEIVED],
				fs->RecStats.Stats[STATS_ACCEPTED],
				fs->RecStats.Stats[STATS_REFUSED],
				fs->RecStats.Stats[STATS_REF_PRECOMMIT],
				fs->RecStats.Stats[STATS_REF_POSTCOMMIT],
				fs->RecStats.Stats[STATS_REF_HISTORY],
				fs->RecStats.Stats[STATS_REF_BADMSGID],
				fs->RecStats.Stats[STATS_REF_IFILTHASH],
				fs->RecStats.Stats[STATS_REJECTED],
				fs->RecStats.Stats[STATS_CONTROL],
				fs->RecStats.Stats[STATS_REJ_FAILSAFE],
				fs->RecStats.Stats[STATS_REJ_MISSHDRS],
				fs->RecStats.Stats[STATS_REJ_TOOOLD],
				fs->RecStats.Stats[STATS_REJ_GRPFILTER],
				fs->RecStats.Stats[STATS_REJ_INTSPAMFILTER],
				fs->RecStats.Stats[STATS_REJ_EXTSPAMFILTER],
				fs->RecStats.Stats[STATS_REJ_INCFILTER],
				fs->RecStats.Stats[STATS_REJ_NOSPOOL],
				fs->RecStats.Stats[STATS_REJ_IOERROR],
				fs->RecStats.Stats[STATS_REJ_NOTINACTV],
				fs->RecStats.Stats[STATS_REJ_PATHTAB],
				fs->RecStats.Stats[STATS_REJ_NGTAB],
				fs->RecStats.Stats[STATS_REJ_POSDUP],
				fs->RecStats.Stats[STATS_REJ_HDRERROR],
				fs->RecStats.Stats[STATS_REJ_TOOSMALL],
				fs->RecStats.Stats[STATS_REJ_ARTINCOMPL],
				fs->RecStats.Stats[STATS_REJ_ARTNUL],
				fs->RecStats.Stats[STATS_REJ_NOBYTES],
				fs->RecStats.Stats[STATS_REJ_PROTOERR],
				fs->RecStats.Stats[STATS_REJ_MSGIDMIS],
				fs->RecStats.Stats[STATS_REJ_ERR],
				fs->RecStats.Stats[STATS_REJ_TOOBIG],
				ftos(fs->RecStats.ReceivedBytes),
				ftos(fs->RecStats.AcceptedBytes),
				ftos(fs->RecStats.RejectedBytes)
	);
}

void
dumpOutStats(FILE *fo, char *hostname, FeedStats *fs, int raw)
{
    if (fs->SentStats.TimeStart == 0)
	return;
    if (raw)
	fprintf(fo, "OUTFEED %-20s secs=%u con=%u off=%u acc=%u ref=%u rej=%u deffail=%u accbytes=%.0f rejbytes=%.0f\n",
				hostname,
				(int)(time(NULL) - fs->SentStats.TimeStart),
				fs->SentStats.ConnectCnt,
				fs->SentStats.OfferedCnt,
				fs->SentStats.AcceptedCnt,
				fs->SentStats.RefusedCnt,
				fs->SentStats.RejectedCnt,
				fs->SentStats.DeferredFailCnt,
				fs->SentStats.AcceptedBytes,
				fs->SentStats.RejectedBytes
		);
    else
	fprintf(fo, "OUTFEED %-20s secs=%u con=%u off=%u acc=%u ref=%u rej=%u deffail=%u accbytes=%s rejbytes=%s\n",
				hostname,
				(int)(time(NULL) - fs->SentStats.TimeStart),
				fs->SentStats.ConnectCnt,
				fs->SentStats.OfferedCnt,
				fs->SentStats.AcceptedCnt,
				fs->SentStats.RefusedCnt,
				fs->SentStats.RejectedCnt,
				fs->SentStats.DeferredFailCnt,
				ftos(fs->SentStats.AcceptedBytes),
				ftos(fs->SentStats.RejectedBytes)
	);
}

void
dumpSpoolStats(FILE *fo, char *hostname, FeedStats *fs, int raw)
{
    if (fs->SpoolStats.TimeStart == 0)
	return;
    if (raw)
	fprintf(fo, "SPOOL %-20s secs=%u con=%u stat=%u article=%u head=%u body=%u bytes=%.0f\n",
				hostname,
				(int)(time(NULL) - fs->SpoolStats.TimeStart),
				fs->SpoolStats.ConnectCnt,
				fs->SpoolStats.Arts[STATS_S_STAT],
				fs->SpoolStats.Arts[STATS_S_ARTICLE],
				fs->SpoolStats.Arts[STATS_S_HEAD],
				fs->SpoolStats.Arts[STATS_S_BODY],
				fs->SpoolStats.ArtsBytesSent
		);
    else
	fprintf(fo, "SPOOL %-20s secs=%u con=%u stat=%u article=%u head=%u body=%u bytes=%s\n",
				hostname,
				(int)(time(NULL) - fs->SpoolStats.TimeStart),
				fs->SpoolStats.ConnectCnt,
				fs->SpoolStats.Arts[STATS_S_STAT],
				fs->SpoolStats.Arts[STATS_S_ARTICLE],
				fs->SpoolStats.Arts[STATS_S_HEAD],
				fs->SpoolStats.Arts[STATS_S_BODY],
				ftos(fs->SpoolStats.ArtsBytesSent)
	);
}

void
dumpSpoolDetStats(FILE *fo, char *hostname, FeedStats *fs, int raw)
{
    if (fs->SpoolStats.TimeStart == 0)
	return;
    if (raw)
	fprintf(fo, "SPOOLDETAIL %-20s secs=%u con=%u stat=%u statmiss=%d statexp=%d staterr=%d article=%u articlemiss=%d articleexp=%d articleerr=%d articlepro=%d head=%u headmiss=%d headexp=%d headerr=%d headpro=%d body=%u bodymiss=%d bodyexp=%d bodyerr=%d bodypro=%d bytes=%.0f\n",
				hostname,
				(int)(time(NULL) - fs->SpoolStats.TimeStart),
				fs->SpoolStats.ConnectCnt,
				fs->SpoolStats.Arts[STATS_S_STAT],
				fs->SpoolStats.Arts[STATS_S_STATMISS],
				fs->SpoolStats.Arts[STATS_S_STATEXP],
				fs->SpoolStats.Arts[STATS_S_STATERR],
				fs->SpoolStats.Arts[STATS_S_ARTICLE],
				fs->SpoolStats.Arts[STATS_S_ARTICLEMISS],
				fs->SpoolStats.Arts[STATS_S_ARTICLEEXP],
				fs->SpoolStats.Arts[STATS_S_ARTICLEERR],
				fs->SpoolStats.Arts[STATS_S_ARTICLEPRO],
				fs->SpoolStats.Arts[STATS_S_HEAD],
				fs->SpoolStats.Arts[STATS_S_HEADMISS],
				fs->SpoolStats.Arts[STATS_S_HEADEXP],
				fs->SpoolStats.Arts[STATS_S_HEADERR],
				fs->SpoolStats.Arts[STATS_S_HEADPRO],
				fs->SpoolStats.Arts[STATS_S_BODY],
				fs->SpoolStats.Arts[STATS_S_BODYMISS],
				fs->SpoolStats.Arts[STATS_S_BODYEXP],
				fs->SpoolStats.Arts[STATS_S_BODYERR],
				fs->SpoolStats.Arts[STATS_S_BODYPRO],
				fs->SpoolStats.ArtsBytesSent
		);
    else
	fprintf(fo, "SPOOLDETAIL %-20s secs=%u con=%u stat=%u statmiss=%d statexp=%d staterr=%d article=%u articlemiss=%d articleexp=%d articleerr=%d articlepro=%d head=%u headmiss=%d headexp=%d headerr=%d headpro=%d body=%u bodymiss=%d bodyexp=%d bodyerr=%d bodypro=%d bytes=%s\n",
				hostname,
				(int)(time(NULL) - fs->SpoolStats.TimeStart),
				fs->SpoolStats.ConnectCnt,
				fs->SpoolStats.Arts[STATS_S_STAT],
				fs->SpoolStats.Arts[STATS_S_STATMISS],
				fs->SpoolStats.Arts[STATS_S_STATEXP],
				fs->SpoolStats.Arts[STATS_S_STATERR],
				fs->SpoolStats.Arts[STATS_S_ARTICLE],
				fs->SpoolStats.Arts[STATS_S_ARTICLEMISS],
				fs->SpoolStats.Arts[STATS_S_ARTICLEEXP],
				fs->SpoolStats.Arts[STATS_S_ARTICLEERR],
				fs->SpoolStats.Arts[STATS_S_ARTICLEPRO],
				fs->SpoolStats.Arts[STATS_S_HEAD],
				fs->SpoolStats.Arts[STATS_S_HEADMISS],
				fs->SpoolStats.Arts[STATS_S_HEADEXP],
				fs->SpoolStats.Arts[STATS_S_HEADERR],
				fs->SpoolStats.Arts[STATS_S_HEADPRO],
				fs->SpoolStats.Arts[STATS_S_BODY],
				fs->SpoolStats.Arts[STATS_S_BODYMISS],
				fs->SpoolStats.Arts[STATS_S_BODYEXP],
				fs->SpoolStats.Arts[STATS_S_BODYERR],
				fs->SpoolStats.Arts[STATS_S_BODYPRO],
				ftos(fs->SpoolStats.ArtsBytesSent)
	);
}

/*
 * FeedStatsSnapShot - create a snapshot of the feedstats file
 */
void
FeedStatsSnapShot(FILE *fo, char *hostname, char *ext)
{
    int oldf;
    int newf;
    char fnameout[PATH_MAX];
    char timebuf[64];
    FeedStats fs;
    int count = 0;
    int res = 1;

    oldf = open(PatDbExpand(DFeedStatsPat), O_RDONLY);
    if (oldf == -1) {
	fprintf(fo, "Unable to open feeder stats file: %s (%s)\n",
			PatDbExpand(DFeedStatsPat), strerror(errno));
	return;
    }
    if (ext == NULL) {
	struct tm *tp;
	time_t t = time(NULL);
	tp = localtime(&t);
	strftime(timebuf, sizeof(timebuf), "%Y%m%d-%H%M%S", tp);
	ext = timebuf;
    }
    snprintf(fnameout, sizeof(fnameout), "%s.%s",
					PatDbExpand(DFeedStatsPat), ext);
    newf = open(fnameout, O_WRONLY|O_CREAT, 0644);
    if (newf == -1) {
	fprintf(fo, "Unable to create snapshot feeder stats file: %s (%s)\n",
					fnameout, strerror(errno));
	return;
    }
    while (res > 0) {
	hflock(oldf, count * sizeof(fs), XLOCK_EX);
	res = read(oldf, &fs, sizeof(fs));
	if (res == sizeof(fs) &&
		(hostname == NULL || strcmp(fs.hostname, hostname) == 0)) {
	    if (write(newf, &fs, sizeof(fs)) != sizeof(fs)) {
		fprintf(fo, "Error writing snapshot feeder stats file (%s)\n",
							strerror(errno));
		res = 0;
	    }
	}
	hflock(oldf, count * sizeof(fs), XLOCK_UN);
	if (res == -1)
	    fprintf(fo, "Error reading feeder stats file (%s)\n",
							strerror(errno));
	if (res <= 0)
	    break;
	count++;
    }
    close(oldf);
    close(newf);
    fprintf(fo, "%d records written to snapshot file %s\n", count, fnameout);
}
