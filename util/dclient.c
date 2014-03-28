/*-
 * Copyright (c) 2000 Russell Vincent
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
 * Program to connect to a news server and execute commands and optionally
 * report on the performance handling the commands
*/

#include "defs.h"

int verbose = 0;
char *hostname;
int s;
struct timeval connectstart;
struct timeval connectend;
struct timeval commandstart;
struct timeval commandend;
struct timeval totaltime;
FILE *fp;
char line[512];
int response;
int numarts;
int lowmark;
int highmark;

void
usage(char *progname) {
    printf("Usage: %s [-f filename] [-p port] [-v] hostname [command]\n\n", progname);
    printf(" Execute a command on an NNTP server and write output to stdout\n");
    printf(" If no command is specified then read commands from STDIN\n\n");
    printf("\t-f file = read NNTP commands from a file and send them to server\n");
    printf("\t-v      = be more verbose in output\n");
    printf("\t-p port = specify the remote port (default: 119)\n");
    exit(1);
}

void
connectnews(char *hostname, char *port) {
    struct hostent *hp;
    struct servent *sp;
    struct sockaddr_in sin;
    char line[512];

    if ((hp = gethostbyname(hostname)) != NULL) {
	memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    } else {
	perror("gethostbyname");
	exit(1);
    }
    if ((sin.sin_port = atoi(port)) == 0) {
	if (!(sp = getservbyname(port, "tcp"))) {
	    perror("getservbyname");
	    exit(1);
	}
	sin.sin_port = sp->s_port;
    }
    sin.sin_port = htons(sin.sin_port);
    sin.sin_family = AF_INET;
    if ((s = socket(hp->h_addrtype, SOCK_STREAM, 0)) < 0) {
	perror("socket");
	exit(1);
    }

    gettimeofday(&connectstart, NULL);
    if (connect(s, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
	perror("connect");
	exit(1);
    }
    if ((fp = fdopen(s, "r+")) == NULL) {
	perror("fdopen");
	exit(1);
    }
    setvbuf(fp, NULL, _IOLBF, 0);
    fgets((char *)&line, 512, fp);
    if (verbose)
	printf("%s", line);
    if (strncmp(line, "200 ", 4) != 0 && strncmp(line, "201 ", 4) != 0) {
	printf("Unexpected response from server: %s", line);
	exit(2);
    }
}

void
newscommand(char *command) {
    char line[MAXLINE];
    char inp[MAXLINE];

    if (verbose)
	printf(">>%s\r\n", command ? command : "STDIN");

    while (command || fgets(inp, sizeof(inp), stdin) != NULL) {
	fflush(fp);
	if (command)
	    fprintf(fp, "%s\r\n", command);
	else
	    fprintf(fp, "%s\r\n", inp);
	rewind(fp);
	while (fgets(line, MAXLINE, fp) != NULL) {
	    printf("%s", line);
	    if (strncmp(line, ".\r\n", 3) == 0)
		break;
	}
	if (command)
	    break;
    }
}

void
dodumpfile(char *filename)
{
    FILE *f;
    char line[MAXLINE];

    if ((f = fopen(filename, "r")) == NULL) {
	printf("Unable to open %s\n", filename);
	exit(1);    
    }
    while (fgets(line, sizeof(line), f) != NULL) {
	if (strncmp(&line[strlen(line) - 2], "\r\n", 2) == 0) {
	    fprintf(fp, "%s", line);
	} else {
	    line[strlen(line) - 1] = 0;
	    fprintf(fp, "%s\r\n", line);
	    
	}
    }
    fclose(f);
}

void
newsquit(void) {
    char line[512];

    fflush(fp);
    fprintf(fp,"QUIT\r\n");
    rewind(fp);
    fgets((char *)&line, 512, fp);
    if (verbose)
	printf("%s", line);
    if (strncmp(line, "205 ", 4) != 0 && strncmp(line, "238 ", 4) != 0) {
	printf("Unexpected response from server: %s", line);
	exit(2);
    }
}

int
main(int argc, char **argv) {
    extern char *optarg;
    extern int optind;
    char ch;
    char *progname;
    char *port = "119";
    char *command;
    char *dumpfile = NULL;

    progname = *argv;

    optind = 1;
    while ((ch = getopt(argc, argv, "f:p:Vv")) != -1) {
	switch(ch) {
	    case 'f':
		dumpfile = optarg;
		break;
	    case 'p':
		port  = optarg;
		break;
	    case 'V':
		PrintVersion();
		break;
	    case 'v':
		verbose = 1;
		break;
	    default:
		usage(argv[0]);
	}
    }

    argv += optind;
    if (*argv == NULL)
	usage(progname);
    hostname = *argv++;
    if (*argv != NULL)
	command = *argv++;
    else
	command = NULL;

    connectnews(hostname, port);
    gettimeofday(&connectend, NULL);

    gettimeofday(&commandstart, NULL);
    if (dumpfile != NULL)
	dodumpfile(dumpfile);
    else
	newscommand(command);
    gettimeofday(&commandend, NULL);

    newsquit();

    gettimeofday(&totaltime, NULL);

    if (verbose) {
    	printf("Connect time: %3.3f\n",
		(((connectend.tv_sec * 1000000.0) + connectend.tv_usec) -
		((connectstart.tv_sec * 1000000.0) + connectstart.tv_usec)) /
				1000000);
	printf("Elapsed time: %3.3f\n",
		(((totaltime.tv_sec * 1000000.0) + totaltime.tv_usec) -
		((connectstart.tv_sec * 1000000.0) + connectstart.tv_usec)) /
				1000000);
	printf("Command time: %3.3f\n",
		(((commandend.tv_sec * 1000000.0) + commandend.tv_usec) -
		((commandstart.tv_sec * 1000000.0) + commandstart.tv_usec)) /
				1000000);
    }
    exit(0);
}

