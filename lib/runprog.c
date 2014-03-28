
/*
 * LIB/RUNPROG.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype pid_t RunProgramPipe(int rfds[3], int flags, char **argv, char **env);

void SetDevNull(int dfd);

#define MASTER_IN	0
#define SLAVE_OUT	1
#define SLAVE_IN	2
#define MASTER_OUT	3
#define MASTER_ERR_IN	4
#define SLAVE_ERR_OUT	5

pid_t
RunProgramPipe(int rfds[3], int flags, char **argv, char **env)
{
    pid_t pid = 0;
    int fds[6] = { -1, -1, -1, -1, -1, -1 };

    if (((flags & RPF_STDIN) ? pipe(&fds[0]) : 0) == 0 && 
	((flags & RPF_STDOUT) ? pipe(&fds[2]) : 0) == 0 && 
	((flags & RPF_STDERR) ? pipe(&fds[4]) : 0) == 0
    ) {
	fflush(stdout);
	fflush(stderr);

	if ((pid = fork()) == 0) {
	    /*
	     * exec pgpverify w/ stdin and stdout
	     */
	    int fd;
	    int i;

	    if (flags & RPF_STDOUT) {
		dup2(fds[SLAVE_IN], 0);
	    } else {
		SetDevNull(0);
	    }
	    if (flags & RPF_STDIN) {
		dup2(fds[SLAVE_OUT], 1);
	    } else {
		SetDevNull(1);
	    }
	    if (flags & RPF_STDERR) {
		dup2(fds[SLAVE_ERR_OUT], 2);
	    } else {
		SetDevNull(2);
	    }
	    for (i = 0; i < arysize(fds); ++i) {
		if (fds[i] >= 0)
		    close(fds[i]);
	    }
	    for (fd = 3; fd < MAXFDS; ++fd)
		close(fd);
	    if (env == NULL)
		execv(argv[0], argv + 1);
	    else
		execve(argv[0], argv + 1, env);
	    _exit(9);
	}
	if (pid > 0) {
	    if (flags & RPF_STDIN) {
		rfds[0] = fds[MASTER_IN];
		fds[MASTER_IN] = -1;
	    }
	    if (flags & RPF_STDOUT) {
		rfds[1] = fds[MASTER_OUT];
		fds[MASTER_OUT] = -1;
	    }
	    if (flags & RPF_STDERR) {
		rfds[2] = fds[MASTER_ERR_IN];
		fds[MASTER_ERR_IN] = -1;
	    }
	}
    }
    {
	int i;
	for (i = 0; i < arysize(fds); ++i) {
	    if (fds[i] >= 0)
		close(fds[i]);
	}
    }
    return(pid);
}

void
SetDevNull(int dfd)
{
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
	if (fd != dfd) {
	    dup2(fd, dfd);
	    close(fd);
	}
    }
}
