
/*
 * LIB/NEWSFEED.C
 *
 * (c)Copyright 1997-1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * NOTE: hlabel, once found, may not be changed.
 */

#include "defs.h"

Prototype int FeedAdd(const char *msgid, time_t t, History *h, const char *nglist, const char *npath, const char *dist, int headerOnly, const char *artType, const char *cSize);
Prototype int FeedWrite(int logIt, int (*callback)(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize), const char *msgid, const char *path, const char *offsize, const char *nglist, const char *npath, const char *dist, const char *headOnly, const char *artType, int spamArt, const char *cSize);
Prototype void FeedFlush(void);
Prototype void LoadNewsFeed(time_t t, int force, const char *hlabel);
Prototype void TouchNewsFeed(void);
Prototype int FeedValid(const char *hlabel, int *pcount);
Prototype int IsFiltered(const char *hlabel, const char *nglist);
Prototype int IsDelayed(const char *hlabel);
Prototype int FeedQuerySpam(const char *hlabel, const char *hostInfo);
Prototype int PathElmMatches(const char *hlabel, const char *p, int bytes, int *pidx);
Prototype int CommonElmMatches(const char *common, const char *p, int bytes);
Prototype int FeedInDelay(const char *hlabel);
Prototype void FeedGetThrottle(const char *hlabel, int *delay, int *lines);
Prototype void ConfigNewsFeedSockOpts(const char *hlabel, int infd, int outfd);
Prototype int FeedReadOnly(const char *hlabel);
Prototype int FeedWhereIs(const char *hlabel);
Prototype int FeedPriority(const char *hlabel);
Prototype int FeedPrecommitReject(const char *hlabel);
Prototype LabelList *FeedLinkLabelList(void);
Prototype NewslinkInfo *FeedLinkInfo(const char *hlabel);
Prototype int FeedFilter(char *hlabel, const char *nglist, const char *npath, const char *dist, const char *artType, int bytes);
Prototype int FeedSpam(int which, const char *nglist, const char *npath, const char *dist, const char *artType, int bytes);
Prototype void DumpAllFeedInfo(FILE *fo);
Prototype void DumpFeedInfo(FILE *fo, char *label);
Prototype void FeedGetMaxInboundRate(const char *hlabel, int *bps);
Prototype int ArtHashIsFiltered(const char *msgid);
#if USE_OFFER_FILTER
Prototype int OfferIsFiltered(const char *hlabel, const char *msgid);
#endif  /* USE_OFFER_FILTER */

Prototype FILE *FeedFo;
Prototype int FeedDebug;

/*
 * NewsFeed - a newsfeeds label or group.
 *
 *	For most integer options, 0 is the default (usually off), -1 is
 *	off, and +1 is on.  Other values are also possible depending on the
 *	option.
 */

typedef struct NewsFeed {
    struct NewsFeed	*nf_Next;
    char		*nf_Label;
    struct Node		*nf_PathAliasBase;
    struct Node		*nf_SpamPathAliasBase;
    struct Node		*nf_GroupAcceptBase;
    struct Node		*nf_FilterBase;
    struct Node		*nf_SpamBase;
    struct Node		*nf_RequireGroupsBase;
#if USE_OFFER_FILTER
    struct Node		*nf_OfferFilterBase;
#endif  /* USE_OFFER_FILTER */
    int			nf_MaxCrossPost;	/* 0=unlimited 		*/
    int			nf_MinCrossPost;	/* 0=unlimited 		*/
    int			nf_MaxArtSize;		/* 0=unlimited 		*/
    int			nf_MinArtSize;		/* 0=unlimited 		*/
    int			nf_MaxPathLen;		/* maximum outgoing path len */
    int			nf_MinPathLen;		/* minimum outgoing path len */
    int			nf_MaxConnect;		/* max connections	*/
    char		*nf_MaxInboundRate;	/* maximum B/s per conn */
    char		*nf_Dist;		/* distribution		*/
    char		*nf_NoDist;		/* !distribution	*/
    char		nf_PerLineFlushOpt;
    char		nf_NoMisMatch;
    char		nf_SpamFeedOpt;
    char		nf_Resolved;
    int			nf_ThrottleDelay;
    int			nf_ThrottleLines;
    int			nf_FeederTXBuf;
    int			nf_FeederRXBuf;
    int			nf_ReadOnly;
    int			nf_WhereIs;
    int			nf_IncomingPriority;	/* nice() value of process */
    int			nf_PrecommitReject;	/* reject precommit hits */
    HashFeed_MatchList	*nf_HashFeed;		/* hashfeed */
    ArtTypeList		*nf_ArtTypes;		/* What type of articles */
    /*
     * The following struct is only used by dspoolout and doutq
     */
    struct NewslinkInfo	nf_LinkInfo;
} NewsFeed;

#define MAXRECURSION	16

#define RTF_ENABLED	0x01			/* enable realtime feed  */
#define RTF_NOBATCH	0x02			/* do not generate batch */

NewsFeed 	*NFBase;			/* label base */
NewsFeed	*GRBase;			/* groupref base */
NewsFeed 	*NFCache;			/* Most recently used label */
NewsFeed	*NFGlob;			/* The GLOBAL label */

NewsFeed	*ISBase;			/* Internal SPAM label */
NewsFeed	*ESBase;			/* External SPAM label */
NewsFeed 	*IFBase;			/* Incoming Filter label */

char	NFBuf[16384];
FILE	*FeedFo = NULL;
MemPool	*NFMemPool = NULL;
const char *SaveHLabel = NULL;
int FeedDebug = 0;
int UseSpamAlias = 0;

static int RecurCount = 0;
static int RecurWarn = 0;


int feedHashFeedOK(HashFeed_MatchList *hashfeed, unsigned char *md5hash, const char *msgid);
int feedQueryPaths(NewsFeed *feed, const char *npath, int size, int artType);
int feedQuerySpamPaths(NewsFeed *feed, const char *npath, int size, const char * msgid);
int feedQueryGroups(NewsFeed *feed, const char *nglist);
int feedQueryDists(NewsFeed *feed, const char *dist);
int filterRequireGroups(Node *node, const char *nglist);
int filterQueryGroups(NewsFeed *feed, const char *nglist);
int feedPathQuery(NewsFeed *feed, const char *path);
int feedSpamPathQuery(NewsFeed *feed, const char *path);
int feedDistQuery(const char *item, int len, const char *list);
int feedSpamFeedOK(int feed, int article);
#if USE_OFFER_FILTER
int feedOfferFilterQuery(NewsFeed *feed, const char *msgid);
#endif  /* USE_OFFER_FILTER */

void resolveGroupList(const char *label, Node *scan);
void AltFeed(NewsFeed *nf, FILE *fi);
int TooNear(time_t t);
void setLinkGLOBAL(NewslinkInfo *nf);
void setLinkStart(NewslinkInfo *nf);
void setLinkDefaults(NewslinkInfo *nf, NewslinkInfo *gl);
void resolveDefaults(NewsFeed *nf, Node *no);

int cbWildCmpStopWhenFound(Node *node, const void *data, int *pabort, int def);
int cbWildCmpNoStop(Node *node, const void *data, int *pabort, int def);
int cbFeedGroupQuery(Node *node, const void *data, int *pabort, int def);
int cbFilterRequireGroups(Node *node, const void *data, int *pabort, int def);
int recursiveScan(NewsFeed *feed, int off, const void *data, int (*callback)(Node *node, const void *data, int *pabort, int def), int def);

/*
 * FEEDADD()  - given message-id, history file data, newsgroup list, pathlist,
 *		and article size, commit the article to outgoing feeds.
 *
 *		NOTE: none of the string objects is allowed to contain a tab,
 *		this is checked prior to the call.
 *
 * This ia used in the slave diablos and essentially performs an RPC
 * call to FeedWrite in the master diablo.
 */

int 
FeedAdd(const char *msgid, time_t t, History *h, const char *nglist, const char *npath, const char *dist, int headerOnly, const char *artType, const char *cSize)
{
    int r = 0;

    LoadNewsFeed(t, 0, NULL);

    if (FeedFo) {
	char path[256];

	ArticleFileName(path, sizeof(path), h, ARTFILE_FILE_REL);

	fprintf(FeedFo, "SOUT\t%s\t%lld,%ld\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
	    path, (long long)h->boffset, (long)h->bsize, msgid, nglist, dist, npath,
	    headerOnly, artType, cSize
	);
	fflush(FeedFo);	/* simplify the parent reader on the pipe */
	if (ferror(FeedFo)) {
	    logit(LOG_CRIT, "lost backchannel to master server");
	    r = -1;
	}
    }
    return(r);
}

/*
 * FeedWrite()
 * Used in the master and called after the receipt of a SOUT in the
 * control pipe.
 */
int
FeedWrite(int logIt,
    int (*callback)(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize), 
    const char *msgid, 
    const char *path, 
    const char *offsize,
    const char *nglist,
    const char *npath,
    const char *dist,
    const char *headOnly,
    const char *artType,
    int spamArt,
    const char *cSize
) {
    NewsFeed *nf;
    int r = 0;
    int bytes = 0;
    char linebuf[4096];
    int linesiz, siz;
    char *ptr;
    unsigned char md5hash[16];

    HM_MD5MessageID((mid_t)msgid, md5hash);

    if ((ptr = strchr(npath, '!'))) {
	snprintf(linebuf, ptr - npath + 1, "%s", npath);
    } else {
	snprintf(linebuf, sizeof(linebuf), "%s", npath);
    }

    linesiz = strlen(linebuf);

    if (offsize && ((ptr = strchr(offsize, ',')))) {
	ptr++;
    } else {
	*ptr = '?';
    }

    siz = strlen(msgid) + strlen(ptr) + strlen(artType) + 4;
    if (cSize != NULL && strlen(cSize) > 1)
	siz += strlen(cSize);
    if (linesiz + siz < sizeof(linebuf) - 1) {
	strcat(linebuf, " + ");
	strcat(linebuf, msgid);
	strcat(linebuf, " ");
	strcat(linebuf, ptr);
	if (cSize != NULL && strlen(cSize) > 1) {
	    strcat(linebuf, ":");
	    strcat(linebuf, cSize);
	}
	strcat(linebuf, " ");
	strcat(linebuf, artType);
	linesiz += siz;
    }

    {
	char *p;
	if ((p = strchr(offsize, ',')) != NULL)
	    bytes = strtol(p + 1, NULL, 0);
    }

    for (nf = NFBase; nf; nf = nf->nf_Next) {
	if (FeedDebug)
	    printf(">>SCAN:%s\n", nf->nf_Label);
	if (UseSpamAlias && feedQuerySpamPaths(nf, npath, bytes, msgid))
	    break;
	if (feedQueryPaths(nf, npath, bytes, strtol(artType, NULL, 16)) == 0 && 
	    feedQueryGroups(nf, nglist) == 0 &&
	    feedSpamFeedOK(nf->nf_SpamFeedOpt, spamArt) == 0 &&
	    feedQueryDists(nf, dist) == 0 &&
	    feedHashFeedOK(nf->nf_HashFeed, md5hash, msgid) == 0
	) {
	    siz = strlen(nf->nf_Label) + 1;
	    if (linesiz + siz < sizeof(linebuf) - 1) {
		strcat(linebuf, " ");
		strcat(linebuf, nf->nf_Label);
		linesiz += siz;
	    }

	    r += callback(nf->nf_Label, msgid, path, offsize, nf->nf_PerLineFlushOpt, strtol(headOnly, NULL, 10), artType, cSize);

	}
    }
    /* logit(LOG_INFO, "%s", linebuf); */
    if (logIt)
	LogIncoming("%s%s%s", linebuf, "", "");
    return(r);
}

