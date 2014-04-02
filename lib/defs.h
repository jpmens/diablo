
/*
 * DEFS.H
 *
 *
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 * Modified 12/4/1997 to include support for compressed data streams.
 * Modifications (c) 1997, Christopher M Sedore, All Rights Reserved.
 * Modifications may be distributed freely as long as this copyright
 * notice is included without modification.
 *
 */

#include "config.h"

/*
 * Cannot increase FD_SETSIZE on Linux, but we can increase __FD_SETSIZE
 * with glibc 2.2 and 2.3 at least. We do this by including
 * bits/types.h which defines __FD_SETSIZE first (before any other include),
 * then we redefine __FD_SETSIZE. Ofcourse a user program may NEVER
 * include bits/whatever.h directly, so this is a dirty hack!
 */
#if LARGE_FD_SETSIZE > 1024
#  ifdef __linux__
#    include <features.h>
#    if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
#      include <bits/types.h>
#      undef __FD_SETSIZE
#      define __FD_SETSIZE LARGE_FD_SETSIZE
#    endif
#  else
     /* Works on most BSDs */
#    define FD_SETSIZE LARGE_FD_SETSIZE
#  endif
#endif

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/socket.h>		/* internet sockets	*/
#include <sys/un.h>		/* unix domain sockets	*/
#include <sys/mman.h>		/* mmap()		*/
#ifdef _AIX
#include <sys/select.h>
#endif
#if USE_SYSV_DIR
#include <dirent.h>
typedef struct dirent den_t;
#else
#include <sys/dir.h>
typedef struct direct den_t;
#endif
#include <netinet/in.h>		/* internet sockets	*/
#include <netinet/tcp.h>	/* TCP_NODELAY sockopt  */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <utime.h>

#if USE_BSTRING_H
#include <bstring.h>
#endif

#if HAS_PROC_TITLE
#include <libutil.h>
#endif

#if USE_POLL
#include <poll.h>		/* poll()		*/
#endif

#if USE_PCOMMIT_SHM || USE_SPAM_SHM || USE_CANCEL_SHM
#include <sys/ipc.h>		/* SYSV shared memory	*/
#include <sys/shm.h>		/* SYSV shared memory	*/
#endif

#include <stdarg.h>
#include <stddef.h>
#if USE_STRINGS_H
#include <strings.h>
#endif
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>		/* internet sockets	*/
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <math.h>
#include <fcntl.h>

#ifdef	POST_HIDENNTPPOSTHOST
#include	<md5.h>
#endif
#ifdef	POST_CRYPTXTRACE
#if __FreeBSD_version > 470100
#include        <openssl/des.h>
#else
#include	<des.h>
#endif
#endif
#ifdef	NETREMOTE_ENABLED
#if __FreeBSD_version > 470100
#include        <openssl/des.h>
#else
#include	<des.h>
#endif
#endif
#ifdef	RADIUS_ENABLED
#include	<radlib.h>
#endif
#ifdef	USE_ZLIB
#if _FILE_OFFSET_BITS == 64 && !defined(_LARGEFILE64_SOURCE)
/* work around 64 bit confusion in zlib.h */
#define _LARGEFILE64_SOURCE
#define off64_t off_t
#include	<zlib.h>
#undef _LARGEFILE64_SOURCE
#undef off64_t
#else
#include	<zlib.h>
#endif
#endif

/* For libfarse compatibility */
typedef unsigned char *mid_t;
#include	"hashfeed.h"

typedef signed char	int8;
typedef short		int16;
typedef int		int32;
typedef unsigned char	uint8;
typedef unsigned short	uint16;
typedef unsigned int	uint32;

#define Prototype extern
#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

#define	LL_MAX		0x7FFFFFFFFFFFFFFFLL
typedef	long long	artno_t;

/*
 * diablo.config
 */

#define HASH_PRIME	0
#define HASH_CRC	1
#define HASH_OCRC	2

#define	CONF_FEEDER	0x01
#define	CONF_READER	0x02

/*
 * The read group name hash type
 * This cannot be > 9. 10+ are used for directory numbers
 */
#define	HASHGRP_NONE	0
#define	HASHGRP_CRC	1
#define	HASHGRP_32MD5	2
#define	HASHGRP_64MD5	3
#define	HASHGRP_HIER	4

