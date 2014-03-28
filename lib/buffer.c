
/*
 * LIB/BUFFER.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 * Buffered I/O, may be used for reading or writing, but not both.
 *
 * Modified 12/4/1997 to include support for compressed data streams.
 * Modifications (c) 1997, Christopher M Sedore, All Rights Reserved.
 * Modifications may be distributed freely as long as this copyright 
 * notice is included without modification.
 *
 */

#include "defs.h"

Prototype Buffer *bopen(int fd, int bsize);
Prototype void bsetfd(Buffer *b, int fd);
Prototype void bclear(Buffer *b);
Prototype char *bstart(Buffer *b);
Prototype void bclose(Buffer *b, int closeFd);
Prototype void efree(char **pcopy, int *pcopymax);
Prototype char *bgets(Buffer *b, int *pbytes);
Prototype char *egets(Buffer *b, int *pbytes);
Prototype void bwrite(Buffer *b, const void *data, int bytes);
Prototype void bflush(Buffer *b);
Prototype int  berror(Buffer *b);
Prototype off_t btell(Buffer *b);
Prototype int bsize(Buffer *b);
Prototype int bunget(Buffer *b, int n);
Prototype int bextfree(Buffer *b);
#ifdef USE_ZLIB
Prototype void bsetcompress(Buffer *b, gzFile *cfile);
Prototype int bzwrote(Buffer *b);
#endif

#if USE_ANON_MMAP || USE_ZERO_MMAP
#define NOMEXTSIZE	(1024 * 1024)	/* no cost	  */
#else
#define NOMEXTSIZE	(128 * 1024)	/* potential cost */
#endif

/*
 * bopen() - open buffered I/O for a descriptor.  If the buffer is to be used
 *	     for writing, the descriptor may be -1.
 */

Buffer *
bopen(int fd, int bsize)
{
    Buffer *b;
    int pgSize;

    b = pagealloc(&pgSize, bsize);
    bzero(b, sizeof(Buffer));
    b->bu_BufMax = pgSize - offsetof(Buffer, bu_Buf[0]) - sizeof(int);
    b->bu_DataMax = b->bu_BufMax;
    b->bu_Data = b->bu_Buf;
    b->bu_Fd = fd;
    b->bu_gzFile = NULL;
    b->bu_CBuf = NULL;
    b->bu_BufSize = bsize;
    return(b);
}

void
bsetfd(Buffer *b, int fd)
{
    b->bu_Fd = fd;
}

#ifdef USE_ZLIB

void
bsetcompress(Buffer *b, gzFile *cfile)
{
    b->bu_gzFile = cfile;
}

int
bzwrote(Buffer *b)
{
    return(b->bu_gzWrote);
}

#endif

char *
bstart(Buffer *b)
{
    return b->bu_Data + b->bu_Beg;
}

void
bclear(Buffer *b)
{
    b->bu_Beg = b->bu_End = b->bu_NLScan = 0;
    if (b->bu_Data != b->bu_Buf)
	(void)bextfree(b);
}

void
bclose(Buffer *b, int closeFd)
{
    if (b != NULL) {
	if (closeFd && b->bu_Fd >= 0) {
#ifdef USE_ZLIB
	    if (b->bu_gzFile != NULL)
		gzclose(b->bu_gzFile);
	    else
#endif
		close(b->bu_Fd);
	    b->bu_Fd = -1;
	    b->bu_gzFile = NULL;
	}
	b->bu_Beg = 0;
	b->bu_NLScan = 0;
	b->bu_End = 0;
	(void)bextfree(b);
	pagefree(b, b->bu_BufSize);
    }
}

/*
 * bgets() - get a line from the input.  Return NULL if no data
 *	     ready to read (non-blocking only), -1 on EOF, or a
 *	     pointer to the data.  If ptr[bytes-1] != '\n', a full
 *	     line could not be returned due to the limited buffer size.
 *
 *	     if a real pointer is returned, *pbytes will be non-zero. 
 */

