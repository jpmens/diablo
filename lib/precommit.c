
/*
 * LIB/PRECOMMIT.C	- Precommit caching
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void InitPreCommit(void);
Prototype int PreCommit(const char *msgid, int flags);
Prototype void SetPCommitExpire(int pret, int post, int hsize);
Prototype int GetPCommit(int which);

#ifndef PC_HSIZE
#define PC_HSIZE	16384	/* times sizeof(pchash_t) */
#endif
#ifndef PC_EXPIRE
#define PC_EXPIRE	30	/* 30 second expiration	  */
#endif
#ifndef POC_EXPIRE
#define POC_EXPIRE	45*60	/* 45 minute expiration	  */
#endif
#define PC_HMASK	(PC_HSIZE - 1)

typedef struct pchash_t {
    hash_t	pc_Hash;	/* hash code		*/
    time_t	pc_Time;	/* time of entry	*/
    pid_t	pc_Pid;		/* process id, -1 if post commit */
} pchash_t;

pchash_t *PCHAry;
int	PCFd = -1;
int	PCExpire = PC_EXPIRE;		/* 30 second  default	*/
int	PCPostExpire = POC_EXPIRE;	/* 30 minute default	*/
int	PCHSize = PC_HSIZE;		/* History cache size	*/
int	PCHMask = PC_HMASK;		/* History cache hash mask */
int	PCPid = -1;

void 
SetPCommitExpire(int pret, int post, int hsize)
{
    if (pret >= 0)
	PCExpire = pret;
    if (post >= 0)
	PCPostExpire = post;
    if (hsize >= 0) {
	PCHSize = hsize;
	PCHMask = hsize - 1;
    }
}

/*
 * InitPreCommit() is called by the master diablo to initialize any 
 *		   server-private (but inherited on fork) shared memory.
 *
 *		   We generate a private shared memory segment which is
 *		   mapped, then immediately removed.  The map is inherited
 *		   on fork.  If we don't remove it now, shm segments may
 *		   build up on the machine.
 *
 *		   NOTE: InitPreCommit() may not open() any descriptors because
 *		   our code is not designed to deal with the shared lseek
 *		   for the descriptor on fork.
 */

void
InitPreCommit(void)
{
#if USE_PCOMMIT_SHM
    int sid = shmget(IPC_PRIVATE, PCHSize * sizeof(pchash_t), SHM_R|SHM_W);
    struct shmid_ds ds;

    if (sid < 0) {
	logit(LOG_CRIT, "sysv shared memory alloc of %d failed, is your machine configured with a high enough maximum segment size?",
	    PCHSize * sizeof(pchash_t)
	);
	exit(1);
    }

    PCHAry = (pchash_t *)shmat(sid, NULL, SHM_R|SHM_W);

    if (shmctl(sid, IPC_STAT, &ds) < 0 || shmctl(sid, IPC_RMID, &ds) < 0) {
	logit(LOG_CRIT, "sysv shmctl stat/rmid failed");
	exit(1);
    }
    if (PCHAry == (pchash_t *)-1) {
	PCHAry = NULL;
	logit(LOG_CRIT, "sysv shared memory map failed");
	exit(1);
    }
#endif
}

int
PreCommit(const char *msgid, int flags)
{
    int r = 0;

#if DO_PCOMMIT_POSTCACHE == 0
    /*
     * This option is turned on by default.  If it is 
     * off, do not write the precommit cache to post-cache
     * history lookup hits.
     */
    if (flags & PC_POSTCOMM)
	return(0);
#endif

    if (PCExpire == 0)
	return(0);

    if (PCPid == (pid_t)-1)
	PCPid = (int)getpid();

    if (PCHAry == NULL) {
#if USE_PCOMMIT_SHM
	logit(LOG_CRIT, "unable to initialize precommit cache");
	exit(1);
#else
	struct stat st;

	PCFd = open(PatDbExpand(PCommitCachePat), O_RDWR|O_CREAT, 0644);

	if (PCFd >= 0 && fstat(PCFd, &st) == 0) {
	    int prot = PROT_READ | (USE_PCOMMIT_RW_MAP * PROT_WRITE);
	    if (st.st_size < PCHSize * sizeof(pchash_t))
		ftruncate(PCFd, PCHSize * sizeof(pchash_t));
	    PCHAry = xmap(NULL, PCHSize * sizeof(pchash_t), prot, MAP_SHARED, PCFd, 0);
	}
#endif
    }
    if (PCHAry == NULL && PCFd >= 0) {
	close(PCFd);
	PCFd = -1;
    }
    if (PCHAry) {
	hash_t hv = hhash(msgid);
	int i = (hv.h1 ^ hv.h2) & PCHMask;
	pchash_t *pc = &PCHAry[i];
	time_t t = time(NULL);
	int32 dt = t - pc->pc_Time;

	if (pc->pc_Hash.h1 == hv.h1 && 
	    pc->pc_Hash.h2 == hv.h2 &&
	    dt >= 0 && 
	    ((dt < PCExpire && pc->pc_Pid != PCPid) ||
	    (dt < PCPostExpire && pc->pc_Pid == (pid_t)-1))
	) {
	    /*
	     * collision.  Return -1 if postcommit or -2 if precommit.
	     * If we are posting a history cache
	     * hit/commit, change the pid to -1, which lengthens the expire
	     * time.  We can do this because the message-id is already in
	     * the history file.
	     */
	    if (pc->pc_Pid == (pid_t)-1)
		r = -1;
	    else
		r = -2;
	    /*
	     * If we are removing from precommit, then set pid to 0
	     * This stops any future cache hits for the Message-ID
	     * We don't care about the return code
	     */
	    if (flags & PC_DELCOMM) {
		pc->pc_Time = 0;
		pc->pc_Pid = 0;
		r = 0;
	    }
#if USE_PCOMMIT_RW_MAP && DO_PCOMMIT_POSTCACHE
	    if ((flags & PC_POSTCOMM) && pc->pc_Pid != (pid_t)-1) {
		pc->pc_Pid = (pid_t)-1;
	    }
#endif
	} else {
	    pchash_t npc;
	    /*
	     * enter new info, we don't care about collisions
	     *
	     * PC_POSTCOMM is set only when we are using the precommit
	     * cache as a post-commit 'in the history file' cache.
	     */
	    npc.pc_Hash = hv;
	    npc.pc_Time = t - 1;
	    npc.pc_Pid = (flags & PC_POSTCOMM) ? (pid_t)-1 : PCPid;
	    if (flags & PC_DELCOMM) {
		npc.pc_Time = 0;
		npc.pc_Pid = 0;
		r = 0;
	    }
#if USE_PCOMMIT_RW_MAP
	    PCHAry[i] = npc;
#else
	    lseek(PCFd, i * sizeof(pchash_t), 0);
	    write(PCFd, &npc, sizeof(npc));
#endif
	}
    }
    return(r);
}

int
GetPCommit(int which)
{
    switch (which) {
	case 1: return(PCExpire);
	case 2: return(PCPostExpire);
	case 3: return(PCHSize);
    }
    return(0);
}

