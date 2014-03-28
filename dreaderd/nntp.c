
/*
 * NNTP.C	- general nntp reader commands
 *
 *	Reader-specific NNTP commands
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNTPQuit(Connection *conn, char **pptr);
Prototype void NNTPAuthInfo(Connection *conn, char **pptr);
Prototype void NNTPXNumbering(Connection *conn, char **pptr);
Prototype void NNTPArticle(Connection *conn, char **pptr);
Prototype void NNTPHead(Connection *conn, char **pptr);
Prototype void NNTPBody(Connection *conn, char **pptr);
Prototype void NNTPDate(Connection *conn, char **pptr);
Prototype void NNTPGroup(Connection *conn, char **pptr);
Prototype void NNTPLast(Connection *conn, char **pptr);
Prototype void NNTPNext(Connection *conn, char **pptr);
Prototype void NNTPStat(Connection *conn, char **pptr);
Prototype void NNExecuteOnRange(Connection *conn, const char *artid, int mode);
Prototype int GoodRC(Connection *conn);
Prototype const char *GoodResId(Connection *conn);
Prototype void DumpOVHeaders(Connection *conn, const char *ovdata, int ovlen);

void NNArticleRetrieve(Connection *conn, const char *artid, const char *artopts, int mode);
void NNArticleRetrieveByArtNo(Connection *conn, artno_t artNo);

void
NNTPQuit(Connection *conn, char **pptr)
{
     /*
      * Print closing banner
      */
     MBLogPrintf(conn, &conn->co_TMBuf, "205 Transferred %.0f bytes in %lu article%s, %lu group%s.  Disconnecting.\r\n", conn->co_ClientTotalByteCount, conn->co_ClientTotalArticleCount, conn->co_ClientTotalArticleCount != 1 ? "s" : "", conn->co_ClientGroupCount, conn->co_ClientGroupCount != 1 ? "s" : "");
     NNTerminate(conn);
}

/*
 * xnumbering rfc3977
 */

void 
NNTPXNumbering(Connection *conn, char **pptr)
{
    char *mode = parseword(pptr, " \t");
    int ok = 0;

    if (mode) {
	if (strcasecmp(mode, "rfc3977") == 0) {
	    conn->co_Numbering = CON_RFC3977;
	    MBLogPrintf(conn, &conn->co_TMBuf, "203 Mode Command RFC3977 Numbering OK.\r\n");
	    ok = 1;
	}
	if (strcasecmp(mode, "rfc977") == 0) {
	    conn->co_Numbering = CON_RFC977;
	    MBLogPrintf(conn, &conn->co_TMBuf, "203 Mode Command RFC977 Numbering OK.\r\n");
	    ok = 1;
	}
	if (strcasecmp(mode, "window") == 0) {
	    conn->co_Numbering = CON_WINDOW;
	    MBLogPrintf(conn, &conn->co_TMBuf, "203 Mode Command Windowed Numbering OK.\r\n");
	    ok = 1;
	}
    }
    if (ok == 0)
	MBLogPrintf(conn, &conn->co_TMBuf, "500 Syntax error or bad command\r\n");
    NNCommand(conn);
}

void
NNTPAuthInfo(Connection *conn, char **pptr)
{
    char *type = parseword(pptr, " \t");
    char *args = (type) ? parseword(pptr, " \t") : NULL;
    int ok = 0;

    if (type && args && strlen(args) < 64) {
	if (strcasecmp(type, "user") == 0) {
	    strncpy(conn->co_Auth.dr_AuthUser, args,
				sizeof(conn->co_Auth.dr_AuthUser) - 1);
	    conn->co_Auth.dr_AuthUser[sizeof(conn->co_Auth.dr_AuthUser) - 1] = '\0';
	    ok = 1;
	    conn->co_Auth.dr_Flags |= DF_AUTHREQUIRED;
	    MBLogPrintf(conn, &conn->co_TMBuf, "381 PASS required\r\n");
	} else if (strcasecmp(type, "pass") == 0 &&
				*conn->co_Auth.dr_AuthUser) {
            strncpy(conn->co_Auth.dr_AuthPass, args,
				sizeof(conn->co_Auth.dr_AuthPass) - 1);
	    conn->co_Auth.dr_AuthPass[sizeof(conn->co_Auth.dr_AuthPass) - 1] = '\0';
	    conn->co_Auth.dr_Flags &= ~DF_AUTHREQUIRED;
	    conn->co_Auth.dr_ResultFlags = DR_REQUIRE_DNS;
	    return;
	}
    }
    if (ok == 0)
	MBLogPrintf(conn, &conn->co_TMBuf, "501 user Name|pass Password\r\n");
    if (ok > 0)
	NNCommand(conn);
}