void
FeedFlush(void)
{
/*    NewsFeed *nf;*/

    /*
    if (FeedFo)
	fflush(FeedFo);
    */
}

int
FeedValid(const char *hlabel, int *pcount)
{
    NewsFeed *nf;

    if ((nf = NFCache) == NULL) {
	for (nf = NFBase; nf; nf = nf->nf_Next) {
	    if (strcmp(hlabel, nf->nf_Label) == 0)
		break;
	}
	NFCache = nf;
    }
    if (nf == NULL)
	return(FEED_MISSINGLABEL);
    if (nf->nf_MaxConnect && *pcount > nf->nf_MaxConnect) {
	*pcount = nf->nf_MaxConnect;
	return(FEED_MAXCONNECT);
    }
    return(0);
}

int
feedHashFeedOK(HashFeed_MatchList *hashfeed, unsigned char *md5hash, const char *msgid)
{
    if (! hashfeed)
	return(0);
    return(!HM_CheckForMatch_PC(hashfeed, (mid_t)msgid, md5hash, HMOPER_MATCHONE));
}

int
ArtHashIsFiltered(const char *msgid)
{
    if(IFBase && IFBase->nf_HashFeed) {
      return(!HM_CheckForMatch(IFBase->nf_HashFeed, (mid_t)msgid, HMOPER_MATCHONE));
    }
    return(0);
}


/*
 * IsFiltered() - return 0 if the article should be scrapped because
 *		  one of the gropus is filtered out, -1 otherwise.
 */

int 
IsFiltered(const char *hlabel, const char *nglist)
{
    NewsFeed *nf;
    int r;

    if ((nf = NFCache) == NULL) {
	for (nf = NFBase; nf; nf = nf->nf_Next) {
	    if (strcmp(hlabel, nf->nf_Label) == 0)
		break;
	}
	NFCache = nf;
    }

    if (nf == NULL) {
	r = 0;
    } else {
	r = filterQueryGroups(nf, nglist);
    }
    if (DebugOpt > 1)
	printf("IsFiltered: %d (%s,%s)\n", r, hlabel, nglist);
    return(r);
}

/*
 * IsDelayed() - return 1 if the feed is delayed
 */

int 
IsDelayed(const char *hlabel)
{
    NewsFeed *nf;

    for (nf = NFBase; nf; nf = nf->nf_Next) {
	if (strcmp(hlabel, nf->nf_Label) == 0)
	    break;
    }

    if (nf == NULL)
	return(0);
    return(nf->nf_LinkInfo.li_DelayFeed != 0);
}

/*
 * Return 0 if the feed does NOT have an alias for an element in the path
 *	AND the article is not too large, otherwise return -1.
 */

int
feedQueryPaths(NewsFeed *feed, const char *npath, int size, int artType)
{
    const char *p;
    int cnt = 0;

    if (feed->nf_MaxArtSize && size > feed->nf_MaxArtSize)
	return(-1);
    if (feed->nf_MinArtSize && size <= feed->nf_MinArtSize)
	return(-1);

    /*
     * If we don't want any art types, then return
     * If arttype is unknown and we don't want the default, then return
     * If artype is known, but doesn't match the feed request, then return
     */
    if (ArtTypeMatch(artType, feed->nf_ArtTypes) == 0)
	return(-1);

    while (*npath == ' ' || *npath == '\t')
	++npath;

    for (p = npath; p; p = strchr(p, '!')) {
	char pat[MAXGNAME];
	int l;

	++cnt;

	if (*p == '!')
	    ++p;
	for (l = 0; l < MAXGNAME - 1 && p[l] && p[l] != '!' && p[l] != ' ' && p[l] != '\t' && p[l] != '\n' && p[l] != '\r'; ++l)
	    ;
	strncpy(pat, p, l);
	pat[l] = 0;
	if (feedPathQuery(feed, pat) == 0)
	    break;
    }
    if (p == NULL) {
	/*
	 * no path aliases matched, we are ok... unless MaxPathLen 
	 * or MinPathLen is set.
	 */
	if (feed->nf_MaxPathLen && feed->nf_MaxPathLen < cnt)
	    return(-1);
	if (feed->nf_MinPathLen && feed->nf_MinPathLen > cnt)
	    return(-1);
	return(0);
    }
    /*
     * path alias found, skip this feed
     */

    return(-1);
}

/*
 * Return 0 if the feed does NOT have a spamalias for either the user or
 * last site in the path, otherwise return -1.
 */

int
feedQuerySpamPaths(NewsFeed *feed, const char *npath, int size, const char *msgid)
{
    const char *p;
    char *q;
    char *r;
    char pat[MAXGNAME];

    if (*npath == 0)
	 return(0);

    p = npath;
    q = 0;
    r = 0;
    while (*p && (p = strchr(p, '!')) != NULL) { /* Walk to end of path */
	p++;
	r = q;
	q = (char *)p;
    }
    if (r == NULL)
	r = (char *)npath;
    strcpy(pat, r);
    r = strchr(pat,'!');
    if (r != NULL) {
	*r = 0;
	r++;
    } else
	r = pat;

    if (DebugOpt > 4)
	logit(LOG_DEBUG, "%s: Spam test, '%s' '%s' '%s'", 
		PatLibExpand(DNewsfeedsPat), npath, pat, r);

    /*
       At this point, r should point at the username at the end of the
       path and pat should point at the sitename the user is at,
       assuming the Path: syntax was not bogus
     */
    if (feedSpamPathQuery(feed, pat) == 0) {
	logit(LOG_DEBUG, "Spam Path A detect, %s %s", msgid, pat);
	return(-1);
    }
    if (feedSpamPathQuery(feed, r) == 0) {
	logit(LOG_DEBUG, "Spam Path B detect, %s %s", msgid, r);
	return(-1);
    }
    return(0);
}


/*
 * Return 0 if the feed has a group match against a group in the group list
 * Return -1 if there is no match
 */

int
feedQueryGroups(NewsFeed *feed, const char *nglist)
{
    const char *p;
    int r = -1;
    int count = 0;

    if (FeedDebug)
	printf(">>QUERY:%s:%s\n", feed->nf_Label, nglist);

    while (*nglist == ' ' || *nglist == '\t')
	++nglist;

    for (p = nglist; p && r != -2; p = strchr(p, ',')) {
	int l;
	int nr;
	char group[MAXGNAME];

	if (*p == ',')
	    ++p;

	for (l = 0; l < MAXGNAME - 1 && p[l] && p[l] != ',' && p[l] != ' ' && p[l] != '\t' && p[l] != '\n' && p[l] != '\r'; ++l)
	    ;

	/*
	 * r:
	 *	0	feed based on group
	 *	-1	do not feed based on group
	 *	-2	do not feed based on group if group appears at all
	 */

	strncpy(group, p, l);
	group[l] = 0;
	nr = recursiveScan(feed, offsetof(NewsFeed, nf_GroupAcceptBase), group, cbFeedGroupQuery, -1);
	++count;
	if (nr != -1) {
	    r = nr;
	}
    }
    if (r >= 0 && feed->nf_MaxCrossPost && count > feed->nf_MaxCrossPost)
	r = -1;
    if (r >= 0 && feed->nf_MinCrossPost && count < feed->nf_MinCrossPost)
	r = -1;
    if (r >= 0 && recursiveScan(feed, offsetof(NewsFeed, nf_RequireGroupsBase), nglist, cbFilterRequireGroups, 0) < 0)
	r = -2;
    return(r);
}

/*
 * Check whether a distribution is valid.  Return 0 if so, -1 if we should
 * drop the article.
 *
 * Original by larso@ifi.uio.no (=?iso-8859-1?Q?Lars_Henrik_B=F8ler_Olafsen?=),
 * this routine rewritten by Matt.
 *
 * The algorithm works like this:
 *
 *	If no feed distributions defined, Pass.
 *
 *	If positive feed distributions defined, require that we match at
 *	least one of them to return 0, else we return -1.  But if no
 *	positive feed distributions have been defined we will return 0 unless
 *	a negative feed distribution match occurs.
 *
 *	If negative feed distributions defined and we match *any* of them,
 *	we dump, even if we had other positive matches.  I.e. negative rules
 *	have precedence.
 *
 * Individual distributions cannot be more then 30 characters long.
 */

int
feedQueryDists(NewsFeed *feed, const char *dist)
{
    int r = 0;

    if (feed->nf_Dist != NULL || feed->nf_NoDist != NULL) {
	/*
	 * If match distributions exist, the default return value is -1
	 * and we MUST match something.
	 */
	int i = 0;

	if (feed->nf_Dist != NULL)
	    r = -1;
	
	while (dist[i]) {
	    int j;

	    i += strspn(dist + i, " \t\r\n,"); /* skip ws 		*/
	    j = strcspn(dist + i, " \t\r\n,"); /* parse distribution	*/
	    if (j) {
		/*
		 * &dist[i] for j characters
		 *
		 * If we find a match in nodist, that's it.
		 */
		if (feedDistQuery(dist + i, j, feed->nf_NoDist) == 0) {
		    r = -1;
		    break;
		}
		/*
		 * If we find a match in dist, set r = 0, but a nodist match
		 * later on can still dump us.
		 */
		if (feedDistQuery(dist + i, j, feed->nf_Dist) == 0) {
		    r = 0;
		}
	    }
	    i += j;

	    /*
	     * after skipping past the distribution string, the next character
	     * MUST be a comma if we are going to have more distributions, with
	     * no white space.  If someone puts whitespace here they're bozos
	     * anyway.
	     */
	    if (dist[i] != ',')
		break;
	}
    }
    return(r);
}

int
feedDistQuery(const char *item, int len, const char *list)
{
    int i = 0;
    int r = -1;

    if (list) {
	while (list[i]) {
	    int j = strcspn(list + i, ",");

	    if (j == len && strncasecmp(item, list + i, j) == 0) {
		r = 0;
		break;
	    }
	    i += j;
	    if (list[i] != ',')
		break;
	    ++i;
	}
    }
    return(r);
}

