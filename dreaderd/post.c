/* TODO: backoff algorithm */
/* TODO: moderator mail error check */
/* TODO: check if authenticated users are properly put in X-Trace */

/*
 * POST.C
 *
 *	Post an article.  The article is posted to one or several of the
 *	upstream servers depending on prioritization and the existence of
 *	the 'P'ost flag.  
 *
 *	The article is loaded into memory prior to posting.  If no servers
 *	are available, the article is written to a local posting queue and
 *	the post is retried later.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

#ifdef	POST_FORCEMSGID
#ifndef	POST_CKPATHLOOP
#error	"Please go back and read the message in vendor.h about POST_FORCEMSGID"
#endif
#endif

#ifdef	POST_CFILTERHOOK
char *PostCFilter(char *base, int artLen, Connection *conn);
#endif
#ifdef        POST_XFILTERHOOK
char *PostXFilter(char *base, int artLen, Connection *conn);
#endif


Prototype void NNPostBuffer(Connection *conn);
Prototype void NNPostCommand1(Connection *conn);

void NNPostResponse1(Connection *conn);
void NNPostArticle1(Connection *conn);
void NNPostResponse2(Connection *conn);
int MailIfModerated(char *groups, Connection *conn);
int groupAlready(int v, int *save, int saveCount);
void MailToModerator(const char *group, Connection *conn, const char *modMail);
int CheckGroupModeration(const char *group);
void GenerateDistribution(MBufHead *mbuf, char *newsGrps);
int BreakIfBadGroup(char *groups, Connection *conn);
int CheckValidRestrictedGroup(const char *group);
int HeaderCmp(const char *big, const char *little, char **errMsg);

/* JG19990908 don't publicize NNPH's, but still present a NNPH field that
   can be used to identify posts from the same node (i.e. for things like
   UPS and DSRS).  Hash changes every 1hour. */

char *NNPHMangle(char *vserv, char *node, char *cryptpw)
{
	char buffer[256], md5buf[34];
	static char res[256];

	/* This string is arbitrary, as long as it is the same for
	   any given node, you're ok */
	snprintf(buffer, sizeof(buffer), "%s/%d/%s/%s", node, (int)time(NULL) / 3600, vserv, cryptpw);
	/* Safety: in the event we cant md5, then leave some useful
	   data in md5buf */
	snprintf(md5buf, sizeof(md5buf), "%s", node);
	md5hashstr(md5hash(buffer), md5buf);
	/* Clip the string.  8 digits is enough */
	md5buf[8] = '\0';
	snprintf(res, sizeof(res), "%s.%s", md5buf, vserv);
	return(res);
}

#ifdef	POST_CRYPTXTRACE

/* JG19990908 encrypt (for loose def'n of encrypt, heh) X-Trace, presenting
   a uuencode-style line that can be decoded by abuse */

char *XTRAMangle(char *xtrace, char *cryptpw)
{
	static char res[256];
	char *rptr, *xptr;
	int count;
	unsigned char plain[8], enc[8];
	des_cblock key;
	des_key_schedule sched;

	des_string_to_key(cryptpw, &key);
	des_set_key(&key, sched);

	rptr = res;

	/* Overflow protect; 192 chars expands to 256, overflowing res */
	if (strlen(xtrace) > 191) {
		xtrace[191] = '\0';
	}

	/* Step thru 'xtrace' 8 chars at a time, encrypt as we go */
	xptr = xtrace;
	count = 0;
	bzero(plain, sizeof(plain));
	while (*xptr) {
	    plain[count++ % 8] = *xptr++;
	    if (! *xptr || (count % 8) == 0) {
		/* Encrypt block of (up to) 8 and pump out coded ASCII */
		des_ecb_encrypt((des_cblock *)plain,(des_cblock *)enc, sched, 1);
		bzero(plain, sizeof(plain));
		*rptr++ = '0' + (enc[0] & 0x3f);
		*rptr++ = '0' + (enc[0] >> 6) + ((enc[1] & 0x0f) << 2);
		*rptr++ = '0' + (enc[1] >> 4) + ((enc[2] & 0x03) << 4);
		*rptr++ = '0' + (enc[2] >> 2);
		*rptr++ = '0' + (enc[3] & 0x3f);
		*rptr++ = '0' + (enc[3] >> 6) + ((enc[4] & 0x0f) << 2);
		*rptr++ = '0' + (enc[4] >> 4) + ((enc[5] & 0x03) << 4);
		*rptr++ = '0' + (enc[5] >> 2);
		*rptr++ = '0' + (enc[6] & 0x3f);
		*rptr++ = '0' + (enc[6] >> 6) + ((enc[7] & 0x0f) << 2);
		*rptr++ = '0' + (enc[7] >> 4) + ((time(NULL) & 0x03) << 4);
	    }
	}
	*rptr = '\0';
	return(res);
}