void 
NNTPArticle(Connection *conn, char **pptr)
{
    char *msgid = parseword(pptr, " \t");

    NNArticleRetrieve(conn, msgid, parseword(pptr, " \t"), COM_ARTICLEWVF);
}

void 
NNTPHead(Connection *conn, char **pptr)
{
    char *msgid = parseword(pptr, " \t");

    NNArticleRetrieve(conn, msgid, parseword(pptr, " \t"), COM_HEAD);
}

void 
NNTPBody(Connection *conn, char **pptr)
{
    char *msgid = parseword(pptr, " \t");

    NNArticleRetrieve(conn, msgid, parseword(pptr, " \t"), COM_BODYWVF);
}

void
GroupStats(Connection *conn)
{
    if ((conn->co_GroupName != NULL) &&
	(conn->co_Auth.dr_Flags & DF_GROUPLOG)) {

	char statbuf[1024];

        snprintf(statbuf, sizeof(statbuf), "group %s articles %lu bytes %.0f", conn->co_GroupName, conn->co_ClientGroupArticleCount, conn->co_ClientGroupByteCount);
	LogCmd(conn, '$', statbuf);
	if (conn->co_ClientGroupArticleCount)
            logit(LOG_INFO, "info %s%s%s%s%s %s",
                *conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
                *conn->co_Auth.dr_AuthUser ? "/" : "",
                *conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
                *conn->co_Auth.dr_IdentUser ? "@" : "",
		conn->co_Auth.dr_Host,
                statbuf);
	conn->co_ClientGroupArticleCount = 0;
	conn->co_ClientGroupByteCount = 0.0;
    }
}

void 
NNTPGroup(Connection *conn, char **pptr)
{
    char *group = parseword(pptr, " \t");
    int recLen;
    artno_t artBeg, dispArtBeg;
    artno_t artEnd, dispArtEnd;
    artno_t adjustedArtBeg = 0;
    const char *rec;
    OverInfo *ov;
    struct GroupList *groups = conn->co_Auth.dr_GroupDef->gr_Groups;

    ++conn->co_Auth.dr_GrpCount;

    if (group == NULL || strlen(group) > MAXGNAME || 
	parseword(pptr, " \t") != NULL
    ) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 newsgroup\r\n");
	NNCommand(conn);
	return;
    }
    if (groups != NULL && !GroupFindWild(group, groups)) {
	MBLogPrintf(conn, &conn->co_TMBuf, "411 No such group %s\r\n", group);
	NNCommand(conn);
	return;
    }
    if ((rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen)) == NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "411 No such group %s\r\n", group);
	NNCommand(conn);
	return;
    }
    artBeg = strtoll(KPDBGetField(rec, recLen, "NB", NULL, "-1"), NULL,10);
    artEnd = strtoll(KPDBGetField(rec, recLen, "NE", NULL, "-1"), NULL,10);
    KPDBUnlock(KDBActive, rec);

    /*
     * Handle range fixup
     */

    if (artBeg > artEnd) {
	artBeg = artEnd;
	adjustedArtBeg = 1;
    }

    if ((ov = GetOverInfo(group)) != NULL) {
	if (artBeg < artEnd - ov->ov_Head->oh_MaxArts) {
	    artBeg = artEnd - ov->ov_Head->oh_MaxArts;
	    adjustedArtBeg = 1;
	}
	PutOverInfo(ov);
    } else {
	if (artBeg < artEnd - DEFMAXARTSINGROUP) {
	    artBeg = artEnd - DEFMAXARTSINGROUP;
	    adjustedArtBeg = 1;
	}
    }
    if (artEnd == 0 && artBeg != 1) {
	artBeg = 1;
	adjustedArtBeg = 1;
    }

    GroupStats(conn);
    conn->co_ClientGroupCount++;

    zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
    conn->co_GroupName = zallocStr(&conn->co_MemPool, group);

    conn->co_ArtNo = artBeg;
    conn->co_ArtBeg = dispArtBeg = artBeg;
    conn->co_ArtEnd = dispArtEnd = artEnd;

    dispArtBeg = artno_nb(artBeg, artEnd, conn->co_Numbering);
    dispArtEnd = artno_ne(artBeg, artEnd, conn->co_Numbering);

    MBLogPrintf(conn, &conn->co_TMBuf, "211 %lld %lld %lld %s\r\n",
	dispArtEnd - dispArtBeg + 1, dispArtBeg, dispArtEnd, group
    );
    NNCommand(conn);
}

