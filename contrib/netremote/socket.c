/*
 * Copyright (c) 1990-1996
 *	Joe Greco.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Joe Greco.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOE GRECO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JOE GRECO OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include "socket.h"

extern int errno;

#define	DIE(x)	if (errno != EWOULDBLOCK) {perror(x);} return(-1);
#define	BACKLOG	32	/* Maximum pending incoming TCP connections allowed */
#define	LIN_DEL	3	/* Seconds to wait for pending data to flush on close */
#define	X11_DSP	6000	/* Base X11 display port number */

#define bcopy(x, y, z) memcpy((y), (x), (z))
#ifdef	ULTRIX
char *strdup(x)
char *x;
{
	char *rval;

	if (! (rval = malloc(strlen(x) + 1))) {
		return(NULL);
	}
	bcopy(x, rval, strlen(x) + 1);
	return(rval);
}
#endif




#if 0
int create_named_socket(pathname, opt)
char *pathname;
int opt;
{
	int s, i, j;
	struct sockaddr_un sun;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		DIE("create_named_socket: socket");
	}

	sun.sun_family = AF_UNIX;
	strcpy(sun.sun_path, pathname);

	i = 1;
	j = sizeof(i);
	if (opt & OPT_REUSEADDR && setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&i, j) < 0) {
		close(s);
		DIE("create_named_socket: setsockopt");
	}
	if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
		close(s);
		DIE("create_named_socket: bind");
	}
	if (listen(s, BACKLOG) < 0) {
		close(s);
		DIE("create_named_socket: listen");
	}
	if (opt & OPT_NONBLOCK && fcntl(s, F_SETFL, O_NDELAY) < 0) {
		close(s);
		DIE("create_named_socket: fcntl ndelay");
	}
	if (opt & OPT_VERBOSE) {
		printf("accepting connections on named socket %s\n", pathname);
	}
	return(s);
}
#endif





int create_tcp_listen_socket(port, opt, bindaddr)
int port, opt;
char *bindaddr;
{
	int s, i, j;
	struct sockaddr_in sin;
	struct linger lin;

	(void)memset((char *)&sin, 0, sizeof(sin));
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;
	if (bindaddr) {
		sin.sin_addr.s_addr = inet_addr(bindaddr);
	} else {
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
	}

	lin.l_onoff = 1;
	lin.l_linger = LIN_DEL;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		DIE("create_tcp_listen_socket: socket");
	}

	i = 1;
	j = sizeof(i);
	if (opt & OPT_REUSEADDR && setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&i, j) < 0) {
		close(s);
		DIE("create_tcp_listen_socket: setsockopt");
	}

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(s);
		DIE("create_tcp_listen_socket: bind");
	}

	if (listen(s, BACKLOG) < 0) {
		close(s);
		DIE("create_tcp_listen_socket: listen");
	}
	if (opt & OPT_NONBLOCK && fcntl(s, F_SETFL, O_NDELAY) < 0) {
		close(s);
		DIE("create_tcp_listen_socket: fcntl ndelay");
	}
	if (opt & OPT_VERBOSE) {
		printf("accepting connections on port %d\n", port);
	}
	return(s);
}





int accept_tcp_connection(s, opt)
int s, opt;
{
	int ns;
	struct sockaddr_in sin;
	struct linger lin;
	int addrlen = sizeof(sin);
	struct hostent *host;
	char *hostname;
	extern char *inet_ntoa();

	if ((ns = accept(s, (struct sockaddr *)&sin, &addrlen)) < 0) {
		DIE("accept_tcp_connection: accept");
	}
	if (opt & OPT_LINGER && setsockopt(ns, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin)) < 0) {
		close(ns);
		DIE("accept_tcp_connection: setsockopt linger");
	}
	if (opt & OPT_NONBLOCK && fcntl(ns, F_SETFL, O_NDELAY) < 0) {
		close(ns);
		DIE("accept_tcp_connection: fcntl ndelay");
	}
	if (opt & OPT_VERBOSE) {
		if (! (host = gethostbyaddr((char *)&sin.sin_addr.s_addr, sizeof(sin.sin_addr.s_addr), AF_INET))) {
			hostname = inet_ntoa(sin.sin_addr);
		} else {
			hostname = host->h_name;
		}
		printf("accepted tcp connection: fd %2d from %s.%d\n", ns, hostname, ntohs(sin.sin_port));
	}
	return(ns);
}





/*
 * int make_tcp_sockaddr(struct sockaddr_in *addr, char *ascii)
 *
 * Given an ASCII string, make an intelligent guess as to what the
 * user wants.  It's assumed we want an X11 socket, but others may be
 * specified.  We ambitiously try to deal with:
 *
 * dom.ain		(assumed to be port 6000)
 * a.b.c.d		( "       "      "    " )
 * dom.ain.6000
 * a.b.c.d.6000
 * dom.ain:0
 * a.b.c.d:0		
 */

