
/*
 * LIB/LOCK.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype int xflock(int fd, int flags);
Prototype int hflock(int fd, off_t offset, int flags);

/*
 * xflock() - a global lock.  
 *
 * NOTE: cannot be used in conjuction with hflock() due to odd overlap
 * interaction.  If we create other locks, they may wind up sticking
 * around while the unlocks take holes out of the original.
 */

int 
xflock(int fd, int flags)
{
    int r;
    struct flock fl = { 0 };

    fl.l_whence = SEEK_SET;

    switch(flags & 0x0F) {
    case XLOCK_SH:
	fl.l_type = F_RDLCK;
	break;
    case XLOCK_EX:
	fl.l_type = F_WRLCK;
	break;
    case XLOCK_UN:
	fl.l_type = F_UNLCK;
	break;
    }
    r = fcntl(fd, ((flags & XLOCK_NB) ? F_SETLK : F_SETLKW), &fl);
    return(r);
}

int 
hflock(int fd, off_t offset, int flags)
{
    int r;
    struct flock fl = { 0 };

    switch(flags & 0x0F) {
    case XLOCK_SH:
	fl.l_type = F_RDLCK;
	break;
    case XLOCK_EX:
	fl.l_type = F_WRLCK;
	break;
    case XLOCK_UN:
	fl.l_type = F_UNLCK;
	break;
    }
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = 4;

    r = fcntl(fd, ((flags & XLOCK_NB) ? F_SETLK : F_SETLKW), &fl);
    if (DebugOpt > 4)
	printf("hflock fd %d offset %d flags %02x\n", fd, (int)offset, flags);
    return(r);
}

