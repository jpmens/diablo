/*-
 * Copyright (c) 2001 Russell Vincent
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * NOTIFY.C
 *
 * Handle feed article notification
 *
 */

#include "defs.h"

Prototype void CloseNotify(int *NotifyFd, int *NotifyLockFd, char *l);
Prototype int ListenNotify(int *NotifyFd, int *NotifyLockFd, char *l);
Prototype int RegisterNotify(char *notify, int status);
Prototype int OpenFeedNotify(char *l);

int NotifyConnectFailCount = 0;

/*
 * Close the notify socket, unlock the lock file and remove both files
 * The file removal is done before the unlock+close to prevent race conditions
 */
void
CloseNotify(int *NotifyFd, int *NotifyLockFd, char *l)
{
    char path[PATH_MAX];
    if (*NotifyFd != -1) {
	sprintf(path, "%s.%s", PatRunExpand(DFeedNotifySocketPat), l);
	remove(path);
	close(*NotifyFd);
	*NotifyFd = -1;
	sprintf(path, "%s.%s", PatRunExpand(DFeedNotifyLockPat), l);
	remove(path);
	xflock(*NotifyLockFd, XLOCK_UN);
	close(*NotifyLockFd);
	*NotifyLockFd = -1;
    }
}

/*
 * Listen on a Unix domain DGRAM socket for 1 byte packets
 * Obtain a lock on a separate file because some OS's (Note: FreeBSD)
 * don't seem to allow locks on unix domain sockets
 */
int
ListenNotify(int *NotifyFd, int *NotifyLockFd, char *l)
{
    struct sockaddr_un soun;

    if (*NotifyFd != -1)
	return(1);

    if (*NotifyLockFd == -1) {
	char lckBuf[PATH_MAX];
	char buf[64];
	int n;

	sprintf(lckBuf, "%s.%s", PatRunExpand(DFeedNotifyLockPat), l);
	*NotifyLockFd = open(lckBuf, O_RDWR|O_CREAT, 0644);
	if (*NotifyLockFd == -1) {
	    logit(LOG_ERR, "Cannot create notify lock: %s", strerror(errno));
	    *NotifyLockFd = -1;
	    return(0);
	}
	if (xflock(*NotifyLockFd, XLOCK_EX|XLOCK_NB) == -1) {
	    if (errno != EAGAIN)
		logit(LOG_INFO, "Cannot obtain notify lock: %s",
							strerror(errno));
	    close(*NotifyLockFd);
	    *NotifyLockFd = -1;
	    return(0);
	}
	n = sprintf(buf, "%d\n", getpid());
	write(*NotifyLockFd, buf, n);
    }
    if ((*NotifyFd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
	logit(LOG_ERR, "Cannot obtain notify socket: %s", strerror(errno));
	*NotifyFd = -1;
	return(0);
    }
    if (fcntl(*NotifyFd, F_SETFL, O_NONBLOCK) == -1) {
	logit(LOG_ERR, "Unable to set notify socket as non-blocking: %s",
							strerror(errno));

	close(*NotifyFd);
	*NotifyFd = -1;
	return(0);
    }
    bzero(&soun, sizeof(soun));
    soun.sun_family = AF_UNIX;
    sprintf(soun.sun_path, "%s.%s", PatRunExpand(DFeedNotifySocketPat), l);
    remove(soun.sun_path);
    if (bind(*NotifyFd, (struct sockaddr *)&soun, sizeof(soun)) < 0) {
	logit(LOG_ERR, "Unable to bind notify socket: %s", strerror(errno));
	close(*NotifyFd);
	*NotifyFd = -1;
	RegisterNotify(l, 0);
	return(0);
    }
    chmod(soun.sun_path, 0770);
    return(1);
}

/*
 * Register with the feed notification system on the master diablo process
 *
 * We send a request that is equiv to: dicmd feednotify on|off:label
 * The diablo process responds by sending 1 byte packets to the UNIX
 * domain DGRAM socket (made from '.feednotify.label') when an article
 * is ready to be fed.
 */
int
RegisterNotify(char *notify, int status)
{
    struct sockaddr_un soun;
    int ufd;
    FILE *fo;

    memset(&soun, 0, sizeof(soun));

    if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	logit(LOG_ERR, "Unable to obtain notify server socket: %s",
							strerror(errno));
	return(0);
    }
    soun.sun_family = AF_UNIX;
    sprintf(soun.sun_path, "%s", PatRunExpand(DiabloSocketPat));
    if (connect(ufd, (struct sockaddr *)&soun, offsetof(struct sockaddr_un, sun_path[strlen(soun.sun_path)+1])) < 0) {
	if (NotifyConnectFailCount++ % 100 == 1)
	    logit(LOG_ERR, "Cannot connect to notify server: %s",
							strerror(errno));
	close(ufd);
	return(0);
    }
    fo = fdopen(ufd, "w");
    fprintf(fo, "feednotify %s:%s\n", status ? "on" : "off", notify);
    fflush(fo);
    fclose(fo);
    NotifyConnectFailCount = 0;
    return(1);
}

/*
 * Handle feed registration and notification
 *
 * We get a dicmd command of 'feednotify switch: on|off:label
 *
 * We then send 1 byte packets to the Unix domain DGRAM socket when
 * an article has been flushed to the dqueue file for that label.
 */
int
OpenFeedNotify(char *l)
{
    struct sockaddr_un soun;
    int fd;

    if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
	    logit(LOG_ERR, "Cannot obtain notify socket (%s)", strerror(errno));
	    return(-1);
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
	logit(LOG_ERR, "Unable to set notify socket as non-blocking: %s (%s)",
						soun.sun_path, strerror(errno));
	close(fd);
	return(-1);
    }
    bzero(&soun, sizeof(soun));
    soun.sun_family = AF_UNIX;
    sprintf(soun.sun_path, "%s.%s", PatRunExpand(DFeedNotifySocketPat), l);
    if (connect(fd, (struct sockaddr *)&soun, sizeof(soun)) < 0) {
	if (errno != ENOENT) {
	    logit(LOG_ERR, "Cannot connect to notify socket at %s (%s)",
						soun.sun_path, strerror(errno));

	}
	close(fd);
	return(-1);
    }
    return(fd);
}

