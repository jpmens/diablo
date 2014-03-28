
/*
 * UTIL/DOUTQ.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

void ScanSeq(char *name, char *host, int m);

int VerboseOpt = -1;
int HostsOpt = -1;

int
main(int ac, char **av)
{
    FILE *fi;
    char *cmp = NULL;

    LoadDiabloConfig(ac, av);

    {
	int i;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-') {
		cmp = ptr;
		if (VerboseOpt < 0)
		    VerboseOpt = 1;
		continue;
	    } else {
		ptr += 2;
		switch(ptr[-1]) {
		case 'C':
		    if (*ptr == 0)
			++i;
		    break;
		case 'V':
		    PrintVersion();
		    break;
		case 'v':
		    VerboseOpt = 1;
		    break;
		case 'h':
		    HostsOpt = 1;
		    break;
		default:
		    fprintf(stderr, "Illegal option: %s\n", ptr - 2);
		    exit(1);
		}
	    }
	}
    }

    if (chdir(NewsHome) != 0) {
	perror("chdir(NewsHome)");
	exit(1);
    }
    if ((fi = fopen(PatLibExpand(DNNTPSpoolCtlPat), "r")) != NULL) {
	char buf[256];

	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    char name[256];
	    char host[256];
	    int m;

	    if (buf[0] == '\n' || buf[0] == '#')
		continue;
	    if (sscanf(buf, "%s %s %d", name, host, &m) != 3)
		continue;
	    if (cmp == NULL || strcmp(name, cmp) == 0 || strcmp(host, cmp) ==0)
		    ScanSeq(name, host, m);
	}
	fclose(fi);
    } else {
	LabelList *ll;

	LoadNewsFeed(0, 1, cmp);

	for (ll = FeedLinkLabelList(); ll != NULL && ll->label != NULL;
							ll = ll->next) {
	    NewslinkInfo *li = FeedLinkInfo(ll->label);
	    if (li == NULL)
		continue;

	    ScanSeq(ll->label, li->li_HostName, li->li_MaxQueueFile);
	}
    }
    return(0);
}

void
ScanSeq(char *name, char *host, int m)
{
    char path[256];
    int i;
    int b;
    int e;
    int c = 0;

    if (HostsOpt > 0) {
	printf("%s %s\n", name, host);
	return;
    }

    snprintf(path, sizeof(path), "%s/.%s.seq", PatExpand(DQueueHomePat), name);
    {
	int fd;
	if ((fd = open(path, O_RDONLY)) < 0) {
	    printf("%-10s no sequence file\n", name);
	    return;
	}
	bzero(path, sizeof(path));
	read(fd, path, sizeof(path) - 1);
	close(fd);
    }
    if (sscanf(path, "%d %d", &b, &e) != 2) {
	printf("%-10s bad sequencing: %s\n", name, path);
	return;
    }
    printf("%-10s %5d-%-5d (%3d/%3d files %3d%% full)\t",
	name,
	b,
	e,
	e - b,
	m,
	((m) ? (e - b) * 100 / m : 999)
    );
    fflush(stdout);
    for (i = b; i <= e; ++i) {
	int fd;

	snprintf(path, sizeof(path), "%s/%s.S%05d", PatExpand(DQueueHomePat), name, i);
	if ((fd = open(path, O_RDWR)) >= 0) {
	    if (xflock(fd, XLOCK_EX|XLOCK_NB) < 0) {
		printf("%05d ", i);
		c = 0;
	    }
	    close(fd);
	}
	if (VerboseOpt <= 0 && ++c > 25)
	    break;
    }
    printf("\n");
}

