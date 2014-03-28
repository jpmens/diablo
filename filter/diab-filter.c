/*-
 * Copyright (c) 1998 Joe Greco and sol.net Network Services
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

/* Todo:	option for only filtering headers
 *		option for "early abort" like Cyclone does
 */

#include "defs.h"

Prototype int DiabFilter(char *fpath, char *loc, int wireformat);
Prototype void DiabFilter_freeMem(void);
Prototype void DiabFilterClose(int dowait);

static ssize_t writeAll(int fd, char *buf, ssize_t len);
void filter_failed(char *reason, char *fpath);
void open_filter_program(char *fpath);
int sizeit(int len);

int filter_fd_stdin = -1;
int filter_fd_stdout = -1;
int filter_fail_count = 0;
time_t filter_try_again = 0;
time_t filter_last_fail = 0;
pid_t filter_pid = 0;

int filter_abufsiz = 0;
int filter_nbufsiz = 0;
char *filter_abuf = NULL;
char *filter_nbuf = NULL;
MemPool *filter_abufPool = NULL;
MemPool *filter_nbufPool = NULL;

static ssize_t
writeAll(int fd, char *buf, ssize_t len)
{
    size_t off = 0;
    ssize_t res;

    while (len) {
	if ((res = write(fd, buf+off, len)) >= 0) {
	    off += res;
	    len -= res;
	} else
	    return res;
    }
    return off;
}

void
DiabFilter_freeMem(void)
{
	if (filter_abufPool) {
		freePool(&filter_abufPool);
		filter_abufPool = NULL;
		filter_abuf = NULL;
	}

	if (filter_nbufPool) {
		freePool(&filter_nbufPool);
		filter_nbufPool = NULL;
		filter_nbuf = NULL;
	}
}

void 
filter_failed(char *reason, char *fpath)
{
	int delay;
	time_t now = time(NULL);

	/* Yeah, yeah, it's hokey. */
	if (now - filter_last_fail < 300) {
		filter_fail_count++;
	} else {
		filter_fail_count--;
	}
	filter_last_fail = now;
	delay = filter_fail_count * filter_fail_count;
	delay = (delay > 900) ? 900 : delay;
	filter_try_again = time(NULL) + delay;
	logit(LOG_ERR, "diab-filter(%s): filter failed, %s, sleeping for %d seconds\n", fpath, reason, delay);
}

void 
DiabFilterClose(int dowait)
{
	int status, rval, loop;

	if (! (filter_fd_stdin < 0)) {
		close(filter_fd_stdin);
		filter_fd_stdin = -1;
	}
	if (! (filter_fd_stdout < 0)) {
		close(filter_fd_stdout);
		filter_fd_stdout = -1;
	}
	if (dowait && filter_pid) {
		for (loop = 0; loop < 10; loop++) {
			if ((rval = waitpid(filter_pid, &status, WNOHANG)) < 0) {
				logit(LOG_ERR, "External filter waitpid for %d failed: %m", filter_pid);
				filter_pid = 0;
				return;
			}
			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status)) {
					logit(LOG_ERR, "External filter returned exit %d", WEXITSTATUS(status));
				} else {
					logit(LOG_NOTICE, "External filter exited normally");
				}
				filter_pid = 0;
				return;
			}
			if (WIFSIGNALED(status)) {
				logit(LOG_ERR, "External filter exited on signal %d", WTERMSIG(status));
				filter_pid = 0;
				return;
			}
			sleep(1);
		}
		logit(LOG_ERR, "filter failed to exit");
		filter_pid = 0;
		return;
	}
}

void
open_filter_program(char *fpath)
{
	int stdinfds[2];
	int stdoutfds[2];
	int nfd;
	pid_t newpid;

	if (filter_try_again) {
		if (time(NULL) < filter_try_again) {
			return;
		}
		filter_try_again = 0;
	}

	if (! (*fpath == '/')) {
		/* Not a path name!  Guess that it is a TCP connection */
		if ((nfd = connect_tcp_socket(fpath, 0, 0)) < 0) {
			logit(LOG_ERR, "couldnt connect to remote filter (%s): %m", fpath);
			filter_failed("couldnt connect to remote filter", fpath);
			return;
		}
		filter_fd_stdin = nfd;
		filter_fd_stdout = nfd;

		if (fcntl(filter_fd_stdin, F_SETFD, 1) < 0) {
			logit(LOG_ERR, "fcntl filter stdin: %m");
		}
		if (fcntl(filter_fd_stdout, F_SETFD, 1) < 0) {
			logit(LOG_ERR, "fcntl filter stdout: %m");
		}

		filter_pid = 0;

		/* "Woohoo!" */
		logit(LOG_NOTICE, "filter connected to remote filter");
		return;
	}

	if (pipe(stdinfds) < 0) {
		filter_failed("cant create pipe", fpath);
		return;
	}

	if (pipe(stdoutfds) < 0) {
		filter_failed("cant create pipe", fpath);
		close(stdinfds[0]);
		close(stdinfds[1]);
		return;
	}

	/* We foolishly assume SIGPIPE has been handled elsewhere as SIG_IGN */
	/* Assumption is the mother ... XXX */

	if ((newpid = fork()) < 0) {
		filter_failed("cant create child process", fpath);
		close(stdinfds[0]);
		close(stdinfds[1]);
		close(stdoutfds[0]);
		close(stdoutfds[1]);
		return;
	}

	if (! newpid) {
		/* Child processing. */

		if (dup2(stdinfds[0], fileno(stdin)) < 0) {
			filter_failed("cant dup2 stdin", fpath);
			close(stdinfds[0]);
			close(stdinfds[1]);
			close(stdoutfds[0]);
			close(stdoutfds[1]);
			exit(1);
		}
		close(stdinfds[0]);
		close(stdinfds[1]);

		if (dup2(stdoutfds[1], fileno(stdout)) < 0) {
			filter_failed("cant dup2 stdout", fpath);
			close(fileno(stdin));
			close(stdoutfds[0]);
			close(stdoutfds[1]);
			exit(1);
		}
		close(stdoutfds[0]);
		close(stdoutfds[1]);

		execl(fpath, fpath, NULL);
		filter_failed("cant exec external filter", fpath);
		close(fileno(stdin));
		close(fileno(stdout));
		exit(1);
	}
	/* Parent processing. */

	close(stdinfds[0]);
	close(stdoutfds[1]);

	filter_fd_stdin = stdinfds[1];
	filter_fd_stdout = stdoutfds[0];

	if (fcntl(filter_fd_stdin, F_SETFD, 1) < 0) {
		logit(LOG_ERR, "fcntl filter stdin: %m");
	}
	if (fcntl(filter_fd_stdout, F_SETFD, 1) < 0) {
		logit(LOG_ERR, "fcntl filter stdout: %m");
	}

	filter_pid = newpid;

	/* "Woohoo!" */
	logit(LOG_NOTICE, "External filter launched");
	return;
}

