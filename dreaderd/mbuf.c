
/*
 * DREADERD/MBUF.C
 *
 *	Non-blocking Queued/Buffered I/O routines.   Direct tie-in with
 *	select() descriptor bitmaps.  Embedded memory-concious buffer
 *	management.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void MBFlush(Connection *conn, MBufHead *mh);
Prototype void MBFree(MBufHead *mh);
Prototype void MBPoll(MBufHead *mh);
Prototype void MBInit(MBufHead *mh, int fd, MemPool **mpool, MemPool **bpool);
Prototype void MBWrite(MBufHead *mh, const void *data, int len);
Prototype int MZInit(Connection *conn, MBufHead *mh, int level);
Prototype void MZWrite(Connection *conn, const void *data, int len);
Prototype void MZPrintf(Connection *conn, const char *ctl, ...);
Prototype void MZFinish(Connection *conn);
Prototype void MBWriteDecode(MBufHead *mh, const char *data, int len);
Prototype void MBCopy(MBufHead *m1, MBufHead *m2);
Prototype void MBPrintf(MBufHead *mh, const char *ctl, ...);
Prototype void MBLogPrintf(Connection *conn, MBufHead *mh, const char *ctl, ...);
Prototype int MBRead(MBufHead *mh, void *data, int len);
Prototype int MBReadLine(MBufHead *mh, char **pptr);
Prototype char *MBNormalize(MBufHead *mh, int *plen);

void DebugData(const char *h, const void *buf, int n);

/*
 * MBFlush() - attempt to write output to descriptor, set select bits
 *	       if anything is left after we are through.
 */

