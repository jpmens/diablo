
/*
 * DREADART [-h] [-v] 'messageid'
 * DREADART [-h] [-v] < file
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

int DumpArticle(char *fname, History *h, const char *msgid);
int LookupHash(hash_t hv, const char *msgid, History *ph);

int VerifyOnly = 0;
int HeadOnly = 0;
int ForceOpt = 0;
int StripCR = 1;
int QuietOpt = 0;
int ShowFileHeader = 0;
FILE *LogFo;

void
Usage(char *progname)
{
    printf("Retrieve an article from the spool\n");
    printf("Usage: dreadart [-F] [-f scanfile] [-H] [-h] [-s] OBJECT\n");
    printf("\n");
    printf("\t-F\tforce retrieval, ignoring possible errors\n");
    printf("\t-f file\tspecify a file containing Message-ID's to retrieve\n");
    printf("\t-H\tonly show file header data\n");
    printf("\t-h\tonly retrieve article header\n");
    printf("\t-q\tdon't display the article/header\n");
    printf("\t-s\tdon't strip CR from each line (if wireformat)\n");
    printf("\n");
    printf("    OBJECT can be one of:\n");
    printf("\t<message-id>\t\tMessage-ID\n");
    printf("\thv1.hv2\t\t\tMessage-ID hash\n");
    printf("\t/path/file:offset,size\tfile offset\n");
    printf("\n");
    exit(1);
}

int
main(int ac, char **av)
{
    int r = 0;
    int i;
    char *arg = NULL;
    char *file = NULL;

    LogFo = stderr;

    LoadDiabloConfig(ac, av);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    arg = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'C':
	    if (*ptr == 0)
		++i;
	    break;
	case 'd':
	    DebugOpt = atoi(*ptr ? ptr : av[++i]);
	    break;
	case 'F':
	    ForceOpt = 1;
	    break;
	case 'f':
	    file = (*ptr) ? ptr : av[++i];
	    break;
	case 'H':
	    ShowFileHeader = 1;
	    break;
	case 'h':
	    HeadOnly = 1;
	    break;
	case 'q':
	    QuietOpt = 1;
	    break;
	case 's':
	    StripCR = 0;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	case 'v':
	    VerifyOnly = 1;
	    LogFo = stdout;
	    break;
	default:
	    fprintf(stderr, "dreadart: Illegal option: %s\n", ptr - 2);
	    Usage(av[0]);
	}
    }

    if (arg == NULL && file == NULL)
	Usage(av[0]);

    HistoryOpen(NULL, HGF_READONLY);
    LoadSpoolCtl(0, 1);

    if (arg == NULL) {
	char buf[8192];
	char msgid[MAXMSGIDLEN];
	FILE *fi = (strcmp(file, "-") == 0) ? stdin : fopen(file, "r");

	if (fi) {
	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		hash_t hv;
		char *m;

		if (strncmp(buf, "DUMP ", 5) == 0) {
		    History h = { 0 };
		    if (sscanf(buf + 5, "%s gm=%d ex=%hd boff=%d bsize=%d",
					msgid, &h.gmt, &h.exp,
					&h.boffset, &h.bsize) == 5) {
			char *p;

			h.hv.h1 = (int32)strtoul(msgid, &p, 16);
			if (*p == '.')
			    h.hv.h2 = (int32)strtoul(p + 1, &p, 16);
			if (*p == '.')
			    h.iter = (int16)strtoul(p + 1, NULL, 16);
			r = LookupHash(h.hv, NULL, &h);
		    }
		} else if ((m = strchr(buf, '<')) != NULL) {
		    char *p;
		    if ((p = strchr(m, '>')) == NULL)
			continue;
		    *++p = 0;
		    hv = hhash(m);
		    r = LookupHash(hv, m, NULL);
		} else if (sscanf(buf, "%x.%x", &hv.h1, &hv.h2) == 2) {
		    r = LookupHash(hv, NULL, NULL);
		}
	    }
	    if (fi != stdin)
		fclose(fi);
	} else {
	    fprintf(stderr, "Unable to open %s (%s)\n", file, strerror(errno));
	}
    } else {
	hash_t hv;
	char *msgid = NULL;

	if (arg[0] == '<') {
	    msgid = arg;
	    hv = hhash(arg);
	    r = LookupHash(hv, msgid, NULL);
	} else if (arg[0] == 'D' && arg[1] == '.') {
	    int32 dummy;

	    if (sscanf(arg + 2, "%x/%x.%x", &dummy, &hv.h1, &hv.h2) != 3) {
		fprintf(stderr, "argument error\n");
		exit(1);
	    }
	    r = LookupHash(hv, msgid, NULL);
	} else if (sscanf(arg, "%x.%x", &hv.h1, &hv.h2) == 2) {
	    r = LookupHash(hv, msgid, NULL);
	} else {
	    char fname[PATH_MAX];
	    char *p = fname;
	    History h = { 0 };

	    *p = 0;
	    if (*arg != '/') {
		sprintf(p, "%s/", PatExpand(SpoolHomePat));
		p += strlen(p);
	    }
	    if (sscanf(arg, "%[^:]:%d,%d", p, &h.boffset, &h.bsize) == 3) {
		DumpArticle(fname, &h, NULL);
	    } else {
		printf("Unknown argument: %s\n", arg);
	    }
	}
    }
    exit(r);
}

int
DumpFileHeader(int fd, off_t offset)
{
    SpoolArtHdr artHdr;

    lseek(fd, offset, SEEK_SET);
    if (read(fd, &artHdr, sizeof(artHdr)) != sizeof(artHdr)) {
	fprintf(stderr, "Error reading data header\n");
	return(1);
    }
    if ((uint8)artHdr.Magic1 != STORE_MAGIC1 ||
					(uint8)artHdr.Magic2 != STORE_MAGIC2) {
	fprintf(stderr, "No or corrupt data header (%d:%d)\n", artHdr.Magic1,
								artHdr.Magic2);
	return(1);
    }
    printf("OK\n");
    printf("Offset   : %lld\n", (long long)offset);
    printf("Version  : %d\n", artHdr.Version);
    printf("HeadLen  : %d\n", artHdr.HeadLen);
    printf("StoreType:%s%s%s\n",
		(artHdr.StoreType & STORETYPE_TEXT) ? " text" : "",
		(artHdr.StoreType & STORETYPE_GZIP) ? " gzip" : "",
		(artHdr.StoreType & STORETYPE_WIRE) ? " wire" : "");
    printf("ArtHdrLen: %d\n", artHdr.ArtHdrLen);
    printf("ArtLen   : %d\n", artHdr.ArtLen);
    printf("StoreLen : %d\n", artHdr.StoreLen);
    return(0);
}

int
MapArticle(int fd, char *fname, char **base, History *h, int *extra, int *artSize, int *compressedFormat)
{
    if (SpoolCompressed(H_SPOOL(h->exp))) {
#ifdef USE_ZLIB
	gzFile gzf;
	SpoolArtHdr tah = { 0 };
	char *p;

	lseek(fd, h->boffset, 0);
	if (read(fd, &tah, sizeof(SpoolArtHdr)) != sizeof(SpoolArtHdr)) {
	    close(fd);
	    fprintf(LogFo, "Unable to read article header (%s)\n",
							strerror(errno));
	    return(-1);
	}
	if ((uint8)tah.Magic1 != STORE_MAGIC1 &&
					(uint8)tah.Magic2 != STORE_MAGIC2) {
	    lseek(fd, h->boffset, 0);
	    tah.Magic1 = STORE_MAGIC1;
	    tah.Magic2 = STORE_MAGIC2;
	    tah.HeadLen = sizeof(tah);
	    tah.ArtLen = h->bsize;
	    tah.ArtHdrLen = h->bsize;
	    tah.StoreLen = h->bsize;
	}
	gzf = gzdopen(fd, "r");
	if (gzf == NULL) {
	    fprintf(LogFo, "Error opening compressed article\n");
	    return(-1);
	}
	*base = (char *)malloc(tah.ArtLen + tah.HeadLen + 2);
	if (*base == NULL) {
	    fprintf(LogFo, "Unable to malloc %d bytes for article (%s)\n",
				tah.ArtLen + tah.HeadLen + 2, strerror(errno));
	    gzclose(gzf);
	    return(-1);
	}
	p = *base;
	*p++ = 0;
	bcopy(&tah, p, tah.HeadLen);
	p += tah.HeadLen;
	if (gzread(gzf, p, tah.ArtLen) != tah.ArtLen) {
	    fprintf(LogFo, "Error uncompressing article\n");
	    free(*base);
	    return(-1);
	}
	p[tah.ArtLen] = 0;
	*extra = 1;
	*artSize = tah.ArtLen;
	*artSize += tah.HeadLen;
	*compressedFormat = 1;
	gzclose(gzf);
#else
        fprintf(LogFo, "Article is on a compressed spool and compression support has not been enabled\n");
#endif
    } else {
	    *base = xmap(
		NULL, 
		h->bsize + *extra + 1, 
		PROT_READ,
		MAP_SHARED, 
		fd, 
		h->boffset - *extra
	    );
	    *artSize = h->bsize;
    }

    if (*base == NULL) {
	fprintf(LogFo, "Unable to map file %s: %s (%lld,%d,%d)\n",
				fname,
				strerror(errno),
				(long long)(h->boffset - *extra),
				 (int)(h->bsize + *extra + 1),
				*extra
	);
	return(-1);
    }
    return(0);
}

int
DumpArticle(char *fname, History *h, const char *msgid)
{
    int fd;
    int rv = 0;
    int wireFormat = 0;
    int artSize = 0;
    int headLen;
    int oldFormat = 0;
    int compressedFormat = 0;
    SpoolArtHdr ah;

    if ((fd = open(fname, O_RDONLY)) >= 0) {
	char *base = NULL;
	const char *ptr;
	int extra = (h->boffset == 0) ? 0 : 1;

	errno = 0;

	if (ShowFileHeader) {
	    rv = DumpFileHeader(fd, h->boffset);
	    close(fd);
	    return(rv);
	}

	if (MapArticle(fd, fname, &base, h, &extra, &artSize, &compressedFormat) != 0)
	    return(1);

	/*
	 * check for prior terminating zero, article body does not 
	 * begin with a null, and post terminating zero
	 */

	ptr = base;

	if (rv == 0 && extra) {
	    if (*ptr != 0) {
		fprintf(LogFo, " missingPreNul");
		rv = 1;
	    }
	    ++ptr;
	}
	if (rv == 0 && ptr[0] == 0) {
	    fprintf(LogFo, " nullArtBody");
	    rv = 1;
	}

	if (rv == 0 && ptr[artSize] != 0) {
	    fprintf(LogFo, " missingPostNul");
	    rv = 1;
	}

	bcopy(ptr, &ah, sizeof(ah));

	if (DebugOpt) {
	    printf("magic1=%x  magic2=%x\n", (uint8)ah.Magic1,
							(uint8)ah.Magic2);
	}
	if ((uint8)ah.Magic1 == (uint8)STORE_MAGIC1 &&
				(uint8)ah.Magic2 == (uint8)STORE_MAGIC2) {
	    headLen = (uint32)ah.ArtHdrLen;
	    artSize -= (uint8)ah.HeadLen;
	    ptr += (uint8)ah.HeadLen;
	    wireFormat = 0;
	    if ((uint8)ah.StoreType & STORETYPE_WIRE)
		wireFormat = 1;
	} else {
	    headLen = artSize;
	    wireFormat = 0;
	    oldFormat = 1;
	}

	/*
	 * Locate Message-ID header and test
	 */

	if (rv == 0 && msgid) {
	    const char *l;
	    int haveMsgId = 0;

	    for (l = ptr - 1; l < ptr + headLen; l = strchr(l, '\n')) {
		if (l == NULL) {
		    rv = 1;
		    break;
		}
		++l;
		if (strncasecmp(l, "Message-ID:", 11) == 0) {
		    int i;

		    haveMsgId = 1;

		    l += 11;
		    while (*l && *l != '<' && *l != '\n')
			++l;
		    for (i = 0; l[i] && l[i] != '>' && l[i] != '\n' &&
							l[i] != '\r'; ++i) {
			if (msgid[i] != l[i])
			    rv = 1;
		    }
		    if (msgid[i] != l[i])
			rv = 1;
		}
		if (!wireFormat && l[0] == '\n')	/* end of headers */
		    break;
		else if (wireFormat && l[0] == '\r' && l[1] == '\n')
		    break;
	    }
	    if (rv) {
		fprintf(LogFo, " messageID-MisMatch");
	    } else if (haveMsgId == 0) {
		fprintf(LogFo, " missing-MessageID");
		rv = 1;
	    }
	}
	if (rv == 0)
	    fprintf(LogFo, "%sOK", oldFormat ? "(old spool) " : "");
	fprintf(LogFo, "\n");
	if (rv == 0 && VerifyOnly == 0) {
	    int i;
	    int lastNl = 1;
	    int lineLen = 0;

	    fflush(LogFo);
	    fflush(stdout);

	    if (HeadOnly) {
		for (i = 0; i < headLen; ++i) {
		    if (StripCR && wireFormat && i > 0 &&
					ptr[i-1] == '\r' && ptr[i] == '\n') {
			if (!QuietOpt) {
			    write(1, ptr + i - lineLen, lineLen - 1);
			    write(1, "\n", 1);
			}
			if (lineLen == 1)
			    break;
			lineLen = 0;
		    } else if (wireFormat && i > 0 &&
					ptr[i-1] == '\r' && ptr[i] == '\n') {
			if (lineLen == 1)
			    break;
			lastNl = 1;
			lineLen = 0;
		    } else if (ptr[i] == '\n') {
			if (lastNl)
			    break;
			lastNl = 1;
		    } else {
			lastNl = 0;
			lineLen++;
		    }
		}
		if ((!StripCR || !wireFormat) && !QuietOpt)
		    write(1, ptr, i);
	    } else {
		if (StripCR && wireFormat) {
		    for (i = 0; i < artSize; ++i) {
			if (i > 0 && ptr[i-1] == '\r' && ptr[i] == '\n') {
			    if (!QuietOpt) {
				write(1, ptr + i - lineLen, lineLen - 1);
				write(1, "\n", 1);
			    }
			    lineLen = 0;
			} else {
			    lineLen++;
			}
		    }
		} else if (!QuietOpt) {
		    write(1, ptr, artSize);
		}
	    }
	    fflush(stdout);
	}
	if (base != NULL) {
	    if (compressedFormat)
		free(base);
	    else
		xunmap((void *)base, h->bsize + extra + 1);
	}
	if (fd != -1 && !compressedFormat)
	    close(fd);
    } else {
	fprintf(LogFo, "Unable to open %s\n", fname);
	rv = 1;
    }
    return(rv);
}

