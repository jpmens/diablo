
/*
 * LIB/XMAP.C
 *
 * Some machines fail if mmap() is passed with odd arguments.  The xmap() calls
 * page-align everything, allowing non-aligned offsets and sizes to be 
 * requested.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void *xmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
Prototype void xadvise(const void *ptr, off_t bytes, int how);

Prototype void xunmap(void *addr, size_t len);

static int XPageMask;

void *
xmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    off_t n;
    void *ptr;
    struct stat st;

    if (XPageMask == 0)
	XPageMask = getpagesize() - 1;
    n = offset & XPageMask;
    offset -= n;
    len += n;
    len = (len + XPageMask) & ~XPageMask;

    st.st_size = 0;
    fstat(fd, &st);
    if (offset + len > ((st.st_size + XPageMask) & ~XPageMask)) {
        ptr = NULL;
	errno = EINVAL;
    } else {
        ptr = (void *)mmap((caddr_t)addr, len, prot, flags, fd, offset);
    }
    if (ptr == (void *)-1)
	ptr = NULL;
    if (ptr)
	return((char *)ptr + n);
    return(NULL);
}

void 
xadvise(const void *ptr, off_t bytes, int how)
{
#if USE_MADVISE
    /*
     * Some systems require page-bounded requests.
     * Satisfy them.  Note: can use int for off, but
     * long may be more optimal on 64 bit systems.
     */
    {
	size_t off;

	if (XPageMask == 0)
	    XPageMask = getpagesize() - 1;

	off = (size_t)ptr & XPageMask;

	ptr = (const char *)ptr - off;
	bytes = bytes + off;
	bytes = (bytes + XPageMask) & ~XPageMask;
    }

    switch(how) {
#ifdef MADV_WILLNEED
    /*
     * pre-fault pages into the page table that are in the 
     * buffer cache
     */
    case XADV_WILLNEED:
	madvise((caddr_t)ptr, bytes, MADV_WILLNEED);
	break;
#endif
#ifdef MADV_SEQUENTIAL
    /*
     * Cause page faults to read-ahead more.
     */
    case XADV_SEQUENTIAL:
	madvise((caddr_t)ptr, bytes, MADV_SEQUENTIAL);
	break;
    }
#endif
#endif
}

void
xunmap(void *ptr, size_t len)
{
    size_t n;

    /*
     * Some systems require page-bounded arguments
     * Satisfy them.  Note: 'n' can be an int since
     * it is only used to hold the masked offset.
     * However, using long may be more optimal on 
     * 64 bit systems.
     *
     * JG20080801 - somebody obviously wasn't aware
     * of the typical sizes of types on 32/64
     */

    if (XPageMask == 0)
	XPageMask = getpagesize() - 1;

    n = (size_t)ptr & XPageMask;
    ptr = (char *)ptr - n;
    len += n;
    len = (len + XPageMask) & ~XPageMask;

    if (munmap((caddr_t)ptr, len) < 0) {
	logit(LOG_CRIT, "munmap fails");
    }
}

