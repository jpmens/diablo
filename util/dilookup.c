
/*
 * DILOOKUP.C	Lookup a history DB entry based on message 
 *		id or hash values
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

int DILookup(char *id);

int ScanOpt = 0;
char *ScanFile = NULL;
int QuietOpt = 0;

void
Usage(void)
{
    fprintf(stderr, "Verify that a Message-ID or Message-ID hash exists in the history file\n");
    fprintf(stderr, "and print out the history entry for it.\n\n");
    fprintf(stderr, "Usage: dilookup [-h historyfile] [-s] [<message-id>] [hv1.hv2]\n");
    fprintf(stderr, "  where:\n");
    fprintf(stderr, "\t-f FILE\t\tscan for Message-ID's or hashes in FILE\n");
    fprintf(stderr, "\t-h FILE\t\tuse FILE as the history lookup file\n");
    fprintf(stderr, "\t-s\t\tscan for Message-ID's or hashes on stdin\n");
    fprintf(stderr, "\t<message-id>\tsearch for a Message-ID\n");
    fprintf(stderr, "\thv1.hv2\t\tsearch for a Message-ID hash\n");
    exit(1);
}

int
main(int ac, char **av)
{
    char *id = NULL;
    int i;
    int r = 0;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    id = ptr;
	} else {
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'f':
		ScanFile = *ptr ? ptr : ptr + 1;
		ScanOpt = 1;
		break;
	    case 'h':
		DHistoryPat = *ptr ? ptr : ptr + 1;
		break;
	    case 'q':
		QuietOpt = 1;
		break;
	    case 's':
		ScanOpt = 1;
		break;
	    case 'V':
		PrintVersion();
		break;
	    default:
		Usage();
	    }
	}
    }

    if (ScanOpt == 0 && id == NULL)
	Usage();

    HistoryOpen(NULL, HGF_READONLY);
    LoadSpoolCtl(0, 1);

    if (ScanOpt == 0) {
	r += DILookup(id);
    } else {
	char buf[1024];
	FILE *f;

	if (ScanFile == NULL)
	    f = stdin;
	else
	    f = fopen(ScanFile, "r");
	if (f == NULL) {
	    fprintf(stderr, "Unable to open %s (%s)\n", ScanFile, strerror(errno));
	    exit(1);
	}
	while (fgets(buf, sizeof(buf), f) != NULL) {
	    char *p = strrchr(buf, ' ');

	    if (p) {
		*p++ = '\0';
		printf("%s", buf);
	    }
	    else 
		p = buf;
	    r += DILookup(p);
	}
	if (ScanFile != NULL)
	    fclose(f);
    }
    return(r);
}

int
DILookup(char *id)
{
    hash_t hv;
    History h;
    int r = 0;
    char *p;

    if (id[0] == '<' && (p = strchr(id, '>')) != NULL) {
	*++p = 0;
	hv = hhash(id);
    } else if (id[0] == 'D' && id[1] == '.') {
	int32 dummy;
	char *p = strchr(id, '/');

	if (p && p[1] == 'B' && p[2] == '.') {
	    /*
	     * dqueue data format
	     */
	    if ((id = strchr(p, '<')) != NULL && strchr(id, '>') != NULL) {
		*(strchr(id, '>') + 1) = 0;
		hv = hhash(id);
	    } else {
		fprintf(stderr, "argument error: %s\n", id);
		exit(1);
	    }
	} else {
	    /*
	     * hash code format 1
	     */
	    if (sscanf(id + 2, "%x/%x.%x", &dummy, &hv.h1, &hv.h2) != 3) {
		fprintf(stderr, "argument error: %s\n", id);
		exit(1);
	    }
	}
    } else if (strncmp(id, "DUMP ", 5) == 0) {
	if (sscanf(id + 5, "%x.%x", &hv.h1, &hv.h2) != 2) {
	    fprintf(stderr, "argument error: %s\n", id);
	    exit(1);
	}
    } else if (sscanf(id, "%x.%x", &hv.h1, &hv.h2) != 2) {
	/*
	 * hash code format 2
	 */
	fprintf(stderr, "argument error: %s\n", id);
	exit(1);
    }

    if (HistoryLookupByHash(hv, &h) == 0) {
	char tbuf1[64];
	char tbuf2[64];
	char buf[1024];

	if (QuietOpt)
	    return(r);

	{
	    struct tm *tp;
	    time_t t;

	    t = h.gmt * 60;
	    tp = localtime(&t);
	    strftime(tbuf1, sizeof(tbuf1), "%d-%b-%Y %H:%M:%S", tp);

	    if (H_EXPIRED(h.exp)) {
		if (h.iter == (unsigned short)-1)
		    sprintf(tbuf2, "rejected");
		else
		    sprintf(tbuf2, "expired");
	    } else {
		sprintf(tbuf2, "valid");
	    }
	}


	if (h.boffset || h.bsize) {
	    ArticleFileName(buf, sizeof(buf), &h, ARTFILE_FILE_REL);
	    printf(" [%s hv=%08x.%08x spool=%02x gm=%d ex=%d off=%d len=%d f=%s]"
		   " GM=(%s) EX=(%s)\n",
		buf,
		h.hv.h1,
		h.hv.h2,
		(int)H_SPOOL(h.exp),
		(int)h.gmt,
		(int)h.exp,
		(int)h.boffset,
		(int)h.bsize,
		((h.exp & EXPF_HEADONLY) ? "H" : ""),
		tbuf1,
		tbuf2
	    );
	} else {
	    ArticleFileName(buf, sizeof(buf), &h, ARTFILE_DIR_REL);
	    printf(" [%s/NOFILE hv=%08x.%08x gm=%d ex=%d f=%s] GM=(%s) EX=(%s) (pre-expired)\n",
		buf,
		h.hv.h1,
		h.hv.h2,
		(int)h.gmt,
		(int)h.exp,
		((h.exp & EXPF_HEADONLY) ? "H" : ""),
		tbuf1,
		tbuf2
	    );
	}
    } else {
	printf("Not Found: %s (%08x.%08x)\n", id, hv.h1, hv.h2);
	r = 1;
    }
    return(r);
}

