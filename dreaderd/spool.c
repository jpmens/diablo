
/*
 * SPOOL.C	- Spool server state machine
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNSpoolCommand1(Connection *conn);

void NNSpoolResponse1(Connection *conn);
void NNSpoolResponse2(Connection *conn);
void NNSpoolResponse3(Connection *conn);
void NNSpoolResponseScrap(Connection *conn);
void NNGetLocal1(Connection *conn);

void
NNSpoolCommand1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;

    if (DebugOpt)
	printf("NNSpoolCommand1: article %s\n", sreq->sr_MsgId);

    if (conn->co_Desc->d_LocalSpool) {
	if (strcmp(conn->co_Desc->d_LocalSpool, "/") == 0)
	    MBPrintf(&conn->co_TMBuf, "whereis %s\r\n", sreq->sr_MsgId);
	else
	    MBPrintf(&conn->co_TMBuf, "whereis %s REL\r\n", sreq->sr_MsgId);
	NNGetLocal1(conn) ;
    } else {
	MBPrintf(&conn->co_TMBuf, "article %s\r\n", sreq->sr_MsgId);
	conn->co_ServerArticleRequestedCount++;
	NNSpoolResponse1(conn);
    }
}

/*
 * Retrieve location
 */
void
NNGetLocal1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    char *buf;
    char *ptr;
    char *filename;
    int len;
    int offset;
    int size;
    char smaxage[32];

    conn->co_Func = NNGetLocal1;
    conn->co_State = "getlcl1";

    *smaxage = '\0';

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len;

	if (sreq->sr_MaxAge) {
	    snprintf(smaxage, sizeof(smaxage), " a%d", sreq->sr_MaxAge);
	}

	if (strtol(buf, NULL, 10) == 223) {
	    /*
	     * sr_CConn may be NULL if client was terminated while
	     * server operation was still in progress.
	     */
	    if (conn->co_SReq->sr_CConn) {
		MBFree(&conn->co_SReq->sr_CConn->co_ArtBuf);
		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_BODYNOSTAT:
			break;
		    case COM_STAT:
		    case COM_HEAD:
		    case COM_BODY:
		    case COM_ARTICLE:
			MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d 0 %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
			);
			break;
		    case COM_BODYWVF:
			MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d %lld %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
			);
			break;
		    case COM_ARTICLEWVF: {
			const char *ovdata;
			const char *msgid;
			int ovlen;
			if ((ovdata = NNRetrieveHead(sreq->sr_CConn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
			    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_TMBuf, "%03d %lld %s %s\r\n",
						GoodRC(sreq->sr_CConn),
						artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
						sreq->sr_MsgId,
						GoodResId(sreq->sr_CConn)
);
			    DumpOVHeaders(sreq->sr_CConn, ovdata, ovlen);
			    MBPrintf(&sreq->sr_CConn->co_TMBuf, "\r\n"); 
			    sreq->sr_CConn->co_ArtMode = COM_BODYNOSTAT;
			} else {
			    NNSpoolResponseScrap(conn);
			    return; /* bleh */
			}
		    }
		    break;
		}
	    }
	    /*
	     * We have to get additionnal information from the buffer
	     */
	    strtok(buf, " ") ;
	    offset = -1;
	    size = -1;
	    filename = NULL;
	    while ((ptr = strtok(NULL, " ")) != NULL) {
		if (strcasecmp(ptr, "in") == 0) {
		    filename = strtok(NULL," ");
		} else if (strcasecmp("offset", ptr) == 0) {
		    ptr = strtok(NULL," ");
		    if (ptr) {
			offset = strtol(ptr, NULL, 10);
			if (offset == LONG_MIN || offset == LONG_MAX)
			    offset = -1;
		    } else {
			offset = -1;
		    }
		} else if (strcasecmp("length", ptr) == 0) {
		    ptr = strtok(NULL," ");
		    if (ptr) {
			size = strtol(ptr, NULL, 10);
			if (size == LONG_MIN || size == LONG_MAX)
			    size = -1;
		    } else {
			size = -1;
		    }
		}
	    }

	    if (filename != NULL && offset >= 0 && size >= 0) {
		int lf;
		static char filepath[PATH_MAX];
		if (DebugOpt)
		    printf("NNGetLocal1 : whereis returned file %s offset %i size %i\n",
						filename, offset, size);
		if (strcmp(conn->co_Desc->d_LocalSpool, "/") == 0)
		    snprintf(filepath, sizeof(filepath), "%s", filename);
		else
		    snprintf(filepath, sizeof(filepath), "%s/%s",
					conn->co_Desc->d_LocalSpool, filename);
		lf = open(filepath, O_RDONLY);
		if (lf >= 0 && NewDFA(conn, lf, offset, size)) {
		    NNSendLocalArticle(conn);
		} else {
		    if (lf == -1) {
			logit(LOG_ERR, "NNGetLocal1 : problem while openning file %s",
							filepath);
		    } else {
			logit(LOG_ERR, "NNGetLocal1 : problem while seeking file %s offset %i",
							filepath, offset);
		    }
		    close(lf);
		    MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
		    NNSpoolResponse1(conn);
		}
	    } else {
		logit(LOG_ERR, "NNGetLocal1 : error while getting whereis informations");
		MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
		NNSpoolResponse1(conn);
	    }
	} else {
	    /*
	     * whereis request failed, try the normal way
	     */
	    MBPrintf(&conn->co_TMBuf, "article %s%s\r\n", sreq->sr_MsgId, smaxage);
	    conn->co_ServerArticleRequestedCount++;
	    NNSpoolResponse1(conn);
	}
    } else if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (1), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * Retrieve return code
 */

