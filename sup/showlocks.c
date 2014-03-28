
/*
 * SHOWLOCKS.C
 *
 * SHOWLOCKS <files>
 *
 * Show all locks held by other processes on specified files
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

void ShowLocks(const char *fileName);

int
main(int ac, char **av)
{
    int i;

    for (i = 1; i < ac; ++i) {
	ShowLocks(av[i]);
    }
    return(0);
}

void
ShowLocks(const char *fileName)
{
    int fd = open(fileName, O_RDWR);
    struct flock fl = { 0 };

    if (fd < 0) {
	fprintf(stderr, "Unable to open %s\n", fileName);
	return;
    }

    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    fl.l_start = 0;
    fl.l_type = F_WRLCK;

    printf("%s:\n", fileName);

    for (;;) {
	if (fcntl(fd, F_GETLK, &fl) < 0) {
	    fprintf(stderr, "fcntl() failed on %s\n", fileName);
	    break;
	}
	if (fl.l_type == F_UNLCK)
	    break;
	printf("    start= %d len= %d type= %s pid=%d\n",
	    (int)fl.l_start,
	    (int)fl.l_len,
		((fl.l_type == F_WRLCK) ? "exclusive" : 
		 (fl.l_type == F_UNLCK) ? "end      " : 
		 (fl.l_type == F_RDLCK) ? "shared   " : 
					  "unknown  "),
	    (int)fl.l_pid
	);

	/*
	 * If lock covers rest of file, exit
	 */
	if (fl.l_len == 0)
	    break;

	/*
	 * setup for next scan
	 */

	fl.l_whence = SEEK_SET;
	fl.l_start += fl.l_len;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_pid = 0;
    }
    close(fd);
}


