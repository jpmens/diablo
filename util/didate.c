
/*
 * UTIL/DIDATE.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

void
Usage(char *progname)
{
    printf("Date conversion/check utility\n");
    printf("Usage: %s [-c ctime] [-g histime] [datestr]\n", progname);
    printf("\t-c ctime\tconvert unix time to string\n");
    printf("\t-g histime\tconvert history gmt value to string\n");
    printf("\tdatestr\t\tcheck the format of a date string\n");
    exit(1);
}

int
main(int ac, char **av)
{
    time_t t;
    int i = 1;

    LoadDiabloConfig(ac, av);

    while (i < ac) {
	char *p = av[i];
	if (*p == '-') {
	    p++;
	    i++;
	    switch (*p++) {
		case 'c':
			t = strtol(*p ? p : av[i++], NULL, 0);
			if (t != 0)
			    printf("%s", ctime(&t));
			else
			    printf("Bad time specification\n");
			exit(0);
		case 'g':
			t = strtol(*p ? p : av[i++], NULL, 0) * 60;
			if (t != 0)
			    printf("%s", ctime(&t));
			else
			    printf("Bad time specification\n");
			exit(0);
		default:
			Usage(av[0]);
	    }
	} else {
	    break;
	}
    }
    if (ac == 1)
	Usage(av[0]);
    t = parsedate(av[ac - 1]);
    if (t == (time_t)-1)
	printf("Illegal format\n");
    else
	printf("%s", ctime(&t));
    return(0);
}

