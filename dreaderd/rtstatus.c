
/*
 * DREADERD/MAIN.C - diablo newsreader
 *
 *	This is the startup for the dreaderd daemon.
 *
 *	The diablo newsreader requires one or remote diablo news boxes to 
 *	obtain articles from, but keeps track of article assignments to groups
 *	and overview information locally.
 *
 *	Operationally, dreaderd runs as a standalone multi-forking, 
 *	multi-threaded daemon.  It forks X resolvers and Y readers.  Each
 *	reader can support N connections (NxY total connections) and maintains
 *	its own links to one or more news servers (diablo, INN, whatever). 
 *	Overview information is utilized and articles are accessed by 
 *	message-id.
 *
 *	The diablo newsreader is also able to accept header-only feeds from
 *	one or more diablo servers via 'mode headfeed'.  Processing of these
 *	feeds is not multi-threaded, only processing of newsreader functions is
 *	multithreaded.  The diablo newsreader does not operate with a dhistory
 *	file.  If taking a feed from several sources you must also run the
 *	diablo server on the same box and use that to feed the dreaderd system,
 *	though the diablo server need not store any articles itself.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void RTStatusOpen(const char *fileName, int slotBase, int numSlots);
Prototype void RTStatusBase(int slot, const char *ctl, ...);
Prototype void RTStatusUpdate(int slot, const char *ctl, ...);
Prototype void RTStatusClose(void);

#define RTLINESIZE	140

int RTSFd = -1;
int RTSlotBase;
int RTBaseLen;

void
RTStatusOpen(const char *fileName, int slotBase, int numSlots)
{
    /*
     * Each process needs its own copy of RTSFd so the seek 
     * position is not shared.
     */
    if (RTSFd >= 0) {
	close(RTSFd);
	RTSFd = open(fileName, O_RDWR, 0644);
    } else {
	RTSFd = open(fileName, O_RDWR|O_CREAT|O_TRUNC, 0644);
    }

    RTSlotBase = slotBase;

    if (RTSFd >= 0) {
	int i;

	lseek(RTSFd, slotBase * RTLINESIZE, 0);

	for (i = 0; i < numSlots; ++i) {
	    RTStatusBase(i, "");
	}
    }
}

void
RTStatusClose(void)
{
    if (RTSFd >= 0)
	close(RTSFd);
}

void
RTStatusBase(int slot, const char *ctl, ...)
{
    va_list va;
    char buf[RTLINESIZE];

    if (slot >= 0 && RTSFd >= 0) {
	sprintf(buf, "%05d %04d %010u ", (int)getpid(), slot + RTSlotBase, (int)time(NULL));

	/*
	 * WARNING!  snprintf() may not understand full printf semantics,
	 *	     see lib/snprintf.c
	 */

	va_start(va, ctl);
	vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ctl, va);
	va_end(va);

	RTBaseLen = strlen(buf);
	if (RTBaseLen > RTLINESIZE/2)
	    RTBaseLen = RTLINESIZE/2;
	memset(buf + RTBaseLen, ' ', RTLINESIZE - RTBaseLen);
	buf[RTLINESIZE-1] = '\n';
	lseek(RTSFd, (RTSlotBase + slot) * RTLINESIZE, 0);
	write(RTSFd, buf, RTLINESIZE);
    }
}

void
RTStatusUpdate(int slot, const char *ctl, ...)
{
    int ulineSize = RTLINESIZE - RTBaseLen;
    va_list va;
    char buf[RTLINESIZE];

    if (slot >= 0 && RTSFd >= 0) {
	int len;

	buf[0] = ' ';
	va_start(va, ctl);
	vsnprintf(buf + 1, ulineSize - 1, ctl, va);
	va_end(va);
	len = strlen(buf);
	memset(buf + len, ' ', ulineSize - len);
	buf[ulineSize-1] = '\n';
	lseek(RTSFd, (RTSlotBase + slot) * RTLINESIZE + RTBaseLen, 0);
	write(RTSFd, buf, ulineSize);
    }
}