void
MBFlush(Connection *conn, MBufHead *mh)
{
    MBuf *mbuf;
    ForkDesc *desc = conn->co_Desc;

    /*
     * Try to flush mbuf's, but if a timer gets set we have a delayed-write
     * situation and must break out of the loop.
     */

    while ((mbuf = mh->mh_MBuf) != NULL && (desc == NULL || desc->d_Timer == NULL)) {
	int n = 0;

	if (mh->mh_WError == 0 && mh->mh_Fd >= 0) {
	    n = mbuf->mb_Size - mbuf->mb_Index;

	    /*
	     * figure out how much we can write based on rate limiting.
	     * For the moment just use tv_sec to calculate times.  If 
	     * rate limiting is turned on and there is an attempt to 
	     * write too much, we write what is allowed and setup a 
	     * timer to re-enable the select descriptor later when we
	     * can write more.
	     */
	    if (desc != NULL && conn->co_Auth.dr_ReaderDef != NULL &&
			conn->co_ByteCountType < DRBC_XXXX &&
			conn->co_Auth.dr_ReaderDef->rd_RateLimit[conn->co_ByteCountType]) {
		int rl;
		int dt = CurTime.tv_sec - conn->co_RateTv.tv_sec;
		if (dt != 0) {
		    conn->co_RateCounter = 0;
		    conn->co_RateTv.tv_sec = CurTime.tv_sec;
		    if (conn->co_Auth.dr_ReaderDef->rd_RateLimitRangeLow) {
			conn->co_RateLimitRangeCurrentRandom = (int) random();
		    }
		}
		rl = conn->co_Auth.dr_ReaderDef->rd_RateLimit[conn->co_ByteCountType];
		if (conn->co_Auth.dr_ReaderDef->rd_RateLimitTax)
		    rl -= conn->co_Auth.dr_ReaderDef->rd_RateLimitTax *
			conn->co_Auth.dr_ConnCount;
		if (conn->co_Auth.dr_ReaderDef->rd_RateLimitRangeLow) {
		    rl *= (conn->co_Auth.dr_ReaderDef->rd_RateLimitRangeLow +
			  (conn->co_RateLimitRangeCurrentRandom %
			  (conn->co_Auth.dr_ReaderDef->rd_RateLimitRangeHigh -
			  conn->co_Auth.dr_ReaderDef->rd_RateLimitRangeLow)));
		    rl /= 100;
		}

		/*
		 * If you want to rate limit someone to less than 100cps,
		 * get a teletype.  The above logic allows an unchecked
		 * negative value for rl.  Stupid timewasting bug. JG20041228
		 */
		if (rl < 100)
		    rl = 100;

		if (n > rl - conn->co_RateCounter) {
		    n = rl - conn->co_RateCounter;
		    if (n < 0)
			n = 0;	/* n shouldn't be < zero, but be check anyway */
		    AddTimer(desc, (1000000 - CurTime.tv_usec) / 1000, TIF_WRITE);
		}
	    }

	    /*
	     * And do it
	     */

	    errno = 0;
	    n = write(mh->mh_Fd, mbuf->mb_Buf + mbuf->mb_Index, n);

	    if (n < 0) {
		if (errno == EINTR)
		    continue;	/* continue while */
		if (errno == EWOULDBLOCK || errno == EINPROGRESS)
		    break;	/* break while */
		if (errno == ENOTCONN)
		    break;	/* break while */
		mh->mh_WError = 1;
	    } else {
		conn->co_RateCounter += n;
		conn->co_ClientTotalByteCount += n;
		conn->co_ClientGroupByteCount += n;
		switch(conn->co_ByteCountType) {
			case	DRBC_ARTICLE:	conn->co_Auth.dr_ByteCountArticle += n;
						break;
			case	DRBC_HEAD:	conn->co_Auth.dr_ByteCountHead += n;
						break;
			case	DRBC_BODY:	conn->co_Auth.dr_ByteCountBody += n;
						break;
			case	DRBC_LIST:	conn->co_Auth.dr_ByteCountList += n;
						break;
			case	DRBC_XOVER:	conn->co_Auth.dr_ByteCountXover += n;
						break;
			case	DRBC_XHDR:	conn->co_Auth.dr_ByteCountXhdr += n;
						break;
			case	DRBC_XZVER:	conn->co_Auth.dr_ByteCountXover += n;
						break;
			case	DRBC_NONE:	conn->co_Auth.dr_ByteCountOther += n;
						break;
	        }
		if (n > 0)
		    conn->co_LastActiveTime = CurTime.tv_sec;
	    }
	}

	if (mh->mh_WError)
	    n = mbuf->mb_Size - mbuf->mb_Index;

	if (DebugOpt > 1) {
	    DebugData(">>", mbuf->mb_Buf + mbuf->mb_Index, n);
	}

	mbuf->mb_Index += n;
	mh->mh_Bytes -= n;
	if (mbuf->mb_Index == mbuf->mb_Size) {
	    mh->mh_MBuf = mbuf->mb_Next;
	    if (mbuf->mb_Buf)
		zfree(mh->mh_BufPool, mbuf->mb_Buf, mbuf->mb_Max);
	    zfree(mh->mh_MemPool, mbuf, sizeof(MBuf));
	}
    }

    if (mh->mh_Fd >= 0) {
	if (mh->mh_WError || mh->mh_MBuf == NULL || (desc && desc->d_Timer))
	    FD_CLR(mh->mh_Fd, &WFds); 
	else
	    FD_SET(mh->mh_Fd, &WFds); 
    }
}

void
MBFree(MBufHead *mh)
{
    MBuf *mbuf;

    while ((mbuf = mh->mh_MBuf) != NULL) {
	mh->mh_MBuf = mbuf->mb_Next;
	if (mbuf->mb_Buf)
	    zfree(mh->mh_BufPool, mbuf->mb_Buf, mbuf->mb_Max);
	zfree(mh->mh_MemPool, mbuf, sizeof(MBuf));
    }
    mh->mh_Bytes = 0;
    mh->mh_TotalBytes = 0.0;
    mh->mh_Wait = 0;
    mh->mh_REof = 0;
    mh->mh_WEof = 0;
    mh->mh_RError = 0;
    mh->mh_WError = 0;
}

/*
 * MBInit() initialize an MBuf header.  MBuf's are allocated on the
 * fly as needed.
 */

void
MBInit(MBufHead *mh, int fd, MemPool **mpool, MemPool **bpool)
{
    bzero(mh, sizeof(MBufHead));
    mh->mh_MemPool = mpool;
    mh->mh_BufPool = bpool;
    mh->mh_Fd = fd;
}