void 
NNTPLast(Connection *conn, char **pptr)
{
    artno_t saveArtNo = conn->co_ArtNo;

    if (parseword(pptr, " \t") != NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 Usage error\r\n");
	NNCommand(conn);
	return;
    }

    while (artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering) > artno_nb(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_Numbering)) {
	const char *ovdata;
	const char *msgid;
	int ovlen;

	--conn->co_ArtNo;
	if ((ovdata = NNRetrieveHead(conn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, 
		"223 %lld %s Article retrieved; request text separately.\r\n", 
		artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering),
		msgid
	    );
	    NNCommand(conn);
	    return;
	}
    }
    MBLogPrintf(conn, &conn->co_TMBuf, "422 No previous to retrieve.\r\n");
    conn->co_ArtNo = saveArtNo;
    NNCommand(conn);
}

void 
NNTPNext(Connection *conn, char **pptr)
{
    artno_t saveArtNo = conn->co_ArtNo;

    if (parseword(pptr, " \t") != NULL) {
	MBLogPrintf(conn, &conn->co_TMBuf, "501 Usage error\r\n");
	NNCommand(conn);
	return;
    }

    while (artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering) < artno_ne(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_Numbering)) {
	const char *ovdata;
	const char *msgid;
	int ovlen;

	++conn->co_ArtNo;
	if ((ovdata = NNRetrieveHead(conn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, 
		"223 %lld %s Article retrieved; request text separately.\r\n", 
		artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering),
		msgid
	    );
	    NNCommand(conn);
	    return;
	}
    }
    MBLogPrintf(conn, &conn->co_TMBuf, "421 No next to retrieve.\r\n");
    conn->co_ArtNo = saveArtNo;
    NNCommand(conn);
}

void 
NNTPStat(Connection *conn, char **pptr)
{
    char *msgid = parseword(pptr, " \t");

    NNArticleRetrieve(conn, msgid, parseword(pptr, " \t"), COM_STAT);
}

void
NNArticleRetrieve(Connection *conn, const char *artid, const char *artopts, int mode)
{
    conn->co_ArtMode = mode;

    ++conn->co_Auth.dr_ArtCount;

    conn->co_RequestFlags = ARTFETCH_MSGID;
    if (artid == NULL) {
	if (conn->co_GroupName == NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "412 Not in a newsgroup\r\n");
	    NNCommand(conn);
	    return;
	}
	NNArticleRetrieveByArtNo(conn, conn->co_ArtNo);
    } else if (isdigit((int)artid[0])) {
	if (conn->co_GroupName == NULL) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "412 Not in a newsgroup\r\n");
	    NNCommand(conn);
	    return;
	}
	NNArticleRetrieveByArtNo(conn, artno_input(conn->co_ArtBeg, conn->co_ArtEnd, strtoll(artid, NULL, 10), conn->co_Numbering));
    } else {
	artid = MsgId(artid, NULL);
	if (strcmp(artid, "<>") == 0) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "430 No such article\r\n");
	    NNCommand(conn);
	} else {
	    /*
	     * XXX this isn't clean.  We have to turn off verify mode
	     * when retrieving articles by message-id.  It works anyway.
	     * We also use WVF to ensure we don't use headers for
	     * retrieval by artno and not msgid
	     */
	    if (mode == COM_ARTICLEWVF)
		conn->co_ArtMode = COM_ARTICLE;
	    NNArticleRetrieveByMessageId(conn, artid, artopts, 0, 0, -1, 0);
	}
    }
}

void
NNArticleRetrieveByArtNo(Connection *conn, artno_t artNo)
{
    const char *ovdata;
    const char *msgid;
    int ovlen, timeRcvd=0, grpIter=-1;
    artno_t endNo=0;

    if (DebugOpt)
	printf("ArtNo %lld\n", artNo);

    conn->co_RequestFlags = ARTFETCH_ARTNO;
    conn->co_ArtNo = artNo;
    if ((ovdata = NNRetrieveHead(conn, &ovlen, &msgid, &timeRcvd, &grpIter, &endNo)) != NULL) {
	
	/*
	 * COM_ARTICLEWVF requires that we verify that we have a valid article
	 * body prior to dumping a valid response code.  At this point we only
	 * have headers.  COM_ARTICLE allows us to dump the status & headers
	 * and then dump (article not available) as the body if we cannot 
	 * retrieve the article.
	 */

	if (conn->co_ArtMode != COM_ARTICLEWVF &&
	    conn->co_ArtMode != COM_BODYWVF
	) {
	    /*
	     * Dump status header
	     */

	    MBLogPrintf(conn, &conn->co_TMBuf, 
		"%03d %lld %s %s\r\n", 
		GoodRC(conn),
		artno_art(conn->co_ArtBeg, conn->co_ArtEnd, conn->co_ArtNo, conn->co_Numbering),
		msgid,
		GoodResId(conn)
	    );
	}

	/*
	 * Dump headers.  Do not dump them yet for COM_ARTICLEWVF.
	 */
	if (conn->co_ArtMode == COM_HEAD || conn->co_ArtMode == COM_ARTICLE) {
	    DumpOVHeaders(conn, ovdata, ovlen);
	}

	/*
	 * Blank line if both header & body (article)
	 */
	if (conn->co_ArtMode == COM_ARTICLE)
	    MBPrintf(&conn->co_TMBuf, "\r\n");

	/*
	 * Dump body (for COM_ARTICLEWVF, this also dumps the headers when
	 * we have verified that the body can be retrieved)
	 */

	if (conn->co_ArtMode == COM_ARTICLEWVF ||
	    conn->co_ArtMode == COM_BODYWVF
	) {
	    NNArticleRetrieveByMessageId(conn, msgid, NULL, 1, timeRcvd, grpIter, endNo);
	} else if (
	    conn->co_ArtMode == COM_ARTICLE || 
	    conn->co_ArtMode == COM_BODY
	) {
	    conn->co_ArtMode = COM_BODYNOSTAT;
	    NNArticleRetrieveByMessageId(conn, msgid, NULL, 1, timeRcvd, grpIter, endNo);
	} else {
	    if (conn->co_ArtMode != COM_STAT)
		MBPrintf(&conn->co_TMBuf, ".\r\n");
	    NNCommand(conn);
	}
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "423 No such article number in this group\r\n");
	NNCommand(conn);
    }
}

