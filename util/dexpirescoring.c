/*
 * DEXPIRESCORING.C    - dreaderd cache scoring expire.
 *
 * (c)Copyright 2002, Francois Petillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"
#include <math.h>

int CacheHitsFD=-1;
char *CacheHits;

void Usage(char *progname)
{
    fprintf(stderr, "Expire the cache scoring\n");
    fprintf(stderr, "dexpirescoring [-H halflife]\n");
    exit(1);
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

struct CacheHitEntry*
FindCacheHitEntry(struct CacheHash_t *ch) {
    uint32 *i=NULL;
    struct CacheHitEntry *che=NULL;
    struct CacheHitHead *chh=(struct CacheHitHead *)CacheHits;
    i = (uint32*) (CacheHits+sizeof(struct CacheHitHead)+((ch->h1^ch->h2)%chh->chh_hashSize)*sizeof(uint32));

    while (*i != 0) {
	che = (struct CacheHitEntry*) (CacheHits + (*i));
	if (memcmp(ch, &(che->che_hash), sizeof(struct CacheHash_t))==0) {
	    return che;
	}
	i = &(che->che_Next);
    }
    return NULL;
}

int
main(int ac, char **av) {
    struct CacheHitHead chh;
    double expire=0;
    int i,r,halflife=0;
    int verbose=0;

    LoadDiabloConfig(ac, av);

    CacheHitsFD = open(PatDbExpand(CacheHitsPat), O_RDWR, 0644);
    if (CacheHitsFD<0) {
	fprintf(stderr, "Can not open cache hits file (%s)\n", PatDbExpand(CacheHitsPat));
	exit(1);
    }
    hflock(CacheHitsFD, 0, XLOCK_EX);
    r = read(CacheHitsFD, &chh, sizeof(struct CacheHitHead));
    if ( (r < sizeof(struct CacheHitHead)) || (chh.chh_magic != CHMAGIC) || (chh.chh_version != CHVERSION) ) {
	fprintf(stderr, "Wrong version, can not expire scoring\n");
	hflock(CacheHitsFD, 0, XLOCK_UN);
	exit(1);
    }
    CacheHits = mmap(NULL, chh.chh_end, PROT_READ|PROT_WRITE, MAP_SHARED, CacheHitsFD, 0);
    if (CacheHits == NULL) {
	fprintf(stderr, "Error on cache hits mmap (%s)", strerror(errno));
	hflock(CacheHitsFD, 0, XLOCK_UN);
	close(CacheHitsFD);
	exit(1);
    }

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-') {
	    fprintf(stderr, "Unexpected argument: %s\n", ptr);
	    Usage(av[0]);
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'H':
	    halflife = strtol((*ptr) ? ptr : av[++i], NULL, 0);
	    break;
	case 'v':
	    verbose++;
	    break;
	default:
	    fprintf(stderr, "Unknown option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (halflife>0) {
	time_t delay, now;
	struct CacheHitHead *chh = (struct CacheHitHead *) CacheHits;

	now = time(NULL);
	delay = now - chh->chh_lastExpired;
	chh->chh_lastExpired = now;
	expire = pow(2, ((double)delay/halflife));

	if (verbose) {
	    printf("delay : %i (%f)\n", (int)delay, expire);
	}
    }

    {
	KPDB  *KDBActive;
	int recOff, recLen;
	KDBActive = KPDBOpen(PatDbExpand(ReaderDActivePat), O_RDWR);
	if (KDBActive == NULL) {
	    fprintf(stderr, "Unable to open dactive.kp\n");
	    exit(1);
	}
	for (recOff = KPDBScanFirst(KDBActive, 0, &recLen);
		recOff;
		recOff = KPDBScanNext(KDBActive, recOff, 0, &recLen)
	) {
	    int groupLen;
	    struct CacheHash_t ch;
	    struct CacheHitEntry *che;
	    const char *rec = KPDBReadRecordAt(KDBActive, recOff, 0, NULL);
	    const char *group = KPDBGetField(rec, recLen, NULL, &groupLen, NULL);
	    artno_t endNo = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL, 10);
	    int iter = (int)strtoul(KPDBGetField(rec, recLen, "ITER", NULL, "0"), NULL, 16);
	    
	    if (group)
		group = allocTmpCopy(group, groupLen);

	    SetCacheHash(&ch, group, iter, &DOpts.ReaderGroupHashMethod);
	    che = FindCacheHitEntry(&ch);
	    if (che != NULL) {
		int new;
		new = che->che_NewArt+endNo-che->che_LastHi;
		che->che_LastHi = endNo;
		if (expire>1) {
		    che->che_ReadArt = (int)((float)che->che_ReadArt/expire);
		    che->che_Hits = (int)((float)che->che_Hits/expire);
		    che->che_NewArt = (int)((float)new/expire);
		    new = che->che_NewArt+endNo-che->che_LastHi;
		} else {
		    che->che_NewArt = new;
		}
		if (verbose) {
		    if (new<1) new=1;
		    if (che->che_Hits) {
			printf("read:%7i (hits:%6.2f%%) new:%6i (r/n=%6.2f) %s\n", che->che_ReadArt+che->che_Hits, 100*(float)che->che_Hits/(che->che_ReadArt+che->che_Hits), new, ((float)(che->che_ReadArt+che->che_Hits)/new), group);
		    } else {
			printf("read:%7i                new:%6i (r/n=%6.2f) %s\n", che->che_ReadArt+che->che_Hits, new, ((float)che->che_ReadArt/new), group);
		    }
		}
	    }
	}
    }

    
    hflock(CacheHitsFD, 0, XLOCK_UN);
    close(CacheHitsFD);
    exit(0);
}