void
MBWrite(MBufHead *mh, const void *data, int len)
{
    /*
     * If nothing is queued, attempt to write the data buffer
     * directly to the descriptor
     */

    if (mh->mh_Fd >= 0)
	FD_CLR(mh->mh_Fd, &WFds); 

    /*
     * Queue the remainder and set WFds for the descriptor
     */

    while (len) {
	MBuf **pmbuf = &mh->mh_MBuf;
	MBuf *mbuf;
	int n;

	while ((mbuf = *pmbuf) != NULL && 
	       (mbuf->mb_Size == mbuf->mb_Max || mbuf->mb_Next != NULL)
	) {
	    pmbuf = &mbuf->mb_Next;
	}
	if (mbuf == NULL) {
	    *pmbuf = mbuf = zalloc(mh->mh_MemPool, sizeof(MBuf));
	    mbuf->mb_Buf = nzalloc(mh->mh_BufPool, MBUF_SIZE);
	    mbuf->mb_Max = MBUF_SIZE;
	}
	n = mbuf->mb_Max - mbuf->mb_Size;
	if (n > len)
	    n = len;
	bcopy(data, mbuf->mb_Buf + mbuf->mb_Size, n);
	mbuf->mb_Size += n;
	mh->mh_Bytes += n;
	mh->mh_TotalBytes += n;
	len -= n;
	data = (char *)data + n;
    }
    if (mh->mh_Fd >= 0)
	FD_SET(mh->mh_Fd, &WFds); 
}

int
MZInit(Connection *conn, MBufHead *mh, int level)
{
    z_streamp ZStream;
    int r;

    conn->co_TMZBufP = mh;
    if (! ((ZStream = zalloc(&SysMemPool, sizeof(z_stream))))) {
	logit(LOG_CRIT, "MZInit: unable to zalloc z_stream");
	return(-1);
    }
    bzero((void *)ZStream, sizeof(z_stream));

    conn->co_ZStream = ZStream;

    if (level == 0) {
	level = 1;
    }

    r = deflateInit(conn->co_ZStream, level);

    if (r != Z_OK) {
	logit(LOG_CRIT, "MZInit: unable to deflateInit %d", r);
	return(-1);
    }

    return(0);
}

int
MZWriteIt(Connection *conn, const void *data, int len, int flush)
{
    static char compoutbuf[65536];
    int r, got;

    // cheap/quick sanity check
    if (len >= 60000) {
	logit(LOG_CRIT, "MZWrite: buffer %d too large", len);
	return(-1);
    }

    conn->co_ZStream->avail_in = len;
    conn->co_ZStream->next_in = (char *)data;

    conn->co_ZStream->avail_out = sizeof(compoutbuf);
    conn->co_ZStream->next_out = compoutbuf;

    r = deflate(conn->co_ZStream, flush);

    if (r != Z_OK && (flush == Z_FINISH && r != Z_STREAM_END)) {
	logit(LOG_CRIT, "MZWrite: unable to deflate %d", r);
	return(-1);
    }
    if (r == Z_OK && ! &conn->co_ZStream->avail_out) {
	logit(LOG_CRIT, "MZWrite: avail_out zero, this should not happen");
	return(-1);
    }
    got = sizeof(compoutbuf) - conn->co_ZStream->avail_out;
    if (got)
        MBWrite(conn->co_TMZBufP, compoutbuf, got);
    return(r);
}

void
MZWrite(Connection *conn, const void *data, int len)
{
    MZWriteIt(conn, data, len, 0);
}

void
MZPrintf(Connection *conn, const char *ctl, ...)
{
    va_list va;
    char buf[1024];

    va_start(va, ctl);
    vsnprintf(buf, sizeof(buf), ctl, va);
    va_end(va);
    MZWrite(conn, buf, strlen(buf));
}

void
MZFinish(Connection *conn)
{
    int r;

    MZWriteIt(conn, "", 0, Z_FINISH);
    if (((r = deflateEnd(conn->co_ZStream))) != Z_OK) {
	logit(LOG_CRIT, "MZFinish: unable to deflateEnd %d", r);
    } else {
        logit(LOG_NOTICE, "MZFinish: %lu bytes compressed to %lu", conn->co_ZStream->total_in, conn->co_ZStream->total_out);
    }
    zfree(&SysMemPool, conn->co_ZStream, sizeof(z_stream));
    conn->co_ZStream = NULL;
    conn->co_TMZBufP = NULL;
}