int
LookupHash(hash_t hv, const char *msgid, History *ph)
{
    History h;
    int rv = 0;

    if (msgid)
	fprintf(LogFo, "%60s\t", msgid);
    else
	fprintf(LogFo, "%08x.%08x\t", hv.h1, hv.h2);

    if (ph != NULL || HistoryLookupByHash(hv, &h) == 0) {
	char buf[8192];

	if (ph != NULL)
	    memcpy(&h, ph, sizeof(h));
	if (ForceOpt || (
		!H_EXPIRED(h.exp) && 
		h.iter != (unsigned short)-1 &&
		(h.boffset || h.bsize)
	    )
	) {
	    int headOnly = (int)(h.exp & EXPF_HEADONLY);

	    ArticleFileName(buf, sizeof(buf), &h, ARTFILE_FILE);

	    if (HeadOnly == 0 && headOnly) {
		fprintf(LogFo, "Article stored as header-only, use -h\n");
		rv = 1;
	    } else {
		rv = DumpArticle(buf, &h, msgid);
	    }
	} else if (h.boffset || h.bsize) {
	    fprintf(LogFo, "Article expired\n");
	    rv = 1;
	} else {
	    fprintf(LogFo, "Article pre-expired\n");
	    rv = 1;
	}
    } else {
	fprintf(LogFo, "Article not found in history\n");
	rv = 1;
    }
    return(rv);
}

