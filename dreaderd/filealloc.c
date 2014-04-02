/*
 * DREADERD/FILEALLOC.C
 *
 *	Disk space (pre-)allocation routines.
 *
 *	To prevent fragmentation, it's useful to preallocate disk
 *	space. Some filesystems such as XFS and ext4 allow you to
 *	do this even beyond EOF, without changing the filesize.
 *	We need a OS-specific system call or ioctl for this though.
 *
 *	For initial file creation, we can use posix_fallocate
 *	which should be a bit more common.
 *
 * (c)Copyright 2008 Miquel van Smoorenburg.
 *    Refer to the COPYRIGHT file in the base directory of this
 *    distribution for specific rights granted.
 */

#include "defs.h"

#ifdef __linux__
#  define LINUX_SYS_FALLOCATE	1
#endif

#if LINUX_SYS_FALLOCATE
#  include <sys/types.h>
#  include <sys/syscall.h>
#  if defined(SYS_fallocate)
#    include <linux/falloc.h>
#  endif
#endif

#define MIN_PREALLOCSZ	(16*1024)
#define MAX_PREALLOCSZ	(256*1024)

Prototype int FilePreAllocSpace(int fd, off_t off, int preallocsz, int objsz);
Prototype int FileAllocSpace(int fd, off_t off, off_t len);

int
FilePreAllocSpace(int fd, off_t off, int preallocsz, int objsz)
{
#if (LINUX_SYS_FALLOCATE) && defined(SYS_fallocate)
    int e;
    static int fallocate_ok = 1;

    if (preallocsz < MIN_PREALLOCSZ)
	return 0;
    if (preallocsz > MAX_PREALLOCSZ)
	preallocsz = MAX_PREALLOCSZ;

    if (fallocate_ok) {
	/*
	 * only act if extending the file by 'objsz' would
	 * go over a n * preallocsz boundary
	 */
	if (off > 0 && (off / preallocsz == (off + objsz) / preallocsz))
		return 0;

	off = (off + preallocsz - 1) & ~(off_t)(preallocsz - 1);
	e = syscall(SYS_fallocate, fd, FALLOC_FL_KEEP_SIZE,
				(loff_t)off, (loff_t)preallocsz);
	if (e < 0 && (errno == ENOSYS || errno == EOPNOTSUPP))
		fallocate_ok = 0;
    }
#endif
    return 0;
}

int 
FileAllocSpace(int fd, off_t off, off_t len)
{
#if (LINUX_SYS_FALLOCATE) && defined(SYS_fallocate)
    int e;
    static int fallocate_ok = 1;
    static int posix_fallocate_ok = 1;

    if (fallocate_ok) {
	e = syscall(SYS_fallocate, fd, 0, (loff_t)off, (loff_t)len);
	if (e == 0)
	    return 0;
	if (e < 0 && (errno == ENOSYS || errno == EOPNOTSUPP))
	    fallocate_ok = 0;
    }
    if (posix_fallocate_ok) {
	e = posix_fallocate(fd, off, len);
	if (e == 0)
	    return 0;
	if (e < 0 && (errno == ENOSYS || errno == EOPNOTSUPP))
	    posix_fallocate_ok = 0;
    }
#endif
    return ftruncate(fd, off + len);
}