/*
 * MBWriteDecode() - write buffer out but decode % escapes while
 *		     doing it.
 */

void
MBWriteDecode(MBufHead *mh, const char *data, int len)
{
    /*
     * If nothing is queued, attempt to write the data buffer
     * directly to the descriptor
     */

    if (mh->mh_Fd >= 0)
	FD_CLR(mh->mh_Fd, &WFds); 

    /*
     * Queue the remainder and set WFds for the descriptor
     */

    while (len) {
	MBuf **pmbuf = &mh->mh_MBuf;
	MBuf *mbuf;

	while ((mbuf = *pmbuf) != NULL && 
	       (mbuf->mb_Size == mbuf->mb_Max || mbuf->mb_Next != NULL)
	) {
	    pmbuf = &mbuf->mb_Next;
	}
	if (mbuf == NULL) {
	    *pmbuf = mbuf = zalloc(mh->mh_MemPool, sizeof(MBuf));
	    mbuf->mb_Buf = nzalloc(mh->mh_BufPool, MBUF_SIZE);
	    mbuf->mb_Max = MBUF_SIZE;
	}
	while (len && mbuf->mb_Size < mbuf->mb_Max) {
	    if (*data == '%' && len >= 3) {
		char s[3];
		s[0] = data[1];
		s[1] = data[2];
		s[2] = 0;
		mbuf->mb_Buf[mbuf->mb_Size] = strtol(s, NULL, 16);
		data += 3;
		len -= 3;
	    } else {
		mbuf->mb_Buf[mbuf->mb_Size] = *data;
		++data;
		--len;
	    }
	    ++mbuf->mb_Size;
	    ++mh->mh_Bytes;
	    ++mh->mh_TotalBytes;
	}
    }
    if (mh->mh_Fd >= 0)
	FD_SET(mh->mh_Fd, &WFds); 
}

void 
MBCopy(MBufHead *m1, MBufHead *m2)
{
    MBuf *mbuf;
    MBuf **pmbuf;

    if (m1->mh_MemPool != m2->mh_MemPool ||
	m1->mh_BufPool != m2->mh_BufPool
    ) {
	logit(LOG_CRIT, "MBCopy: illegal mbuf copy %08lx/%08lx %08lx/%08lx",
	    (long)m1->mh_MemPool,
	    (long)m2->mh_MemPool,
	    (long)m1->mh_BufPool,
	    (long)m2->mh_BufPool
	);
	exit(1);
    }

    for (pmbuf = &m2->mh_MBuf; (mbuf = *pmbuf) != NULL; pmbuf = &mbuf->mb_Next)
	;
    *pmbuf = m1->mh_MBuf;
    m1->mh_MBuf = NULL;
    m2->mh_Bytes += m1->mh_Bytes;
    m2->mh_TotalBytes += m1->mh_TotalBytes;
    m1->mh_Bytes = 0;
    m1->mh_TotalBytes = 0.0;
    if (m2->mh_Fd >= 0)
	FD_SET(m2->mh_Fd, &WFds); 
}

void
MBPrintf(MBufHead *mh, const char *ctl, ...)
{
    va_list va;
    char buf[1024];

    va_start(va, ctl);
    vsnprintf(buf, sizeof(buf), ctl, va);
    va_end(va);
    MBWrite(mh, buf, strlen(buf));
}

void
MBLogPrintf(Connection *conn, MBufHead *mh, const char *ctl, ...)
{
    va_list va;
    char buf[1024];
    char *ptr;

    va_start(va, ctl);
    vsnprintf(buf, sizeof(buf), ctl, va);
    va_end(va);
    MBWrite(mh, buf, strlen(buf));

    if ((ptr = strrchr(buf, '\n'))) {
	*ptr = '\0';
    }
    if ((ptr = strrchr(buf, '\r'))) {
	*ptr = '\0';
    }
    LogCmd(conn, '>', buf);
}

/*
 * MBRead() - read up to N bytes.  Return -1 on EOF, 0 if no
 *	      data is available at all.  If less then requested
 *	      amount of data is returned, RFds for the descriptor
 *	      will be set, else it will be cleared.
 */