/*
 * cbFilterRequireGroups()
 *
 *	If no requiregroups nodes specified, allow any group.  Otherwise
 *	group must be in requiregroups list.  abort if def > 0.  For the
 *	recursion, if someone else sets def > 0, we are already done.
 */

int
cbFilterRequireGroups(Node *node, const void *data, int *pabort, int def)
{
    const char *nglist = data;

    if (def <= 0) {
	/*
	 * 0 -> -1, indicates that we have at leaset one requiregroup
	 * so the default has changed.. we MUST find the group.
	 */
	const char *p;

	def = -1;

	while (*nglist == ' ' || *nglist == '\t')
	    ++nglist;

	for (p = nglist; p; p = strchr(p, ',')) {
	    int l;
	    char group[MAXGNAME];

	    if (*p == ',')
		++p;

	    for (l = 0; l < MAXGNAME - 1 && p[l] && p[l] != ',' && p[l] != ' ' && p[l] != '\t' && p[l] != '\n' && p[l] != '\r'; ++l)
		;
	    strncpy(group, p, l);
	    group[l] = 0;
	    if (WildCmp(node->no_Name, group) == 0) {
		def = 1;
		break;
	    }
	}
    }
    if (def > 0)
	*pabort = 1;
    return(def);
}

/*
 * FeedQuerySpam() - scan addspam/delspam filters on NNTP-Posting-Host:
 *
 *	0 - could be either, use normal spam filter
 *	-1 - definitely spam
 *      +1 - definitely not spam
 */

int
FeedQuerySpam(const char *hlabel, const char *hostInfo)
{
    NewsFeed *feed;
    int r = 0;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed) {
	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_SpamBase),
	    hostInfo, 
	    cbWildCmpNoStop,
	    r 
	);
    }

    /*
     * My directives are weird.  'delspam' means 'not spam' but has a node
     * value of -1.  'addspam' means 'spam' but has a node value of +1. 
     */

    if (r)
	r = -r;

    return(r);
}

/*
 * PathElmMatches() - match first path element against aliases.
 *		      Return -1 on failure, 0 on success.  Set
 *		      *pidx to the length of the first path element.
 */

int
PathElmMatches(const char *hlabel, const char *p, int bytes, int *pidx)
{
    NewsFeed *feed;
    int i;
    int r = 0;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }
    for (i = 0; i < bytes && p[i] != '!' && p[i] != '\n' && p[i] != '\r'; ++i)
	;
    *pidx = i;

    if (feed && feed->nf_NoMisMatch <= 0) {
	char buf[256];

	if (i >= sizeof(buf))
	    i = sizeof(buf) - 1;
	bcopy(p, buf, i);
	buf[i] = 0;

	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_PathAliasBase),
	    buf, 
	    cbWildCmpStopWhenFound,
	    -1
	);
    }
    return(r);
}

/*
 * Return 0 if the common path element exists in the given
 * path string.
 */

int 
CommonElmMatches(const char *common, const char *p, int bytes)
{
    int l = strlen(common);

    while (bytes >= l) {
	if (strncmp(common, p, l) == 0 &&
	    (bytes == l || p[l] == '!' || p[l] == '\n' || p[l] == '\r')
	) {
	    return(0);
	}
	while (bytes && *p != '!' && *p != '\n' && *p != '\r') {
	    --bytes;
	    ++p;
	}
	if (bytes && *p == '!') {
	    --bytes;
	    ++p;
	}
    }
    return(-1);
}

/*
 * Returns how long the feed in quesiton should be delayed before allowing
 * a connection.
 */

int
FeedInDelay(const char *hlabel)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed)
	return feed->nf_LinkInfo.li_DelayInFeed;
    else
	return 0;
}

/*
 * Write the throttling delay and line count into the appropriate
 * memory locations.
 */

void
FeedGetThrottle(const char *hlabel, int *delay, int *lines)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed) {
	*delay = feed->nf_ThrottleDelay;
	*lines = feed->nf_ThrottleLines;
    } else {
	*delay = 0;
	*lines = 0;
    }
}

/*
 * Return 1 if the feed is read-only (i.e., no IHAVE/CHECK/TAKETHIS),
 * 0 otherwise.
 */
int
FeedReadOnly(const char *hlabel)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed)
	return feed->nf_ReadOnly;
    else
	return 0;
}


/*
 * Configure per-feed socket values
 */
void
ConfigNewsFeedSockOpts(const char *hlabel, int infd, int outfd)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed) {
	if (feed->nf_FeederTXBuf) {
	    if (setsockopt(outfd, SOL_SOCKET, SO_SNDBUF, (void *)&(feed->nf_FeederTXBuf), sizeof(int)) < 0) {
	        logit(LOG_ERR, "setsockopt %s %d failed: %s", hlabel, feed->nf_FeederTXBuf, strerror(errno));
	    }
	}
	if (feed->nf_FeederRXBuf) {
	    if (setsockopt(outfd, SOL_SOCKET, SO_RCVBUF, (void *)&(feed->nf_FeederRXBuf), sizeof(int)) < 0) {
	        logit(LOG_ERR, "setsockopt %s %d failed: %s", hlabel, feed->nf_FeederRXBuf, strerror(errno));
	    }
	}
    }
    return;
}

/*
 * Return 1 if the feed is allowed to use use the 'whereis' command
 * 0 otherwise.
 */
int
FeedWhereIs(const char *hlabel)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed)
	return feed->nf_WhereIs;
    else
	return 0;
}

/*
 * Return the incoming priority setting
 */

int
FeedPriority(const char *hlabel)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed)
	return feed->nf_IncomingPriority;
    else
	return 0;
}

/*
 * Return 1 if we need to reject precommit hits for this incoming feed
 * 0 otherwise.
 */

int
FeedPrecommitReject(const char *hlabel)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (feed)
	return feed->nf_PrecommitReject;
    else
	return 0;
}

/*
 * filterQueryGroups()
 *
 *	0	if no filter commands matched
 *	-1	if last matching filter command was 'nofilter'
 *	+1	if last matching filter command was 'filter'
 *
 * If one group returns -1 and another returns +1, we always return +1.
 */

int
filterQueryGroups(NewsFeed *feed, const char *nglist)
{
    const char *p;
    int r = 0;

    while (*nglist == ' ' || *nglist == '\t')
	++nglist;

    for (p = nglist; p; p = strchr(p, ',')) {
	int l;
	char group[MAXGNAME];

	if (*p == ',')
	    ++p;
	for (l = 0; l < MAXGNAME - 1 && p[l] && p[l] != ',' && p[l] != ' ' && p[l] != '\t' && p[l] != '\n' && p[l] != '\r'; ++l)
	    ;
	strncpy(group, p, l);
	group[l] = 0;

	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_FilterBase),
	    group, 
	    cbWildCmpNoStop,
	    r
	);

	if (r > 0)
	    break;
    }
    return(r);
}

/*
 * cbFeedGroupQuery()
 *
 *	-2	do not feed ANY group if this group appears in the newsgroups
 *		line.
 *
 *	-1	do not feed this group
 *
 *	0	feed this group
 *
 */

int
cbFeedGroupQuery(Node *node, const void *data, int *pabort, int def)
{
    const char *group = data;

    if (WildCmp(node->no_Name, group) == 0) {
	if (node->no_Value < 0) {
	    def = node->no_Value;	/* -1 or -2 */
	} else if (node->no_Value > 0) {
	    def = 0;			/* +1	    */
	}
    }

    if (FeedDebug)
	printf(">>CMP:%s:%s:%d:%d\n", node->no_Name, group, node->no_Value, def);

    return(def);
}

/*
 * Return 0 if we get a match
 */

int
feedSpamPathQuery(NewsFeed *feed, const char *path)
{
    int r = -1;

    if (feed) {
	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_SpamPathAliasBase),
	    path, 
	    cbWildCmpStopWhenFound,
	    r 
	);
    }
    return(r);
}

/*
 * Return 0 if we get a match
 */

int
feedPathQuery(NewsFeed *feed, const char *path)
{
    int r = -1;

    if (feed) {
	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_PathAliasBase),
	    path, 
	    cbWildCmpStopWhenFound,
	    r 
	);
    }
    return(r);
}

#if USE_OFFER_FILTER
/*
 * OfferIsFiltered() - return 0 if the article should be refused because
 *		  the Message-ID is filtered out, -1 otherwise.
 */

int 
OfferIsFiltered(const char *hlabel, const char *msgid)
{
    NewsFeed *nf;
    int r;

    if ((nf = NFCache) == NULL) {
	for (nf = NFBase; nf; nf = nf->nf_Next) {
	    if (strcmp(hlabel, nf->nf_Label) == 0)
		break;
	}
	NFCache = nf;
    }

    if (nf == NULL) {
	r = 0;
    } else {
	r = feedOfferFilterQuery(nf, msgid);
    }
    if (DebugOpt > 1)
	printf("OfferIsFiltered: %d (%s,%s)\n", r, hlabel, msgid);
    return(r);
}

/*
 * Return non-0 if we get a match
 */

int
feedOfferFilterQuery(NewsFeed *feed, const char *msgid)
{
    int r = 0;

    if (feed) {
	r = recursiveScan(
	    feed, 
	    offsetof(NewsFeed, nf_OfferFilterBase),
	    msgid, 
	    cbWildCmpNoStop,
	    r 
	);
    }
    return(r);
}
#endif  /* USE_OFFER_FILTER */

time_t NFGmtMin = (time_t)-1;
time_t NFMTime = 0;

void
TouchNewsFeed(void)
{
    struct utimbuf ut;

    time(&ut.actime);
    ut.modtime = ut.actime;

    utime(PatLibExpand(DNewsfeedsPat), &ut);
}

/*
 * LoadNewsFeed() - [re]load dnewsfeeds file
 */

