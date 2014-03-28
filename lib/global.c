
/*
 * LIB/GLOBAL.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype const char *NewsHome;

Prototype const char *SpoolHomePat;
Prototype const char *DQueueHomePat;
Prototype const char *GroupHomePat;
Prototype const char *CacheHomePat;
Prototype const char *FeedsHomePat;
Prototype const char *LogHomePat;
Prototype const char *LibHomePat;
Prototype const char *DbHomePat;
Prototype const char *RunHomePat;

Prototype const char *DiabloSocketPat;		/* run relative */
Prototype const char *DReaderSocketPat;		/* run relative */
Prototype const char *DFeedNotifySocketPat;	/* run relative */
Prototype const char *DFeedNotifyLockPat;	/* run relative */

Prototype const char *DExpireCtlPat;		/* lib relative	*/
Prototype const char *DSpoolCtlPat;		/* lib relative	*/
Prototype const char *DControlCtlPat;		/* lib relative	*/
Prototype const char *DiabloHostsPat;		/* lib relative	*/
Prototype const char *DReaderAccessPat;		/* lib relative	*/
Prototype const char *VServerPat;		/* lib relative */
Prototype const char *DServerHostsPat;		/* lib relative	*/
Prototype const char *ModeratorsPat;		/* lib relative	*/
Prototype const char *DNewsfeedsPat;		/* lib relative	*/
Prototype const char *DNNTPSpoolCtlPat;		/* lib relative	*/
Prototype const char *DistribDotPatsPat;	/* lib relative	*/
Prototype const char *DistributionsPat;		/* lib relative	*/

Prototype const char *ServerDActivePat;		/* db relative	*/
Prototype const char *ReaderDActivePat;		/* db relative	*/
Prototype const char *DHistoryPat;		/* db relative	*/
Prototype const char *DumpHistPat;		/* db relative	*/
Prototype const char *SpamBodyCachePat;		/* db relative	*/
Prototype const char *SpamNphCachePat;		/* db relative	*/
Prototype const char *PCommitCachePat;		/* db relative	*/
Prototype const char *DExpireOverListPat;	/* db relative  */
Prototype const char *DHostsCachePat;		/* db relative  */
Prototype const char *DHostsLockPat;		/* db relative  */
Prototype const char *DFeedStatsPat;		/* db relative  */
Prototype const char *CacheHitsPat;		/* db relative  */

Prototype const char *DRVserverCachePat;	/* db relative  */
Prototype const char *DRGroupCachePat;		/* db relative  */
Prototype const char *DRAuthCachePat;		/* db relative  */
Prototype const char *DRFeedCachePat;		/* db relative  */
Prototype const char *DRReaderCachePat;		/* db relative  */
Prototype const char *DRAccessCachePat;		/* db relative  */
Prototype const char *DRAccessLockPat;		/* db relative  */

Prototype const char *GeneralLogPat;		/* log relative */
Prototype const char *IncomingLogPat;		/* log relative */
Prototype const char *DRIncomingLogPat;		/* log relative */
Prototype const	char *FPathLogPat;		/* log relative */
Prototype const	char *FArtLogPat;		/* log relative */

Prototype const char *ShutdownCleanup;

Prototype volatile int DebugOpt;
Prototype  MemPool *SysMemPool;

const char *NewsHome	= "/news";

const char *SpoolHomePat = "%s/spool/news";
const char *DQueueHomePat = "%s/dqueue";
const char *GroupHomePat = "%s/spool/group";
const char *CacheHomePat = "%s/spool/cache";
const char *FeedsHomePat = "%s/feeds";
const char *LogHomePat = "%s/log";
const char *LibHomePat = "%s";
const char *DbHomePat = "%s";
const char *RunHomePat = "%s/run";

const char *DiabloSocketPat = "%s/.diablosock";
const char *DReaderSocketPat = "%s/.dreadersock";
const char *DFeedNotifySocketPat = "%s/feednotify";
const char *DFeedNotifyLockPat = "%s/.lck.feednotify";
const char *ServerDActivePat = "%s/dactive.kp";
const char *ReaderDActivePat = "%s/dactive.kp";
const char *DExpireCtlPat = "%s/dexpire.ctl";
const char *DSpoolCtlPat = "%s/dspool.ctl";
const char *DControlCtlPat = "%s/dcontrol.ctl";
const char *DHistoryPat = "%s/dhistory";
const char *DumpHistPat = "NONE";
const char *DiabloHostsPat = "%s/diablo.hosts";
const char *DReaderAccessPat = "%s/dreader.access";
const char *VServerPat = "%s/dreader.virtuals"; 
const char *DServerHostsPat = "%s/dserver.hosts";
const char *ModeratorsPat = "%s/moderators";
const char *SpamBodyCachePat = "%s/spam.body.cache";
const char *SpamNphCachePat = "%s/spam.nph.cache";
const char *PCommitCachePat = "%s/pcommit.cache";
const char *DExpireOverListPat = "%s/dexpover.dat";
const char *DHostsCachePat = "%s/dhosts.cache";
const char *CacheHitsPat = "%s/cache.hits";
const char *DHostsLockPat = "%s/.hostslock";
const char *DFeedStatsPat = "%s/feedstats";
const char *DNewsfeedsPat = "%s/dnewsfeeds";
const char *DNNTPSpoolCtlPat = "%s/dnntpspool.ctl";
const char *DistribDotPatsPat = "%s/distrib.pats";
const char *DistributionsPat = "%s/distributions";

const char *DRVserverCachePat = "%s/cache.vserverdef";
const char *DRGroupCachePat = "%s/cache.groupdef";
const char *DRAuthCachePat = "%s/cache.authdef";
const char *DRFeedCachePat = "%s/cache.feeddef";
const char *DRReaderCachePat = "%s/cache.readerdef";
const char *DRAccessCachePat = "%s/cache.readeraccess";
const char *DRAccessLockPat = "%s/.accesslock";

const char *GeneralLogPat = "SYSLOG";
const char *IncomingLogPat = "%s/incoming.log";
const char *DRIncomingLogPat = "NONE";
const char *FPathLogPat = "NONE";
const char *FArtLogPat = "NONE";

const char *ShutdownCleanup = "NONE";

volatile int  DebugOpt = 0;
MemPool	*SysMemPool;

