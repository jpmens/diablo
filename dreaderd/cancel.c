
/*
 * DREADERD/CANCEL.C
 *
 * 	Implement pre-cancel cache via shared memory segment.  The cache only
 *	applies to cancels that are received prior to the article being
 *	received.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void InitCancelCache(void);
Prototype void EnterCancelCache(const char *msgid);
Prototype int FindCancelCache(const char *msgid);

#define C_HSIZE		16384
#define C_HMASK		(C_HSIZE-1)

hash_t *CCacheAry = NULL;

void 
InitCancelCache(void)
{
#if USE_CANCEL_SHM
    int sid = shmget(IPC_PRIVATE, C_HSIZE * sizeof(hash_t), SHM_R|SHM_W);
    struct shmid_ds ds;

    if (sid < 0) {
	logit(LOG_WARNING, "InitCancelCache cannot allocate sysv shared memory");
    } else {
	CCacheAry = (hash_t *)shmat(sid, NULL, SHM_R|SHM_W);
	if (shmctl(sid, IPC_STAT, &ds) < 0 || shmctl(sid, IPC_RMID, &ds) < 0) {
	    logit(LOG_CRIT, "sysv shmctl stat/rmid failed");
	    exit(1);
	}
	if (CCacheAry == (hash_t *)-1) {
	    CCacheAry = NULL;
	    logit(LOG_CRIT, "sysv shared memory map failed");
	    exit(1);
	}
	if (CCacheAry)
	    bzero(CCacheAry, C_HSIZE * sizeof(hash_t));
    }
#endif
}

void 
EnterCancelCache(const char *msgid)
{
    if (CCacheAry) {
	hash_t hv = hhash(msgid);
	int hi = ((hv.h1 ^ hv.h2) & 0x7FFFFFFF) % C_HSIZE;

	CCacheAry[hi] = hv;
    }
}

int 
FindCancelCache(const char *msgid)
{
    if (CCacheAry) {
	hash_t hv = hhash(msgid);
	hash_t *hp;
	int hi = ((hv.h1 ^ hv.h2) & 0x7FFFFFFF) % C_HSIZE;

	hp = &CCacheAry[hi];
	if (hv.h1 == hp->h1 && hv.h2 == hp->h2)
	    return(0);
    }
    return(-1);
}