void
LoadNewsFeed(time_t t, int force, const char *hlabel)
{

    /*
     * check for dnewsfeeds file modified once a minute
     */

    if (hlabel != (void *)-1)
	SaveHLabel = hlabel;

    if (force || t == 0 || t / 60 != NFGmtMin) {
	struct stat st = { 0 };
	FILE *fi = NULL;

	if (t)
	    NFGmtMin = t / 60;

	errno = 0;

	if ((istat(0, &st) == 0 && st.st_mtime != NFMTime && !TooNear(st.st_mtime)) ||
	    force ||
	    NFMTime == 0
	) {
	    char buf[MAXGNAME+256];

	    if (DebugOpt > 1)
	        printf("Opening dnewsfeeds file: %s\n", PatLibExpand(DNewsfeedsPat));
	    fi = iopen(PatLibExpand(DNewsfeedsPat), "r");

	    if (fi == NULL)
	        logit(LOG_CRIT, "%s: %s", PatLibExpand(DNewsfeedsPat), strerror(errno));

	    NFMTime = st.st_mtime;	/* may be 0 if file failed to open */

	    /*
	     * flush existing feed information
	     */

	    if (DebugOpt) {
		printf("Reloading dnewsfeeds file hlabel=%s\n",
		    ((hlabel) ? hlabel : "?")
		);
	    }

	    FeedFlush();

	    /*
	     * free up feed structures
	     */

	    freePool(&NFMemPool);
	    NFCache = NULL;
	    NFGlob = NULL;
	    NFBase = NULL;
	    IFBase = NULL;
	    GRBase = NULL;
	    ISBase = NULL;
	    ESBase = NULL;

	    UseSpamAlias = 0;

	    /*
	     * Reset MaxPerRemote if it wasn't specified on the command line.
	     *	0 = disabled
	     * -1 = disabled but there may be per-feed limits
	     * +N = some global limit
	     */

	    if (SaveHLabel == NULL && DOpts.MaxPerRemote == -1)
		DOpts.MaxPerRemote = 0;

	    /*
	     * load up new feed structures
	     */

	    {
		NewsFeed *nf = NULL;
		Node **paNode = NULL;
		Node **psaNode = NULL;
		Node **pgNode = NULL;
		Node **psNode = NULL;
		Node **pfNode = NULL;
		Node **prNode = NULL;
#if USE_OFFER_FILTER
		Node **pofNode = NULL;
#endif  /* USE_OFFER_FILTER */
		int lineNo = 0;
		int inGroupDef = 0;
		FILE *altFi = NULL;

		while (fi && igets(buf, sizeof(buf), fi) != NULL) {
		    char *s1 = strtok(buf, " \t\n");
		    char *s2 = (s1) ? strtok(NULL, " \t\n") : NULL;
		    int err = 1;

		    ++lineNo;

		    if (s1 == NULL || *s1 == '#')
			continue;

		    if (strcmp(s1, "label") == 0) {
			if (nf) {
			    logit(LOG_CRIT, 
				"Newsfeed config line %d, no end before new label!\n",
				lineNo
			    );

			    if (pfNode) {
				*pfNode = NULL;
				pfNode = NULL;
			    }
			    if (paNode) {
				*paNode = NULL;
				paNode = NULL;
			    }
			    if (psaNode) {
				*psaNode = NULL;
				psaNode = NULL;
			    }
			    if (pgNode) {
				*pgNode = NULL;
				pgNode = NULL;
			    }
			    if (psNode) {
				*psNode = NULL;
				psNode = NULL;
			    }
			    if (prNode) {
				*prNode = NULL;
				prNode = NULL;
			    }
#if USE_OFFER_FILTER
			    if (pofNode) {
				*pofNode = NULL;
				pofNode = NULL;
			    }
#endif  /* USE_OFFER_FILTER */

			    if (nf) {
				if (inGroupDef) {
				    nf->nf_Next = GRBase;
				    GRBase = nf;
				} else if (strcmp(nf->nf_Label, "ISPAM") != 0 &&
					strcmp(nf->nf_Label, "ESPAM") != 0 &&
					strcmp(nf->nf_Label, "IFILTER") != 0) {
				    nf->nf_Next = NFBase;
				    NFBase = nf;
				}
				if (altFi) {
				    AltFeed(nf, altFi);
				    fclose(altFi);
				    altFi = NULL;
				}
			    }
			    nf = NULL;
			}

			inGroupDef = 0;

			/*
			 * If we are loading a particular label, it must exist.
			 */

			if (s2 &&
				(SaveHLabel == NULL ||
				strcmp(s2, SaveHLabel) == 0 ||
				strcmp(s2, "GLOBAL") == 0 ||
				strcmp(s2, "ISPAM") == 0 ||
				strcmp(s2, "ESPAM") == 0 ||
				strcmp(s2, "IFILTER") == 0
			)) {
			    char path[256];

			    nf = zalloc(&NFMemPool, sizeof(NewsFeed) + strlen(s2) + 1);
			    nf->nf_Label = (char *)(nf + 1);
			    nf->nf_ArtTypes = NULL;
			    strcpy(nf->nf_Label, s2);
			    pfNode = &nf->nf_FilterBase;
			    paNode = &nf->nf_PathAliasBase;
			    psaNode = &nf->nf_SpamPathAliasBase;
			    pgNode = &nf->nf_GroupAcceptBase;
			    psNode = &nf->nf_SpamBase;
			    prNode = &nf->nf_RequireGroupsBase;
#if USE_OFFER_FILTER
			    pofNode = &nf->nf_OfferFilterBase;
#endif  /* USE_OFFER_FILTER */

			    setLinkStart(&nf->nf_LinkInfo);

			    snprintf(path, sizeof(path), "%s/%s", PatExpand(FeedsHomePat), s2);
			    altFi = fopen(path, "r");
			    err = 0;
			    if (strcmp(nf->nf_Label, "GLOBAL") == 0) {
				NFGlob = nf;
				setLinkGLOBAL(&nf->nf_LinkInfo);
			    } else {
				char *gl = "GLOBAL";

				if (strcmp(s2, "ISPAM") == 0)
				    ISBase = nf;
				else if (strcmp(s2, "ESPAM") == 0)
				    ESBase = nf;
				else if (strcmp(s2, "IFILTER") == 0)
				    IFBase = nf;

				MakeNodeAppList(&pgNode, &NFMemPool, gl, 2);
				MakeNodeAppList(&paNode, &NFMemPool, gl, 2);
				MakeNodeAppList(&psNode, &NFMemPool, gl, 2);
				MakeNodeAppList(&prNode, &NFMemPool, gl, 2);
				MakeNodeAppList(&pfNode, &NFMemPool, gl, 2);
#if USE_OFFER_FILTER
				MakeNodeAppList(&pofNode, &NFMemPool, gl, 2);
#endif  /* USE_OFFER_FILTER */
			    }
			}
		    } else if (strcmp(s1, "alias") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&paNode, &NFMemPool, s2, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "spamalias") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&psaNode, &NFMemPool, s2, 0);
			    err = 0;
			    UseSpamAlias = 1;
			}
		    } else if (strcmp(s1, "adddist") == 0) {
			if (nf && s2) {
			    zappendStr(&NFMemPool, &nf->nf_Dist, ",", s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "deldist") == 0) {
			if (nf && s2) {
			    zappendStr(&NFMemPool, &nf->nf_NoDist, ",", s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "rtflush") == 0) {
			if (nf) {
			    nf->nf_PerLineFlushOpt = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "nortflush") == 0) {
			if (nf) {
			    nf->nf_PerLineFlushOpt = -1;
			    err = 0;
			}
		    } else if (strcmp(s1, "nospam") == 0) {
			if (nf) {
			    nf->nf_SpamFeedOpt = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "onlyspam") == 0) {
			if (nf) {
			    nf->nf_SpamFeedOpt = 2;
			    err = 0;
			}
		    } else if (strcmp(s1, "nomismatch") == 0) {
			if (nf) {
			    nf->nf_NoMisMatch = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "domismatch") == 0) {
			if (nf) {
			    nf->nf_NoMisMatch = -1;
			    err = 0;
			}
		    } else if (strcmp(s1, "filter") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&pfNode, &NFMemPool, s2, 1);
			    err = 0;
			}
		    } else if (strcmp(s1, "nofilter") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&pfNode, &NFMemPool, s2, -1);
			    err = 0;
			}
#if USE_OFFER_FILTER
		    } else if (strcmp(s1, "offerfilter") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&pofNode, &NFMemPool, s2, 1);
			    err = 0;
			}
		    } else if (strcmp(s1, "noofferfilter") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&pofNode, &NFMemPool, s2, 0);
			    err = 0;
			}
