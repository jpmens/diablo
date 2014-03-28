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
 * Program to lock the history file hash table in memory using mlock.
 */

#include "defs.h"

void
usage(char *progname) {
    printf("Usage: %s [-f] [-s sleeptime] historypath\n\n", progname);
    printf(" Mlock the history hash table into memory\n");
    printf("\t-a           = lock the entire history file into memory\n");
    printf("\t-f           = don't check for root user\n");
    printf("\t-s sleeptime = specify the time (in secs) between checks for new history\n");
    printf("\n");
    printf("WARNING: This program has to be run as the root user due to OS\n");
    printf("  restrictions on the mlock() function call.\n");
    printf("  Doing so can compromise security. Make sure you understand\n");
    printf("  what it does, how and why before trusting it.\n");
    exit(1);
}

int
main(int argc, char **argv) {
    extern char *optarg;
    extern int optind;
    char *progname = *argv;
    char *HistoryFile;
    int SleepTime = 5;
    int Opened = 0;
    char ch;
    struct stat st;
    int PrevIno = -1;
    int Force = 0;
    char *map = NULL;
    off_t mapsize = 0;
    int All = 0;

    optind = 1;
    while ((ch = getopt(argc, argv, "afsV")) != -1) {
	switch(ch) {
	    case 'a':
		All = 1;
		break;
	    case 'f':
		Force = 1;
		break;
	    case 's':
		SleepTime = strtol(optarg, NULL, 0);
		break;
	    case 'V':
		PrintVersion();
		break;
	    default:
		usage(argv[0]);
	}
    }

    argv += optind;
    if (*argv == NULL)
	usage(progname);
    HistoryFile = *argv;

    if (!Force && geteuid() != 0) {
	printf("This daemon must be run as root due to the use of mlock()\n");
	printf("which is only allowed to be executed by the root user\n");
	exit(1);
    }

    while (1) {
	if (!Opened) {
	    if (stat(HistoryFile, &st) == 0)
		PrevIno = st.st_ino;
	    if (All) {
		int fd;
		mapsize = st.st_size;
		fd = open(HistoryFile, O_RDONLY);
		if (fd == -1) {
		    perror("history open");
		    exit(1);
		}
		map = xmap(NULL, mapsize, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		mlock(map, mapsize);
	    } else {
		HistoryOpen(HistoryFile, HGF_MLOCK);
	    }
	    Opened = 1;
	}
	sleep(SleepTime);
	if (stat(HistoryFile, &st) != 0 || st.st_ino != PrevIno ||
					(All && st.st_size != mapsize)) {
	    if (All && map != NULL) {
		munlock(map, mapsize);
		xunmap((void *)map, mapsize);
	    } else {
		HistoryClose();
	    }
	    Opened = 0;
	    printf("New history\n");
	}
    }
}