#define HASHGRPTYPE_OVER	0x1000
#define HASHGRPTYPE_DATA	0x2000

#define	GRPFTYPE_OVER	0
#define	GRPFTYPE_DATA	1

#define	HASHGRP_DIR_NUM	1	/* Dirs per level specified as a number */
#define	HASHGRP_DIR_BIT	2	/* Dirs per level specified as bytes of hash */

typedef struct GroupHashType {
    int	gh_type;		/* The HASHGRP_ type */
    int gh_sigbytes;		/* Number of significant hash bytes */
    int gh_dirtype;		/* Algorithm for choosing dirs per level */
    int gh_dirlvl;		/* Directory levels */
    int gh_dirinfo[10];		/* Number of directories per level */
} GroupHashType;

#define ACTIVE_OFF	0
#define ACTIVE_ON	1

typedef struct PathListType {
    char *pathent;
    int pathtype;
    struct PathListType *next;
} PathListType;

typedef struct CacheDirType {
	int dt_dirlvl;		/* Directory levels */
	int dt_dirinfo[10];	/* Number of dirs per level */
} CacheDirType;

typedef struct BindList {
    struct BindList *bl_Next;
    char        *bl_Port;
    char        *bl_Host;
} BindList;

struct DiabloOpts {
    int HashMethod;
    uint32 HashSize;
    int CompatHashMethod;
    int FeederXRefSlave;
    int FeederXRefSync;
    int FeederActiveEnabled;
    int FeederActiveDrop;
    int FeederAutoAddToActive;
    int FeederRTStats;
    int ReaderForks;
    int ReaderThreads;
    int ReaderFeedForks;
    int ReaderDns;
    int ReaderCacheMode;
    int ReaderCacheHashSize;
    int ReaderXOverMode;
    int ReaderAutoAddToActive;
    int ReaderDetailLog;
    int RememberSecs;
    int FeederMaxAcceptAge;
    int MaxPerRemote;
    int HostCacheRebuildTime;
    int DisplayAdminVersion;
    int RejectArtsWithNul;
    int RejectArtsWithBareCR;
    int FeederBufferSize;
    int FeederMaxArtSize;
    int ReaderMaxArtSize;
    int WireFormat;
    int FeederArtTypes;
    int FeederPreloadArt;
    int SpoolPreloadArt;
    int FeederMaxHeaderSize;
    int ReaderIdentTimeout;
    char *SpamFilterOpt;
    char *FeederPathHost;
    char *FeederXRefHost;
    char *FeederHostName;
    char *FeederFilter;
    char *ReaderXRefHost;
    char *ReaderXRefSlaveHost;
    char *ReaderPathHost;
    char *ReaderHostName;
    char *ReaderCrashHandler;
    char *NewsAdmin;
    char *NewsMaster;
    PathListType *PathList;
    char *FeederBindHost;
    char *FeederPort;
    char *ReaderBindHost;
    char *ReaderPort;
    char *ReaderBan;
    CacheDirType ReaderCacheDirs;
    GroupHashType ReaderGroupHashMethod;
    char *PgpVerifyArgs[10];
    char *PostXFilter;
} DiabloOpts;

/*
 * History
 */

typedef struct {
    int32	h1;
    int32	h2;
} hash_t;

typedef struct {
    uint64_t    h1;
    uint64_t    h2;
} md5hash_t;

typedef struct HistHead {
    uint32	hmagic;		/* 0xA1B2C3D4			*/
    uint32	hashSize;	/* entries in hash table	*/
    uint16	version;	/* version of history file	*/
    uint16	henSize;	/* size of history entry	*/
    uint16	headSize;	/* size of history header	*/
} HistHead;

#define HMAGIC		((uint32)0xA1B2C3D4)
#define HDEADMAGIC	((uint32)0xDEADF5E6)
#define HVERSION	2

/*
 * Dreaderd cache scoreboard
 *
 * cache consist of :
 * - an header (CacheHitHead)
 * - an hash (uint32[chh_hashSize]) pointing to a list of CacheHitEntry
 * - entries (CacheHitEntry)
 */

#define CHMAGIC		((uint32)0xD1C2B3A4)
#define CHVERSION	1