#endif  /* USE_OFFER_FILTER */
		    } else if (strcmp(s1, "maxconnect") == 0) {
			if (nf && s2) {
			    nf->nf_MaxConnect = strtol(s2, NULL, 0);
			    err = 0;
			    if (nf->nf_MaxConnect && DOpts.MaxPerRemote == 0)
				DOpts.MaxPerRemote = -1;
			}
		    } else if (strcmp(s1, "maxinboundrate") == 0) {
			if (nf && s2) {
			    nf->nf_MaxInboundRate = zallocStr(&NFMemPool, s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "mincross") == 0) {
			if (nf && s2) {
			    nf->nf_MinCrossPost = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "maxcross") == 0) {
			if (nf && s2) {
			    nf->nf_MaxCrossPost = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "minpath") == 0) {
			if (nf && s2) {
			    nf->nf_MinPathLen = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "maxpath") == 0) {
			if (nf && s2) {
			    nf->nf_MaxPathLen = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "minsize") == 0) {
			if (nf && s2) {
			    nf->nf_MinArtSize = bsizetol(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "maxsize") == 0) {
			if (nf && s2) {
			    nf->nf_MaxArtSize = bsizetol(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "groupdef") == 0) {
			if (nf) {
			    logit(LOG_CRIT,
			       "Newsfeed config line %d, no end before new groupdef!\n", 
				lineNo
			    );
			    if (pfNode) {
				*pfNode = NULL;
				pfNode = NULL;
			    }
			    if (paNode) {
				*paNode = NULL;
				paNode = NULL;
			    }
			    if (psaNode) {
				*psaNode = NULL;
				psaNode = NULL;
			    }
			    if (pgNode) {
				*pgNode = NULL;
				pgNode = NULL;
			    }
			    if (psNode) {
				*psNode = NULL;
				psNode = NULL;
			    }
			    if (prNode) {
				*prNode = NULL;
				prNode = NULL;
			    }
#if USE_OFFER_FILTER
			    if (pofNode) {
				*pofNode = NULL;
				pofNode = NULL;
			    }
#endif  /* USE_OFFER_FILTER */

			    if (inGroupDef) {
				nf->nf_Next = GRBase;
				GRBase = nf;
			    } else if (strcmp(nf->nf_Label, "ISPAM") != 0 &&
					strcmp(nf->nf_Label, "ESPAM") != 0 &&
					strcmp(nf->nf_Label, "IFILTER") != 0) {
				nf->nf_Next = NFBase;
				NFBase = nf;
			    }
			    if (altFi) {
				AltFeed(nf, altFi);
				fclose(altFi);
				altFi = NULL;
			    }
			    nf = NULL;
			}

			inGroupDef = 1;

			if (s2) {
			    nf = zalloc(&NFMemPool, sizeof(NewsFeed) + strlen(s2) + 1);
			    nf->nf_Label = (char *)(nf + 1);
			    nf->nf_ArtTypes = NULL;
			    strcpy(nf->nf_Label, s2);
			    pfNode = &nf->nf_FilterBase;
			    paNode = &nf->nf_PathAliasBase;
			    psaNode = &nf->nf_SpamPathAliasBase;
			    pgNode = &nf->nf_GroupAcceptBase;
			    psNode = &nf->nf_SpamBase;
			    prNode = &nf->nf_RequireGroupsBase;
#if USE_OFFER_FILTER
			    pofNode = &nf->nf_OfferFilterBase;
#endif  /* USE_OFFER_FILTER */
			    err = 0;
			}
		    } else if (strcmp(s1, "groupref") == 0) {
			if (nf && s2) {
			    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, 2);
			    (void)MakeNodeAppList(&paNode, &NFMemPool, s2, 2);
			    (void)MakeNodeAppList(&psaNode, &NFMemPool, s2, 2);
			    (void)MakeNodeAppList(&psNode, &NFMemPool, s2, 2);
			    (void)MakeNodeAppList(&prNode, &NFMemPool, s2, 2);
			    (void)MakeNodeAppList(&pfNode, &NFMemPool, s2, 2);
#if USE_OFFER_FILTER
			    (void)MakeNodeAppList(&pofNode, &NFMemPool, s2, 2);
#endif  /* USE_OFFER_FILTER */
			    err = 0;
			}
		    } else if (strcmp(s1, "delgroupany") == 0) {
			if (pgNode && s2) {
			    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, -2);
			    err = 0;
			}
		    } else if (strcmp(s1, "delgroup") == 0) {
			if (pgNode && s2) {
			    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, -1);
			    err = 0;
			}
		    } else if (strcmp(s1, "requiregroup") == 0) {
			if (prNode && s2) {
			    (void)MakeNodeAppList(&prNode, &NFMemPool, s2, 1);
			    err = 0;
			}
		    } else if (strcmp(s1, "addgroup") == 0) {
			if (pgNode && s2) {
			    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, 1);
			    err = 0;
			}
		    } else if (strcmp(s1, "delspam") == 0) {
			if (psNode && s2) {
			    (void)MakeNodeAppList(&psNode, &NFMemPool, s2, -1);
			    err = 0;
			}
		    } else if (strcmp(s1, "addspam") == 0) {
			if (psNode && s2) {
			    (void)MakeNodeAppList(&psNode, &NFMemPool, s2, 1);
			    err = 0;
			}
		    } else if (strcmp(s1, "throttle_delay") == 0) {
			if (nf && s2) {
			    nf->nf_ThrottleDelay = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "throttle_lines") == 0) {
			if (nf && s2) {
			    nf->nf_ThrottleLines = strtol(s2, NULL, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "feedertxbuf") == 0) {
			if (nf) {
			    nf->nf_FeederTXBuf = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "feederrxbuf") == 0) {
			if (nf) {
			    nf->nf_FeederRXBuf = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "readonly") == 0) {
			if (nf) {
			    nf->nf_ReadOnly = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "allow_readonly") == 0) {
			if (nf) {
			    logit(LOG_CRIT, 
				"Newsfeed config line %d, allow_readonly ignored, use R flag in dserver.hosts\n",
				lineNo
			    );
			    err = 0;
			}
		    } else if (strcmp(s1, "whereis") == 0) {
			if (nf) {
			    nf->nf_WhereIs = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "incomingpriority") == 0) {
			if (nf) {
			    nf->nf_IncomingPriority = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "precomreject") == 0) {
			if (nf) {
			    nf->nf_PrecommitReject = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "noprecomreject") == 0) {
			if (nf) {
			    nf->nf_PrecommitReject = -1;
			    err = 0;
			}
		    } else if (strcmp(s1, "startdelay") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_StartDelay = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "delayfeed") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_DelayFeed = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "delayinfeed") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_DelayInFeed = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "transmitbuf") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_TransmitBuf = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "receivebuf") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_ReceiveBuf = atoi(s2);
			    err = 0;
			}
		    /* setqos is now deprecated */
		    } else if ((strcmp(s1, "setqos") == 0) ||
			       (strcmp(s1, "settos") == 0)) {
			if (nf) {
			    nf->nf_LinkInfo.li_TOS = strtol(s2,NULL,0); /* both hex & decimal */
			    err = 0;
			}
		    } else if (strcmp(s1, "maxparallel") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_MaxParallel = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "stream") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_NoStream = !enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "realtime") == 0) {
			if (nf && s2 != NULL) {
			    if (strcmp(s2, "flush") == 0) {
				nf->nf_PerLineFlushOpt = 1;
				nf->nf_LinkInfo.li_RealTime = 1;
			    } else if (strcmp(s2, "notify") == 0) {
				nf->nf_PerLineFlushOpt = 1;
				nf->nf_LinkInfo.li_RealTime = 1;
				nf->nf_LinkInfo.li_Notify = 1;
			    } else {
				nf->nf_LinkInfo.li_RealTime = enabled(s2);
			    }
			    err = 0;
			}
		    } else if (strcmp(s1, "notify") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_Notify = enabled(s2);
			    err = 0;
			}
		    } else if ((strcmp(s1, "maxqueuefile") == 0) ||
			       (strcmp(s1, "maxqueue") == 0)) {
			if (nf) {
			    nf->nf_LinkInfo.li_MaxQueueFile = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "headfeed") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_HeadFeed = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "preservebytes") == 0 ||
		    		strcmp(s1, "ignorebytes") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_PreserveBytes = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "genlines") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_GenLines = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "check") == 0) {
			if (nf && s2) {
			    if (strcmp(s2, "nortcheck") == 0) {
			    	nf->nf_LinkInfo.li_Check = 2;
			    } else {
				nf->nf_LinkInfo.li_Check = enabled(s2);
			    }
			    err = 0;
			}
		    } else if (strcmp(s1, "compress") == 0) {
			if (nf) {
#ifndef	USE_ZLIB
			    logit(LOG_NOTICE, "%s: compression not enabled - ignoring compress option",
						PatLibExpand(DNewsfeedsPat));
#endif
			    nf->nf_LinkInfo.li_Compress = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "maxstream") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_MaxStream = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "queueskip") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_QueueSkip = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "articlestat") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_ArticleStat = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "nobatch") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_NoBatch = enabled(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "priority") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_Priority = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "port") == 0) {
			if (nf) {
			    nf->nf_LinkInfo.li_Port = atoi(s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "hostname") == 0) {
			if (nf && s2) {
			    nf->nf_LinkInfo.li_HostName = zallocStr(&NFMemPool, s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "bindaddress") == 0) {
			if (nf && s2) {
			    nf->nf_LinkInfo.li_BindAddress = zallocStr(&NFMemPool, SanitiseAddr(s2));
			    err = 0;
			}
		    } else if (strcmp(s1, "logarts") == 0) {
			if (nf && s2) {
			    zappendStr(&NFMemPool, &nf->nf_LinkInfo.li_LogArts, ",", s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "hours") == 0) {
			if (nf && s2) {
			    zappendStr(&NFMemPool, &nf->nf_LinkInfo.li_Hours, ",", s2);
			    err = 0;
			}
		    } else if (strcmp(s1, "arttypes") == 0) {
			if (nf && s2) {
			    char *p = s2;
			    ArtTypeList *atp = NULL;
			    ArtTypeList *tatp;
			    while ((p = strsep(&s2, " \t:,")) != NULL) {
				tatp = zalloc(&NFMemPool, sizeof(ArtTypeList));
				tatp->negate = 0;
				if (*p == '!') {
				    tatp->negate = 1;
				    p++;
				}
				tatp->arttype = ArtTypeConv(p);
				tatp->next = NULL;
				if (atp != NULL)
				    atp->next = tatp;
				atp = tatp;
				if (nf->nf_ArtTypes == NULL)
				    nf->nf_ArtTypes = atp;
			    }
			    err = 0;
			}
		    } else if (strcmp(s1, "hashfeed") == 0) {
			if (nf && s2) {
			    err = 0;
			    if (! ((nf->nf_HashFeed = DiabHashFeedParse(&NFMemPool, s2)))) {
			        logit(LOG_CRIT,
			           "Newsfeed config line %d, hash parse error!\n", 
				    lineNo
			        );
			    }
			}
		    } else if (strcmp(s1, "inhost") == 0) {
			/* Used in lib/hostauth.c */
			err = 0;
		    } else if (strcmp(s1, "host") == 0) {
			/* Also used in lib/hostauth.c */
			if (nf && s2) {
			    nf->nf_LinkInfo.li_HostName = zallocStr(&NFMemPool, s2);
			    (void)MakeNodeAppList(&paNode, &NFMemPool, s2, 0);
			    err = 0;
			}
		    } else if (strcmp(s1, "end") == 0) {
			if (nf) {
			    *paNode = NULL;
			    *psaNode = NULL;
			    *pgNode = NULL;
			    *psNode = NULL;
			    *prNode = NULL;
			    *pfNode = NULL;
#if USE_OFFER_FILTER
			    *pofNode = NULL;
#endif  /* USE_OFFER_FILTER */

			    paNode = NULL;
			    psaNode = NULL;
			    pgNode = NULL;
			    psNode = NULL;
			    prNode = NULL;
			    pfNode = NULL;
#if USE_OFFER_FILTER
			    pofNode = NULL;
#endif  /* USE_OFFER_FILTER */

			    if (inGroupDef ||
					strcmp(nf->nf_Label, "GLOBAL") == 0) {
				nf->nf_Next = GRBase;
				GRBase = nf;
			    } else if (strcmp(nf->nf_Label, "ISPAM") != 0 &&
					strcmp(nf->nf_Label, "ESPAM") != 0 &&
					strcmp(nf->nf_Label, "IFILTER") != 0) {
				nf->nf_Next = NFBase;
				NFBase = nf;
			    }
			    if (altFi) {
				AltFeed(nf, altFi);
				fclose(altFi);
				altFi = NULL;
			    }
			    nf = NULL;
			    err = 0;
			}
		    } else {
			logit(LOG_CRIT, 
			    "Newsfeed config line %d, unknown command\n", 
			    lineNo
			);
			err = 0;
		    }

		    /*
		     * deal with errors inside active labels (nf != NULL) or
		     * general errors when parsing the entire file (hlabel == NULL)
		     */

		    if (err && (nf != NULL || SaveHLabel == NULL)) {
			logit(LOG_ERR, "Newsfeed config line %d, command in unexpected position or command requires argument", lineNo);
		    }
		}
		if (nf) {
		    *paNode = NULL;
		    *psaNode = NULL;
		    *pgNode = NULL;
		    *psNode = NULL;
		    *prNode = NULL;
		    *pfNode = NULL;
#if USE_OFFER_FILTER
		    *pofNode = NULL;
#endif  /* USE_OFFER_FILTER */

		    paNode = NULL;
		    psaNode = NULL;
		    pgNode = NULL;
		    psNode = NULL;
		    prNode = NULL;
		    pfNode = NULL;
#if USE_OFFER_FILTER
		    pofNode = NULL;
#endif  /* USE_OFFER_FILTER */

		    if (inGroupDef) {
			nf->nf_Next = GRBase;
			GRBase = nf;
		    } else if (strcmp(nf->nf_Label, "ISPAM") != 0 &&
				strcmp(nf->nf_Label, "ESPAM") != 0 &&
				strcmp(nf->nf_Label, "IFILTER") != 0) {
			nf->nf_Next = NFBase;
			NFBase = nf;
		    }
		    nf = NULL;
		}
		if (altFi) {
		    fclose(altFi);
		    altFi = NULL;
		}
	    }
	}
	if (fi)
	    iclose(fi);

	/*
	 * Resolve NewsFeed references after the fact.  This allows
	 * grouplist definitions to put after newsfeed definitions.
	 */
	{
	    NewsFeed *nf;

	    for (nf = NFBase; nf; nf = nf->nf_Next) {
		resolveGroupList(nf->nf_Label, nf->nf_PathAliasBase);
		resolveGroupList(nf->nf_Label, nf->nf_SpamPathAliasBase);
		resolveGroupList(nf->nf_Label, nf->nf_GroupAcceptBase);
		resolveGroupList(nf->nf_Label, nf->nf_FilterBase);
		resolveGroupList(nf->nf_Label, nf->nf_SpamBase);
		resolveGroupList(nf->nf_Label, nf->nf_RequireGroupsBase);
#if USE_OFFER_FILTER
		resolveGroupList(nf->nf_Label, nf->nf_OfferFilterBase);
#endif  /* USE_OFFER_FILTER */
	    }
	    for (nf = GRBase; nf; nf = nf->nf_Next) {
		resolveGroupList(nf->nf_Label, nf->nf_PathAliasBase);
		resolveGroupList(nf->nf_Label, nf->nf_SpamPathAliasBase);
		resolveGroupList(nf->nf_Label, nf->nf_GroupAcceptBase);
		resolveGroupList(nf->nf_Label, nf->nf_FilterBase);
		resolveGroupList(nf->nf_Label, nf->nf_SpamBase);
		resolveGroupList(nf->nf_Label, nf->nf_RequireGroupsBase);
#if USE_OFFER_FILTER
		resolveGroupList(nf->nf_Label, nf->nf_OfferFilterBase);
#endif  /* USE_OFFER_FILTER */
	    }

	    /*
	     * This recursively resolves defaults for all groupdef's, we
	     * can scan any of our lists to find the groups so we use the
	     * one that is most likely to be short.
	     *
	     * We must also resolve global defaults in
	     */

	    for (nf = NFBase; nf; nf = nf->nf_Next) {
		resolveDefaults(nf, nf->nf_FilterBase);
		if (NFGlob)
		    resolveDefaults(nf, NFGlob->nf_FilterBase);
	    }

	    /*
	     * This isn't really necessary, we should have resolved them
	     * all above, but if we use groups for something else in the
	     * future we need to make sure the recursive grouprefs are
	     * resolved.
	     */
	    for (nf = GRBase; nf; nf = nf->nf_Next) {
		resolveDefaults(nf, nf->nf_FilterBase);
	    }
	}
    }
}

