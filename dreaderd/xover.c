
/*
 * XOVER.C	- general nntp reader commands
 *
 *	Overview-related NNTP commands
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNTPNewNews(Connection *conn, char **pptr);
Prototype void NNTPXHdr(Connection *conn, char **pptr);
Prototype void NNTPXOver(Connection *conn, char **pptr);
Prototype void NNTPXZver(Connection *conn, char **pptr);
Prototype void NNTPXPat(Connection *conn, char **pptr);
Prototype void NNTPXPath(Connection *conn, char **pptr);
Prototype int OutputOverview(Connection *conn, const char *res, int resLen, int artSize);

void NNListNewNews(Connection *conn);
void NNStartListOverview(Connection *conn, const char *hdr, const char *ran, const char *pat, int mode);
void NNStartListOverviewRange(Connection *conn);
void NNStartListOverviewMsgId(Connection *conn, const char *msgid);
void NNListOverviewRange(Connection *conn);

Prototype char *OverViewFmt;

char *OverViewFmt = OVERVIEW_FMT;

void
NNTPNewNews(Connection *conn, char **pptr)
{
    char *newsgroups = NULL;
    const char *yymmdd = NULL;
    const char *hhmmss = NULL;
    const char *gmt = NULL;
    const char *distributions = NULL;
 
    if (!conn->co_Auth.dr_ReaderDef->rd_AllowNewnews) {
	MBLogPrintf(conn, &conn->co_TMBuf, "500 \"newnews\" not implemented\r\n");
	NNCommand(conn);
	return;
    }

    if (pptr) {
	newsgroups = parseword(pptr, " \t");
	yymmdd = parseword(pptr, " \t");
	hhmmss = parseword(pptr, " \t");
	gmt = parseword(pptr, " \t");
	distributions = parseword(pptr, " \t");
    }
    if (SetTimeRestrict(&conn->co_TimeRestrict, yymmdd, hhmmss, gmt) < 0) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 newsgroups yymmdd hhmmss [\"GMT\"] [<distributions>]\r\n");
	NNCommand(conn);
    } else {
	if (conn->co_Auth.dr_Flags & DF_GROUPLOG)
	    logit(LOG_INFO, "info %s%s%s%s%s newnews %s %s %s %s %s",
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",  
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host,
		newsgroups,
		yymmdd,
		hhmmss,
		gmt ? gmt : "-",
		distributions ? distributions : "-");

	MBLogPrintf(conn, &conn->co_TMBuf, "230 New news follows.\r\n");
	zfreeStr(&conn->co_MemPool, &conn->co_ListPat);
	zfreeStr(&conn->co_MemPool, &conn->co_ListHdrs);
	conn->co_ListPat = (distributions) ? zallocStr(&conn->co_MemPool, distributions) : NULL;
	conn->co_ListHdrs = zallocStr(&conn->co_MemPool, "Message-ID");
	conn->co_ArtMode = COM_NEWNEWS;
	conn->co_ListCacheGroups = NULL;
	ListActiveGroups(conn, newsgroups);
	NNListNewNews(conn);
    }
}

void
NNListNewNews(Connection *conn)
{
    conn->co_Func = NNListNewNews;
    conn->co_State = "listnewnews";

    if (conn->co_ListCacheGroups == NULL) {
	MBPrintf(&conn->co_TMBuf, ".\r\n");
	if (conn->co_GroupName)
	    zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
	NNCommand(conn);
    } else {
	GroupList *gl = conn->co_ListCacheGroups;
	if (conn->co_GroupName)
	    zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
	conn->co_GroupName = gl->group;
	conn->co_ListBegNo = 0;
	conn->co_ListEndNo = LL_MAX;
	conn->co_ListCacheGroups = gl->next;
	zfree(&conn->co_MemPool, gl, sizeof(GroupList));
	NNStartListOverviewRange(conn);
    }
}

void 
NNTPXHdr(Connection *conn, char **pptr)
{
    char *hdr;

    if ((hdr = parseword(pptr, " \t")) == NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 header [range|MessageID]\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, hdr, parseword(pptr, " \t"), NULL, COM_XHDR);
    return;
}

void 
NNTPXOver(Connection *conn, char **pptr)
{
    NNStartListOverview(conn, OverViewFmt, parseword(pptr, " \t"), NULL, COM_XOVER);
    return;
}

void 
NNTPXZver(Connection *conn, char **pptr)
{
    if (!conn->co_Auth.dr_ReaderDef->rd_XzverLevel < 0) {
	MBLogPrintf(conn, &conn->co_TMBuf, "500 \"xzver\" not enabled\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, OverViewFmt, parseword(pptr, " \t"), NULL, COM_XZVER);
    return;
}

void 
NNTPXPat(Connection *conn, char **pptr)
{
    char *hdr;
    char *ran;
    char *pat;

    if ((hdr = parseword(pptr, " \t")) == NULL ||
	(ran = parseword(pptr, " \t")) == NULL ||
	*(pat = *pptr) == '\0'
    ) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 header range|MessageID pat\r\n");
	NNCommand(conn);
	return;
    }
    NNStartListOverview(conn, hdr, ran, pat, COM_XPAT);
}

void 
NNTPXPath(Connection *conn, char **pptr)
{
    MBLogPrintf(conn, &conn->co_TMBuf, "500 What? (xpath not implemented yet)\r\n");
    NNCommand(conn);
}

void
FinishOverviewDotNewline(Connection *conn)
{
    if (conn->co_ArtMode == COM_XZVER) {
        MZPrintf(conn, ".\r\n");
        MZFinish(conn);
    } else {
        MBPrintf(&conn->co_TMBuf, ".\r\n");
    }
}


/*
 * NNStartListOverview() - list selected overview for specified article range
 *			   or message-id
 */