char *
bgets(Buffer *b, int *pbytes)
{
    int i;

    if (b->bu_Beg == b->bu_End) {
	b->bu_Beg = 0;
	b->bu_NLScan = 0;
	b->bu_End = 0;
    }

    for (;;) {
	/*
	 * look for a newline, return the line as appropriate.  NLScan
	 * is an optimization so we do not rescan portions of the input
	 * buffer that we have already checked.
	 */

	for (i = b->bu_NLScan; i < b->bu_End; ++i) {
	    if (b->bu_Data[i] == '\n') {
		char *p = b->bu_Data + b->bu_Beg;

		/*
		 * terminate buffer, removing newline, or return
		 * exact size of buffer with nothing removed or terminated.
		 */

		if (pbytes) {
		    *pbytes = i + 1 - b->bu_Beg;
		} else {
		    b->bu_Data[i] = 0;
		}
		/*
		 * note: cannot reset to 0/0 or bunget will not work.
		 */
		b->bu_Beg = i + 1;
		b->bu_NLScan = b->bu_Beg;
		return(p);
	    }
	}
	b->bu_NLScan = i;

	/*
	 * If there is no room to append new data, attempt to
	 * make some.
	 */
	if (b->bu_End == b->bu_DataMax && b->bu_Beg != 0) {
	    memmove(b->bu_Data, b->bu_Data + b->bu_Beg, b->bu_End - b->bu_Beg);
	    b->bu_End -= b->bu_Beg;
	    b->bu_NLScan -= b->bu_Beg;
	    b->bu_Beg = 0;
	}

	/*
	 * If the buffer is full, we have an overflow problem.
	 */
	if (b->bu_End == b->bu_DataMax) {
	    char *p = b->bu_Data + b->bu_Beg;

	    if (pbytes == NULL) {
		logit(LOG_ERR, "Line buffer overflow fd %d", b->bu_Fd);
		p = (void *)-1;
	    } else {
		*pbytes = b->bu_End - b->bu_Beg;
		/*
		 * note: cannot reset to 0 or bunget will not work
		 */
		b->bu_Beg = b->bu_NLScan = b->bu_End;
	    }
	    return(p);
	}
	/*
	 * otherwise read some data and repeat
	 */
	{
	    int n = 0;

	    if (b->bu_CBuf) {
#ifdef	USE_ZLIB
		/*
		 * The block contains the mods to deal with compressed
		 * streams. cmsedore@maxwell.syr.edu 12/4/97
		 */
		int nc;
		int rc;
		int code;
		int decompcount;

		if (b->bu_CBuf->dataError)
		    return ((void *) -1);

		while (!n) {

		    /*
		     * if we have bytes left over, process them before
		     * reading
		     */

		     if (b->bu_CBuf -> z_str.avail_out != -1)  {
			rc = COMPRESS_BUFFER_LENGTH - b->bu_CBuf->z_str.avail_in;
			/* check for decompressor buffer overflow */
			if (rc == 0) {
			    logit(LOG_ERR, "decompressor buffer overflow fd %d",
								b->bu_Fd);
			    return((void *)-1);
			}
			nc = read(b->bu_Fd, &b->bu_CBuf->cb_Buf[b->bu_CBuf->z_str.avail_in], rc);
			if (nc < 1) { 
			    logit(LOG_ERR,"eof (%i) on read",nc);
			    n = nc;
			    goto err_or_eof;
			}   
			b->bu_CBuf->orig = b->bu_CBuf->orig + (double)nc;
			b->bu_CBuf->z_str.next_in = b->bu_CBuf->cb_Buf; 
			rc = nc + b->bu_CBuf->z_str.avail_in;
			b->bu_CBuf->z_str.avail_in = rc;
		    } else {
			nc = 0;
		    }
		    decompcount = b->bu_DataMax-b->bu_End;
		    b->bu_CBuf->z_str.avail_out = decompcount;
		    b->bu_CBuf->z_str.next_out = b->bu_Data + b->bu_End;

		    code = inflate(&b->bu_CBuf->z_str, Z_PARTIAL_FLUSH);

		    /*
		     * Z_BUF_ERROR indicates that no progress was possible due
		     * to buffering constraints.  We try again, this time
		     * we'll do a read() for sure.  We may not have before
		     * because z_str.avail_out==0
		     */

		    if (code == Z_BUF_ERROR) {
#ifdef COMPDEBUG 
			logit(LOG_ERR, "code=%i,read=%u,decomp=%u,avail_in=%u,avail_out=%u",
				code, nc, decompcount,
				b->bu_CBuf->z_str.avail_in,
				b->bu_CBuf->z_str.avail_out
			);
#endif               
			continue;
		    }

		    /* 
		     * this indicates an inconsistency in the z_str structure
		     * members.  We don't mess with them.  I *think* this 
		     * happens when we hit z_str.avail_in=0 and
		     * z_str.avail_out=0 simultaneously.  
		     *
		     * at this point, we continue.
		     */

		    if (code == Z_STREAM_ERROR) {
			logit(LOG_ERR, "code=%i,read=%u,decomp=%u,avail_in=%u,avail_out=%u",
				code, nc, decompcount,
				b->bu_CBuf->z_str.avail_in,
				b->bu_CBuf->z_str.avail_out
			);
			continue;
		    }                 

		    /*
		     * any other error probably means we're done.  We set the
		     * dataError member so that when diablo reads again, we
		     * indicate that this stream is exhausted.
		     *
		     * future improvements might try recovery.
		     */

		    if (code != Z_OK) {
			logit(LOG_ERR, "inflate error (%i) on fd %d: %s",
					code, b->bu_Fd, b->bu_CBuf->z_str.msg);
			b->bu_CBuf->dataError = 1;
			return((void *)-1);
		    }

		    decompcount -= b->bu_CBuf->z_str.avail_out; 

		    if (b->bu_CBuf->z_str.avail_out==0) {
			b->bu_CBuf->z_str.avail_out=-1;
		    } else  {
			if (b->bu_CBuf->z_str.avail_in) {
			    if ((char *)b->bu_CBuf->z_str.next_in !=
						(char *)b->bu_CBuf->cb_Buf) {
				memmove(b->bu_CBuf->cb_Buf,
					b->bu_CBuf->z_str.next_in,
					b->bu_CBuf->z_str.avail_in
				);
				b->bu_CBuf->z_str.next_in = b->bu_CBuf->cb_Buf;
			    }
			}
		    }

		    n = decompcount;
		    b->bu_CBuf->decomp = b->bu_CBuf->decomp + (double)decompcount;

		} /* while (!n) */
#else
		logit(LOG_ERR, "Compression buffer is not initialised");
#endif	/* USE_ZLIB */
	    } else {               
		errno = 0;
		n = read(b->bu_Fd, b->bu_Data + b->bu_End, b->bu_DataMax - b->bu_End);
	    }

#ifdef	USE_ZLIB
err_or_eof:
#endif

	    /*
	     * EOF
	     */
	    if (n == 0) {
		return((void *)-1);
	    }
	    if (n < 0) {
		if (errno == EAGAIN || errno == EINTR || 
		    errno == EWOULDBLOCK ||
		    errno == EINPROGRESS
		) {
		    return(NULL);
		}
		return((void *)-1);
	    }
	    b->bu_End += n;
	}
    }
    /* not reached */
    return((void *)-1);
}