void
resolveGroupList(const char *label, Node *scan)
{
    for (; scan; scan = scan->no_Next) {
	if (scan->no_Value == 2) {
	    NewsFeed *gl;

	    for (gl = GRBase; gl; gl = gl->nf_Next) {
		if (strcmp(scan->no_Name, gl->nf_Label) == 0) {
		    scan->no_Data = gl;
		    break;
		}
	    }
	    if (gl == NULL) {
		logit(LOG_CRIT, "Error: grouplist %s does not exist (from %s)\n", scan->no_Name, label);
	    }
	}
    }
}

void
AltFeed(NewsFeed *nf, FILE *fi)
{
    char buf[MAXGNAME+256];
    Node **pgNode = &nf->nf_GroupAcceptBase;
    Node **psNode = &nf->nf_SpamBase;
    Node **prNode = &nf->nf_RequireGroupsBase;

    while (*pgNode)
	pgNode = &(*pgNode)->no_Next;
    while (*psNode)
	psNode = &(*psNode)->no_Next;

    while (fgets(buf, sizeof(buf), fi) != NULL) {
	char *s1 = strtok(buf, " \t\n");
	char *s2 = strtok(NULL, " \t\n");

	if (s1 == NULL)
	    continue;
	if (s2 == NULL)
	    continue;
	if (strcmp(s1, "addgroup") == 0) {
	    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, 1);
	} else if (strcmp(s1, "delgroup") == 0) {
	    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, -1);
	} else if (strcmp(s1, "requiregroup") == 0) {
	    (void)MakeNodeAppList(&prNode, &NFMemPool, s2, 1);
	} else if (strcmp(s1, "addspam") == 0) {
	    (void)MakeNodeAppList(&psNode, &NFMemPool, s2, 1);
	} else if (strcmp(s1, "delspam") == 0) {
	    (void)MakeNodeAppList(&psNode, &NFMemPool, s2, -1);
	} else if (strcmp(s1, "delgroupany") == 0) {
	    (void)MakeNodeAppList(&pgNode, &NFMemPool, s2, -2);
	} else if (strcmp(s1, "maxsize") == 0) {
	    nf->nf_MaxArtSize = bsizetol(s2);
	} else if (strcmp(s1, "minsize") == 0) {
	    nf->nf_MinArtSize = bsizetol(s2);
	}
    }
    *pgNode = NULL;
    *psNode = NULL;
    *prNode = NULL;
}

int
TooNear(time_t t)
{
    time_t now = time(NULL);
    int32 dt = (int32)(now - t);

    if (dt > -10 && dt < 10)
	return(1);
    return(0);
}

int 
feedSpamFeedOK(int feed, int article)
{
      if (! feed) {
              return(0);
      }
      if (feed == 1) {
              if (article == 1) {
                      return(1);
              }
              return(0);
      }
      if (feed == 2) {
              if (article == 0) {
                      return(1);
              }
              return(0);
      }
      return(0);
}

/*
 * Set the starting values for GLOBAL - these can be overwritten
 */
void
setLinkGLOBAL(NewslinkInfo *nf)
{
    nf->li_Port = 119;
    nf->li_StartDelay = 0;
    nf->li_TransmitBuf = 0;
    nf->li_ReceiveBuf = 0;
    nf->li_TOS = 0;
    nf->li_MaxParallel = 2;
    nf->li_NoStream = 0;
    nf->li_DelayFeed = 0;
    nf->li_DelayInFeed = 0;
    nf->li_RealTime = 0;
    nf->li_Notify = 0;
    nf->li_MaxQueueFile = 0;
    nf->li_HeadFeed = 0;
    nf->li_PreserveBytes = 0;
    nf->li_GenLines = 0;
    nf->li_Check = 1;
    nf->li_Compress = 0;
    nf->li_QueueSkip = 0;
    nf->li_ArticleStat = 0;
    nf->li_NoBatch = 0;
    nf->li_MaxStream = MAXSTREAM;
    nf->li_Priority = 0;
    nf->li_LogArts = NULL;
    nf->li_Hours = NULL;
}

/*
 * Set the initial values for each newsfeed link entry. These will
 * all get overwritten by the GLOBAL or user specified entries.
 *
 * They are set to -1, because we need to use 0
 */
void
setLinkStart(NewslinkInfo *nf)
{
    nf->li_Port = -1;
    nf->li_StartDelay = -1;
    nf->li_TransmitBuf = -1;
    nf->li_ReceiveBuf = -1;
    nf->li_TOS = -1;
    nf->li_BindAddress = NULL;
    nf->li_MaxParallel = -1;
    nf->li_NoStream = -1;
    nf->li_DelayFeed = -1;
    nf->li_DelayInFeed = -1;
    nf->li_RealTime = -1;
    nf->li_Notify = -1;
    nf->li_MaxQueueFile = -1;
    nf->li_HeadFeed = -1;
    nf->li_PreserveBytes = -1;
    nf->li_GenLines = -1;
    nf->li_Check = -1;
    nf->li_Compress = -1;
    nf->li_QueueSkip = -1;
    nf->li_ArticleStat = -1;
    nf->li_NoBatch = -1;
    nf->li_MaxStream = -1;
    nf->li_Priority = -1;
    nf->li_LogArts = NULL;
    nf->li_Hours = NULL;
}

void
setLinkDefaults(NewslinkInfo *nf, NewslinkInfo *gl)
{
    if (nf->li_Port < 0 && gl->li_Port >= 0)
	nf->li_Port = gl->li_Port;
    if (nf->li_StartDelay < 0 && gl->li_StartDelay >= 0)
	nf->li_StartDelay = gl->li_StartDelay;
    if (nf->li_TransmitBuf < 0 && gl->li_TransmitBuf >= 0)
	nf->li_TransmitBuf = gl->li_TransmitBuf;
    if (nf->li_ReceiveBuf < 0 && gl->li_ReceiveBuf >= 0)
	nf->li_ReceiveBuf = gl->li_ReceiveBuf;
    if (nf->li_TOS < 0 && gl->li_TOS >= 0)
	nf->li_TOS = gl->li_TOS;
    if (!nf->li_BindAddress && gl->li_BindAddress)
	nf->li_BindAddress = gl->li_BindAddress;
    if (nf->li_MaxParallel < 0 && gl->li_MaxParallel >= 0)
	nf->li_MaxParallel = gl->li_MaxParallel;
    if (nf->li_NoStream < 0 && gl->li_NoStream >= 0)
	nf->li_NoStream = gl->li_NoStream;
    if (nf->li_DelayFeed < 0 && gl->li_DelayFeed >= 0)
	nf->li_DelayFeed = gl->li_DelayFeed;
    if (nf->li_DelayInFeed < 0 && gl->li_DelayInFeed >= 0)
	nf->li_DelayInFeed = gl->li_DelayInFeed;
    if (nf->li_RealTime < 0 && gl->li_RealTime >= 0)
	nf->li_RealTime = gl->li_RealTime;
    if (nf->li_Notify < 0 && gl->li_Notify >= 0)
	nf->li_Notify = gl->li_Notify;
    if (nf->li_MaxQueueFile < 0 && gl->li_MaxQueueFile >= 0)
	nf->li_MaxQueueFile = gl->li_MaxQueueFile;
    if (nf->li_HeadFeed < 0 && gl->li_HeadFeed >= 0)
	nf->li_HeadFeed = gl->li_HeadFeed;
    if (nf->li_PreserveBytes < 0 && gl->li_PreserveBytes >= 0)
	nf->li_PreserveBytes = gl->li_PreserveBytes;
    if (nf->li_GenLines < 0 && gl->li_GenLines >= 0)
	nf->li_GenLines = gl->li_GenLines;
    if (nf->li_Check < 0 && gl->li_Check >= 0)
	nf->li_Check = gl->li_Check;
    if (nf->li_Compress < 0 && gl->li_Compress >= 0)
	nf->li_Compress = gl->li_Compress;
    if (nf->li_QueueSkip < 0 && gl->li_QueueSkip >= 0)
	nf->li_QueueSkip = gl->li_QueueSkip;
    if (nf->li_ArticleStat < 0 && gl->li_ArticleStat >= 0)
	nf->li_ArticleStat = gl->li_ArticleStat;
    if (nf->li_NoBatch < 0 && gl->li_NoBatch >= 0)
	nf->li_NoBatch = gl->li_NoBatch;
    if (nf->li_MaxStream < 0 && gl->li_MaxStream >= 0)
	nf->li_MaxStream = gl->li_MaxStream;
    if (nf->li_Priority < 0 && gl->li_Priority >= 0)
	nf->li_Priority = gl->li_Priority;
    if (!nf->li_LogArts && gl->li_LogArts)
	nf->li_LogArts = gl->li_LogArts;
    if (!nf->li_Hours && gl->li_Hours)
	nf->li_Hours = gl->li_Hours;
}
/*
 * resolveDefaults() - resolve groupref recursion for simple defaults.
 *
 *	We have to recurse our resolution.  This code resolves simple
 *	defaults:  Distribution patterns, integer values, and so forth.
 */

