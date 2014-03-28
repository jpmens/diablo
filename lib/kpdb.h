
/*
 * LIB/KPDB.H
 */

typedef struct KPDB {
    char 	*kp_FileName;
    const char	*kp_MapBase;
    const char  *kp_Cache;
    int		kp_CacheLen;
    int		kp_HeadLen;
    int		kp_MapSize;
    uint32	kp_AppendSeq;
    int		kp_OFlags;
    int		kp_Fd;
    int		kp_CloseMe;
    int		kp_ReSize;
    int		kp_Lock4;
    int		kp_Modified;
    int		kp_ResortLimit;
} KPDB;

#define KP_LOCK			1
#define KP_LOCK_CONTINUE	2
#define KP_UNLOCK		3