/*
 * egets() - same as bgets, but will allocate an extended buffer to
 *	     fit the line if necessary.
 */

char *
egets(Buffer *b, int *pbytes)
{
    char *ptr = NULL;

    while ((ptr = bgets(b, pbytes)) != NULL && ptr != (char *)-1) {
	/*
	 * looking for newline
	 */
	if (ptr[*pbytes-1] == '\n') {
	    break;
	}
	/*
	 * if an overflow occurs, b->bu_Beg is guarenteed to be 0
	 */
	{
	    int cpyLen;
	    char *cpy = pagealloc(&cpyLen, *pbytes * 3 / 2);

	    bcopy(b->bu_Data, cpy, b->bu_End);
	    if (b->bu_Data != b->bu_Buf) 
		pagefree(b->bu_Data, b->bu_DataMax);
	    b->bu_Data = cpy;
	    b->bu_DataMax = cpyLen;
	}
    }
    return(ptr);
}

/*
 * bunget() partially undoes the most recent read.  You cannot bunget more
 * then the number of bytes in the last read.
 */

int
bunget(Buffer *b, int n)
{
    int r = -1;

    if (n <= b->bu_Beg) {
	b->bu_Beg -= n;
	r = 0;
    }
    return(r);
}

void
bwrite(Buffer *b, const void *data, int bytes)
{
    while (bytes > 0) {
	int n = b->bu_DataMax - b->bu_End;

	if (n > bytes)
	    n = bytes;
	memcpy(b->bu_Data + b->bu_End, data, n);
	b->bu_End += n;
	data = (const char *)data + n;
	bytes -= n;

	if (b->bu_End == b->bu_DataMax)
	    bflush(b);
    }
}