char *XTRAUnmangle(char *str, char *cryptpw)
{
	static char res[256];
	char *rptr, *sptr;
	int count, i;
	unsigned char plain[8], enc[8];
	des_cblock key;
	des_key_schedule sched;

	des_string_to_key(cryptpw, &key);
	des_set_key(&key, sched);

	if (! strncmp(str, "DXC=", 4)) {
		str += 4;
	}

	*res = '\0';
	if (strlen(str) % 11) {
		snprintf(res, sizeof(res), "(X-Trace damaged; may not decode OK) ");
	}

	rptr = res + strlen(res);
	
	sptr = str;
	count = 0;
	bzero(enc, sizeof(enc));
	while (*sptr) {
	    switch (count % 11) {
		case 0:
		    enc[0] = (*sptr - '0');
		    break;
		case 1:
		    enc[0] += ((*sptr - '0') & 0x03) << 6;
		    enc[1] = (*sptr - '0') >> 2;
		    break;
		case 2:
		    enc[1] += ((*sptr - '0') & 0x0f) << 4;
		    enc[2] = (*sptr - '0') >> 4;
		    break;
		case 3:
		    enc[2] += (*sptr - '0') << 2;
		    break;
		case 4:
		    enc[3] = (*sptr - '0');
		    break;
		case 5:
		    enc[3] += ((*sptr - '0') & 0x03) << 6;
		    enc[4] = (*sptr - '0') >> 2;
		    break;
		case 6:
		    enc[4] += ((*sptr - '0') & 0x0f) << 4;
		    enc[5] = (*sptr - '0') >> 4;
		    break;
		case 7:
		    enc[5] += (*sptr - '0') << 2;
		    break;
		case 8:
		    enc[6] = (*sptr - '0');
		    break;
		case 9:
		    enc[6] += ((*sptr - '0') & 0x03) << 6;
		    enc[7] = (*sptr - '0') >> 2;
		    break;
		case 10:
		    enc[7] += ((*sptr - '0') & 0x0f) << 4;
		    break;
	    }
	    count++;
	    sptr++;
	    if (! *sptr || (count % 11) == 0) {
		/* Decrypt block of (up to) 8 and pump out plain ASCII */
		des_ecb_encrypt((des_cblock *)enc,(des_cblock *)plain, sched, 0);
		bzero(enc, sizeof(enc));
		for (i = 0; i < 8; i++) {
			*rptr++ = plain[i];
		}
	    }
	}
	*rptr = '\0';
	return(res);
}
#endif



/*
 * Check to see if the header named in "little" matches the line
 * in big, including simple RFC1036 conformance check (i.e. 
 * little of "Message-ID" errors out on "Message-ID:<foo>" due 
 * to missing whitespace).
 */

int
HeaderCmp(const char *big, const char *little, char **errMsg)
{
	size_t n;
	int rval;

	n = strlen(little);

	if (((rval = strncasecmp(big, little, n))) == 0) {

		// Verify the colon-whitespace
		if (*(big + n) == ':') {
			if (*(big + n + 1) != ' ' &&
			    *(big + n + 1) != '\t') {
				*errMsg = "441 Header does not conform to RFC1036\r\n";
			}
		} else {
			rval = 1;
		}
	}
	return(rval);
}




/*
 * NNPostBuffer() - called when client connection post buffer contains the
 *		    entire article.  We need to check the article headers
 *		    and post it as appropriate.  If we are unable to post it
 *		    immediately, we queue it.
 *
 * STEPS:
 *	(1) remove headers otherwise generated internally (for security)
 *	(2) add internally generated headers
 *	
 * Internally generated headers:
 *	Path:			(removed, generated)
 *	Date:			(removed, generated)
 *	Lines:			(removed, generated)
 *	Message-ID:		(checked, generated)
 *	NNTP-Posting-Date:	(removed, generated - optionally)
 *	NNTP-Posting-Host:	(removed, generated)
 *	X-Trace:		(removed, generated)
 *	Xref:			(removed, RIP)
 *	Distribution:		(checked, generated)
 *
 * Errors:
 *	441 Article has no body -- just headers
 *	441 Required "XXX" header is missing (Subject, From, Newsgroups)
 *	441 435 Bad Message-ID
 */

