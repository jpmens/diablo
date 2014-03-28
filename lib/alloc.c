
/*
 * LIB/ALLOC.C	- memory allocation
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 * These routines are used to allocate buffers with the express intent 
 * of being able to *truely* deallocate the memory when we free it.
 * malloc()/free() does not normally do this, which will bloat the binary
 * and cause inefficiencies on fork().
 *
 * If you are running a large number of incoming connections or outgoing
 * feeds, using either USE_ANON_MMAP or USE_FALL_MMAP is an absolute 
 * requirement.  If you are not, then it doesn't matter.
 *
 * USE_ANON_MMAP is supported on BSDish platforms.  It uses MAP_ANON to
 *		 allocate areas of swap-backed memory.
 *
 * USE_ZERO_MMAP uses MAP_PRIVATE on /dev/zero to allocate memory, which
 *               might be better than the below approach with creating a
 *               temporary file.
 *
 * USE_FALL_MMAP should be supported on all platforms.  It creates a file
 *		 in /tmp and maps it MAP_PRIVATE, then remove()s the file.
 *		 Since this is a private map, the file is not actually used
 *		 for backing store so there is no real filesystem overhead.
 *		 The memory winds up being swap-backed.
 *
 * If neither option is set, malloc/free is used.  This is not recommended
 * if you run more then 10 feeds.
 */

#include "defs.h"

Prototype void *pagealloc(int *psize, int bytes);
Prototype void pagefree(void *p, int bytes);
Prototype int ZoneFd;		/* only necessary for USE_FALL_MMAP   */
Prototype int ZoneBytes;	/* for USE_ZERO_MMAP need only ZoneFd */

static int PageSize = 0;
static int PageMask;
int ZoneFd = -1;		/* only necessary for USE_FALL_MMAP   */
int ZoneBytes = 0;		/* for USE_ZERO_MMAP need only ZoneFd */

#if USE_ANON_MMAP

/*
 * Our first choice is to use MAP_ANON if the system understands it
 */

void *
pagealloc(int *psize, int bytes)
{
    void *b;
    if (PageSize == 0) {
	PageSize = getpagesize();
	PageMask = PageSize - 1;
    }
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)	/* degenerate case */
	bytes = PageSize;

    *psize = bytes;
    b = (void *)mmap((caddr_t)0, bytes, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    if (b == NULL || b == (void *)-1) {
	logit(LOG_EMERG, "mmap/allocation/1 failed: %s", strerror(errno));
	exit(1);
    }
    return(b);
}

void
pagefree(void *page, int bytes)
{
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)
	bytes = PageSize;

    if (page != NULL) {
	munmap((caddr_t)page, bytes);
    }
}

#else
#if USE_ZERO_MMAP

/*
 * Our second choice is to use a MAP_PRIVATE mmap() on /dev/zero
 */

void *
pagealloc(int *psize, int bytes)
{
    void *b;

    if (PageSize == 0) {
	PageSize = getpagesize();
	PageMask = PageSize - 1;
    }
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)	/* degenerate case */
	bytes = PageSize;

    if (ZoneFd < 0) {
	ZoneFd=open("/dev/zero", O_RDWR);
	if (ZoneFd < 0) {
	    logit(LOG_EMERG, "unable to open %dK zone file /dev/zero: %s", 
		bytes / 1024,
		strerror(errno)
	    );
	    exit(1);
	}
	fcntl(ZoneFd, F_SETFD, 1);	/* set close-on-exec */
    }

    *psize = bytes;
    b = (void *)mmap((caddr_t)0, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE, ZoneFd, 0);
    if (b == NULL || b == (void *)-1) {
	logit(LOG_EMERG, "mmap/allocation/3 failed: %s", strerror(errno));
	exit(1);
    }
    return(b);
}

void
pagefree(void *page, int bytes)
{
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)
	bytes = PageSize;

    if (page != NULL) {
	munmap((caddr_t)page, bytes);
    }
}

#else
#if USE_FALL_MMAP

/*
 * Our third choice is to use a MAP_PRIVATE file mmap()
 */

void *
pagealloc(int *psize, int bytes)
{
    void *b;

    if (PageSize == 0) {
	PageSize = getpagesize();
	PageMask = PageSize - 1;
    }
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)	/* degenerate case */
	bytes = PageSize;

    if (ZoneFd < 0) {
	int i;
	int pid = (int)getpid();

	for (i = 0; i < 1000; ++i) {
	    char path[256];

	    sprintf(path, "/tmp/diablo.zone.%d.%d", pid, i);
	    if ((ZoneFd=open(path, O_CREAT|O_TRUNC|O_RDWR|O_EXCL, 0600)) >= 0){
		remove(path);
		break;
	    }
	}
	if (ZoneFd < 0) {
	    logit(LOG_EMERG, "unable to create %dK zone file in /tmp: %s", 
		bytes / 1024,
		strerror(errno)
	    );
	    exit(1);
	}
	fcntl(ZoneFd, F_SETFD, 1);	/* set close-on-exec */
    }

    if (bytes > ZoneBytes) {
	if (ftruncate(ZoneFd, bytes) < 0) {
	    logit(LOG_EMERG, "unable to extend zone file in /tmp: %s", 
		strerror(errno)
	    );
	    exit(1);
	}
	ZoneBytes = bytes;
    }

    *psize = bytes;
    b = (void *)mmap((caddr_t)0, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE, ZoneFd, 0);
    if (b == NULL || b == (void *)-1) {
	logit(LOG_EMERG, "mmap/allocation/2 failed: %s", strerror(errno));
	exit(1);
    }
    return(b);
}

void
pagefree(void *page, int bytes)
{
    bytes = (bytes + PageMask) & ~PageMask;

    if (bytes == 0)
	bytes = PageSize;

    if (page != NULL) {
	munmap((caddr_t)page, bytes);
    }
}

#else

void *
pagealloc(int *psize, int bytes)
{
    void *b;

    if (PageSize == 0) {
	PageSize = getpagesize();
	PageMask = PageSize - 1;
    }
    bytes = (bytes + PageMask) & ~PageMask;

    *psize = bytes;
    b = malloc(bytes);
    if (b == NULL) {
	logit(LOG_EMERG, "mmap/allocation/4 failed: %s", strerror(errno));
	exit(1);
    }
    bzero(b, bytes);
    return(b);
}

void
pagefree(void *page, int bytes)
{
    /* bytes = (bytes + PageMask) & ~PageMask; */

    if (page != NULL) {
	free(page);
    }
}

#endif
#endif
#endif

