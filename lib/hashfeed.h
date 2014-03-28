/*-
 * Copyright (c) 2005 MoreUSENET, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: hashfeed.h,v 1.1 2005/11/02 16:21:59 jgreco Exp $
 */





/*
 * This is the long-awaited rewrite of the ugly hashfeed system I wrote so
 * long ago for Diablo as a way to distribute articles between two spool
 * servers in a deterministic fashion.
 *
 * The old hashfeed was never intended to do more than two spool servers,
 * and used a trivial summation-of-MID algorithm (see "quickhash" funct)
 * to generate a hash.  This worked fine for load balancing between two
 * servers, but never should have seen daylight in the mainline Diablo
 * distribution, where people are feeling the pain of my shortsightedness.
 * The algorithm works vaguely well for hash mod values up to perhaps 100,
 * but it is not cryptographic in nature and is subject to trivial funnel
 * behaviours.  On average, though, it is "okay".
 *
 * Further, we have seen interesting applications of the functionality that
 * go far beyond what was originally envisioned or intended.  For example,
 * distributing articles onto spool disks based on hash in order to
 * guarantee that the same disks on redundant spool servers contain the same
 * set of articles.  However, this has its problems:  due to the limits of
 * the old implementation, it would be very difficult to leverage that sort
 * of capability while simultaneously being able to deterministically
 * retrieve articles from one of N servers.
 *
 * The new hashfeed has been designed from the ground up to allow for much
 * expanded functionality.  It uses a cryptographic MD5 hash of the
 * Message-ID as a data source, and can take 32-bit values from that value
 * for various uses.  This allows up to four completely independent hash
 * values to be derived from a single Message-ID, with a full 32-bit range
 * for each.  It also allows for the specification of multiple ranges, and
 * other similarly useful stuff. 
 * 
 * JG20050502
 */





/*
 * General purpose 32-bit pulled out of MD5 rval
 */

#ifdef	NO_UINT32
typedef unsigned int    u_int32_t;
#endif

typedef u_int32_t	hashint_t;





/*
 * Use MATCHONE if you are merely interested in one matching list item, or
 * MATCHALL if all must match
 */

#define	HMOPER_MATCHONE	0x01
#define	HMOPER_MATCHALL	0x02

#define	HMTYPE_OLD	0x01
#define	HMTYPE_MD5	0x02

/*
 * Structure to be used for hashfeed match comparisons
 */

typedef struct HashFeed_MatchList {
    hashint_t			HM_Start, HM_End; /* Hashfeed range */
    hashint_t			HM_ModVal;	/* Mod value for hash (0-n) */
    char			HM_Offset;	/* Offset val for HMTYPE_MD5 */
    char			HM_Type;	/* Hash type */
    struct HashFeed_MatchList	*HM_Next;	/* Next list ptr */
} HashFeed_MatchList;