void
resolveDefaults(NewsFeed *nf, Node *no)
{
    nf->nf_Resolved = 1;

    while (no) {
	if (no->no_Value == 2 && no->no_Data != NULL) {
	    NewsFeed *gl = no->no_Data;

	    if (gl->nf_Resolved == 0)
		resolveDefaults(gl, gl->nf_FilterBase);

	    if (!nf->nf_MaxCrossPost && gl->nf_MaxCrossPost)
		nf->nf_MaxCrossPost = gl->nf_MaxCrossPost;
	    if (!nf->nf_MinCrossPost && gl->nf_MinCrossPost)
		nf->nf_MinCrossPost = gl->nf_MinCrossPost;
	    if (!nf->nf_MaxArtSize && gl->nf_MaxArtSize)
		nf->nf_MaxArtSize = gl->nf_MaxArtSize;
	    if (!nf->nf_MinArtSize && gl->nf_MinArtSize)
		nf->nf_MinArtSize = gl->nf_MinArtSize;
	    if (!nf->nf_MaxPathLen && gl->nf_MaxPathLen)
		nf->nf_MaxPathLen = gl->nf_MaxPathLen;
	    if (!nf->nf_MinPathLen && gl->nf_MinPathLen)
		nf->nf_MinPathLen = gl->nf_MinPathLen;
	    if (!nf->nf_MaxConnect && gl->nf_MaxConnect)
		nf->nf_MaxConnect = gl->nf_MaxConnect;
	    if (!nf->nf_MaxInboundRate && gl->nf_MaxInboundRate)
	        nf->nf_MaxInboundRate = zallocStr(&NFMemPool, gl->nf_MaxInboundRate);
	    if (nf->nf_ArtTypes == NULL && gl->nf_ArtTypes)
		nf->nf_ArtTypes = gl->nf_ArtTypes;

	    if (nf->nf_Dist == NULL && gl->nf_Dist)
		zappendStr(&NFMemPool, &nf->nf_Dist, NULL, gl->nf_Dist);
	    if (nf->nf_NoDist == NULL && gl->nf_NoDist)
		zappendStr(&NFMemPool, &nf->nf_NoDist, NULL, gl->nf_NoDist);

	    if (!nf->nf_PerLineFlushOpt && gl->nf_PerLineFlushOpt)
		nf->nf_PerLineFlushOpt = gl->nf_PerLineFlushOpt;
	    if (!nf->nf_NoMisMatch && gl->nf_NoMisMatch)
		nf->nf_NoMisMatch = gl->nf_NoMisMatch;
	    if (!nf->nf_SpamFeedOpt && gl->nf_SpamFeedOpt)
		nf->nf_SpamFeedOpt = gl->nf_SpamFeedOpt;

	    if (!nf->nf_ThrottleDelay && gl->nf_ThrottleDelay)
		nf->nf_ThrottleDelay = gl->nf_ThrottleDelay;
	    if (!nf->nf_ThrottleLines && gl->nf_ThrottleLines)
		nf->nf_ThrottleLines = gl->nf_ThrottleLines;
	    if (!nf->nf_FeederTXBuf && gl->nf_FeederTXBuf)
		nf->nf_FeederTXBuf = gl->nf_FeederTXBuf;
	    if (!nf->nf_FeederRXBuf && gl->nf_FeederRXBuf)
		nf->nf_FeederRXBuf = gl->nf_FeederRXBuf;
	    if (!nf->nf_ReadOnly && gl->nf_ReadOnly)
		nf->nf_ReadOnly = gl->nf_ReadOnly;
	    if (!nf->nf_WhereIs && gl->nf_WhereIs)
		nf->nf_WhereIs = gl->nf_WhereIs;
	    if (!nf->nf_IncomingPriority && gl->nf_IncomingPriority)
		nf->nf_IncomingPriority = gl->nf_IncomingPriority;
	    if (!nf->nf_PrecommitReject && gl->nf_PrecommitReject)
		nf->nf_PrecommitReject = gl->nf_PrecommitReject;

	    setLinkDefaults(&nf->nf_LinkInfo, &gl->nf_LinkInfo);

	}
	no = no->no_Next;
    }
}

int
recursiveScan(NewsFeed *feed, int off, const void *data, int (*callback)(Node *node, const void *data, int *pabort, int def), int def)
{
    int r = def;
    int abortMe = 0;
    Node *node;

    if (NFGlob && NFGlob != feed)
	r = recursiveScan(NFGlob, off, data, callback, r);

    for (node = *(Node **)((char *)feed + off); node; node = node->no_Next) {
	if (node->no_Value == 2) {
	    if (++RecurCount == MAXRECURSION) {
		if (RecurWarn == 0) {
		    RecurWarn = 1;
		    logit(LOG_EMERG, "Infinite recursion in dnewsfeeds file!");
		}
	    } else {
		if (node->no_Data != NULL)
		    r = recursiveScan(node->no_Data, off, data, callback, r);
	    } 
	    --RecurCount;
	} else {
	    r = callback(node, data, &abortMe, r);
	}
	if (abortMe)
	    break;
    }
    return(r);
}

int
cbWildCmpStopWhenFound(Node *node, const void *data, int *pabort, int def)
{
    if (WildCmp(node->no_Name, (char *)data) == 0) {
	def = node->no_Value;
	*pabort = 1;
    }
    return(def);
}

int
cbWildCmpNoStop(Node *node, const void *data, int *pabort, int def)
{
    if (WildCmp(node->no_Name, (char *)data) == 0) {
	def = node->no_Value;
    }
    return(def);
}

LabelList *
FeedLinkLabelList(void)
{
    NewsFeed *feed;
    LabelList *ll = NULL;
    LabelList *tl;
    LabelList *ptl = NULL;

    if (NFBase) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (feed && feed->nf_LinkInfo.li_HostName) {
		tl = zalloc(&NFMemPool, sizeof(LabelList));
		tl->label = feed->nf_Label;
		if (ptl)
		    ptl->next = tl;
		ptl = tl;
		if (ll == NULL)
		    ll = tl;
	    }
	}
    }
    return(ll);
}

NewslinkInfo *
FeedLinkInfo(const char *hlabel)
{
    NewsFeed *feed;

    for (feed = NFBase; feed; feed = feed->nf_Next) {
	if (strcmp(hlabel, feed->nf_Label) == 0)
	    break;
    }

    if (feed)
	return(&feed->nf_LinkInfo);
    else
	return NULL;
}

int
FeedFilter(char *hlabel, const char *nglist, const char *npath, const char *dist, const char *artType, int bytes)
{
    NewsFeed *nf = NULL;

    nf = IFBase;

    if (nf == NULL)
	return(0);

    if (feedQueryPaths(nf, npath, bytes, strtol(artType, NULL, 16)) == 0 &&
				feedQueryGroups(nf, nglist) == 0 &&
				feedQueryDists(nf, dist) == 0) {
	return(1);
    }
    return(0);
}

int
FeedSpam(int which, const char *nglist, const char *npath, const char *dist, const char *artType, int bytes)
{
    NewsFeed *nf = NULL;

    if (which == 1)
	nf = ISBase;
    else if (which == 2)
	nf = ESBase;

    if (nf == NULL)
	return(0);

    if (feedQueryPaths(nf, npath, bytes, strtol(artType, NULL, 16)) == 0 &&
				feedQueryGroups(nf, nglist) == 0 &&
				feedQueryDists(nf, dist) == 0) {
	return(1);
    }
    return(0);
}

/*
 * Write the current max inbound rate into *bps
 */

void
FeedGetMaxInboundRate(const char *hlabel, int *bps)
{
    NewsFeed *feed;

    if ((feed = NFCache) == NULL) {
	for (feed = NFBase; feed; feed = feed->nf_Next) {
	    if (strcmp(hlabel, feed->nf_Label) == 0)
		break;
	}
	NFCache = feed;
    }

    if (! feed || ! feed->nf_MaxInboundRate || ! *(feed->nf_MaxInboundRate)) {
	*bps = 0;
    } else {
	char *ptr = feed->nf_MaxInboundRate;
	char *p2;
	time_t now;
        struct tm *tm;

	/* Check for simple 24/7 rate limit - no colon */
	if (! (p2 = strchr(ptr, ':'))) {
	    *bps = atoi(ptr);
	} else {

	    /* It's a more complex rate limit */
	    int hours[24], bpstmp, hourtmp, hourendtmp, i;

	    /* Default is no limit for a given hour */
	    for (i = 0; i < 24; i++) {
		hours[i] = 0;
	    }

	    /* 
	     * Now parse a string of the format
	     * speed:hr-hr,hr;speed:hr,hr,hr-hr;etc
	     *
	     * I really, really, really, really hate writing character
	     * string parsers.  But it's fast (I hope) and correct (I
	     * think).  JG200103131808
	     */
	    while (*ptr) {
		/* We might be sitting on a colon from a previous iteration */
		while (*ptr == ';') {
		    ptr++;
		}

		/* "speed:" */
		bpstmp = atoi(ptr);
		while (*ptr && *ptr != ':') {
		    ptr++;
		}
		ptr++;

		/* Process hour range(s) */
		while (*ptr && *ptr != ';') {
		    p2 = ptr;
		    while (*ptr && *ptr >= '0' && *ptr <= '9') {
		        ptr++;
		    }
		    if (ptr == p2) {
		        logit(LOG_ERR, "Newsfeed entry %s having trouble making sense of MaxInboundRate expression %s (A%02x)", hlabel, feed->nf_MaxInboundRate, *ptr);
		    }
		    hourtmp = atoi(p2);
		    if (*ptr == ',' || *ptr == ';' || ! *ptr) {
			if (hourtmp < 0 || hourtmp > 23) {
		            logit(LOG_ERR, "Newsfeed entry %s having trouble making sense of MaxInboundRate expression %s (B%d)", hlabel, feed->nf_MaxInboundRate, hourtmp);
			} else {
			    hours[hourtmp] = bpstmp;
			}
			if (*ptr != ';') {
			    ptr++;
			}
		    } else if (*ptr == '-') {
			/* Move past dash */
			ptr++;
			p2 = ptr;
			/* Scan past second number */
		        while (*ptr && *ptr >= '0' && *ptr <= '9') {
		            ptr++;
		        }
		        if (ptr == p2) {
		            logit(LOG_ERR, "Newsfeed entry %s having trouble making sense of MaxInboundRate expression %s (C%02x)", hlabel, feed->nf_MaxInboundRate, *ptr);
		        }
		        hourendtmp = atoi(p2);
			if (hourtmp < 0 || hourtmp > 23 || hourendtmp < 0 || hourendtmp > 23 || hourtmp >= hourendtmp) {
		            logit(LOG_ERR, "Newsfeed entry %s having trouble making sense of MaxInboundRate expression %s (D%d/%d)", hlabel, feed->nf_MaxInboundRate, hourtmp, hourendtmp);
			} else {
			    for (i = hourtmp; i <= hourendtmp; i++) {
			        hours[i] = bpstmp;
			    }
			}
			if (*ptr != ';') {
			    ptr++;
			}
		    } else {
		        logit(LOG_ERR, "Newsfeed entry %s having trouble making sense of MaxInboundRate expression %s (E%02x)", hlabel, feed->nf_MaxInboundRate, *ptr);
			ptr++;
		    }
		}
	    }

	    /*
	     * What an incredible amount of putzing around that was.
	     * Now we ask the system what hour (0-23) it is, and set *bps
	     * accordingly
	     */

	    time(&now);
	    tm = localtime(&now);
	    *bps = hours[tm->tm_hour];
	}
    }
}