int
MBRead(MBufHead *mh, void *data, int len)
{
    int r = 0;

    if (mh->mh_Fd >= 0)
	FD_CLR(mh->mh_Fd, &RFds);

    while (len) {
	MBuf *mbuf;

	if ((mbuf = mh->mh_MBuf) != NULL) {
	    int n = mbuf->mb_Size - mbuf->mb_Index;
	    if (n > len)
		n = len;
	    bcopy(mbuf->mb_Buf + mbuf->mb_Index, data, n);
	    data = (char *)data + n;
	    len -= n;
	    mh->mh_Bytes -= n;
	    mbuf->mb_Index += n;
	    if (mbuf->mb_NLScan < mbuf->mb_Index)
		mbuf->mb_NLScan = mbuf->mb_Index;
	    r += n;
	    if (mbuf->mb_Index == mbuf->mb_Size) {
		mh->mh_MBuf = mbuf->mb_Next;
		if (mbuf->mb_Buf)
		    zfree(mh->mh_BufPool, mbuf->mb_Buf, mbuf->mb_Max);
		zfree(mh->mh_MemPool, mbuf, sizeof(MBuf));
	    }
	} else {
	    int n = 0;

	    errno = 0;
	    if (mh->mh_Fd < 0)
		mh->mh_REof = 1;
	    if (mh->mh_REof == 0)
		n = read(mh->mh_Fd, data, len);
	    if (n < 0) {
		if (errno == EINTR)
		    continue;
		if (errno == EWOULDBLOCK || 
		    errno == EINPROGRESS || 
		    errno == EAGAIN
		) {
		    FD_SET(mh->mh_Fd, &RFds);
		    break;	/* break while */
		}
		if (errno == ENOTCONN) {
		    FD_SET(mh->mh_Fd, &RFds);
		    break;	/* break while */
		}
	    }
	    if (n == 0) {
		mh->mh_REof = 1;
		break;
	    }
	    if (DebugOpt > 1) {
		DebugData("<<", data, n);
	    }
	    data = (char *)data + n;
	    len -= n;
	    r += n;
	}
    }
    if (r == 0 && mh->mh_REof)
	r = -1;
    return(r);
}

/*
 * MBReadLine() - read one line and return the length of the
 *		  line.  Return -1 on EOF, 0 if no line is yet
 *		  available.  If a line cannot be returned,
 *		  RFds for the descriptor will be set, else
 *		  it will be cleared.
 *
 *		  The length of the line returned includes the
 *		  newline, but the newline is replaced with a \0.
 *		  (the caller can set it back to \n).  However,
 *		  if we hit EOF and the last character is not a 
 *		  newline, we add a \0 terminator but do NOT
 *		  include it in the length.  If the caller is adding
 *		  newlines back in, it must check for this condition.
 */