typedef struct CacheHitHead {
    uint32	chh_magic;	/* 0xD1C2B3A4			*/
    uint32	chh_version;	/* version of scoreboard	*/
    uint32	chh_hashSize;	/* entries in hash table	*/
    uint32	chh_newEntry;	/* next entry to add		*/
    uint32	chh_end;	/* number of byte in mmap	*/
    time_t	chh_lastExpired; /* last expiration date	*/
} CacheHitHead;

typedef struct CacheHash_t {		/* hash key for scoreboad cache	*/
    uint64_t    h1;
    uint64_t    h2;
    int		iter;
} CacheHash_t;

typedef struct CacheHitEntry {
    struct CacheHitHead	che_hash; /* group hash			*/
    uint32	che_Next;	/* linking entries		*/
    int		che_ReadArt;	/* number of read articles	*/
    uint32	che_Hits;	/* number of cache hits		*/
    uint32	che_LastHi;	/* to compute number of new articles */
    uint32	che_NewArt;	/* number of new articles */
} CacheHitEntry;

/*
 * History file entry.  The filename is calculated from the gmt start
 * (calculates the directory), and file id.  Each incoming process
 * allocates its own file & id and appends to it.
 *
 * The exp value is:
 *  0		= instant-expired
 *  1 - 100	= expire slot
 *  101 - 200	= spool partition
 *  0x1000	= unused
 *  0x2000	= unused
 *  0x4000	= expired/rejected
 *  0x8000	= header-only
 *
 *  h.iter == -1 for rejected articles
 */

typedef uint32  HistIndex;

typedef struct History {
    HistIndex 	next;		/* next link			*/
    uint32	gmt;		/* expire slot number		*/
    hash_t	hv;		/* hash value			*/
    uint16 	iter;		/* file id 			*/
    uint16	exp;		/* hours relative to gmt minutes */
    uint32	boffset;	/* starting offset in file	*/
    int32	bsize;		/* size of article		*/
} History;

#define EXPF_HEADONLY	0x8000	/* header-only flag (in exp field) */
#define EXPF_EXPIRED	0x4000
#define EXPF_FLAGS	0x0FFF
#define H_NOFLAGS(n)	((uint16)((n) & EXPF_FLAGS))
#define H_EXPIRED(n)	((uint16)((n) & EXPF_EXPIRED))
#define H_SPOOL(n)      ((uint16)(H_NOFLAGS(n) > 100 && H_NOFLAGS(n) < 500 ? \
				H_NOFLAGS(n) - 100 : 0))


/*
 * Internal spamfilter
 */

typedef struct SpamInfo {
    md5hash_t	BodyHash;
    char	*PostingHost;
    md5hash_t	PostingHostHash;
    int		Lines;
    char	*MsgId;
    hash_t	MsgIdHash;
} SpamInfo;

/*
 * Pre/PostCommit cache
 */

#define PC_PRECOMM		0x001
#define PC_POSTCOMM		0x002
#define PC_DELCOMM		0x004

/*
 * Article header - prepended to all articles written to the spool.
 *
 * All articles on spool consist of:
 *	SpoolArtHdr
 *	Article
 *	Nul
 *
 *	SpoolArtHdr.StoreLen = SpoolArtHdr + Article + Nul
 *	SpoolArtHdr.HeadLen  = SpoolArtHdr
 *	SpoolArtHdr.ArtLen   = Article
 */

#define	STORE_MAGIC1		0xff
#define	STORE_MAGIC2		0x99

#define	STOREAPI_REVISION	1

#define	STORETYPE_TEXT		0x01
#define	STORETYPE_GZIP		0x02
#define	STORETYPE_WIRE		0x04

typedef struct SpoolArtHdr {
    uint8 Magic1;
    uint8 Magic2;
    uint8 Version;
    uint8 HeadLen;		/* Length of this header */
    uint8 StoreType;
    uint32 ArtHdrLen;		/* Size of stored article header */
    uint32 ArtLen;		/* Total article size (including \r\n) */
    uint32 StoreLen;		/* Size of article on disk */
    uint8 HdrEnd;		/* Nul */
} SpoolArtHdr;

/*
 * FeedValid()
 */

#define FEED_VALID		0
#define FEED_MAXCONNECT		-1
#define FEED_MISSINGLABEL	-2