void
NNPostBuffer(Connection *conn)
{
    int artLen;
    int haveMsgId = 0;
    int haveSubject = 0;
    int haveFrom = 0;
    int haveDate = 0;
    int haveApproved = 0;
    int haveNewsgroups = 0;
    int haveOrganization = 0;
    int haveDist = 0;
    char *errMsg = NULL;
    char *newsGrps = NULL, *newsGrps2 = NULL;
    char *base = MBNormalize(&conn->co_ArtBuf, &artLen);
#ifdef	POST_CRYPTXTRACE
    char xtrace[256];
#endif
    MBufHead	tmbuf;

    MBInit(&tmbuf, -1, &conn->co_MemPool, &conn->co_BufPool);

    if (!errMsg && conn->co_Flags & COF_POSTTOOBIG) {
	conn->co_Flags &= ~COF_POSTTOOBIG;
	errMsg = "441 Article too big for this server\r\n";
    }

#ifdef	POST_CFILTERHOOK
    if (! errMsg) {
	errMsg = PostCFilter(base, artLen, conn);
    }
#endif
#ifdef        POST_XFILTERHOOK
    if (DOpts.PostXFilter && ! errMsg) {
      errMsg = PostXFilter(base, artLen, conn);
    }
#endif


    /*
     * Output Path: line
     */

    MBPrintf(&tmbuf, "Path: %s%snot-for-mail\r\n",
		conn->co_Auth.dr_VServerDef->vs_PostPath, 
		(*conn->co_Auth.dr_VServerDef->vs_PostPath ? "!" : ""));

    /*
     * Scan Headers, check for required headers
     */

    while (artLen > 1 && !(base[0] == '\n') && !(base[0] == '\r' && base[1] == '\n')) {
	int l;
	int keep = 1;

	/*
	 * extract header.  Headers can be multi-line so deal with that.
	 */

	for (l = 0; l < artLen; ++l) {
	    if (base[l] == '\r' && 
		base[l+1] == '\n' &&
		(l + 2 >= artLen || (base[l+2] != ' ' && base[l+2] != '\t'))
	    ) {
		l += 2;
		break;
	    }
	}

	/*
	 * check against specials
	 */
	if (HeaderCmp(base, "Subject", &errMsg) == 0) {
	    haveSubject = 1;
	} else if (HeaderCmp(base, "From", &errMsg) == 0) {
#ifdef	POST_CKADDRESS
	    char *ptr = zallocStrTrim(&conn->co_MemPool, base+5, l - 5);
	    if (ptr && *ptr) {
		HeaderCleanFrom(ptr);
		/*
		 * If somebody realm authenticated, then they are
		 * unconditionally allowed to use that address to post
		 */
		if (! (strchr(ptr, '@') && *conn->co_Auth.dr_AuthUser && ! strcasecmp(ptr, conn->co_Auth.dr_AuthUser))) {
			/*
			 * Otherwise we sanity check the address
			 */
			if (ckaddress(ptr)) {
			    errMsg = "441 From: address not in Internet syntax\r\n";
			}
		}
	    }
	    zfreeStr(&conn->co_MemPool, &ptr);
#endif
	    haveFrom = 1;
	} else if (HeaderCmp(base, "Approved", &errMsg) == 0) {
	    haveApproved = 1;
	} else if (HeaderCmp(base, "Newsgroups", &errMsg) == 0) {
	    haveNewsgroups = 1;
	    newsGrps = zallocStrTrim(&conn->co_MemPool, base + 11, l - 11);
	    newsGrps2 = zallocStrTrim2(&conn->co_MemPool, ',', base + 11, l - 11);
	    if (strcmp(newsGrps, newsGrps2)) {
#ifdef	POST_CLEANUPNEWSGROUPSLINE
		keep = 2;	/* keep = 2 overloaded to mean substitute ng hdr */
#else
		errMsg = "441 Invalid newsgroup(s) line\r\n";
#endif
	    }
	} else if (HeaderCmp(base, "Organization", &errMsg) == 0) {
	    haveOrganization = 1;
	} else if (HeaderCmp(base, "Date", &errMsg) == 0) {
	    haveDate = 1;
	} else if (HeaderCmp(base, "Distribution", &errMsg) == 0) {
	    /*
	     * Only keep the header and set haveDist if it has something in 
	     * it, otherwise we (potentially) override it.
	     */
	    {
		char *ptr = zallocStrTrim2(&conn->co_MemPool, 0, base+13, l - 13);
		if (ptr && *ptr)
		    haveDist = 1;
		zfreeStr(&conn->co_MemPool, &ptr);
	    }
	    if (haveDist == 0)
		keep = 0;
	} else if (HeaderCmp(base, "Path", &errMsg) == 0) {
	    /* JG19990907 path element count and reject */
	    char *ptr = zallocStrTrim2(&conn->co_MemPool, '!', base+5, l - 5);
	    if (conn->co_Auth.dr_ReaderDef->rd_PathComponents && ptr && *ptr) {
		char *p = ptr;
		int count = 0;
		while ((p = strchr(p + 1, '!')))
			count++;
		if (count > conn->co_Auth.dr_ReaderDef->rd_PathComponents)
			errMsg = "441 Can't inject non-local news article\r\n";

#ifdef POST_CKPATHLOOP
		if (strstr(ptr, POST_CKPATHLOOP))
			errMsg = "441 Can't reinject news article\r\n";
#endif
	    }
	    zfreeStr(&conn->co_MemPool, &ptr);
	    keep = 0;	/* delete */
	} else if (HeaderCmp(base, "Date", &errMsg) == 0) {
	    keep = 0;	/* delete */
	} else if (HeaderCmp(base, "Lines", &errMsg) == 0) {
	    keep = 0;	/* delete */
	} else if (HeaderCmp(base, "Message-ID", &errMsg) == 0) {
#ifdef	POST_FORCEMSGID
	    keep = 0;   /* delete */
#else
	    const char *msgmsgid;

	    if (strcmp((msgmsgid = MsgId(base + 11, NULL)), "<>") == 0 ||
		strchr(base, '@') == NULL
	    ) {
		keep = 0;	/* delete */
		errMsg = "441 435 Bad Message-ID\r\n";
		if (conn->co_IHaveMsgId)
		    zfreeStr(&conn->co_MemPool, &conn->co_IHaveMsgId);
	    } else {
		keep = 1;
		haveMsgId = 1;
		conn->co_IHaveMsgId = zallocStr(&conn->co_MemPool, msgmsgid);
	    }
#endif
#ifdef	POST_BOFHCLEANUP
	/* JG19990304 other cleanups */
	} else if (HeaderCmp(base, "Date-Received", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Date-Received\" header rejected\r\n";
	} else if (HeaderCmp(base, "Received", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Received\" header rejected\r\n";
	} else if (HeaderCmp(base, "Posted", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Posted\" header rejected\r\n";
	} else if (HeaderCmp(base, "Posting-Version", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Posting-Version\" header rejected\r\n";
	} else if (HeaderCmp(base, "Relay-Version", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Relay-Version\" header rejected\r\n";
	} else if (HeaderCmp(base, "To", &errMsg) == 0) {
		errMsg = "441 Obsolete \"To\" header rejected\r\n";
	} else if (HeaderCmp(base, "Cc", &errMsg) == 0) {
		errMsg = "441 Obsolete \"Cc\" header rejected\r\n";
#endif
	} else if (HeaderCmp(base, "X-Complaints-To", &errMsg) == 0) {
		errMsg = "441 Illegal \"X-Complaints-To\" header rejected\r\n";
	} else if (HeaderCmp(base, "NNTP-Posting-Date", &errMsg) == 0) {
		errMsg = "441 Illegal \"NNTP-Posting-Date\" header rejected\r\n";
	} else if (HeaderCmp(base, "NNTP-Posting-Host", &errMsg) == 0) {
		errMsg = "441 Illegal \"NNTP-Posting-Host\" header rejected\r\n";
	} else if (HeaderCmp(base, "X-Trace", &errMsg) == 0) {
		errMsg = "441 Illegal \"X-Trace\" header rejected\r\n";
	} else if (HeaderCmp(base, "Xref", &errMsg) == 0) {
	    keep = 0;	/* delete */
	} else if (HeaderCmp(base, "Control", &errMsg) == 0) {
		if (strncasecmp(base, "Control: cancel", 15)) {
			if (! (conn->co_Auth.dr_Flags & DF_CONTROLPOST)) {
				errMsg = "441 Illegal \"Control\" message rejected\r\n";
			}
		} else {
#ifdef	POST_RESTRICTCANCEL
			char *ptr = zallocStrTrim(&conn->co_MemPool, base+15, l - 15);
			
			if (ptr && *ptr && (strstr(ptr,
			       conn->co_Auth.dr_VServerDef->vs_ClusterName) == 0))
			    errMsg = "441 Illegal Cancel Message-ID\r\n";
			zfreeStr(&conn->co_MemPool, &ptr);
#endif
		}
	} else if (HeaderCmp(base, "Also-Control", &errMsg) == 0) {
		if (strncasecmp(base, "Also-Control: cancel", 20)) {
			if (! (conn->co_Auth.dr_Flags & DF_CONTROLPOST))
				errMsg = "441 Illegal \"Control\" message rejected\r\n";
		} else {
#ifdef	POST_RESTRICTCANCEL
			char *ptr = zallocStrTrim(&conn->co_MemPool, base+20, l - 20);
			
			if (ptr && *ptr && (strstr(ptr, 
			       conn->co_Auth.dr_VServerDef->vs_ClusterName) == 0))
			    errMsg = "441 Illegal Cancel Message-ID\r\n";
			zfreeStr(&conn->co_MemPool, &ptr);
#endif
		}
	} else if (HeaderCmp(base, "Supersedes", &errMsg) == 0) {
#ifdef	POST_RESTRICTCANCEL
		char *ptr = zallocStrTrim(&conn->co_MemPool, base+11, l - 11);
		
		if (ptr && *ptr && (strstr(ptr,
			       conn->co_Auth.dr_VServerDef->vs_ClusterName) == 0))
		    errMsg = "441 Illegal Supersedes Message-ID\r\n";
		zfreeStr(&conn->co_MemPool, &ptr);
#endif
	}
	if (keep == 2) {	/* Overloaded to mean "clean up ng line" */
	    MBWrite(&tmbuf, "Newsgroups: ", 12);
	    MBWrite(&tmbuf, newsGrps2, strlen(newsGrps2));
	    MBWrite(&tmbuf, "\r\n", 2);
	} else if (keep)
	    MBWrite(&tmbuf, base, l);
	base += l;
	artLen -= l;
    }

    /*
     * Check errors
     */

    if (haveNewsgroups == 0) {
	errMsg = "441 Required \"Newsgroups\" header is missing\r\n";
    } else if (conn->co_Auth.dr_ReaderDef->rd_CheckPostGroups &&
	       BreakIfBadGroup(newsGrps2, conn) != 0) {
	errMsg = "441 Nonexistent newsgroup(s)\r\n";
    }

    if (haveFrom == 0)
	errMsg = "441 Required \"From\" header is missing\r\n";
    if (haveSubject == 0)
	errMsg = "441 Required \"Subject\" header is missing\r\n";
    if (artLen <= 2)
	errMsg = "441 Article has no body -- just headers\r\n";

    /*
     * If no error, add our own headers
     */
    if (errMsg == NULL) {
	time_t t = time(NULL);

	/*
	 * Add Date:
	 */
	if (haveDate == 0) {
	    struct tm *tp = gmtime(&t);
	    char buf[64];

	    strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S GMT", tp);
	    MBPrintf(&tmbuf, "Date: %s\r\n", buf);
	}
	/*
	 * Add Lines:
	 */
	{
	    int lines = 0;
	    int i;

	    for (i = 2; i < artLen; ++i) {
		if (base[i] == '\n')
		    ++lines;
	    }
	    if (base[i-1] != '\n')	/* last line not terminated? */
		++lines;
	    MBPrintf(&tmbuf, "Lines: %d\r\n", lines);
	}
	/*
	 * Add Message-ID:
	 */
	if (haveMsgId == 0) {
	    if (conn->co_IHaveMsgId == NULL)
		GenerateMessageID(conn);
	    MBPrintf(&tmbuf, "Message-ID: %s\r\n", conn->co_IHaveMsgId);
	}
	/*
	 * Add Distribution:
	 */
	if (haveDist == 0 && newsGrps2) {
	    GenerateDistribution(&tmbuf, newsGrps2);
	}

	if (!haveOrganization && *conn->co_Auth.dr_VServerDef->vs_Org) {
		MBPrintf(&tmbuf, "Organization: %s\r\n",
			conn->co_Auth.dr_VServerDef->vs_Org);
	}
	if (*conn->co_Auth.dr_VServerDef->vs_Comments) {
		if (! strstr(conn->co_Auth.dr_VServerDef->vs_Comments, "\\n")) {
			MBPrintf(&tmbuf, "X-Comments: %s\r\n",
				conn->co_Auth.dr_VServerDef->vs_Comments);
		} else {
			/* This is sinful even for me. JG20030118 */
			char buf[512], *ptr, *head;
			int commentnum = 1;

			snprintf(buf, sizeof(buf), "%s", conn->co_Auth.dr_VServerDef->vs_Comments);
			head = buf;
			while ((ptr = strstr(head, "\\n"))) {
				*ptr = '\0';
				MBPrintf(&tmbuf, "X-Comments-%d: %s\r\n",
					commentnum++, head);
				head = ptr + 2;
			}
			MBPrintf(&tmbuf, "X-Comments-%d: %s\r\n",
				commentnum++, head);
		}
	}

#if defined  POST_NNTP_POSTING_DATE_GMT || defined POST_NNTP_POSTING_DATE_LOCALTIME
	/*
	 * Add NNTP-Posting-Date:
	 */
	{
	    char buf[64];
	    struct tm *tp;

#ifdef POST_NNTP_POSTING_DATE_GMT
	    tp = gmtime(&t);
	    strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S GMT", tp);
#else
	    tp = localtime(&t);
	    strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S %Z", tp);
#endif
	    MBPrintf(&tmbuf, "NNTP-Posting-Date: %s\r\n", buf);
	}
#endif

	/*
	 * Add NNTP-Posting-Host:
	 */
	if (! conn->co_Auth.dr_ReaderDef->rd_TurnOffNNPH) {
	    if (*conn->co_Auth.dr_VServerDef->vs_CryptPw)
	        MBPrintf(&tmbuf, "NNTP-Posting-Host: %s\r\n",
		    NNPHMangle(conn->co_Auth.dr_VServerDef->vs_ClusterName,
			    conn->co_Auth.dr_Host,
			    conn->co_Auth.dr_VServerDef->vs_CryptPw));
	    else
	        MBPrintf(&tmbuf, "NNTP-Posting-Host: %s\r\n", conn->co_Auth.dr_Host);
	}

	/*
	 * Add X-Trace:
	 */
#ifdef  POST_CRYPTXTRACE
	if (*conn->co_Auth.dr_VServerDef->vs_CryptPw) {
	    snprintf(xtrace, sizeof(xtrace), "%lu %s %d %s%s%s%s%s",
		(unsigned long)t,
		conn->co_Auth.dr_VServerDef->vs_HostName,
		(int)getpid(),              /* should probably be slot id */
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		NetAddrToSt(0, (struct sockaddr *)&conn->co_Auth.dr_Addr, 1, 1, 1)
	    );
	    MBPrintf(&tmbuf, "X-Trace: DXC=%s\r\n",
		XTRAMangle(xtrace, conn->co_Auth.dr_VServerDef->vs_CryptPw));
	} else
#endif
	    MBPrintf(&tmbuf, "X-Trace: %lu %s %d %s%s%s%s%s\r\n",
		(unsigned long)t,
		conn->co_Auth.dr_VServerDef->vs_HostName,
		(int)getpid(),		/* should probably be slot id */
		*conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
		*conn->co_Auth.dr_AuthUser ? "/" : "",
		*conn->co_Auth.dr_IdentUser ? conn->co_Auth.dr_IdentUser : "",
		*conn->co_Auth.dr_IdentUser ? "@" : "",
		NetAddrToSt(0, (struct sockaddr *)&conn->co_Auth.dr_Addr, 1, 1, 1)
	    );
	if (*conn->co_Auth.dr_VServerDef->vs_AbuseTo)
		MBPrintf(&tmbuf, "X-Complaints-To: %s\r\n",
			conn->co_Auth.dr_VServerDef->vs_AbuseTo);
    }

    /*
     * blank line and article body
     */

    if (errMsg == NULL) {
	MBWrite(&tmbuf, base, artLen);
    }
    if (*conn->co_Auth.dr_VServerDef->vs_PostTrailer) {
	/* This is sinful even for me. JG20030206 */
	char buf[512], *ptr, *head;

	snprintf(buf, sizeof(buf), "%s", conn->co_Auth.dr_VServerDef->vs_PostTrailer);
	head = buf;
	while ((ptr = strstr(head, "\\n"))) {
		*ptr = '\0';
		MBPrintf(&tmbuf, "%s\r\n", head);
		head = ptr + 2;
	}
	MBPrintf(&tmbuf, "%s\r\n", head);
    }
    MBFree(&conn->co_ArtBuf);

    /*
     * If no error, pump out to ready server or queue for transmission, 
     * else write the error out and throw away.
     *
     * When POSTing to a moderated group, the article is mailed to the
     * moderator and NOT otherwise posted, even if crossposted to non-moderated
     * groups.
     */

    if (errMsg == NULL) {
	/*
	 * Copy into co_ArtBuf for transmission/mailing
	 */
	MBCopy(&tmbuf, &conn->co_ArtBuf);
	MBFree(&tmbuf);

	logit(
	    LOG_NOTICE,
	    "%s%s%s%s%s (%s) post ok %s %d", 
	    *conn->co_Auth.dr_AuthUser ? conn->co_Auth.dr_AuthUser : "",
	    *conn->co_Auth.dr_AuthUser ? "/" : "",
	    (conn->co_Auth.dr_IdentUser[0] ? conn->co_Auth.dr_IdentUser : ""),
	    (conn->co_Auth.dr_IdentUser[0] ? "@" : ""),
	    conn->co_Auth.dr_Host,
	    NetAddrToSt(0, (struct sockaddr *)&conn->co_Auth.dr_Addr, 1, 1, 1),
	    conn->co_IHaveMsgId,
	    conn->co_ArtBuf.mh_Bytes
	);

	conn->co_ClientPostCount++;
	conn->co_Auth.dr_PostCount++;
	conn->co_Auth.dr_PostBytes += conn->co_ArtBuf.mh_Bytes;

	if (newsGrps2 && 
	    haveApproved == 0 && 
	    MailIfModerated(newsGrps2, conn) == 0
	) {
	    if (conn && conn->co_IHaveMsgId)
		zfreeStr(&conn->co_MemPool, &conn->co_IHaveMsgId);
	    MBFree(&conn->co_ArtBuf);
	    MBLogPrintf(conn, &conn->co_TMBuf, "240 Article mailed to moderator\r\n");
	    NNCommand(conn);
	} else {
	    NNServerRequest(conn, NULL, conn->co_IHaveMsgId, 0, SREQ_POST, 0, -1, 0);
	}
    } else {
	MBLogPrintf(conn, &conn->co_TMBuf, "%s", errMsg);
	MBFree(&tmbuf);
	conn->co_Auth.dr_PostFailCount++;
	NNCommand(conn);
    }
    if (newsGrps)
	zfreeStr(&conn->co_MemPool, &newsGrps);
    if (newsGrps2)
	zfreeStr(&conn->co_MemPool, &newsGrps2);
}

/*
 * NNPostCommand1() - called when article has been queued to a server 
 *		      connection, in the server's context, to post the
 *		      article.
 *
 * SEQUENCE:
 */

void
NNPostCommand1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;

    MBLogPrintf(conn, &conn->co_TMBuf, "ihave %s\r\n", sreq->sr_MsgId);
    NNPostResponse1(conn);
}

void
NNPostResponse1(Connection *conn)
{
    /*ServReq *sreq = conn->co_SReq;*/
    char *buf;
    int len;

    conn->co_Func = NNPostResponse1;
    conn->co_State = "pores1";

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	LogCmd(conn, '<', buf);
	if (strtol(buf, NULL, 10) == 335) {
	    NNPostArticle1(conn);
	} else {
	    char errMsg[256];

	    MBFree(&conn->co_ArtBuf);

	    if (len >= 1 && buf[len-1] == 0)
		--len;
	    if (len >= 1 && buf[len-1] == '\r')
		--len;
	    if (len > sizeof(errMsg) - 32)
		len = sizeof(errMsg) - 32;
	    strcpy(errMsg, "441 ");
	    bcopy(buf, errMsg + 4, len);
	    errMsg[4+len] = '\r';
	    errMsg[5+len] = '\n';
	    errMsg[6+len] = 0;
	    NNFinishSReq(conn, errMsg, 0);
	}
    } else if (len < 0) {
	NNServerTerminate(conn);
    } /* else we haven't gotten the reponse yet */
}

void
NNPostArticle1(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;

    conn->co_Func = NNPostArticle1;
    conn->co_State = "poart1";

    /* Verify that the client has not disconnected yet. If such is the
     * case, don't bother returning an error message, there's no client
     */
    if (sreq == NULL || sreq->sr_CConn == NULL) {
	logit(LOG_DEBUG, "Client vanished during post (%s)", sreq->sr_MsgId);
	MBPrintf(&conn->co_TMBuf, ".\r\n"); /* Just junk the post. */
	NNPostResponse2(conn);
	return;
    }

    while (conn->co_TMBuf.mh_Bytes < 800) {
	char *buf;
	int len;

	if ((len = MBReadLine(&sreq->sr_CConn->co_ArtBuf, &buf)) > 0) {
if (buf[len-1] != '\n' && buf[len-1]) {
syslog(LOG_EMERG, "bad carriage return in %s %d", sreq->sr_MsgId, buf[len-1]);
}
	    buf[len-1] = '\n';
	    MBWrite(&conn->co_TMBuf, buf, len);
	} else if (len <= 0) {
	    MBPrintf(&conn->co_TMBuf, ".\r\n");
	    NNPostResponse2(conn);
	    return;
	}
    }
}

void
NNPostResponse2(Connection *conn)
{
    char *buf;
    int len;

    conn->co_Func = NNPostResponse2;
    conn->co_State = "pores2";

    if ((len = MBReadLine(&conn->co_RMBuf, &buf)) > 0) {
	LogCmd(conn, '<', buf);
	if (conn->co_SReq == NULL || conn->co_SReq->sr_CConn == NULL) {
	    /* Client has gone away. we don't care any more */
	    NNFinishSReq(conn, NULL, 0);
	} else if (strtol(buf, NULL, 10) == 235) {
	    char msgbuf[1024];
	    snprintf(msgbuf, sizeof(msgbuf), "240 %s Article posted\r\n",
			conn->co_SReq->sr_CConn->co_IHaveMsgId != NULL ?
				conn->co_SReq->sr_CConn->co_IHaveMsgId : "");
	    NNFinishSReq(conn, msgbuf, 0);
	} else {
	    char errMsg[256];

	    MBFree(&conn->co_ArtBuf);

	    if (len >= 1 && buf[len-1] == 0)
		--len;
	    if (len >= 1 && buf[len-1] == '\r')
		--len;
	    if (len > sizeof(errMsg) - 32)
		len = sizeof(errMsg) - 32;
	    strcpy(errMsg, "441 ");
	    bcopy(buf, errMsg + 4, len);
	    errMsg[4+len] = '\r';
	    errMsg[5+len] = '\n';
	    errMsg[6+len] = 0;
	    NNFinishSReq(conn, errMsg, 0);
	}
	if (conn->co_SReq != NULL && conn->co_SReq->sr_CConn != NULL &&
				conn->co_SReq->sr_CConn->co_IHaveMsgId != NULL)
	    zfreeStr(&conn->co_SReq->sr_CConn->co_MemPool,
				&conn->co_SReq->sr_CConn->co_IHaveMsgId);
    } else if (len < 0) {
	NNServerTerminate(conn);
    } /* else we haven't gotten the reponse yet */
}

/*
 * BreakIfBadGroup() -	if posting to a nonexistent or S=x group, stop
 *
 */

int
BreakIfBadGroup(char *groups, Connection *conn)
{
    int r = 0;
    int rval = 0;
    struct GroupList *accessgroups = conn->co_Auth.dr_PostGroupDef->gr_Groups;

    int i = 0;

    while (groups[i]) {
	int j;
	char c;

	for (j = i; groups[j] && groups[j] != ','; ++j)
	    ;

	if (j - i > MAXGNAME) {
	    r = -1;
	    break;
	}

	c = groups[j];
	groups[j] = 0;

	if (i != j) {
		if (accessgroups != NULL && !GroupFindWild(groups + i, accessgroups)) {
		    r = -1;
		    break;
		}
		if ((rval = CheckValidRestrictedGroup(groups + i))) {
			r = rval;
		}
	}
	i = j;
	if ((groups[j] = c) == ',')
	    ++i;
    }
    return(r);
}


/*
 * MailIfModerated() -	if posting to moderated groups and the article
 *			was not approved, mail the message to the moderators 
 *			instead
 *
 *	Return 0 if article was mailed, -1 otherwise
 *
 */

int
MailIfModerated(char *groups, Connection *conn)
{
    FILE *fi;
    int r = -1;
    int saveCount = 0;
    char buf[256];
    int save[256];

    if ((fi = fopen(PatLibExpand(ModeratorsPat), "r")) != NULL) {
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    char *modGroupWild = strtok(buf, ": \t\r\n");
	    char *modMail = (modGroupWild) ? strtok(NULL, ": \t\r\n") : NULL;
	    int i = 0;

	    if (modGroupWild == NULL || modMail == NULL)
		continue;

	    while (groups[i]) {
		int j;
		char c;

		for (j = i; groups[j] && groups[j] != ','; ++j)
		    ;
		c = groups[j];
		groups[j] = 0;

		if (i != j && 
		    groupAlready(i, save, saveCount) < 0 &&
		    saveCount != arysize(save)
		) {
		    if (CheckGroupModeration(groups + i) < 0)
			save[saveCount++] = i;
		}

		if (i != j && 
		    groupAlready(i, save, saveCount) < 0 &&
		    saveCount != arysize(save) &&
		    WildCmp(modGroupWild, groups + i
		) == 0) {
		    r = 0;
		    MailToModerator(groups + i, conn, modMail);
		    save[saveCount++] = i;
		}
		i = j;
		if ((groups[j] = c) == ',')
		    ++i;
	    }
	}
	fclose(fi);
    }
    return(r);
}

int
groupAlready(int v, int *save, int saveCount)
{
    int i;
    int r = -1;

    for (i = 0; i < saveCount; ++i) {
	if (save[i] == v) {
	    r = 0;
	    break;
	}
    }
    return(r);
}

/*
 * MailToModerator() -  send mail to the moderator.  
 *
 *	Note: we have to unescape the posted article and we cannot pass
 *	certain headers.  We cannot pass Path: because the moderator may
 *	forward it, causing the posting news machine to miss the approved
 *	posting made by the moderator (because it will have locked itself
 *	out due to the Path: ).  We also do not pass To:, Cc:, or Bcc:
 *	headers for two reasons:  First, because the news client is 
 *	supposed to handle those fields and we don't parse them for
 *	non-moderated postings, and second, for security reasons.
 */

void
MailToModerator(const char *group, Connection *conn, const char *modMail)
{
    char mailDest[256];
    pid_t pid;
    int fds[3];
    char *argv[5] = { SENDMAIL_PATH, SENDMAIL_ARG0, "-t", "-i", NULL };

    {
	char *mgroup = zallocStr(&conn->co_MemPool, group);
	int i;

	for (i = 0; mgroup[i]; ++i) {
	    if (mgroup[i] == '.')			/* dots to dashes */
		mgroup[i] = '-';
	    if (mgroup[i] == '@' || mgroup[i] == ',')	/* shouldn't happen */
		mgroup[i] = '-';
	}
	snprintf(mailDest, sizeof(mailDest), modMail, mgroup);
	zfreeStr(&conn->co_MemPool, &mgroup);
    }

    if ((pid = RunProgramPipe(fds, RPF_STDOUT, argv, NULL)) > 0) {
	FILE *fo;

	if ((fo = fdopen(fds[1], "w")) != NULL) {
	    int artLen = 0;
	    int lastLineOk = 1;
	    int inHeaders = 1;
	    int b = 0;
	    char *base = MBNormalize(&conn->co_ArtBuf, &artLen);

	    fprintf(fo, "To: %s\n", mailDest);

	    while (b < artLen) {
		int i;
		int l;
		int lineOk = 1;

		for (i = b; i < artLen && base[i] != '\n'; ++i)
		    ;
		if (i < artLen && base[i] == '\n')
		    ++i;

		/*
		 * Length of line including CR+LF
		 */
		l = i - b;

		/*
		 * Remove CR+LF or LF (LF will be added back in the email)
		 */
		if (l > 0 && base[i-1] == '\n') {
		    --l;
		    if (l > 0 && base[i-2] == '\r')
			--l;
		}

		/*
		 * Get rid of leading dot if dot-escaping
		 */
		if (l > 0 && base[b] == '.') {
		    --l;
		    ++b;
		}

		/*
		 * Process headers, output line.   The lastLineOk stuff is
		 * to handle header line continuation.
		 */

		if (inHeaders) {
		    if (l == 0) {
			inHeaders = 0;
		    } else if (base[b] == ' ' || base[b] == '\t') {
			lineOk = lastLineOk;
		    } else if (l >= 3 && !strncasecmp(base + b, "To:", 3)) {
			lineOk = 0;
		    } else if (l >= 3 && !strncasecmp(base + b, "Cc:", 3)) {
			lineOk = 0;
		    } else if (l >= 4 && !strncasecmp(base + b, "Bcc:", 4)) {
			lineOk = 0;
		    } else if (l >= 5 && !strncasecmp(base + b, "Path:", 5)) {
			lineOk = 0;
		    } 
		}
		if (lineOk) {
		    fwrite(base + b, l, 1, fo);
		    fputc('\n', fo);
		}
		lastLineOk = lineOk;
		b = i;
	    }
	    fclose(fo);
	} else {
	    close(fds[1]);
	}
	waitpid(pid, NULL, 0);
    }
}

int
CheckGroupModeration(const char *group)
{
    const char *rec;
    int recLen;
    int r = -1;

    rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen);
    if (rec != NULL) {
        int slen;
        const char *s = KPDBGetField(rec, recLen, "S", &slen, NULL);
        if (s) {
            int i;

	    for (i = 0; i < slen; ++i) {
		if (s[i] == 'm')
                    r = 0;
            }
        }
        KPDBUnlock(KDBActive, rec);
    }
    return(r);
}

int
CheckValidRestrictedGroup(const char *group)
{
    const char *rec;
    int recLen;
    int r = -1;

    /* Allow posting into a (currently) nonexistent group.  We will create */
    if (DOpts.ReaderAutoAddToActive)
	r = 0;

    rec = KPDBReadRecord(KDBActive, group, KP_LOCK, &recLen);

    if (rec != NULL) {
        int slen;
        const char *s = KPDBGetField(rec, recLen, "S", &slen, NULL);
	r = 0;
        if (s) {
            int i;

	    for (i = 0; i < slen; ++i) {
		if (s[i] == 'x')
                    r = 1;
            }
        }
        KPDBUnlock(KDBActive, rec);
    }
    return(r);
}

/*
 * GenerateDistribution(): generate a distribution header.  At the moment
 *			   we only generate one distribution tag, even if
 *			   crossposting, using the earliest highest-weight
 *			   tag found in distrib.pats.
 *
 *			   If we cannot find any matches, no Distribution:
 *			   header is generated.
 */

void
GenerateDistribution(MBufHead *mbuf, char *newsGrps)
{
    FILE *fi; 
    int bestWeight = -1;
    char *bestDist = NULL;

    if ((fi = fopen(PatLibExpand(DistribDotPatsPat), "r")) != NULL) {
        char buf[256];
    
        while (fgets(buf, sizeof(buf), fi) != NULL) {
	    char *weight = strtok(buf, ":\r\n");
	    char *pattern = weight ? strtok(NULL, ":\r\n") : NULL;
	    char *value = pattern ? strtok(NULL, ":\r\n") : NULL;
	    int i = 0;

	    if (value == NULL || 
	        weight == NULL || 
		weight[0] == '#' ||
		weight[0] == 0
	    ) {
		continue;
	    }
	    while (newsGrps[i]) {
		int j;
		char c;

		for (j = i; newsGrps[j] && newsGrps[j] != ','; ++j)
		    ;
		c = newsGrps[j];
		newsGrps[j] = 0;
		if (WildCmp(pattern, newsGrps + i) == 0) {
		    if (strtol(weight, NULL, 0) > bestWeight) {
			zfreeStr(&SysMemPool, &bestDist);
			bestWeight = strtol(weight, NULL, 0);
			bestDist = zallocStr(&SysMemPool, value);
		    }
		}
		newsGrps[j] = c;
		if (newsGrps[j] != ',')
		    break;
		i = j + 1;
	    }
        }
        fclose(fi);
    }
    if (bestDist) {
	MBPrintf(mbuf, "Distribution: %s\r\n", bestDist);
	zfreeStr(&SysMemPool, &bestDist);
    }
}



#if POST_XFILTERHOOK

char *
PostXFilter(char *base, int artLen, Connection *conn);
{
    const char *badCmd = "441 Unable to exec postxfilter program\r\n";
    int fds[2];
    pid_t pid;

    if ((pid = RunProgramPipe(fds, RPF_STDIN|RPF_STDOUT,
                                      DOpts.PostXFilter, NULL)) > 0) {
      /*
       * write article to postxfilter, result answer
       */
      char buf[1025];
      int n;
      int lcr = 0;
      FILE *fo = fdopen(fds[1], "w");

      for (n = 0; n < artLen; ++n) {
          if (lcr == 0 && art[n] == '\r') {
              lcr = 1;
              continue;
          }
          if (art[n] == '\n')
              lcr = 0;
          if (lcr) {
              fputc('\r', fo);
          }
          fputc((int)(uint8)art[n], fo);
          lcr = 0;
      }
      fflush(fo);
      if (ferror(fo))
          logit(LOG_ERR, "write error sending article to postxfilter");
      fclose(fo);

      {
          int v = 0;
          for (n = 0; n < sizeof(buf); n += v) {
              v = read(fds[0], buf + n, sizeof(buf) - n);
              if (v <= 0)
                  break;
          }
          if (n > 0 && buf[n-1] == '\n')
              --n;
          if (n > 0 && buf[n-1] == '\r')
              --n;
          if (n == sizeof(buf))
              --n;
          buf[n] = 0;

          /*
           * if non-null answer, PostXFilter succeeded
           */
          if (n) {
              if (*buf == '2') {
                  badCmd = NULL;
              } else {
                  badCmd = buf;
              }
          } else {
              badCmd = "441 postxfilter failed\r\n";
          }
      }
      close(fds[0]);
      waitpid(pid, NULL, 0);
    } else {
      logit(LOG_ERR, "Unable to run %s", DOpts.PostXFilter);
    }
    return(badCmd);
}

#endif /* POST_XFILTERHOOK */