int 
sizeit(int len)
{
	int c = 0;

	while (len >>= 1) {
		c++;
	}
	len = 2;
	while (c--) {
		len <<= 1;
	}
	return(len);
}

int 
DiabFilter(char *fpath, char *loc, int wireformat)
{
	int rval, count, eoln, nbytes, llen;
	char *aptr, *nptr, *pptr;

	if (! loc || ! (aptr = strrchr(loc, ','))) {
		return(-1);
	}

	llen = atoi(aptr + 1);	/* get article length */

	if (llen >= filter_abufsiz) {
		count = sizeit(llen);
		if (filter_abuf) {
			freePool(&filter_abufPool);
			filter_abufPool = NULL;
			filter_abuf = NULL;
		}
		aptr = nzalloc(&filter_abufPool, count);
		if (aptr == NULL) {
			logit(LOG_ERR, "zalloc %d failed", count);
			return(-1);
		}
		filter_abufsiz = count;
		filter_abuf = aptr;

		count = filter_abufsiz * 2 + 6;
		if (filter_nbuf) {
			freePool(&filter_nbufPool);
			filter_nbufPool = NULL;
			filter_nbuf = NULL;
		}
		nptr = nzalloc(&filter_nbufPool, count);
		if (nptr == NULL) {
			logit(LOG_ERR, "zalloc %d failed", count);
			return(-1);
		}
		filter_nbufsiz = count;
		filter_nbuf = nptr;
	}

	if (filter_fd_stdin < 0) {
		open_filter_program(fpath);
	}
	if (filter_fd_stdin < 0) {
		return(-1);
	}

	if ((rval = diab_read(loc, filter_abuf, filter_abufsiz)) < 0) {
#ifdef DEBUG
		logit(LOG_ERR, "diab_read failed: %s", loc);
#endif
		return(-1);
	}

	count = rval;
	aptr = filter_abuf + sizeof(SpoolArtHdr);
	nptr = filter_nbuf;
	pptr = aptr;
	eoln = 0;
	while (count--) {
		if (wireformat) {
			if (*pptr == '\r' && *aptr == '\n')
			    eoln = 1;
			pptr = aptr;
			*nptr++ = *aptr++;
		} else if (*aptr == '\n') {
			*nptr++ = '\r';
			*nptr++ = *aptr++;
			eoln = 1;
		} else {
			if (eoln) {
				if (*aptr == '.') {
					*nptr++ = '.';
				}
				eoln = 0;
			}
			*nptr++ = *aptr++;
		}
	}
	if (! eoln) {
		logit(LOG_ERR, "article did not end with a return: %s", loc);
		*nptr++ = '\r';
		*nptr++ = '\n';
	}
	if (!wireformat || rval < 5 || strncmp(nptr - 5, "\r\n.\r\n", 5) != 0) {
		*nptr++ = '.';
		*nptr++ = '\r';
		*nptr++ = '\n';
	}

	nbytes = nptr - filter_nbuf;

	/* Send the article to the filter ... */
	if ((rval = writeAll(filter_fd_stdin, filter_nbuf, nbytes)) != nbytes) {
		logit(LOG_ERR, "filter write failure: wanted to write %d, wrote %d: %m", nbytes, rval);
		filter_failed("write", fpath);
		DiabFilterClose(1);
		return(-1);
	}

	/* Get the response of the filter ... */
	/* XXX this is Pure Evil(tm) because the response isn't going to 
	 * have to be atomic */
	filter_abuf[0] = '\0';
	if ((rval = read(filter_fd_stdout, filter_abuf, filter_abufsiz)) <= 0) {
		logit(LOG_ERR, "filter read failure: got %d: %m", rval);
		filter_failed("read", fpath);
		DiabFilterClose(1);
		return(-1);
	}
	filter_abuf[rval] = '\0';
	if (DebugOpt > 1)
	    printf("External filter response: %s\n", filter_abuf);

	filter_abuf[rval] = '\0';

	if (*filter_abuf == '3') {
		return(0);
	}
	if (*filter_abuf == '4') {
		return(1);
	}
	logit(LOG_ERR, "filter read failure: got unknown response: %s",
			filter_abuf);
	return(-1);
}

