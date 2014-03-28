
/*
 * FEED.C
 *
 *	Feed-related NNTP commands.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNTPMode(Connection *conn, char **pptr);
Prototype void NNTPIHave(Connection *conn, char **pptr);
Prototype void NNTPCheck(Connection *conn, char **pptr);
Prototype void NNTPTakeThis(Connection *conn, char **pptr);
Prototype void NNTPPost(Connection *conn, char **pptr);
Prototype int ValidXRef(const char *buf, int len);

void NNAcceptArticleStart(Connection *conn, const char *msgid);
void NNAcceptArticle(Connection *conn);

void DebugData(const char *h, const void *buf, int n);

/*
 * mode stream
 * mode reader
 * mode headfeed
 */

void 
NNTPMode(Connection *conn, char **pptr)
{
    char *mode = parseword(pptr, " \t");
    int ok = 0;

    if (mode) {
	if (strcasecmp(mode, "stream") == 0) {
	    conn->co_Flags |= COF_STREAM;
	    MBLogPrintf(conn, &conn->co_TMBuf, "203 StreamOK.\r\n");
	    ok = 1;
	}
	if (strcasecmp(mode, "headfeed") == 0) {
	    if (conn->co_Flags & COF_SERVER) {
		conn->co_Flags |= COF_HEADFEED;
		MBLogPrintf(conn, &conn->co_TMBuf, "250 Mode Command OK.\r\n");
	    } else {
		MBLogPrintf(conn, &conn->co_TMBuf, "500 Mode Command Failed, no server access.\r\n");
	    }
	    ok = 1;
	}
	if (strcasecmp(mode, "reader") == 0) {
	    if (conn->co_Auth.dr_Flags & DF_FEEDONLY) {
		MBLogPrintf(conn, &conn->co_TMBuf, "500 Mode Command Failed, feed-only connection.\r\n");
		ok = 1;
	    } else {
		if (conn->co_Flags & COF_SERVER) {
		    /*
		     * Once out of server mode, you cant get back in and
		     * the reader-mode flags apply.
		     */
		    conn->co_Flags &= ~COF_SERVER;
		}
		NNWriteHello(conn);
		return;
	    }
	}
    }
    if (ok == 0)
	MBLogPrintf(conn, &conn->co_TMBuf, "500 Syntax error or bad command\r\n");
    NNCommand(conn);
}

/*
 *
 */

void 
NNTPIHave(Connection *conn, char **pptr)
{
    if (conn->co_Flags & COF_SERVER) {
	const char *origmsgid = "<>";
	const char *msgid = MsgId(parseword(pptr, ""), &origmsgid);

	if (strcmp(msgid, "<>") == 0) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "435 %s Bad Message-ID\r\n",
								origmsgid);
	    NNCommand(conn);
	    return;
	}

	conn->co_Flags &= ~(COF_IHAVE|COF_POST|COF_POSTTOOBIG);
	conn->co_Flags |= COF_IHAVE;
	MBLogPrintf(conn, &conn->co_TMBuf, "335\r\n");
	NNAcceptArticleStart(conn, msgid);
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "480 Transfer permission denied\r\n");
	NNCommand(conn);
    }
}

void 
NNTPCheck(Connection *conn, char **pptr)
{
    const char *origmsgid = "<>";
    const char *msgid = MsgId(parseword(pptr, " \t"), &origmsgid);

    if (conn->co_Flags & COF_SERVER) {
	if (strcmp(msgid, "<>") == 0)
	    MBLogPrintf(conn, &conn->co_TMBuf, "438 %s\r\n", origmsgid);
	else
	    MBLogPrintf(conn, &conn->co_TMBuf, "238 %s\r\n", msgid);
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "500 What?\r\n");
    }
    NNCommand(conn);
}

void 
NNTPTakeThis(Connection *conn, char **pptr)
{
    const char *origmsgid = "<>";
    const char *msgid = MsgId(parseword(pptr, ""), NULL);

    conn->co_Flags &= ~(COF_IHAVE|COF_POST|COF_POSTTOOBIG);

    if (conn->co_Flags & COF_SERVER) {
	if (strcmp(msgid, "<>") == 0) {
	    MBLogPrintf(conn, &conn->co_TMBuf, "439 %s\r\n", origmsgid);
	    NNCommand(conn);
	} else
	    NNAcceptArticleStart(conn, msgid);
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "500 What?\r\n");
	NNCommand(conn);
    }
}

void
NNTPPost(Connection *conn, char **pptr)
{
    char *rcode;
    char *rdesc;

    if (conn->co_Auth.dr_Flags & DF_POST) {
	conn->co_Flags &= ~(COF_IHAVE|COF_POST);
	conn->co_Flags |= COF_POST;
	GenerateMessageID(conn);
#ifdef POST_FORCEMSGID
	rcode = "345";
	rdesc = "(forced)";
#else
	rcode = "340";
	rdesc = "(desired)";
#endif
	MBLogPrintf(conn, &conn->co_TMBuf, "340 %s %s %s\r\n",
					rcode, conn->co_IHaveMsgId, rdesc);
	NNAcceptArticleStart(conn, NULL);
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "440 Posting Not Allowed.\r\n");
	NNCommand(conn);
    }
}