int
MBReadLine(MBufHead *mh, char **pptr)
{
    int r = 0;
    int haveit = 0;
    int nonl = 0;
    MBuf *mbuf = NULL;

    if (mh->mh_Fd >= 0)
	FD_CLR(mh->mh_Fd, &RFds);

    while (haveit == 0) {
	MBuf **pmbuf = &mh->mh_MBuf;

	r = 0;
	nonl = 0;

	while (haveit == 0 && (mbuf = *pmbuf) != NULL) {
	    int i = mbuf->mb_NLScan;

	    while (i < mbuf->mb_Size) {
		++i;
		/*
		 * ??? i != mbuf->mb_Index ?
		 */
		if (i != mbuf->mb_Index && mbuf->mb_Buf[i - 1] == '\n') {
		    haveit = 1;
		    break;
		}
	    }
	    mbuf->mb_NLScan = i;
	    r += mbuf->mb_NLScan - mbuf->mb_Index;

	    /* 
	     * delete mbuf's left over from the last valid MBReadLine()
	     * if they are marked empty.
	     */
	    if (mbuf->mb_Index == mbuf->mb_Size) {
		*pmbuf = mbuf->mb_Next;
		if (mbuf->mb_Buf)
		    zfree(mh->mh_BufPool, mbuf->mb_Buf, mbuf->mb_Max);
		zfree(mh->mh_MemPool, mbuf, sizeof(MBuf));
	    } else {
		pmbuf = &mbuf->mb_Next;
	    }
	}

	/*
	 * NOTE!  if haveit == 1, pmbuf will not be valid.  If haveit == 0
	 *	  we have exhausted all mbufs and pmbuf is valid to append
	 *	  a new one.
	 */

	if (haveit == 0) {
	    /*
	     * did not find newline in MBuf list, read more.  If there
	     * isn't enough room in this mbuf, allocate a new mbuf.  We
	     * have to do this even if we are at REof or we will not be
	     * able to append the \0 if the last line is unterminated.
	     */
	    int n = 0;

	    if (mbuf == NULL || mbuf->mb_Size == mbuf->mb_Max) {
		*pmbuf = mbuf = zalloc(mh->mh_MemPool, sizeof(MBuf));
		mbuf->mb_Buf = nzalloc(mh->mh_BufPool, MBUF_SIZE);
		mbuf->mb_Max = MBUF_SIZE;
	    }

	    /*
	     * attempt to read.
	     */

	    if (mh->mh_Fd < 0)
		mh->mh_REof = 1;
	    if (mh->mh_REof) {
	        if (mh->mh_Bytes) {
		    nonl = 1;
		    haveit = 1;
		}
		break;
	    }
	    {
		errno = 0;
		n = read(
		    mh->mh_Fd, 
		    mbuf->mb_Buf + mbuf->mb_Size, 
		    mbuf->mb_Max - mbuf->mb_Size
		);
		if (n < 0) {
		    if (errno == EINTR)
			continue;
		    if (errno == EWOULDBLOCK || 
			errno == EINPROGRESS ||
			errno == EAGAIN
		    ) {
			FD_SET(mh->mh_Fd, &RFds);
			break;	/* break while */
		    }
		    if (errno == ENOTCONN) {
			FD_SET(mh->mh_Fd, &RFds);
			break;	/* break while */
		    }
		    n = 0;
		}
		if (n == 0) {
		    mh->mh_REof = 1;
		    /*
		     * leave mb_NLScan == mb_Size.  if mh->mh_Bytes != 0,
		     * the last line at EOF was not newline-terminated.
		     * we set haveit (and later on do not try to replace
		     * the last valid character in the line with \0 because
		     * it will not be a newline).
		     */
		    if (mh->mh_Bytes) {
			haveit = 1;
			nonl = 1;
		    }
		    break;
		}
		if (DebugOpt > 1) {
		    DebugData("<<", mbuf->mb_Buf + mbuf->mb_Size, n);
		}
		mh->mh_Bytes += n;
		mh->mh_TotalBytes += n;
		mbuf->mb_Size += n;

		n = mbuf->mb_NLScan;
		while (n < mbuf->mb_Size) {
		    ++n;
		    if (mbuf->mb_Buf[n - 1] == '\n') {
			haveit = 1;
			break;
		    }
		}
		r += n - mbuf->mb_Index;
		mbuf->mb_NLScan = n;
	    }
	}
    }

    /*
     * If we have a full line, we need to (re)construct it and replace the
     * newline with a \0.  Reconstruction is required if the line runs across
     * multiple MBuf's.
     *
     * The reconstructed buffer is marked as already having been read, but
     * remains valid until the next MBRead() or MBReadLine() call.
     */

    if (DebugOpt > 2)
	logit(LOG_CRIT, "haveit %d %08lx %08lx (%d,%d)", haveit, (long)mbuf,(long)mh->mh_MBuf, r, nonl);

    if (haveit) {
	/*
	 * buffer crosses boundry, replace with new combined buffer
	 */
	if (mbuf != mh->mh_MBuf) {
	    MBuf *nmbuf = zalloc(mh->mh_MemPool, sizeof(MBuf));
	    MBuf *scan;
	    int n = 0;

	    nmbuf->mb_Buf = nzalloc(mh->mh_BufPool, r + nonl);
	    nmbuf->mb_Max = r + nonl;	/* 'fake' \0 terminator if no newline */
	    nmbuf->mb_Size = r;		/* actual size	*/
	    nmbuf->mb_NLScan = r;	/* mark as scanned */

	    while ((scan = mh->mh_MBuf) != NULL) {
		mh->mh_MBuf = scan->mb_Next;
		scan->mb_Next = (void*)-1;	/* catch fault */

		bcopy(
		    scan->mb_Buf + scan->mb_Index, 
		    nmbuf->mb_Buf + n,
		    scan->mb_NLScan - scan->mb_Index
		);
		n += scan->mb_NLScan - scan->mb_Index;
		scan->mb_Index = scan->mb_NLScan;
		if (scan == mbuf) {
		    scan->mb_Next = mh->mh_MBuf;
		    mh->mh_MBuf = scan;
		    break;
		}
		if (scan->mb_Buf)
		    zfree(mh->mh_BufPool, scan->mb_Buf, scan->mb_Max);
		zfree(mh->mh_MemPool, scan, sizeof(MBuf));
	    }
	    if (n != r) {
		if (DebugOpt)
		    logit(LOG_CRIT, "MBReadLine corrupt MBuf list: %d/%d nonl=%d", n, r, nonl);
		exit(1);
	    }
	    nmbuf->mb_Next = mh->mh_MBuf;
	    mh->mh_MBuf = nmbuf;
	    mbuf = nmbuf;
	}

	/*
	 * XXX check bounds for nonl case when we do not cross a buffer
	 * boundry.
	 */
	if (nonl)
	    mbuf->mb_Buf[mbuf->mb_NLScan] = 0;	/* do not replace last char */
	else
	    mbuf->mb_Buf[mbuf->mb_NLScan-1] = 0;/* replace nl	*/
	*pptr = mbuf->mb_Buf + mbuf->mb_Index;
	mh->mh_Bytes -= r;
	mbuf->mb_Index = mbuf->mb_NLScan;
	if (DebugOpt > 2)
	    logit(LOG_CRIT, "haveit ok r = %d bytes %d", r, mh->mh_Bytes);
    } else {
	r = 0;
	*pptr = "";
    }

    if (r == 0 && mh->mh_REof)
	r = -1;

    return(r);
}