void
NNStartListOverview(Connection *conn, const char *hdr, const char *ran, const char *pat, int mode)
{
    zfreeStr(&conn->co_MemPool, &conn->co_ListPat);
    zfreeStr(&conn->co_MemPool, &conn->co_ListHdrs);
    conn->co_ListPat = (pat) ? zallocStr(&conn->co_MemPool, pat) : NULL;
    conn->co_ListHdrs = zallocStr(&conn->co_MemPool, hdr);

    if (ran == NULL || ran[0] != '<') {
	if (conn->co_GroupName == NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "412 Not in a newsgroup\r\n");
	    NNCommand(conn);
	    return;
	}
    }

    conn->co_ArtMode = mode;

    switch(mode) {
    case COM_XPAT:
	MBLogPrintf(conn, &conn->co_TMBuf, "221 %s matches follow.\r\n", hdr);
	break;
    case COM_XHDR:
	MBLogPrintf(conn, &conn->co_TMBuf, "221 %s data follows\r\n", hdr);
	break;
    case COM_XOVER:
	MBLogPrintf(conn, &conn->co_TMBuf, "224 data follows\r\n");
	break;
    case COM_XZVER:
	if (MZInit(conn, &conn->co_TMBuf, conn->co_Auth.dr_ReaderDef->rd_XzverLevel) < 0) {
	  MBLogPrintf(conn, &conn->co_TMBuf, "500 compress init error\r\n");
	  NNCommand(conn);
	  return;
	}
	MBLogPrintf(conn, &conn->co_TMBuf, "224 compressed data follows (zlib version %s)\r\n", zlibVersion());
	break;
    default:
	MBLogPrintf(conn, &conn->co_TMBuf, "500 software error\r\n");
	NNCommand(conn);
	return;
    }
    if (ran == NULL) {
	conn->co_ListBegNo = conn->co_ArtNo;
	conn->co_ListEndNo = conn->co_ArtNo;
	NNStartListOverviewRange(conn);
    } else if (ran[0] != '<') {
	char *p;
	conn->co_ListBegNo = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, strtoll(ran, &p, 10), conn->co_Numbering);
	if (*p == '-') {
	    if (p[1] == 0)
		conn->co_ListEndNo = LL_MAX;
	    else
		conn->co_ListEndNo = artno_input(conn->co_ArtBeg, conn->co_ArtEnd, strtoll(p + 1, NULL, 10), conn->co_Numbering);
	} else {
	    conn->co_ListEndNo = conn->co_ListBegNo;
	}

	/* Protect against some overflow modes around 2^31 with RFC3977... */
	{
	    artno_t xlb = artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering);
	    artno_t xle = artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListEndNo, conn->co_Numbering);

	    if ((xle - xlb) < (conn->co_ListEndNo - conn->co_ListBegNo)) {
		conn->co_ListEndNo = conn->co_ListBegNo + (xle - xlb);
	    }
	}

	NNStartListOverviewRange(conn);
    } else if ((ran = MsgId(ran, NULL)) && strcmp(ran, "<>") != 0) {
	NNStartListOverviewMsgId(conn, ran);
    } else {
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
    }
}