void
NNAcceptArticleStart(Connection *conn, const char *msgid)
{
    if (msgid) {
	if (conn->co_IHaveMsgId)
	    zfreeStr(&conn->co_MemPool, &conn->co_IHaveMsgId);
	conn->co_IHaveMsgId = zallocStr(&conn->co_MemPool, msgid);
    }
    conn->co_Flags |= COF_INHEADER;
    conn->co_Flags &= ~COF_WASCONTROL;
    conn->co_Flags |= COF_MAYNOTCLOSE;
    conn->co_ByteCounter = 0.0;
    conn->co_BytesHeader = 0;
    MBFree(&conn->co_ArtBuf);
    NNAcceptArticle(conn);
}

void
NNAcceptArticle(Connection *conn)
{
    char *buf;
    int len = 0;
    int maxl = 1000;

    conn->co_Func = NNAcceptArticle;
    conn->co_State = "accart";

    while (--maxl > 0 && (len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	/*
	 * expected EOF, ".\r\n", but we also allow ".\n" because some
	 * idiotic newsreaders (like old versions of netscape) don't do the
	 * right thing.
	 */
	if ((len == 3 && strcmp(buf, ".\r") == 0) ||
	    (len == 2 && strcmp(buf, ".") == 0)
	) {
	    if (conn->co_Flags & COF_HEADFEED)	/* can't use bytecounter */
		conn->co_ByteCounter = 0.0;

	    if (conn->co_Flags & COF_POST) {
		NNPostBuffer(conn);
	    } else {
		NNFeedOverview(conn);
	    }
	    conn->co_Flags &= ~COF_MAYNOTCLOSE;
	    return;
	}

	/*
	 * We normally unescape dots, but if this article reception is for
	 * a POST command we leave them intact so we do not have to 
	 * re-escape them when the article buffer is passed on.
	 */

	if ((conn->co_Flags & COF_POST) == 0) {
	    if (len > 0 && buf[0] == '.') {
		++buf;
		--len;
	    }
	}

	/*
	 * end of headers ?
	 * control message ?
	 */

	if (conn->co_Flags & COF_INHEADER) {
	    char ch = tolower(*buf);
	    if (strcmp(buf, "\r") == 0) {
		conn->co_Flags &= ~COF_INHEADER;
	    } else if (ch == 'b' && strncasecmp(buf, "Bytes:", 6) == 0) {
		/*
		 * save received Bytes: header (for header-only feed), but
		 * do not write the header out.
		 */
		conn->co_BytesHeader = strtol(buf + 6, NULL, 10);
		len = 0;
	    } else if (ch == 'c' && strncasecmp(buf, "Control:", 8) == 0) {
		conn->co_Flags |= COF_WASCONTROL;
	    } else if (ch == 'x' && strncasecmp(buf, "Xref:", 5) == 0) {
		/*
		 * strip XRef: unless it's one we want
		 */
		if (ValidXRef(buf, len) < 0)
		    len = 0;
	    }
	}

	/*
	 * buffer the article, but don't bother buffering the article body
	 * for a fed article.  BUT, if it's a Control: message, we need the
	 * whole body so we can pgp-verify it
	 */

	conn->co_Auth.dr_PostBytes += len;
	conn->co_ByteCounter += len;

	if (DOpts.ReaderMaxArtSize > 0 && conn->co_ArtBuf.mh_Bytes > DOpts.ReaderMaxArtSize)
	    conn->co_Flags |= COF_POSTTOOBIG;

	else if ((conn->co_Flags & COF_INHEADER) || (conn->co_Flags & (COF_POST|COF_WASCONTROL))) {
	    if (len > 0)
		buf[len-1] = '\n';
	    MBWrite(&conn->co_ArtBuf, buf, len);
	}
    }
    if (len <= 0) {
	if (len < 0)
	    NNTerminate(conn);
    } else {
	/*
	 * more lines were pending, wake us up immediately, after we've
	 * processed some other people.
	 */
	FD_SET(conn->co_Desc->d_Fd, &WFds);
    }
}

/*
 * Return 0 on success, -1 if XRefSlaveHost is disabled, -2
 * If the XRef is invalid.
 */

int
ValidXRef(const char *buf, int len)
{
    /*
     * strip XRef: unless it's one we want
     */
    int l;
    int lp;
    const char *p;

    if (DOpts.ReaderXRefSlaveHost == NULL)
	return(-1);

    l = strlen(DOpts.ReaderXRefSlaveHost);
    p = buf + 5;
    lp = len - 5;

    while (lp > 0 && *p == ' ') {
	--lp;
	++p;
    }
    if (lp <= l || 
	strncasecmp(DOpts.ReaderXRefSlaveHost, p, l) != 0 ||
	p[l] != ' '
    ) {
	return(-2);
    }
    return(0);
}

