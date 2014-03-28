/*-
 * Copyright (c) 2000-2001 Joe Greco and sol.net Network Services
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
 * NetRemote sample server
 *
 * This is Yet Another Famous Joe Greco Simple Stupid Protocol
 *  
 * My intended use was, due to a large number of reader machines that
 * maintained strong firewalls, to be able to centralize authentication
 * operations on a core set of servers so that maintenance would be
 * easier.
 *  
 * V2 protocol is a bit different from V1.  Server starts by sending the 
 * time as a 12-digit integer.  Client responds with a packet length as a
 * 12-digit integer, and a DES-encrypted packet containing a random number,
 * the server-sent time, the encryption password, a transaction type number, 
 * and (for the normal "authenticate!" transaction type) username being 
 * auth'd, and password being auth'd.  The server then returns a string to
 * report its status.  In plain text.
 *
 * Transaction types:  0 to actually auth, 1 if we are reporting status
 * of a user (i.e. disconnected and want to record stats)
 *
 * "authenticate!" transaction type:
 * dreaderd expects the status string to be in the format "100 Success" or
 * "110 readerdef" and the string may be somewhat long, since we plan to
 * allow additional tokens to be specified at some future point.
 *  
 * This bit of code is particularly appalling.
 */

#include        <stdio.h>
#include	<sys/types.h>
#include	<sys/wait.h>
#include	<sys/time.h>
#include	<sys/resource.h>
#include	<sys/uio.h>
#include	<unistd.h>
#include	<string.h>
#include	<stdlib.h>
#include	<syslog.h>
#include	<varargs.h>
#include	<errno.h>
#include	<signal.h>
#include	<des.h>
#include	"socket.h"






char *authenticate(char *user, char *pass)
{
#error "No auth defined"
	return("500 Fail");
}

void accountadd(char *user, char *acctdata)
{
#error "No accounting defined"
	/*
	 * acctdata will hold a newline-delimited string of:
	 *	ByteCount,
	 *	GrpCount,
	 *	ArtCount,
	 *	PostBytes,
	 *	PostCount,
	 *	PostFailCount,
	 *	ByteCountArticle,
	 *	ByteCountHead,
	 *	ByteCountBody,
	 *	ByteCountList,
	 *	ByteCountXover,
	 *	ByteCountXhdr,
	 *	ByteCountOther,
	 *	SessionLength,
	 *	ClientIPAddr
	 */
	return;
}





#define NETREMOTE_DATASIZE      1432