#define OD_HARTS        256     /* modulo for data files, POWER OF 2    */
#define OD_HMASK        (OD_HARTS-1)

/*
 * Newsfeed stuff
 */

#define	MAXSTREAM	16	/* Minimum is 2 */

typedef struct NewslinkInfo {
    char		*li_HostName;
    int			li_Port;		/* 0 = default NNTP */
    int			li_StartDelay;
    int			li_TransmitBuf;
    int			li_ReceiveBuf;
    int			li_TOS;			/* 0 = default, don't set it */
    char		*li_BindAddress;
    int			li_MaxParallel;
    int			li_NoStream;
    int			li_DelayFeed;
    int			li_DelayInFeed;
    int			li_RealTime;
    int			li_Notify;
    int			li_MaxQueueFile;
    int			li_HeadFeed;
    int			li_PreserveBytes;
    int			li_GenLines;
    int			li_Check;
    int			li_MaxStream;
    int			li_Priority;
    int			li_Compress;
    int			li_QueueSkip;
    int			li_ArticleStat;
    int			li_NoBatch;
    char		*li_LogArts;
    char		*li_Hours;
} NewslinkInfo;

typedef struct LabelList {
    char		*label;
    struct LabelList	*next;
} LabelList;

typedef struct ArtTypeList {
    int			arttype;
    int			negate;
    struct ArtTypeList	*next;
} ArtTypeList;

/*
 * RunProgPipe()
 */

#define RPF_STDIN	0x0001
#define RPF_STDOUT	0x0002
#define RPF_STDERR	0x0004

/*
 *  Parameters for ArticleFileName()
 */
#define	ARTFILE_DIR		0x01
#define	ARTFILE_FILE		0x02
#define	ARTFILE_DIR_REL		0x04
#define	ARTFILE_FILE_REL	0x08

#define HGF_FAST	0x01
#define HGF_NOSEARCH	0x02
#define HGF_MLOCK	0x03
#define HGF_READONLY	0x04
#define HGF_EXCHECK	0x08

typedef struct Node {
    struct Node		*no_Next;
    int32		no_Value;
    const char		*no_Name;
    const char		*no_Name2;
    void		*no_Data;
} Node;

#define MAXLINE		16384
#define MAXMSGIDLEN	250
#define MAXGNAME	256
#ifndef	MAXFORKS
#define MAXFORKS	512
#endif
#ifndef	MAXFEEDS
#define MAXFEEDS	128
#endif
#define MAXFDS		(MAXFORKS+MAXFEEDS+32)
#define MAXDIABLOFDCACHE 8
#if MAXFDS > FD_SETSIZE
#undef MAXFDS
#define MAXFDS FD_SETSIZE
#endif

/*
 * Define a list of groups. Used when checking a newsgroup list
 */
typedef struct GroupList {
    char *group;
    struct GroupList *next;
} GroupList;   

/*
 * Spool allocation structures
 */

#define MAX_SPOOL_OBJECTS	100

/*
 * Spool allocation strategies
 */
#define SPOOL_ALLOC_NONE	0x00
#define SPOOL_ALLOC_SEQ		0x01	/* Default sequential alloc */
#define SPOOL_ALLOC_SPACE	0x02	/* Choose spool with most space free */
#define SPOOL_ALLOC_SINGLE	0x03	/* Write all feeds to a single spool */
#define SPOOL_ALLOC_WEIGHTED	0x04	/* Weighted alloc */

#define	SPOOL_MAX_FILE_SIZE	1073741824	/* Max size of spool files */

/*
 * Expire methods
 */
#define EXM_SYNC		0
#define EXM_DIRSIZE		1

typedef struct SpoolObject {
    uint16		so_SpoolNum;
    double		so_MinFree;		/* bytes */
    long		so_MinFreeFiles;	/* free inodes */
    double		so_MaxSize;		/* bytes */
    long		so_KeepTime;		/* secs */
    uint32		so_DirTime;
    int			so_SpoolDirs;
    int			so_ExpireMethod;
    int			so_CompressLvl;
    int			so_Weight;
    char		so_Path[PATH_MAX];
    struct SpoolObject	*so_Next;
} SpoolObject;

