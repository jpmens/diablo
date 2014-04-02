
/*
 * LIB/ZALLOC.C	- mostly self contained zero-overhead memory pool/allocation 
 *		  subsystem.
 *
 *	This subsystem implements memory pools and memory allocation 
 *	routines.  It uses pagealloc() for a low level allocator and
 *	malloc() for the MemPool structures.  The idea is to (a) prevent 
 *	fragmentation and (b) allow pools to be unmapped from the VM space
 *	when no longer needed.
 *
 *	Pools are managed via a linked list of 'free' areas.  Allocating
 *	memory creates holes in the freelist, freeing memory fills them.
 *	Since the freelist consists only of free memory areas, it is possible
 *	to allocate all the memory in a pool without incuring any structural
 *	overhead.
 *
 *	The system works best when allocating similarly-sized chunks of
 *	memory.
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

#define POOLSIZE	65536

#define MEMNODE_SIZE_MASK	((sizeof(char *) == 4) ? 7 : 15)

Prototype void *nzalloc(MemPool **mpool, int bytes);
Prototype void *zalloc(MemPool **mpool, int bytes);
Prototype char *zallocStr(MemPool **pmp, const char *s);
Prototype char *zappendStr(MemPool **pmp, char **pbuf, const char *s1, const char *s2);
Prototype char *zallocStrTrim(MemPool **pmp, const char *s, int l);
Prototype char *zallocStrTrim2(MemPool **pmp, int trimwsafter, const char *s, int l);
Prototype void zfree(MemPool **mpool, void *ptr, int bytes);
Prototype void zfreeStr(MemPool **pmp, char **ps);
Prototype void allocPool(MemPool **mpool, int bytes);
Prototype void freePool(MemPool **mpool);

MemPool *initPool(int bytes);

void *
nzalloc(MemPool **pmp, int bytes)
{
    MemPool *mp;

    /* 8 or 16-byte alignment required, depending on the pointer size */
    bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

    for(mp = *pmp; mp != NULL; mp = mp->mp_Next) {
	if (bytes <= mp->mp_Size - mp->mp_Used) {
            MemNode **pmn;
            MemNode *mn;

            for (pmn = &mp->mp_First; (mn = *pmn) != NULL; pmn = &mn->mr_Next) {
                if (bytes <= mn->mr_Bytes) {
                    /*
                     *  Cut a chunk of memory out of the beginning of this
                     *  block and fixup the link appropriately.
                     */
		    char *ptr = (char *)mn;
                    if (mn->mr_Bytes == bytes) {
                        *pmn = mn->mr_Next;
                    } else {
                        mn = (MemNode *)((char *)mn + bytes);
                        mn->mr_Next  = ((MemNode *)ptr)->mr_Next;
                        mn->mr_Bytes = ((MemNode *)ptr)->mr_Bytes - bytes;
                        *pmn = mn;
                    }
                    mp->mp_Used += bytes;
                    return(ptr);
                }
            }
        }
    }
    /*
     * Failed to locate sufficient memory, allocate another
     * pool.
     */
    allocPool(pmp, ((bytes < POOLSIZE) ? POOLSIZE : bytes));
    return(zalloc(pmp, bytes));
}

void *
zalloc(MemPool **pmp, int bytes)
{
    void *ptr = nzalloc(pmp, bytes);
    bzero(ptr, bytes);
    return(ptr);
}

char *
zallocStr(MemPool **pmp, const char *s)
{
    char *r = zalloc(pmp, strlen(s) + 1);
    strcpy(r, s);
    return(r);
}

char *
zappendStr(MemPool **pmp, char **pbuf, const char *s1, const char *s2)
{
    int l = ((s1) ? strlen(s1) : 0) + ((s2) ? strlen(s2) : 0);
    int o = 0;
    char *ptr = NULL;

    if (*pbuf) {
	o = strlen(*pbuf);
	ptr = zalloc(pmp, l + o + 1);
	strcpy(ptr, *pbuf);
	zfree(pmp, *pbuf, o + 1);
	*pbuf = NULL;
    } else {
	ptr = zalloc(pmp, l + 1);
    }
    ptr[o] = 0;
    if (s1)
	strcat(ptr + o, s1);
    if (s2)
	strcat(ptr + o, s2);
    *pbuf = ptr;
    return(ptr);
}

char *
zallocStrTrim(MemPool **pmp, const char *s, int l)
{
    char *r;

    while (l && (*s == ' ' || *s == '\t')) {
	++s;
	--l;
    }
    --l;
    while (l >= 0 &&
	    (s[l] == '\r' || s[l] == '\n' || s[l] == ' ' || s[l] == '\t')
    ) {
	--l;
    }
    ++l;

    r = zalloc(pmp, l + 1);
    bcopy(s, r, l);
    r[l] = 0;
    return(r);
}