void
NNStartListOverviewRange(Connection *conn)
{
    /*
     * trim list range
     */
    const char *rec;
    int recLen;
    artno_t lbeg;
    artno_t lend;

    /*
     * Handle user bogosity
     */

    if (conn->co_ListBegNo < 0)
	conn->co_ListBegNo = 0;
    if (conn->co_ListEndNo < 0)
	conn->co_ListEndNo = 0;

    if ((rec = KPDBReadRecord(KDBActive, conn->co_GroupName, KP_LOCK, &recLen)) == NULL) {
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return;
    }
    lbeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "0"), NULL, 10);
    lend = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "0"), NULL, 10);
    KPDBUnlock(KDBActive, rec);

    /*
     * Handle some stupid case begin and end numbers
     */
    if ((conn->co_ListBegNo < lbeg && conn->co_ListEndNo < lbeg) ||
		conn->co_ListBegNo > lend ||
		conn->co_ListEndNo < lbeg ||
		conn->co_ListBegNo > conn->co_ListEndNo) {
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return;
    }
    if (conn->co_ListBegNo < lbeg)
	conn->co_ListBegNo = lbeg;
    if (conn->co_ListEndNo > lend)
	conn->co_ListEndNo = lend;
    NNListOverviewRange(conn);
}

void
NNListOverviewRange(Connection *conn)
{
    OverInfo *ov;
    int xpat_count = 0;

    conn->co_Func = NNListOverviewRange;
    conn->co_State = "listover";

    if ((ov = GetOverInfo(conn->co_GroupName)) == NULL) {
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return;
    }
    if (conn->co_TMBuf.mh_WError) {
	PutOverInfo(ov);
	// This takes care of cleaning up any compression state
	FinishOverviewDotNewline(conn);
	NNCommand(conn);
	return;
    }
    while (conn->co_TMBuf.mh_Bytes < MBUF_HIWAT &&
	    conn->co_ListBegNo <= conn->co_ListEndNo
    ) {
#ifndef USE_OVER_MADVISE
	int resLen;
	int artSize;
	const char *res;
	TimeRestrict *tr = NULL;
#endif

	if (conn->co_ArtMode == COM_XPAT && ++xpat_count > 50) {
	    FD_SET(conn->co_Desc->d_Fd, &WFds);
	    break;
	}
#ifdef USE_OVER_MADVISE
	OutputOverRange(ov, conn);
#else
	if (conn->co_ArtMode == COM_NEWNEWS)
	    tr = &conn->co_TimeRestrict;
	if ((res = GetOverRecord(ov, conn->co_ListBegNo, &resLen, &artSize, tr, NULL)) != NULL) {
	    OutputOverview(conn, res, resLen, artSize);
	}
	++conn->co_ListBegNo;
#endif	/* USE_OVER_MADVISE */
    }
    PutOverInfo(ov);
    if (conn->co_ListBegNo > conn->co_ListEndNo) {
	if (conn->co_ArtMode != COM_NEWNEWS)
	    FinishOverviewDotNewline(conn);
	NNCommand(conn);
	if (conn->co_ArtMode == COM_NEWNEWS) {
	    NNListNewNews(conn);
	}
    }
}

void
NNStartListOverviewMsgId(Connection *conn, const char *msgid)
{
    /* XXX StartListOverviewMsgId */
    FinishOverviewDotNewline(conn);
    NNCommand(conn);
}