void 
NNExecuteOnRange(Connection *conn, const char *artid, int mode)
{
    printf("ExecuteOnRange not implemented yet\n");
}

int
GoodRC(Connection *conn)
{
    switch(conn->co_ArtMode) {
    case COM_STAT:
	return(223);
    case COM_HEAD:
	return(221);
    case COM_ARTICLE:
    case COM_ARTICLEWVF:
	conn->co_ClientTotalArticleCount++;
	conn->co_ClientGroupArticleCount++;
	return(220);
    case COM_BODY:
    case COM_BODYWVF:
	conn->co_ClientTotalArticleCount++;
	conn->co_ClientGroupArticleCount++;
	return(222);
    }
    return(0);
}

const char *
GoodResId(Connection *conn)
{
    switch(conn->co_ArtMode) {
    case COM_STAT:
	return("status");
    case COM_HEAD:
	return("head");
    case COM_ARTICLE:
    case COM_ARTICLEWVF:
	return("article");
    case COM_BODY:
    case COM_BODYWVF:
	return("body");
    }
    return("?");
}

void
DumpOVHeaders(Connection *conn, const char *ovdata, int ovlen)
{
    char *vserver;
    int vslen;

    if (conn->co_Auth.dr_VServerDef)
	vserver = conn->co_Auth.dr_VServerDef->vs_ClusterName;
    else
	vserver = "";
    vslen = strlen(vserver);

    while (ovlen) {
	int i;
	char ch;

	for (i = 0; i < ovlen && ovdata[i] != '\n'; ++i)
	    ;
	if (i == 1 && ovdata[0] == '\r' && ovdata[1] == '\n')
	    break;
	if (ovdata[0] == '.')
	    MBPrintf(&conn->co_TMBuf, ".");
	ch = tolower(ovdata[0]);
	if (*vserver && ch == 'p' && strncasecmp(ovdata, "Path:", 5) == 0) {
	    int b = 5;
	    char newpath[256];

	    sprintf(newpath, "%s!", vserver);

	    while (b < i && (ovdata[b] == ' ' || ovdata[b] == '\t'))
		++b;

	    MBWrite(&conn->co_TMBuf, ovdata, b);
	    i -= b;
	    ovlen -= b;
	    ovdata += b;
	    if (strncmp(newpath, ovdata, vslen + 1) != 0)
		MBWrite(&conn->co_TMBuf, newpath, vslen + 1);
	} else if (*vserver && ch == 'x' && strncasecmp(ovdata, "Xref:", 5) == 0) {
	    int b = 5;

	    while (b < i && (ovdata[b] == ' ' || ovdata[b] == '\t'))
		++b;

	    MBWrite(&conn->co_TMBuf, ovdata, b);
	    MBWrite(&conn->co_TMBuf, vserver, vslen);

	    while (b < i && (ovdata[b] != ' ' && ovdata[b] != '\t'))
		++b;

	    i -= b;
	    ovlen -= b;
	    ovdata += b;
	}
	MBWrite(&conn->co_TMBuf, ovdata, i);
	if (i == 0 || ovdata[i-1] != '\r')
	    MBWrite(&conn->co_TMBuf, "\r\n", 2);
	else
	    MBWrite(&conn->co_TMBuf, "\n", 1);
	if (i < ovlen)
	    ++i;
	ovlen -= i;
	ovdata += i;
    }
}