typedef struct MetaSpool {
    char		ms_Name[32];
    double		ms_MaxSize;		/* bytes */
    int			ms_MaxCross;
    long		ms_KeepTime;		/* secs */
    ArtTypeList		*ms_ArtTypes;
    HashFeed_MatchList	*ms_HashFeed;
    LabelList		*ms_Label;
    int			ms_RejectArts;
    int			ms_DontStore;
    int			ms_AllocationStrategy;
    int			ms_ReAllocInterval;
    int			ms_NumSpoolObjects;
    SpoolObject		*ms_SpoolObjects[MAX_SPOOL_OBJECTS];
    SpoolObject		*ms_AllocatedSpool;
    int			ms_NextAllocation;
    struct MetaSpool	*ms_Next;
} MetaSpool;

typedef struct GroupExpire {
    char                ex_Wild[255];
    MetaSpool		*ex_MetaSpool;
    SpoolObject		*ex_AllocatedSpool;
    struct GroupExpire	*ex_Next;
} GroupExpire;

/*
 * Overview expire structure
 */
typedef struct OverExpire {
    char                oe_Wild[255];
    int                 oe_InitArts;		/* expireover */
    int                 oe_MinArts;		/* expireover */
    int                 oe_MaxArts;		/* expireover */
    int                 oe_DataSize;		/* expireover */
    double              oe_ExpireDays;		/* expireover */
    double		oe_LimitDays;		/* group/xover */
    double		oe_StoreGZDays;		/* compress after X days */
    struct OverExpire	*oe_Next;
} OverExpire;

#define MAXSIMUL	5	/* maximum simultanious from same label */

#define RCOK		0	/* article accepted		*/
#define RCALREADY	1	/* we already have this article	*/
#define RCTRYAGAIN	2	/* something went wrong, requeue it for later */
#define RCREJECT	3	/* there is something wrong with your article */
#define RCERROR		4	/* an unexpected error occured  */

#define XLOCK_SH	1
#define XLOCK_EX	2
#define XLOCK_UN	3
#define XLOCK_NB	0x10

#define MAXFEEDTABLE	256

#define	RTSTATS_NONE	1
#define	RTSTATS_LABEL	2
#define	RTSTATS_HOST	3

#define	FSTATS_IN		1
#define	FSTATS_INDETAIL		2
#define	FSTATS_OUT		3
#define	FSTATS_SPOOL		4
#define	FSTATS_SPOOLDETAIL	5

typedef struct SentStats {
    time_t	TimeStart;
    time_t	DeltaStart;
    int		ConnectCnt;
    int		OfferedCnt;
    int		AcceptedCnt;
    int		RefusedCnt;
    int		RejectedCnt;
    int		DeferredCnt;
    int		DeferredFailCnt;
    double	RejectedBytes;
    double	AcceptedBytes;
    int		ConnectTotal;
    int		OfferedTotal;
    int		AcceptedTotal;
    int		RefusedTotal;
    int		RejectedTotal;
    int		DeferredTotal;
    int		DeferredFailTotal;
    double	RejectedBytesTotal;
    double	AcceptedBytesTotal;
} SentStats;