int call_processor(fd, pwd)
int fd;
char *pwd;
{
	char buffer[NETREMOTE_DATASIZE], output[NETREMOTE_DATASIZE], *ptr, *optr;
	time_t reqtime = time(NULL);
	int pktlen;
	des_cblock key;
	des_key_schedule sched;
	char *user, *pass, *acctdata;
	char *res;
	char transtype;

	alarm(60);

	/* Send 12-digit time for exchange */
	snprintf(buffer, sizeof(buffer), "%012d\n", reqtime);
	write(fd, buffer, strlen(buffer));

	/* Read 12-digit packet length in response, plus \n */
	bzero(buffer, 14);
	if (xread(fd, buffer, 13) != 13) {
		syslog(LOG_ERR, "short read from client, pktlen");
		close(fd);
		exit(1);
	}
	pktlen = atoi(buffer);
	if (pktlen < 16 || pktlen > NETREMOTE_DATASIZE) {
		syslog(LOG_ERR, "pktlen read from client, ridiculous size %d", pktlen);
		close(fd);
		exit(1);
	}

	if (xread(fd, buffer, pktlen) != pktlen) {
		syslog(LOG_ERR, "short read from client, packet");
		close(fd);
		exit(1);
	}

	des_string_to_key(pwd, &key);
	des_set_key(&key, sched);

	bzero(output, sizeof(output));
	ptr = buffer;
	optr = output;
	while (ptr < buffer + sizeof(buffer)) {
		des_ecb_encrypt((des_cblock *)ptr,(des_cblock *)optr, sched, 0);
		bzero(ptr, 8);
		ptr += 8;
		optr += 8;
	}
	ptr = output;
	while (*ptr != '\n' && ptr < (output + pktlen)) {
		ptr++;
	}
	if (*ptr == '\n') {
		ptr++;
	} else {
		syslog(LOG_ERR, "packet data, no line break found, probably invalid password");
		close(fd);
		exit(1);
	}

	/*
	 * We sorta ignore the random number, which is mostly there to
	 * generate more randomness in the data stream should you ever
	 * choose to use something other than des_ecb
	 */
	snprintf(buffer, sizeof(buffer), "%d\n%s\n", reqtime, pwd);

	if (strncmp(ptr, buffer, strlen(buffer))) {
		syslog(LOG_ERR, "packet data, probably invalid password");
		close(fd);
		exit(1);
	}

	/* Move past the time */
	while (*ptr != '\n' && ptr < (output + pktlen)) {
		ptr++;
	}
	if (*ptr == '\n') {
		ptr++;
	} else {
		syslog(LOG_ERR, "packet data, no line break found, probably invalid password");
		close(fd);
		exit(1);
	}

	/* Move past the encrypt p/w */
	while (*ptr != '\n' && ptr < (output + pktlen)) {
		ptr++;
	}
	if (*ptr == '\n') {
		ptr++;
	} else {
		syslog(LOG_ERR, "packet data, no line break found, probably invalid password");
		close(fd);
		exit(1);
	}

	/* Move past the transaction type */
	transtype = *ptr;
	while (*ptr != '\n' && ptr < (output + pktlen)) {
		ptr++;
	}
	if (*ptr == '\n') {
		ptr++;
	} else {
		syslog(LOG_ERR, "packet data, no line break found, probably invalid password");
		close(fd);
		exit(1);
	}

	if (transtype == '1') {
		/* Pull out the username and password */
		user = ptr;

		while (*ptr != '\n' && ptr < (output + pktlen)) {
			ptr++;
		}

		if (*ptr == '\n') {
			*ptr++ = '\0';	/* null terminate the username */
			pass = ptr;
			while (*ptr != '\n' && ptr < (output + pktlen)) {
				ptr++;
			}
			if (*ptr == '\n') {
				*ptr++ = '\0';	/* null terminate the password */
				res = authenticate(user, pass);
				snprintf(buffer, sizeof(buffer), "%s\n", res);
				write(fd, buffer, strlen(buffer));
				close(fd);
				exit(0);
			} else {
				syslog(LOG_ERR, "packet data, just plain odd");
				close(fd);
				exit(1);
			}
		} else {
			syslog(LOG_ERR, "packet data, just plain odd");
			close(fd);
			exit(1);
		}

		close(fd);
		exit(1);
	} else if (transtype == '2') {
		/* Pull out the username and byte count */
		user = ptr;

		while (*ptr != '\n' && ptr < (output + pktlen)) {
			ptr++;
		}

		if (*ptr == '\n') {
			*ptr++ = '\0';	/* null terminate the username */
			acctdata = ptr;	/* byte count, other stats */
			while (*ptr != '\n' && ptr < (output + pktlen)) {
				ptr++;
			}
			if (*ptr == '\n') {
				accountadd(user, acctdata);
				close(fd);
				exit(0);
			} else {
				syslog(LOG_ERR, "packet data, just plain odd");
				close(fd);
				exit(1);
			}
		} else {
			syslog(LOG_ERR, "packet data, just plain odd");
			close(fd);
			exit(1);
		}

		close(fd);
		exit(1);
	} else {
		syslog(LOG_ERR, "packet data, unknown transtype %c", transtype);
		close(fd);
		exit(1);
	}
}





int handle_connection(fd, lfd, pwd)
int fd, lfd;
char *pwd;
{
	int rval;

	if ((rval = fork()) < 0) {
		perror("fork");
		close(fd);
		return(-1);
	}
	if (rval) {
		close(fd);
		return(0);
	}
	close(lfd);
	call_processor(fd, pwd);
	exit(1);
}





void collect_dead_kids(val)
int val;
{
	int i;

	if (wait3(&i, WNOHANG, NULL) < 0) {
		perror("wait3");
	}
}




int xread(fd, buf, siz)
int fd;
char *buf;
int siz;
{
        int rval;
        int chrs = 0;

        while (siz) {
                if ((rval = read(fd, buf, siz)) <= 0) {
                        return(rval);
                }
                chrs += rval;
                siz -= rval;
                buf += rval;
        }
	return(chrs);
}

int main(argc, argv)
int argc;
char *argv[];   
{       
	int fd, lfd, i;
	char *progname = argv[0];
	char peer[64];

	if (strrchr(progname, '/')) {
		progname = strrchr(progname, '/') + 1;
	}

	if (argc < 3) {
		fprintf(stderr, "usage: netremoted listen-port password\n");
		exit(1);
	}
	if ((lfd = create_tcp_listen_socket(atoi(argv[1]), OPT_REUSEADDR, NULL)) < 0) {
                fprintf(stderr, "%s: create_tcp_listen_socket: couldn't open port\n", progname);
                exit(1);
        }

	if (signal(SIGCHLD, collect_dead_kids) < 0) {
		perror("signal");
		exit(1);
	}
	if (daemon(0, 1) < 0) {
                perror("daemon");
                exit(1);
        }
	openlog("netremoted", LOG_PID, LOG_NEWS);
	while (1) {
                if ((fd = accept_tcp_connection(lfd, 0)) < 0) {
                        syslog(LOG_ERR, "cant accept tcp connection on %d", lfd);
                        sleep(1);
                } else {
			handle_connection(fd, lfd, argv[2]);
                        close(fd);
                }
		collect_dead_kids(0);
        }
}
