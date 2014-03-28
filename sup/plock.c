
/*
 * PLOCK.C
 *
 * PLOCK [-r] [-t] <file> <program...>
 *
 * Exclusively lock the specified file, run the program, then
 * unlock.  
 *
 * 	-t :  exit if the lock would block
 *	-r :  use shared lock rather then exclusive lock
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

int
main(int ac, char **av)
{
    int fd;
    int i;
    int j;
    int len;
    int pid;
    int blockExit = 0;
    int type = XLOCK_EX;
    char *buf;

    if (ac == 1) {
	puts("plock [-r][-t] <file> <program...>");
	exit(0);
    }
    for (i = 1; i < ac && av[i][0] == '-'; ++i) {
	char *ptr = av[i] + 2;
	switch(ptr[-1]) {
	case 'r':
	    type = XLOCK_SH;
	    break;
	case 't':
	    blockExit = 1;
	    break;
	default:
	    fprintf(stderr, "plock: unknown option %s\n", ptr - 2);
	    exit(1);
	}
    }
    if ((fd = open(av[i], O_RDWR|O_CREAT, 0600)) < 0) {
	printf("unable to open/create %s\n", av[i]);
	exit(1);
    }
    if (xflock(fd, type|XLOCK_NB) < 0) {
	if (errno == EWOULDBLOCK) {
	    if (blockExit) {
		printf("lock already held, exiting\n");
		exit(1);
	    }
	    printf("waiting on lock %s\n", av[i]);
	    if (xflock(fd, type) < 0) {
		perror("plock, flock");
		exit(1);
	    }
	    printf("got it!\n");
	} else {
	    perror("plock, flock");
	    exit(1);
	}
    }
    for (len = 0, j = i + 1; j < ac; ++j) {
	len += strlen(av[j]) + 1;
    }
    buf = malloc(len);
    for (len = 0, j = i + 1; j < ac; ++j) {
	sprintf(buf + len, "%s", av[j]);
	len += strlen(buf + len);
	if (j + 1 < ac)
	    buf[len++] = ' ';
    }
    buf[len] = 0;
    if ((pid = fork()) == 0) {
	close(fd);
	execl("/bin/sh", "sh", "-c", buf, NULL);
	exit(0);
    }
    waitpid(pid, NULL, 0);

    xflock(fd, XLOCK_UN);
    close(fd);

    return(0);
}