#define STATS_OTHER		0	/* dummy			*/
#define STATS_OFFERED		1	/* offered (ihave/check)	*/
#define STATS_ACCEPTED		2	/* accepted			*/
#define STATS_RECEIVED		3	/* received			*/
#define STATS_REFUSED		4	/* refused			*/
#define STATS_REF_HISTORY	5	/* ref, in history		*/
#define STATS_REF_PRECOMMIT	6	/* ref, offered by another host	*/
#define STATS_REF_POSTCOMMIT	7	/* ref, in history cache	*/
#define STATS_REF_BADMSGID	8	/* ref, bad msgid		*/
#define STATS_IHAVE		9	/* ihave			*/
#define STATS_CHECK		10	/* check			*/
#define STATS_TAKETHIS		11	/* takethis			*/
#define STATS_CONTROL		12	/* control message		*/
#define STATS_REJECTED		13	/* rejected			*/
#define STATS_REJ_FAILSAFE	14	/* rej, failsafe		*/
#define STATS_REJ_MISSHDRS	15	/* rej, missing headers		*/
#define STATS_REJ_TOOOLD	16	/* rej, too old			*/
#define STATS_REJ_GRPFILTER	17	/* rej, incoming grp filter	*/
#define STATS_REJ_INTSPAMFILTER	18	/* rej, internal spam filter	*/
#define STATS_REJ_EXTSPAMFILTER	19	/* rej, external spam filter	*/
#define STATS_REJ_INCFILTER	20	/* rej, incoming filter		*/
#define STATS_REJ_NOSPOOL	21	/* rej, no spool object		*/
#define STATS_REJ_IOERROR	22	/* rej, io error		*/
#define STATS_REJ_NOTINACTV	23	/* rej, not in active file	*/
#define STATS_REJ_PATHTAB	24	/* rej, TAB in Path: header	*/
#define STATS_REJ_NGTAB		25	/* rej, TAB in Newsgroups: hdr	*/
#define STATS_REJ_POSDUP	26	/* rej, dup detected after receive */
#define STATS_REJ_HDRERROR	27	/* rej, dup or missing headers	*/
#define STATS_REJ_TOOSMALL	28      /* rej, article too small	*/
#define STATS_REJ_ARTINCOMPL	29	/* rej, article incomplete	*/
#define STATS_REJ_ARTNUL	30	/* rej, article has a nul	*/
#define STATS_REJ_NOBYTES	31	/* rej, header only, no Bytes:	*/
#define STATS_REJ_PROTOERR	32	/* rej, protocol error		*/
#define STATS_REJ_MSGIDMIS	33	/* rej, msgid mismatch		*/
#define STATS_REJ_ERR		34	/* rej, unknown error		*/
#define STATS_REJ_TOOBIG	35	/* rej, too big			*/
#define STATS_REJ_BIGHEADER	36	/* header too big		*/
#define STATS_REJ_NOHDREND	37	/* header too big		*/
#define STATS_REJ_BARECR	38	/* article has a CR without LF	*/
#define STATS_REF_IFILTHASH	39	/* ref, by IFILTER hash	*/
#define STATS_NSLOTS		40

typedef struct RecStats {
    time_t	TimeStart;
    int		ConnectCnt;
    int		Stats[STATS_NSLOTS];
    double	ReceivedBytes;
    double	AcceptedBytes;
    double	RejectedBytes;
} RecStats;

#define	STATS_S_STAT		1
#define	STATS_S_STATMISS	2
#define	STATS_S_STATEXP		3
#define	STATS_S_STATERR		4
#define	STATS_S_ARTICLE		5
#define	STATS_S_ARTICLEMISS	6
#define	STATS_S_ARTICLEEXP	7
#define	STATS_S_ARTICLEERR	8
#define	STATS_S_ARTICLEPRO	9
#define	STATS_S_HEAD		10
#define	STATS_S_HEADMISS	11
#define	STATS_S_HEADEXP		12
#define	STATS_S_HEADERR		13
#define	STATS_S_HEADPRO		14
#define	STATS_S_BODY		15
#define	STATS_S_BODYMISS	16
#define	STATS_S_BODYEXP		17
#define	STATS_S_BODYERR		18
#define	STATS_S_BODYPRO		19
#define	STATS_S_NSLOTS		20

typedef struct SpoolStats {
    time_t	TimeStart;
    int		ConnectCnt;
    int		Arts[STATS_S_NSLOTS];
    double	ArtsBytesSent;
} SpoolStats;

typedef struct FeedStats {
    int		version;
    char	hostname[255];
    int		region;
    RecStats	RecStats;
    SpoolStats	SpoolStats;
    SentStats	SentStats;
} FeedStats;

/*
 * Additions to support compressed streams
 * cmsedore@maxwell.syr.edu 12/4/97
 */

#ifdef USE_ZLIB

#define COMPRESS_BUFFER_LENGTH 8192

typedef struct compressBuffer {
  char			cb_Buf[COMPRESS_BUFFER_LENGTH];
  int			cb_compressHoldoverCount;
  int			dataError;
  double		orig,decomp;
  z_stream		z_str;
} CompressBuffer;

#endif