int make_tcp_sockaddr(addr, ascii)
struct sockaddr_in *addr;
char *ascii;
{
	struct hostent *host;
	int dots = 0;
	int numeric = 0;
	char *str = strdup(ascii);
	char *colon = strrchr(str, ':');
	char *lastdot = strrchr(str, '.');
	char *ptr;

	if (! str) {
		DIE("make_tcp_sockaddr: malloc");
	}

	addr->sin_port = htons(X11_DSP);
	addr->sin_family = AF_INET;

	/* Count the number of dots in the address. */
	for (ptr = str; *ptr; ptr++) {
		if (*ptr == '.') {
			dots++;
		}
	}

	/* Check if it seems to be numeric. */
	numeric = isdigit(*str);

	/* If numeric and four dots, we have a.b.c.d.6000 */
	if (numeric && dots == 4) {
		*lastdot = '\0';
		addr->sin_port = htons(atoi(lastdot + 1));
	}
	/* If nonnumeric and no colon, check if the last part is a port */
	if (! numeric && ! colon && lastdot && isdigit(*(lastdot + 1))) {
		*lastdot = '\0';
		addr->sin_port = htons(atoi(lastdot + 1));
	}
	/* We might have a :0 display. */
	if (colon) {
		*colon = '\0';
		addr->sin_port = htons(atoi(colon + 1) + X11_DSP);
	}
	/* Now do we have a numeric address */
	if (numeric) {
		addr->sin_addr.s_addr = inet_addr(str);
		free(str);
		return(0);
	}
	/* Or a name */
	if (! (host = gethostbyname(str))) {
		free(str);
		DIE("make_tcp_sockaddr: gethostbyname");
	}
	bcopy(host->h_addr_list[0], (char *)&addr->sin_addr.s_addr, sizeof(addr->sin_addr.s_addr));
	free(str);
	return(0);
}





int connect_tcp_socket(address, opt, port)
char *address;
int opt, port;
{
	int s, i, j, len;
	struct sockaddr_in sin;
	struct linger lin;

	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	lin.l_onoff = 1;
	lin.l_linger = LIN_DEL;

	if (opt & OPT_VERBOSE) {
		printf("opening a connection to %s...", address);
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		DIE("connect_tcp_socket: socket");
	}

	i = 1;
	j = sizeof(i);
	if (opt & OPT_REUSEADDR && setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&i, j) < 0) {
		close(s);
		DIE("connect_tcp_socket: setsockopt");
	}

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(s);
		DIE("connect_tcp_socket: bind");
	}

	if (make_tcp_sockaddr(&sin, address) < 0) {
		close(s);
		return(-1);
	}
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(s);
		DIE("connect_tcp_socket: connect");
	}

	if (opt & OPT_LINGER && setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin)) < 0) {
		close(s);
		DIE("connect_tcp_socket: setsockopt linger");
	}
	if (opt & OPT_NONBLOCK && fcntl(s, F_SETFL, O_NDELAY) < 0) {
		close(s);
		DIE("connect_tcp_socket: fcntl ndelay");
	}
	if (opt & OPT_VERBOSE) {
		printf(" connected\n");
	}
	return(s);
}





int connect_tcp_socket_by_sockaddr(address, opt, port)
struct sockaddr_in *address;
int opt, port;
{
	int s, i, j, len;
	struct sockaddr_in sin;
	struct linger lin;

	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	lin.l_onoff = 1;
	lin.l_linger = LIN_DEL;

	if (opt & OPT_VERBOSE) {
		printf("opening a connection...", address);
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		DIE("connect_tcp_socket_by_sockaddr: socket");
	}

	i = 1;
	j = sizeof(i);
	if (opt & OPT_REUSEADDR && setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&i, j) < 0) {
		close(s);
		DIE("create_tcp_socket_by_sockaddr: setsockopt");
	}

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(s);
		DIE("connect_tcp_socket_by_sockaddr: bind");
	}
	if (connect(s, (struct sockaddr *)address, sizeof(*address)) < 0) {
		close(s);
		DIE("connect_tcp_socket_by_sockaddr: bind");
	}
	if (opt & OPT_LINGER && setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin)) < 0) {
		close(s);
		DIE("connect_tcp_socket_by_sockaddr: setsockopt linger");
	}
	if (opt & OPT_NONBLOCK && fcntl(s, F_SETFL, O_NDELAY) < 0) {
		close(s);
		DIE("connect_tcp_socket_by_sockaddr: fcntl ndelay");
	}
	if (opt & OPT_VERBOSE) {
		printf(" connected\n");
	}
	return(s);
}