/*
 * MBNormalize() - Put all data into a single mbuf and zero-terminate.  The
 *		   zero termination is not included in the length of the mbuf.
 *		   The size of the mbuf is returned.
 *
 *		   "" is returned if the mbuf is empty
 */

char *
MBNormalize(MBufHead *mh, int *plen)
{
    MBuf *mbuf;
    MBuf *scan;
    int bytes = 0;

    for (scan = mh->mh_MBuf; scan; scan = scan->mb_Next) {
	bytes += scan->mb_Size - scan->mb_Index;
    }
    *plen = bytes;

    /*
     * degenerate case: one mbuf, and zero terminator fits
     */
    if ((scan = mh->mh_MBuf) == NULL ||
	(scan->mb_Next == NULL && scan->mb_Size < scan->mb_Max)
    ) {
	if (scan) {
	    scan->mb_Buf[scan->mb_Size] = 0;
	    return(scan->mb_Buf + scan->mb_Index);
	} else {
	    return("");
	}
    }

    /*
     *
     */

    mbuf = zalloc(mh->mh_MemPool, sizeof(MBuf));
    mbuf->mb_Buf = nzalloc(mh->mh_BufPool, bytes + 1);
    mbuf->mb_Max = bytes + 1;
    mbuf->mb_Size = bytes;
    mbuf->mb_Buf[bytes] = 0;

    bytes = 0;

    while ((scan = mh->mh_MBuf) != NULL) {
	mh->mh_MBuf = scan->mb_Next;
	bcopy(
	    scan->mb_Buf + scan->mb_Index, 
	    mbuf->mb_Buf + bytes,
	    scan->mb_Size - scan->mb_Index
	);
	if (scan->mb_Buf)
	    zfree(mh->mh_BufPool, scan->mb_Buf, scan->mb_Max);
	zfree(mh->mh_MemPool, scan, sizeof(MBuf));
	bytes += scan->mb_Size - scan->mb_Index;
    }
    mh->mh_MBuf = mbuf;
    return(mbuf->mb_Buf);
}

void
DebugData(const char *h, const void *buf, int n)
{
    int i;
    int nl = 0;

    printf("%s ", h);
    for (i = 0; i < n; ++i) {
	int c = ((unsigned char *)buf)[i];
	if (nl) {
	   printf("\n%s ", h);
	   nl = 0;
	}
	if (isprint(c))
	    printf("%c", c);
	else
	    printf("<%02x>", c);
	if (c == '\n')
	   nl = 1;
    }
    printf("\n");
    fflush(stdout);
}


