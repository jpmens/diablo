
/*
 * UTIL/DFEEDRESR.C - show which dnewsfeeds entries match an article
 *
 */

#include "defs.h"

void LoadArt(void);
int fwCallBack(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize);

char *Label = NULL;
char *Newsgroups = NULL;
char *Dist = "";
char *Size = "1000";
char *NumPath = "1";
char *MessageId = NULL;
int ArtType = ARTTYPE_DEFAULT;
int HeadOnly = 0;
int Spam = 0;

void
Usage(char *progname)
{
    char *p;

    p = strrchr(progname, '/');
    if (p == NULL)
	p = progname;
    else
	p++;
    printf("This program shows which dnewsfeeds entries match an article\n");
    printf("Usage: %s [-a] [-F] [-h] [-l label ] [-N dnewsfeeds] [-n newsgroups]\n", p);
    printf("           [-t arttypes] [-D distribution] [-s size] [ -p numpath]\n");
    printf("           [-M msgid] [-d[n]]\n");
    printf("   -a  Load an article from STDIN and match it against dnewsfeeds\n");
    printf("   -d  Enable more verbose tracing of dnewsfeeds matching\n");
    printf("   -D  Set Distribution: header\n");
    printf("   -F  Dump the in-memory view of dnewsfeeds\n");
    printf("   -h  Set header-only feed\n");
    printf("   -l  Only load a single dnewsfeeds label\n");
    printf("   -m  Specify Message-ID\n");
    printf("   -N  Specify dnewsfeeds file to use (default from diablo.config)\n");
    printf("   -n  Set Newsgroups: header\n");
    printf("   -p  Set number of Patch: entries\n");
    printf("   -S  Mark the article as spam\n");
    printf("   -s  Set article size (bytes)\n");
    printf("   -t  Set article type (binary, mime, etc)\n");
    printf("   -X  Print out the spool the article would be stored into\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int LoadArticle = 0;
    int DumpFeeds = 0;
    int SpoolMatch = 0;

    if (ac < 1)
	Usage(av[0]);

    LoadDiabloConfig(ac, av);

    /*
     * Options
     */

    {
	int i;

	for (i = 1; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-')
		continue;
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'a':
		LoadArticle = 1;
		break;
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'D':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		Dist = ptr;
		break;
	    case 'd':
		if (isdigit((int)(unsigned char)*ptr)) {
		    DebugOpt = strtol(ptr, NULL, 0);
		} else {
		    --ptr;
		    while (*ptr == 'd') {
			++DebugOpt;
			++ptr;
		    }
		}
		FeedDebug = 1;
		break;
	    case 'F':
		DumpFeeds = 1;
		break;
	    case 'h':
		HeadOnly = 1;
		break;
	    case 'l':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		Label = ptr;
		break;
	    case 'M':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		MessageId = ptr;
		break;
	    case 'N':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		DNewsfeedsPat = ptr;
		break;
	    case 'n':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		Newsgroups = ptr;
		break;
	    case 'p':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		NumPath = ptr;
		break;
	    case 'S':
		Spam = 1;
		break;
	    case 's':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		Size = ptr;
		break;
	    case 't':
		if (*ptr == 0) {
		    ++i;
		    ptr = av[i];
		}
		ArtType = ArtTypeConv(ptr);
		break;
	    case 'V':
		PrintVersion();
		break;
	    case 'X':
		SpoolMatch = 1;
		break;
	    default:
		Usage(av[0]);
	    }
	}
    }

    LoadNewsFeed(0, 1, Label);

    if (LoadArticle)
	LoadArt();

    if (DumpFeeds) {
	if (Label == NULL)
	    DumpAllFeedInfo(stdout);
	else
	    DumpFeedInfo(stdout, Label);
	exit(0);
    }

    if (Newsgroups == NULL) {
	printf("Missing Newsgroups\n\n");
	Usage(av[0]);
    }

    {
	char artType[32];
	char offSize[64];
	sprintf(artType, "%06x", ArtType);
	sprintf(offSize, "0,%s", Size);

	printf("Matching:\n");
	printf("\tNewsgroups: %s\n", Newsgroups);
	printf("\tDistribution: %s\n", Dist);
	printf("\tSize: %s\n", Size);
	printf("\tMessage-ID: %s\n", MessageId != NULL ? MessageId : "NONE");
	printf("\tNumPath: %s\n", NumPath);
	printf("\tArtType: %s\n", artType);
	printf("\tHeadOnly: %s\n", HeadOnly ? "Yes" : "No");
	printf("\tSpam: %s\n", Spam ? "Yes" : "No");
	if (SpoolMatch) {
	    uint16 Spool;
	    LoadSpoolCtl(0, 1);
	    AllocateSpools(0);
	    Spool = GetSpool(MessageId, Newsgroups, atoi(Size),
                        ArtType, Label, NULL, NULL);
	    printf("\nMatched spool: ");
	    if (Spool == (uint16)-3)
		printf("DontStore\n");
	    else if (Spool == (uint16)-2)
		printf("RejectArt\n");
	    else if (Spool == (uint16)-1)
		printf("NoSpool\n");
	    else
		printf("%02d\n", Spool);
		

	    exit(0);
	}
	FeedWrite(0, fwCallBack, "", "",
			offSize, Newsgroups, NumPath, Dist,
			HeadOnly ? "1" : "0",
			artType, Spam, 0);
    }

    exit(0);
}

int
fwCallBack(const char *hlabel, const char *msgid, const char *path, const char *offsize, int plfo, int headOnly, const char *artType, const char *cSize)
{
    printf("Matched: %s\n", hlabel);
    return(1);
}

void
LoadArt(void)
{
    char buf[65536];
    int arttype = ARTTYPE_DEFAULT;
    int inHeader = 1;
    int npath = 1;
    int size = 0;

    InitArticleType();

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
	int len = strlen(buf);
	size += len;

	if (strlen(buf) == 1)
	    inHeader = 0;

        arttype = ArticleType(buf, len - 1, inHeader);

	if (len < 2)
	    continue;

	buf[len - 1] = 0;

	if (inHeader) {
	    if (strncasecmp(buf, "Message-ID:", 11) == 0)
		MessageId = strdup(buf + 11);
	    if (strncasecmp(buf, "Newsgroups:", 11) == 0)
		Newsgroups = strdup(buf + 11);
	    else if (strncasecmp(buf, "Distribution:", 13) == 0)
		Dist = strdup(buf + 13);
	    else if (strncasecmp(buf, "Path:", 5) == 0) {
		char *p = buf + 5;
		for (npath = 1, p = strchr(p, '!'); p != NULL; p = strchr(++p, '!'), npath++);
	    }
	}

    }
    sprintf(buf, "%d", npath);
    NumPath = strdup(buf);
    sprintf(buf, "%d", size);
    Size = strdup(buf);
    ArtType = arttype;
}
