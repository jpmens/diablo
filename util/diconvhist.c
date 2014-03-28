
/*
 * DICONVHIST.C		append an INN history file to a diablo dhistory
 *			file.  Used to prime a diablo history file during
 *			switchover from INN to diablo.  Priming MAY
 *			run in parallel to diablo operation.
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

int
main(int ac, char **av)
{
    char buf[16384];
    int cnt = 0;
    int rep = 0;
    int enters = 0;
    int dups = 0;
    int ignores = 0;
    int lco = 0;
    int lskip = 0;
    int i;
    int fastOpt = 0;
    const char *innhName = NULL;
    uint32 gmt = time(NULL) / 60;
    FILE *fi;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-' || ptr[1] == 0) {
	    innhName = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'f':
	    fastOpt = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    break;
	}
    }

    if (innhName == NULL) {
	puts("diconvhistory [-f] innhistoryfile");
	exit(0);
    }

    if (strcmp(innhName, "-") == 0) {
	if ((fi = fdopen(dup(0), "r")) == NULL) {
	    perror("fdopen");
	    exit(1);
	}
    } else {
	if ((fi = fopen(innhName, "r")) == NULL) {
	    perror("fopen");
	    exit(1);
	}
    }

    HistoryOpen(NULL, 0);

    while (fgets(buf, sizeof(buf), fi) != NULL) {
	char *p = buf;

	if (lskip) {
	    --lskip;
	    ++rep;
	    ++dups;
	    ++cnt;
	    ++ignores;
	    continue;
	}

	if (p[0] != '<')
	    continue;
	while (*p && *p != '>')
	    ++p;
	if (*p != '>')
	    continue;
	p[1] = 0;
	if (HistoryLookup(buf, NULL) != 0) {
	    History h = { 0 };

	    h.hv = hhash(buf);
	    h.iter = (unsigned short)-1;
	    h.exp |= EXPF_EXPIRED;
	    h.gmt = gmt;
	    HistoryAdd(buf, &h);
	    ++enters;
	    lco = 0;
	} else {
	    ++dups;
	    if (fastOpt && ++lco == 10) {
		lco = 9;
		lskip = 100;
	    }
	}
	++cnt;
	if (++rep >= 1000) {
	    rep = 0;
	    printf(" scan=%d add=%d dup=%d ignored=%d\n", cnt, enters, dups, ignores);
	}
    }
    printf(" scan=%d add=%d dup=%d ignored=%d\n", cnt, enters, dups, ignores);
    return(0);
}