void
DumpAllFeedInfo(FILE *fo)
{
    NewsFeed *nf = NULL;

    fprintf(fo, "** GLOBAL **\n");
    DumpFeedInfo(fo, NFGlob->nf_Label);
    DumpFeedInfo(fo, "ISPAM");
    DumpFeedInfo(fo, "ESPAM");
    DumpFeedInfo(fo, "IFILTER");
    fprintf(fo, "** GROUPREFS **\n");
    for (nf = GRBase; nf; nf = nf->nf_Next) {
        DumpFeedInfo(fo, nf->nf_Label);
    }
    fprintf(fo, "** LABELS **\n");
    for (nf = NFBase; nf; nf = nf->nf_Next) {
        DumpFeedInfo(fo, nf->nf_Label);
    }
}

void
DumpFeedInfo(FILE *fo, char *label)
{
    NewsFeed *nf = NULL;
    HashFeed_MatchList *hfptr;
    int nhf;

    if (strcmp(label, "ISPAM") == 0)
	nf = ISBase;
    else if (strcmp(label, "ESPAM") == 0)
	nf = ESBase;
    else if (strcmp(label, "IFILTER") == 0)
	nf = IFBase;
    else for (nf = NFBase; nf; nf = nf->nf_Next) {
	if (strcmp(label, nf->nf_Label) == 0)
	    break;
    }
    if (nf == NULL)
	for (nf = GRBase; nf; nf = nf->nf_Next)
	    if (strcmp(label, nf->nf_Label) == 0)
		break;
    if (nf == NULL)
	return;
    fprintf(fo, "label %s\n", nf->nf_Label);
    fprintf(fo, "  MaxCrossPost   : %d\n", nf->nf_MaxCrossPost);
    fprintf(fo, "  MinCrossPost   : %d\n", nf->nf_MinCrossPost);
    fprintf(fo, "  MaxArtSize     : %d\n", nf->nf_MaxArtSize);
    fprintf(fo, "  MinArtSize     : %d\n", nf->nf_MinArtSize);
    fprintf(fo, "  MaxPathLen     : %d\n", nf->nf_MaxPathLen);
    fprintf(fo, "  MinPathLen     : %d\n", nf->nf_MinPathLen);
    fprintf(fo, "  MaxConnect     : %d\n", nf->nf_MaxConnect);
    fprintf(fo, "  MaxInboundRate : %s\n", nf->nf_MaxInboundRate ?
					nf->nf_MaxInboundRate : "-1");
    fprintf(fo, "  Dist           : %s\n", nf->nf_Dist ? nf->nf_Dist : "NONE");
    fprintf(fo, "  NoDist         : %s\n", nf->nf_NoDist ? nf->nf_NoDist : "NONE");
    fprintf(fo, "  PerLineFlushOpt: %d\n", nf->nf_PerLineFlushOpt);
    fprintf(fo, "  NoMisMatch     : %d\n", nf->nf_NoMisMatch);
    fprintf(fo, "  SpamFeedOpt    : %d\n", nf->nf_SpamFeedOpt);
    fprintf(fo, "  Resolved       : %d\n", nf->nf_Resolved);
    fprintf(fo, "  ThrottleDelay  : %d\n", nf->nf_ThrottleDelay);
    fprintf(fo, "  ThrottleLines  : %d\n", nf->nf_ThrottleLines);
    fprintf(fo, "  FeederTXBuf    : %d\n", nf->nf_FeederTXBuf);
    fprintf(fo, "  FeederRXBuf    : %d\n", nf->nf_FeederRXBuf);
    fprintf(fo, "  ReadOnly       : %d\n", nf->nf_ReadOnly);
    fprintf(fo, "  WhereIs        : %d\n", nf->nf_WhereIs);
    for (hfptr = nf->nf_HashFeed, nhf = 0; hfptr; hfptr = hfptr->HM_Next) {
	fprintf(fo, "  HashFeed       : %d %s%d-%d/%d:%d\n", nhf++, (hfptr->HM_Type == HMTYPE_OLD) ? "@" : "", hfptr->HM_Start, hfptr->HM_End, hfptr->HM_ModVal, hfptr->HM_Offset);
    }

    fprintf(fo, "  IncomingPriority: %d\n", nf->nf_IncomingPriority);
    fprintf(fo, "  PrecommitReject: %d\n", nf->nf_PrecommitReject);
#if USE_OFFER_FILTER
    fprintf(fo, "  OfferFilter    : ");
    {
	Node *n;
	for (n = nf->nf_OfferFilterBase; n != NULL; n = n->no_Next)
	    fprintf(fo, "%s%s,",
	    			(n->no_Value == 1) ? "" : /* not filtered */
	    			(n->no_Value == 0) ? "!" : "?",
	    			n->no_Name);
	fprintf(fo, "\n");
    }
#endif  /* USE_OFFER_FILTER */
    fprintf(fo, "  ArtTypes       : ");
    {
	ArtTypeList *at;
	for (at = nf->nf_ArtTypes; at != NULL; at = at->next) {
	    fprintf(fo, "%s%08x,", at->negate ? "!": "", at->arttype);
	    if (at->next != NULL)
		fprintf(fo, ",");
	}
	fprintf(fo, "\n");
    }
    fprintf(fo, "  GroupAccept    : ");
    {
	Node *n;
	for (n = nf->nf_GroupAcceptBase; n != NULL; n = n->no_Next)
	    fprintf(fo, "%s%s,",
				(n->no_Value == -2) ? "@" : 
				(n->no_Value == -1) ? "!" : 
				(n->no_Value == 0) ? "" : "?",
				n->no_Name);
	fprintf(fo, "\n");
    }
    fprintf(fo, "  Hostname       : %s\n", nf->nf_LinkInfo.li_HostName ?
					nf->nf_LinkInfo.li_HostName : "NONE");
    fprintf(fo, "  Port           : %d\n", nf->nf_LinkInfo.li_Port);
    fprintf(fo, "  StartDelay     : %d\n", nf->nf_LinkInfo.li_StartDelay);
    fprintf(fo, "  Headfeed       : %d\n", nf->nf_LinkInfo.li_HeadFeed);
    fprintf(fo, "  TransmitBuf    : %d\n", nf->nf_LinkInfo.li_TransmitBuf);
    fprintf(fo, "  ReceiveBuf     : %d\n", nf->nf_LinkInfo.li_ReceiveBuf);
#ifdef IP_TOS
    fprintf(fo, "  TOS            : %d\n", nf->nf_LinkInfo.li_TOS);
#endif
    fprintf(fo, "  BindAddress    : %s\n", nf->nf_LinkInfo.li_BindAddress ?
					nf->nf_LinkInfo.li_BindAddress : "ALL");
    fprintf(fo, "  MaxParallel    : %d\n", nf->nf_LinkInfo.li_MaxParallel);
    fprintf(fo, "  NoStream       : %d\n", nf->nf_LinkInfo.li_NoStream);
    fprintf(fo, "  DelayFeed      : %d\n", nf->nf_LinkInfo.li_DelayFeed);
    fprintf(fo, "  DelayInFeed    : %d\n", nf->nf_LinkInfo.li_DelayInFeed);
    fprintf(fo, "  RealTime       : %d\n", nf->nf_LinkInfo.li_RealTime);
    fprintf(fo, "  Notify         : %d\n", nf->nf_LinkInfo.li_Notify);
    fprintf(fo, "  MaxQueueFile   : %d\n", nf->nf_LinkInfo.li_MaxQueueFile);
    fprintf(fo, "  PreserveBytes  : %d\n", nf->nf_LinkInfo.li_PreserveBytes);
    fprintf(fo, "  GenLines       : %d\n", nf->nf_LinkInfo.li_GenLines);
    fprintf(fo, "  Check          : %d\n", nf->nf_LinkInfo.li_Check);
    fprintf(fo, "  MaxStream      : %d\n", nf->nf_LinkInfo.li_MaxStream);
    fprintf(fo, "  Priority       : %d\n", nf->nf_LinkInfo.li_Priority);
    fprintf(fo, "  Compress       : %d\n", nf->nf_LinkInfo.li_Compress);
    fprintf(fo, "  QueueSkip      : %d\n", nf->nf_LinkInfo.li_QueueSkip);
    fprintf(fo, "  ArticleStat    : %d\n", nf->nf_LinkInfo.li_ArticleStat);
    fprintf(fo, "  NoBatch        : %d\n", nf->nf_LinkInfo.li_NoBatch);
    fprintf(fo, "  Logarts        : %s\n", nf->nf_LinkInfo.li_LogArts ?
					nf->nf_LinkInfo.li_LogArts : "NONE");
    fprintf(fo, "  Hours          : %s\n", nf->nf_LinkInfo.li_Hours ?
					nf->nf_LinkInfo.li_Hours : "ALL");
    fprintf(fo, "end\n");
    fprintf(fo, "#------------------------------------------------------------\n");
}