void
NNSpoolResponse1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    char *buf;
    int len;

    conn->co_Func = NNSpoolResponse1;
    conn->co_State = "spres1";

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len;
	if (strtol(buf, NULL, 10) == 220) {
	    /* We have a positive answer, we may cache article */
	    if (conn->co_Desc->d_Cache) {
		CreateCache(conn);
	    }
	    /*
	     * sr_CConn may be NULL if client was terminated while
	     * server operation was still in progress.
	     */
	    if (conn->co_SReq->sr_CConn) {
		MBFree(&conn->co_SReq->sr_CConn->co_ArtBuf);

		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_BODYNOSTAT:
		    break;
		case COM_STAT:
		case COM_HEAD:
		case COM_BODY:
		case COM_ARTICLE:
		    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d 0 %s %s\r\n",
			GoodRC(sreq->sr_CConn),
			sreq->sr_MsgId,
			GoodResId(sreq->sr_CConn)
		    );
		    break;
		case COM_BODYWVF:
		    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_ArtBuf, "%03d %lld %s %s\r\n",
			GoodRC(sreq->sr_CConn),
			artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
			sreq->sr_MsgId,
			GoodResId(sreq->sr_CConn)
		    );
		    break;
		case COM_ARTICLEWVF:
		    {
			const char *ovdata;
			const char *msgid;
			int ovlen;

			if ((ovdata = NNRetrieveHead(sreq->sr_CConn, &ovlen, &msgid, NULL, NULL, NULL)) != NULL) {
			    MBLogPrintf(sreq->sr_CConn, &sreq->sr_CConn->co_TMBuf, "%03d %lld %s %s\r\n",
				GoodRC(sreq->sr_CConn),
				artno_art(sreq->sr_CConn->co_ArtBeg, sreq->sr_CConn->co_ArtEnd, sreq->sr_CConn->co_ArtNo, sreq->sr_CConn->co_Numbering),
				sreq->sr_MsgId,
				GoodResId(sreq->sr_CConn)
			    );
			    DumpOVHeaders(sreq->sr_CConn, ovdata, ovlen);
			    MBPrintf(&sreq->sr_CConn->co_TMBuf, "\r\n"); 
			    sreq->sr_CConn->co_ArtMode = COM_BODYNOSTAT;
			} else {
			    NNSpoolResponseScrap(conn);
			    return; /* bleh */
			}
		    }
		    break;
		}
	    }
	    NNSpoolResponse2(conn);
	    return;
	}
	else if (strtol(buf, NULL, 10) == 430) {
	  conn->co_ServerArticleNotFoundErrorCount++;
	}
	else {
	  conn->co_ServerArticleMiscErrorCount++;
	}

	if (sreq->sr_CConn == NULL)
	    NNFinishSReq(conn, NULL, 0);
	else if (sreq->sr_CConn->co_ArtMode == COM_BODYNOSTAT)
	    NNFinishSReq(conn, "(article not available)\r\n.\r\n", 1);
	else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
            NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	else
            NNFinishSReq(conn, "430 No such article\r\n", 1);
    } else if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (2), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * Retrieve headers
 */