typedef struct Buffer {
    int			bu_Beg;
    int			bu_NLScan;
    int			bu_End;
    int			bu_BufMax;	/* normal maximum	*/
    int			bu_DataMax;	/* extended maximum	*/
    int			bu_Fd;
#ifdef USE_ZLIB
    gzFile		*bu_gzFile;
    int			bu_gzWrote;
    CompressBuffer	*bu_CBuf;
#else
    char		*bu_CBuf;
    char		*bu_gzFile;
#endif
    int			bu_BufSize;
    int			bu_Error;
    char		*bu_Data;	/* included/extended buffer	*/
    char		bu_Buf[1024];	/* included buffer		*/
} Buffer;

typedef struct LogInfo {
    const char		**Pat;
    const char		*Ident;
    FILE		*Fd;
    time_t		NextCheck;
    int			UseSyslog;
    int			Disabled;
    int			LastInode;
    int			Pid;
    char		Fname[PATH_MAX];
} LogInfo;


#define XADV_WILLNEED	1
#define XADV_SEQUENTIAL	2

#include "lib/mem.h"
#include "lib/kpdb.h"
#include "lib/ctl.h"
#include "lib/dmd5.h"

/*
 * Article types that diablo can determine when receiving an article
 */

#define		ARTTYPE_NONE		0x000000

#define		ARTTYPE_DEFAULT		0x000001
#define		ARTTYPE_CONTROL		0x000002
#define		ARTTYPE_CANCEL		0x000004

#define		ARTTYPE_MIME		0x000100
#define		ARTTYPE_BINARY		0x000200
#define		ARTTYPE_UUENCODE	0x000400
#define		ARTTYPE_BASE64		0x000800
#define		ARTTYPE_MULTIPART	0x001000
#define		ARTTYPE_HTML		0x002000
#define		ARTTYPE_PS		0x004000
#define		ARTTYPE_BINHEX		0x008000
#define		ARTTYPE_PARTIAL		0x010000
#define		ARTTYPE_PGPMESSAGE	0x020000
#define		ARTTYPE_YENC		0x040000
#define		ARTTYPE_BOMMANEWS	0x080000
#define		ARTTYPE_UNIDATA		0x100000

#define		ARTTYPE_ALL		0xFFFFFF

/*
 * Data for maintaining an easily accessable list of IP numbers
 */
typedef struct IPList {
    md5hash_t il_hash;
    char *il_ip;
    int il_count;
    time_t il_expire;
    struct IPList *il_next;
} IPList;

IPList *IPHash;

/*
 * MISC
 */

#ifndef LOG_PERROR
#define LOG_PERROR	0
#endif
#ifndef INADDR_NONE
#define INADDR_NONE	((unsigned long)-1)
#endif

#ifndef NI_MAXHOST
#ifdef INET6
#define	NI_MAXHOST	1025
#else
#define	NI_MAXHOST	256
#endif
#endif

/*
 * This is very messy because sa_len doesn't exist as part of the sockaddr
 * structure on some OS's. So much for getting it right with IPv6.
 */
#ifdef INET6
#if HAS_SA_LEN
#define SA_LEN(x) ( (x)->sa_len )
#else
#define SA_LEN(x) ( \
	((x)->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : \
	((x)->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : \
	0)
#endif	/* HAS_SA_LEN */
#else
#define SA_LEN(x) ( \
	((x)->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : \
	0)
#endif	/* INET6 */


#ifndef TEST
#include "obj/lib-protos.h"
#endif

/*
 * can't use Prototype stuff for functions which may
 * already be prototyped.
 */

#if USE_STRERROR
char *strerror(int e);
#endif

/*
 * hack
 */

#if USE_MEMMOVE
void *memmove(char *dst0, const char *src0, size_t length);
#endif

/*
 * If setproctitle() exists, stprintf() is macro'd to it directly, otherwise
 * we use argv stuffing.
 */

#if HAS_PROC_TITLE
#define stprintf	setproctitle
#endif

/*
 * If we don't have snprintf() we have to supply it.  snprintf() is 
 * absolutely critical.
 */

#if HAVE_SNPRINTF == 0

int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif

#if HAVE_STRSEP == 0

char *strsep(register char **stringp, register const char *delim);

#endif

#if USE_INTERNAL_VSYSLOG

void vsyslog(int priority, const char *msg, va_list va);

#endif

#ifdef DMALLOC
#include <dmalloc.h>
#endif