/*
 * Same as zallocStrTrim, but also removes embedded \r and \n,
 * and removes whitespace after the 'trimwsafter' character.
 * For example with trimwsafter set to ',':
 * foo.bar1, foo.bar2,\r\n\tfoo.bar3 => foo.bar1,foo.bar2,foo.bar3
 */
char *
zallocStrTrim2(MemPool **pmp, int trimwsafter, const char *s, int l)
{
    char *r = NULL;
    int i, n, doit;
    int trimws = 0;

    while (l && (*s == ' ' || *s == '\t')) {
	++s;
	--l;
    }
    --l;
    while (l >= 0 &&
	    (s[l] == '\r' || s[l] == '\n' || s[l] == ' ' || s[l] == '\t')
    ) {
	--l;
    }
    ++l;

    n = 0;
    for (doit = 0; doit < 2; doit++) {
	if (doit) {
	    r = zalloc(pmp, n + 1);
	    n = 0;
	}
	for (i = 0; i < l; i++) {
	    /* skip cr and lf */
	    if (s[i] == '\r' || s[i] == '\n')
		continue;
	    /* skip whitespace after the 'trimwsafter' character */
	    if (trimws && (s[i] == ' ' || s[i] == '\t'))
		continue;
	    if (doit)
		r[n] = s[i];
	    n++;
	    trimws = (s[i] == trimwsafter);
	}
    }
    r[n] = 0;
    return(r);
}

void
zfree(MemPool **pmp, void *ptr, int bytes)
{
    MemPool *mp;

    /* 8 or 16-byte alignment required, depending on the pointer size */
    bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

    while ((mp = *pmp) != NULL) {
	if ((char *)ptr >= (char *)mp->mp_Base && (char *)ptr < (char *)mp->mp_Base + mp->mp_Size) {
	    MemNode **pmn;
	    MemNode *mn;

	    mp->mp_Used -= bytes;

	    for (pmn = &mp->mp_First; (mn = *pmn) != NULL; pmn = &mn->mr_Next) {
		/*
		 * If area between last node and current node
		 *  - check range
		 *  - check merge with next area
		 *  - check merge with previous area
		 */
		if ((char *)ptr <= (char *)mn) {
		    /*
		     * range check
		     */
		    if ((char *)ptr + bytes > (char *)mn) {
			logit(LOG_CRIT, "zfree(%08lx,%d) failed1, corrupt memlist", (long)ptr, bytes);
			exit(1);
		    }

		    /*
		     * merge against next area or create independant area
		     */

		    if ((char *)ptr + bytes == (char *)mn) {
			((MemNode *)ptr)->mr_Next = mn->mr_Next;
			((MemNode *)ptr)->mr_Bytes= bytes + mn->mr_Bytes;
		    } else {
			((MemNode *)ptr)->mr_Next = mn;
			((MemNode *)ptr)->mr_Bytes= bytes;
		    }
		    *pmn = mn = (MemNode *)ptr;

		    /*
		     * merge against previous area (if there is a previous
		     * area).
		     */

		    if (pmn != &mp->mp_First) {
			if ((char *)pmn + ((MemNode *)pmn)->mr_Bytes == (char *)ptr) {
			    ((MemNode *)pmn)->mr_Next = mn->mr_Next;
			    ((MemNode *)pmn)->mr_Bytes += mn->mr_Bytes;
			}
		    }
		    return;
		}
		if ((char *)ptr < (char *)mn + mn->mr_Bytes) {
		    logit(LOG_CRIT, "zfree(%08lx,%d) failed2, corrupt memlist", (long)ptr, bytes);
		    exit(1);
		}
	    }
	    /*
	     * We are beyond the last MemNode, append new MemNode.  Merge against
	     * previous area if possible.
	     */
	    ((MemNode *)ptr)->mr_Next = NULL;
	    ((MemNode *)ptr)->mr_Bytes = bytes;
	    *pmn = mn = (MemNode *)ptr;
	    return;
	}
	pmp = &mp->mp_Next;
    }
    logit(LOG_CRIT, "zfree(%08lx,%d) failed3, corrupt memlist", (long)ptr, bytes);
    exit(1);
}

void
zfreeStr(MemPool **pmp, char **ps)
{
    if (*ps) {
	zfree(pmp, *ps, strlen(*ps) + 1);
	*ps = NULL;
    }
}

void
allocPool(MemPool **pmp, int bytes)
{
    MemPool *mp = calloc(sizeof(MemPool), 1);

    mp->mp_Next = *pmp;
    mp->mp_Base = pagealloc(&mp->mp_Size, bytes);
    mp->mp_First = mp->mp_Base;
    mp->mp_First->mr_Next = NULL;
    mp->mp_First->mr_Bytes = mp->mp_Size;
    *pmp = mp;
}

void
freePool(MemPool **pmp)
{
    MemPool *mp;

    while ((mp = *pmp) != NULL) {
	*pmp = mp->mp_Next;
	pagefree(mp->mp_Base, mp->mp_Size);
	free(mp);
    }
}