int
OutputOverview(Connection *conn, const char *res, int resLen, int artSize)
{
    const char *hdr = conn->co_ListHdrs;
    int didIndex = 0;

    while (*hdr) {
	int hlen;
	int hhlen;
	int rleft;
	int printHdr = 0;
	const char *rline;
	char hch;
	char ch;

	/*
	 * locate next header
	 */

	while (*hdr == '\r' || *hdr == '\n')
	    ++hdr;
	for (hlen = 0; hdr[hlen]; ++hlen) {
	    if (hdr[hlen] != '\n')
		continue;
	    if (hdr[hlen+1] == ' ' || hdr[hlen+1] == '\t')	/* multiline */
		continue;
	    ++hlen;
	    break;
	}
	if (hlen == 0)
	    break;

	/*
	 * locate header name non-inclusive of colon
	 */

	for (hhlen = 0; hhlen < hlen && hdr[hhlen] != ':'; ++hhlen)
	    ;

	/*
	 * scan overview info for header
	 */
	rline = res;
	rleft = resLen;

	hch = tolower(*hdr);

	while (rleft) {
	    int rlen;
	    int rrlen;

	    /*
	     * get entire header
	     */

	    for (rlen = 0; rline[rlen]; ++rlen) {
		if (rline[rlen] != '\n')
		    continue;
		if (rline[rlen+1] == ' ' || rline[rlen+1] == '\t')/* multiln */
		    continue;
		++rlen;
		break;
	    }

	    /*
	     * no more headers, don't scan article (if article data passed)
	     */
	    if (rlen == 2 && rline[0] == '\r' && rline[1] == '\n')
		break;

	    /*
	     * This occurs if the map file containing the headers is boshed
	     */

	    if (rlen == 0)
		break;

	    /*
	     * get just header portion, non inclusive of colon
	     */

	    for (rrlen = 0; rrlen < rlen && rline[rrlen] != ':'; ++rrlen)
		;

	    ch = tolower(*rline);
	    if (rrlen == hhlen &&
			ch == hch && strncasecmp(rline, hdr, hhlen) == 0) {
		switch(conn->co_ArtMode) {
		case COM_XPAT:
		    {
			char hdr[128];
			char *p = hdr;
			int glen = rrlen;

			if (glen < rlen && rline[glen] == ':')
			    ++glen;
			while (glen < rlen && 
			    (rline[glen] == ' ' || rline[glen] == '\t')
			) {
			    ++glen;
			}

			if (rlen >= sizeof(hdr))
			    p = zalloc(&conn->co_MemPool, rlen + 1);
			memcpy(p, rline, rlen);
			/* Handle CR/LF in overview data */
			if (rlen > 2 && p[rlen - 2] == '\r' && p[rlen - 1] == '\n') {
			    p[rlen - 2] = 0;
			}
			p[rlen] = 0;
			if (wildmat(p + glen, conn->co_ListPat))
			    printHdr = ' ';
			if (p != hdr)
			    zfree(&conn->co_MemPool, p, rlen + 1);
		    }
		    break;
		case COM_XHDR:
		    printHdr =  ' ';
		    break;
		case COM_NEWNEWS:
		    printHdr = ' ';
		    didIndex = 1;
		    break;
		case COM_XOVER:
		    printHdr = '\t';
		    break;
		case COM_XZVER:
		    printHdr = '\t';
		    break;
		}
		if (printHdr) {
		    int b;
		    int i;
		    int doSpace = 0;

		    if (didIndex == 0) {
			if (conn->co_ArtMode == COM_XZVER) {
			    MZPrintf(conn, "%lld", 
				artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
			} else {
			    MBPrintf(&conn->co_TMBuf, "%lld",
				artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
			}
			didIndex = 1;
		    }
		    if (conn->co_ArtMode != COM_NEWNEWS) {
			if (conn->co_ArtMode == COM_XZVER) {
			    MZPrintf(conn, "%c", printHdr);
			} else {
			    MBPrintf(&conn->co_TMBuf, "%c", printHdr);
			}
		    }

		    /*
		     * hack for overview format options, only deal with
		     * 'full' at the moment.
		     */

		    if (hhlen < hlen && strncmp(hdr + hhlen + 1, "full", 4) == 0) {
			if (conn->co_ArtMode == COM_XZVER) {
			    MZWrite(conn, hdr, hhlen + 1);
			    MZWrite(conn, " ", 1);
			} else {
			    MBWrite(&conn->co_TMBuf, hdr, hhlen + 1);
			    MBWrite(&conn->co_TMBuf, " ", 1);
			}
		    }

		    /*
		     * Compress whitespace if necessary. Mainly applies
		     * to folded lines.
		     */
		    for (b = i = rrlen + 1; i < rlen; ++i) {
			if (rline[i] == '\r' || rline[i] == '\n' ||
			    rline[i] == ' ' || rline[i] == '\t'
			) {
			    /*
			     * Skip first whitespace
			     */
			    if (b == i) {
				++b;
			    } else {
				/*
				 * Don't compress spaces
				 * We will compress space for OVER when it
				 * gets implemented
				 */
				if (rline[i] == ' ')
				    continue;
				/*
				 * Only compress tabs if doing XOVER
				 */
				if (conn->co_ArtMode != COM_XOVER &&
				    conn->co_ArtMode != COM_XZVER &&
							rline[i] == '\t')
				    continue;
				if (doSpace) {
				    if (conn->co_ArtMode == COM_XZVER) {
				        MZWrite(conn, " ", 1);
				    } else {
				        MBWrite(&conn->co_TMBuf, " ", 1);
				    }
				}
				if (conn->co_ArtMode == COM_XZVER) {
				    MZWrite(conn, rline + b, i - b);
				} else {
				    MBWrite(&conn->co_TMBuf, rline + b, i - b);
				}
				b = i + 1;
				doSpace = 1;
			    }
			}
		    }
		    if (b != i) {
			if (doSpace) {
			    if (conn->co_ArtMode == COM_XZVER) {
			        MZWrite(conn, " ", 1);
			    } else {
			        MBWrite(&conn->co_TMBuf, " ", 1);
			    }
			}
			if (conn->co_ArtMode == COM_XZVER) {
			    MZWrite(conn, rline + b, i - b);
			} else {
			    MBWrite(&conn->co_TMBuf, rline + b, i - b);
			}
		    }
		}
		break;
	    }
	    rleft -= rlen;
	    rline += rlen;
	} /* while */
	if (printHdr == 0 && (conn->co_ArtMode == COM_XOVER ||
			      conn->co_ArtMode == COM_XZVER)) {
	    if (didIndex == 0) {
		if (conn->co_ArtMode == COM_XZVER) {
		    MZPrintf(conn, "%lld",
			artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
		} else {
		    MBPrintf(&conn->co_TMBuf, "%lld",
			artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
		}
		didIndex = 1;
	    }
	    if (hhlen == 5 && hch == 'b' && strncasecmp(hdr, "Bytes", 5) == 0) {
		if (conn->co_ArtMode == COM_XZVER) {
		    MZPrintf(conn, "\t%d", artSize + 1);
		} else {
		    MBPrintf(&conn->co_TMBuf, "\t%d", artSize + 1);
		}
	    } else {
		if (conn->co_ArtMode == COM_XZVER) {
		    MZPrintf(conn, "\t");
		} else {
		    MBPrintf(&conn->co_TMBuf, "\t");
		}
	    }
	}

	hdr += hlen;
	conn->co_LastActiveTime = CurTime.tv_sec;
    }
    if (didIndex == 0 && 
	(/* conn->co_ArtMode == COM_XPAT || */ conn->co_ArtMode == COM_XHDR)
    ) {
	if (conn->co_ArtMode == COM_XZVER) {
	    MZPrintf(conn, "%lld (none)",
		artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
	} else {
	    MBPrintf(&conn->co_TMBuf, "%lld (none)",
		artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ListBegNo, conn->co_Numbering));
	}
	didIndex = 1;
    }
    if (didIndex) {
	if (conn->co_ArtMode == COM_XZVER) {
	    MZPrintf(conn, "\r\n");
	} else {
	    MBPrintf(&conn->co_TMBuf, "\r\n");
	}
	return(0);
    }
    return(-1);
}