void
NNSpoolResponse2(Connection *conn)
{
    char *buf;
    int len;
    ServReq *sreq = conn->co_SReq;
    char *vserver;
    int doneconn;
    char ch;

    conn->co_Func = NNSpoolResponse2;
    conn->co_State = "spres2";

    /*
     * We need to check that co_Auth.dr_VServerDef is defined
     * because it probably won't be for a connection to a backend
     * spool
     */
    if (sreq->sr_CConn && sreq->sr_CConn->co_Auth.dr_VServerDef)
	vserver = sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName;
    else
	vserver = "";

    while ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
#ifdef STATS_ART_AGE_DR
	if (! strncasecmp(buf, "Date:", 5)) {
	    logit(LOG_INFO, "articleage %s %d", sreq->sr_MsgId, (int)(time(NULL)) - parsedate(buf));
	}
#endif
	conn->co_ServerByteCount += len;
	if (len == 2 && strcmp(buf, "\r") == 0) {
	    if (sreq->sr_Cache)
		fwrite("\r\n", 1, 2, sreq->sr_Cache);

	    if (sreq->sr_CConn) {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_ARTICLE:
		    MBPrintf(&sreq->sr_CConn->co_ArtBuf, "\r\n");
		    break;
		}
	    }
	    NNSpoolResponse3(conn);
	    return;
	}
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    if (sreq->sr_CConn == NULL)
		NNFinishSReq(conn, NULL, 0);
	    else if (sreq->sr_CConn->co_ArtMode == COM_BODYNOSTAT)
		NNFinishSReq(conn, "(article not available)\r\n.\r\n", 1);
	    else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
		NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	    else
		NNFinishSReq(conn, "430 No such article\r\n", 1);
	    return;
	}

	/*
	 * Only munge Xref: header if we asked for it, the wrong Xref:
	 * header can blow up news readers, so we need to be able to
	 * change it when viewing to match the Path:
	 *
	 * sr_CConn may be NULL if the client terminated or if an
	 * autonomous lookahead request was issued.
	 *
	 */

	doneconn = 0;
	ch = tolower(*buf);
	if (*vserver && sreq->sr_CConn &&
		!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate &&
		ch == 'x' && strncasecmp(buf, "Xref:", 5) == 0) {
	    char *ptr;
	    int l;

	    /*
	     * The len includes the trailing \n converted to a \0
	     */
	    l = len - 2;
	    while (l > 5 && (buf[l] == '\r' || buf[l] == '\n'))
		l--;

	    ptr = buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    while (!isspace((int)*ptr))
		ptr++;
	    while (isspace((int)*ptr))
		ptr++;
	    if (*ptr) {
		char line[8192];
		int e;

		l = (buf + l) - ptr + 1;
		sprintf(line, "Xref: %s ", vserver);
		e = strlen(line);
		memcpy(&line[e], ptr, l);
		line[e + l] = '\0';
		strcat(line, "\r\n");

		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_HEAD:
		    case COM_ARTICLE:
			MBWrite(&sreq->sr_CConn->co_ArtBuf, line, strlen(line));
			break;
		}
		doneconn = 1;
	    }
	}
	if (*vserver && sreq->sr_CConn &&
		!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoReadPath &&
			ch == 'p' && strncasecmp(buf, "Path:", 5) == 0) {
	    char *ptr;
	    int l;
	    int vsl = strlen(vserver);

	    /*
	     * The len includes the trailing \n converted to a \0
	     */
	    l = len - 2;
	    while (l > 5 && (buf[l] == '\r' || buf[l] == '\n'))
		l--;

	    ptr = buf + 5;
	    while (isspace((int)*ptr))
		ptr++;
	    if (*ptr && (strncmp(vserver, ptr, vsl) ||
			 ((ptr[vsl] != '\0') &&
			  (ptr[vsl] != '!')))) {
		char line[8192];
		int e;

		l = (buf + l) - ptr + 1;
		sprintf(line, "Path: %s!", vserver);
		e = strlen(line);
		memcpy(&line[e], ptr, l);
		line[e + l] = '\0';
		strcat(line, "\r\n");

		switch(sreq->sr_CConn->co_ArtMode) {
		    case COM_HEAD:
		    case COM_ARTICLE:
			MBWrite(&sreq->sr_CConn->co_ArtBuf, line, strlen(line));
			break;
		}
		doneconn = 1;
	    }
	}

	if (len) {
	    buf[len-1] = '\n';

	    if (sreq->sr_Cache)
		fwrite(buf, 1, len, sreq->sr_Cache);

	    /*
	     * sr_CConn may be NULL if the client terminated or if an
	     * autonomous lookahead request was issued.
	     */

	    if (!doneconn && sreq->sr_CConn) {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_HEAD:
		case COM_ARTICLE:
		    MBWrite(&sreq->sr_CConn->co_ArtBuf, buf, len);
		    break;
		}
	    }
	}

    }
    if (len < 0) {
	NNServerTerminate(conn);
    } else {
	/* else we haven't got the response yet */
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (3), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

/*
 * Retrieve the body, place in client's ArtBuf
 */

void
NNSpoolResponse3(Connection *conn)
{
    char *buf;
    ServReq *sreq = conn->co_SReq;
    int len = 0;

    conn->co_Func = NNSpoolResponse3;
    conn->co_State = "spres3";

    for (;;) {
	int error = 0;

	len = MBReadLine(&conn->co_RMBuf, &buf);

	/*
	 * If an error occurs and we are not in FastCopyOpt mode,
	 * we can simply terminate the server and the request will be
	 * retried.  If we are an FastCopyOpt mode we have already 
	 * started writing the article back to the client and cannot
	 * simply cut things off.  We simulate an ending, make sure
	 * we do not commit the article to the cache, and *then*
	 * terminate the server.
	 */

	if (len < 0) {
	    if (FastCopyOpt == 0) {
		NNServerTerminate(conn);
		return;
	    }
	    len = 3;
	    buf = ".\r";
	    error = 1;
	}

	/*
	 * If len is 0, we have nothing to do
	 */

	if (len == 0)
	    break;


	conn->co_ServerByteCount += len + 1;
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    conn->co_ServerArticleCount++;
	    if (sreq->sr_CConn) {
		MBCopy(
		    &sreq->sr_CConn->co_ArtBuf,
		    &sreq->sr_CConn->co_TMBuf
		);
	    }
	    if (sreq->sr_Cache) {
		fflush(sreq->sr_Cache);
		if (ferror(sreq->sr_Cache) || error)
		    AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
		else
		    CommitCache(conn, 0);
		fclose(sreq->sr_Cache);
		sreq->sr_Cache = NULL;
	    }
	    if (sreq->sr_CConn == NULL) {
		NNFinishSReq(conn, NULL, 0);
	    } else {
		switch(sreq->sr_CConn->co_ArtMode) {
		case COM_ARTICLE:
		case COM_BODY:
		case COM_BODYWVF:
		case COM_BODYNOSTAT:
		case COM_HEAD:
		    if (error)
			MBPrintf(&sreq->sr_CConn->co_TMBuf, "(spool server died prior to completion of the article dump)\r\n");
		    NNFinishSReq(conn, ".\r\n", 0);
		    break;
		case COM_STAT:
		    NNFinishSReq(conn, "", 0);
		    break;
		default:
		    NNFinishSReq(conn, "(something blew up in the spool code)\r\n.\r\n", 0);
		    break;
		}
	    }

	    /*
	     * If an error occured ( can only happen if we were in FastCopyOpt
	     * mode ), we have to terminate the server now, after we've
	     * finished processing the client request.
	     */
	    if (error) {
		NNServerTerminate(conn);
	    }
	    return;
	}
	if (len > 0)
	    buf[len-1] = '\n';

	if (sreq->sr_CConn) {
	    switch(sreq->sr_CConn->co_ArtMode) {
	    case COM_ARTICLE:
	    case COM_BODY:
	    case COM_BODYWVF:
	    case COM_BODYNOSTAT:
		MBWrite(&sreq->sr_CConn->co_ArtBuf, buf, len);
		break;
	    }
	}
	if (sreq->sr_Cache)
	    fwrite(buf, 1, len, sreq->sr_Cache);
    }
    if (FastCopyOpt && sreq->sr_CConn) {
        MBCopy(
            &sreq->sr_CConn->co_ArtBuf,
            &sreq->sr_CConn->co_TMBuf
        );
    }
    /* still waiting for input */
    if (len == 0) {
	/* note that we get here at least every 30s or so via forceallcheck */
	if (conn->co_Desc->d_Timeout && (time(NULL) > sreq->sr_Time + conn->co_Desc->d_Timeout)) {
	    logit(LOG_ERR, "Timeout elapsed waiting for %s to answer request %s (4), closing spool server", conn->co_Desc->d_Id, sreq->sr_MsgId);
	    NNServerTerminate(conn);
	}
    }
}

void
NNSpoolResponseScrap(Connection *conn)
{
    char *buf;
    int len;
    ServReq *sreq = conn->co_SReq;

    conn->co_Func = NNSpoolResponse3;
    conn->co_State = "spres3";

    while ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	conn->co_ServerByteCount += len + 1;
	if (len == 3 && strcmp(buf, ".\r") == 0) {
	    conn->co_ServerArticleCount++;
	    if (sreq->sr_Cache) {
		fflush(sreq->sr_Cache);
		if (ferror(sreq->sr_Cache))
		    AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
		else
		    CommitCache(conn, 0);
		fclose(sreq->sr_Cache);
		sreq->sr_Cache = NULL;
	    }
	    if (sreq->sr_CConn == NULL)
		NNFinishSReq(conn, NULL, 0);
	    else if (sreq->sr_CConn->co_RequestFlags == ARTFETCH_ARTNO)
		NNFinishSReq(conn, "423 No such article number in this group\r\n", 1);
	    else
		NNFinishSReq(conn, "430 No such article\r\n", 1);
	    return;
	}
    }
    if (len < 0) {
	NNServerTerminate(conn);
    } /* else we are still waiting for input */
}

