
/*
 * DICMD.C	- diablo command issuer
 *
 * Issue one or more commands to the diablo parent
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

int
main(int ac, char **av)
{
    FILE *fi;
    FILE *fo;
    char buf[256];
    int r = 0;

    LoadDiabloConfig(ac, av);

    /*
     * UNIX domain socket
     */

    {
	struct sockaddr_un soun;
	int ufd;

	memset(&soun, 0, sizeof(soun));

	if ((ufd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	    perror("udom-socket");
	    exit(1);
	}
	soun.sun_family = AF_UNIX;
	sprintf(soun.sun_path, "%s", PatRunExpand(DiabloSocketPat));
	stprintf("connecting to %s", soun.sun_path);
	if (connect(ufd, (struct sockaddr *)&soun, offsetof(struct sockaddr_un, sun_path[strlen(soun.sun_path)+1])) < 0) {
	    perror("udom-connect");
	    exit(1);
	}
	stprintf("connected to %s", soun.sun_path);
	fo = fdopen(dup(ufd), "w");
	fi = fdopen(ufd, "r");
    }

    if (ac == 1) {
	while(fgets(buf, sizeof(buf), stdin) != NULL) {
	    r = 1;
	    stprintf("sending %s", buf);
	    fprintf(fo, "%s", buf);
	    fflush(fo);
	    while (fgets(buf, sizeof(buf), fi) != NULL) {
	        stprintf("received %s", buf);
		printf("%s", buf);
		if (strcmp(buf, ".\n") == 0) {
		    r = 0;
		    break;
		}
	    }
	}
    } else {
	int i;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];
	    if (*ptr != '-' || i > 1) {
	        stprintf("sending %s%s", ptr, (i + 1 < ac) ? " " : "");
		fprintf(fo, "%s%s", ptr, (i + 1 < ac) ? " " : "");
	    }
	}
	fprintf(fo, "\n");
	fprintf(fo, "quit\n");
	fflush(fo);
	r = 1;
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    stprintf("received %s", buf);
	    if (strcmp(buf, ".\n") == 0) {
		r = 0;
		break;
	    }
	    printf("%s", buf);
	}
    }
    return(r);
}