/*
 * bfreebuf() - free extended buffer if possible.  Return
 *		0 on success, -1 on failure.
 */

int
bextfree(Buffer *b)
{
    int r = 0;

    if (b->bu_Data != b->bu_Buf) {
	r = -1;
	if (b->bu_End - b->bu_Beg <= b->bu_BufMax) {
	    r = 0;
	    if (b->bu_Beg != b->bu_End)
		bcopy(b->bu_Data, b->bu_Buf, b->bu_End - b->bu_Beg);
	    b->bu_NLScan -= b->bu_Beg;
	    b->bu_End -= b->bu_Beg;
	    b->bu_Beg = 0;
	    pagefree(b->bu_Data, b->bu_DataMax);
	    b->bu_Data = b->bu_Buf;	  /* not really necessary */
	    b->bu_DataMax = b->bu_BufMax; /* not really necessary */
	}
    }
    return(r);
}

/*
 * bflush() - If we have a valid descriptor, we write out pending data
 *	      and free up any externally allocated space.  If we do not
 *	      have a descriptor we allocate or reallocate more space.
 *
 *	      note: the write/write-extension buffering is only designed to
 *	      deal with buffer space for article headers.  It is not designed
 *	      to cache entire articles even though it can.
 */

void
bflush(Buffer *b)
{
    int n;

    if (b->bu_Fd < 0) {
	if (b->bu_End == b->bu_DataMax) {
	    int cpyLen;
	    char *cpy;

	    cpy = pagealloc(
		&cpyLen,
		(b->bu_DataMax < NOMEXTSIZE) ? NOMEXTSIZE : b->bu_DataMax * 2
	    );
	    bcopy(b->bu_Data, cpy, b->bu_End);
	    if (b->bu_Data != b->bu_Buf) 
		pagefree(b->bu_Data, b->bu_DataMax);
	    b->bu_Data = cpy;
	    b->bu_DataMax = cpyLen;
	}
    } else {
	if (b->bu_Error == 0) {
	    if (b->bu_gzFile != NULL) {
#ifdef USE_ZLIB
	    	while (b->bu_Beg != b->bu_End) {
		    n = gzwrite(
			b->bu_gzFile,
			b->bu_Data + b->bu_Beg,
			b->bu_End - b->bu_Beg
		    );
		    if (n == 0) {
			b->bu_Error = 1;
			break;
		    }
		    b->bu_Beg += n;
		    b->bu_gzWrote += n;
		}
#endif
	    } else while (b->bu_Beg != b->bu_End) {
		n = write(
		    b->bu_Fd,
		    b->bu_Data + b->bu_Beg,
		    b->bu_End - b->bu_Beg
		);
		if (n < 0) {
		    b->bu_Error = errno;
		    break;
		}
		b->bu_Beg += n;
	    }
	}
	b->bu_Beg = b->bu_End = b->bu_NLScan = 0;
	if (b->bu_Data != b->bu_Buf)
	    (void)bextfree(b);
    }
}

int
bsize(Buffer *b)
{
    return(b->bu_End - b->bu_Beg);
}

off_t
btell(Buffer *b)
{
    return(lseek(b->bu_Fd, 0L, 1) + (b->bu_End - b->bu_Beg));
}

int
berror(Buffer *b)
{
    return(b->bu_Error);
}

