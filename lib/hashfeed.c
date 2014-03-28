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
 * $Id: hashfeed.c,v 1.2 2006/01/17 11:20:39 rv Exp $
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





#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <netinet/in.h>


#ifdef	LIBFARSE
/* This is probably libfarse. */
#include "farse.h"
#include "hashfeed.h"
#endif

#ifdef	SUBREV
/* Then this is probably Diablo.  We can use its built-in MD5 funcs.  */
#define	MD5_CTX		struct diablo_MD5Context
#define	MD5Init		diablo_MD5Init
#define	MD5Update	diablo_MD5Update
#define	MD5Final	diablo_MD5Final

#include "defs.h"

Prototype hashint_t HM_OldQuickhash(mid_t mid);
Prototype void HM_MD5MessageID(mid_t mid, unsigned char *res);
Prototype hashint_t HM_GetHashInt(mid_t mid, int offloc);
Prototype int HM_CheckForMatch(HashFeed_MatchList *hf, mid_t mid, int hmoper);
Prototype hashint_t HM_GetHashInt_PC(unsigned char *res, int offloc);
Prototype int HM_CheckForMatch_PC(HashFeed_MatchList *hf, mid_t mid, unsigned char *res, int hmoper);
Prototype HashFeed_MatchList * HM_ConfigNode_Sub(HashFeed_MatchList *hf, HashFeed_MatchList *next, char *conf);
#endif





/*
 * Legacy quickhash function.  Only for backwards compatibility.
 * Not For New Deployments.  Bad, evil.  Don't use.  You've been warned.
 * Fortunately, int is not likely to go negative in this use.  Geez.
 */

hashint_t 
HM_OldQuickhash(mid_t mid)
{
	register int rval = 0;
	register unsigned char *p = (unsigned char *)mid;

	while (*p)
		rval += *p++;
	return((hashint_t)rval);
}





/*
 * General "MD5Data"-like function for MID
 * res must point to an unsigned char res[16]
 */

void
HM_MD5MessageID(mid_t mid, unsigned char *res)
{
	MD5_CTX ctx;

	MD5Init(&ctx);
	MD5Update(&ctx, mid, strlen((const char *)mid));
	MD5Final(res, &ctx);
}





/*
 * Pull out a 32-bit value from offset offloc (0-12)
 * for the provided MID.  A 128-bit MD5 has enough
 * bits for four completely independent offsets
 * (0, 4, 8, 12) but we leave in the possibility of
 * others for those who have clever uses and smallish
 * mod values.
 *
 * This is explicitly intended to return the following
 * answers for the Message-ID "<>":
 *
 * Overall MD5: 38559c871fba28d992ead51549367f83
 *
 * offloc 32-bit-val
 * ------ ----------
 *    000 49367f83
 *    001 1549367f
 *    002 d5154936
 *    003 ead51549
 *    004 92ead515
 *    005 d992ead5
 *    006 28d992ea
 *    007 ba28d992
 *    008 1fba28d9
 *    009 871fba28
 *    010 9c871fba
 *    011 559c871f
 *    012 38559c87
 *
 * It is important that this code generate these answers
 * on all platforms, so that hashval calcs are architecture 
 * independent.
 */

hashint_t
HM_GetHashInt(mid_t mid, int offloc)
{
	unsigned char res[16];
	hashint_t rval;

	if (offloc < 0 || offloc > 12) {
		return((hashint_t)0);
	}

	HM_MD5MessageID(mid, res);

	memcpy((void *)&rval, (void *)&res[12 - offloc], sizeof(rval));

	return(ntohl(rval));
}





/*
 * Check for a match in the referenced HashFeed_MatchList
 */

int 
HM_CheckForMatch(HashFeed_MatchList *hf, mid_t mid, int hmoper)
{
	hashint_t hval;

	while (hf) {
		switch (hf->HM_Type) {
			case	HMTYPE_OLD:	hval = HM_OldQuickhash(mid);
						break;
			case	HMTYPE_MD5:	hval = HM_GetHashInt(mid, hf->HM_Offset);
						break;
			default:		return(-1);
		}
#ifdef	HM_DEBUG
		fprintf(stderr, "hval %u, %s%d-%d/%d:%d, res %d\n", hval, (hf->HM_Type == HMTYPE_OLD) ? "@" : "", hf->HM_Start, hf->HM_End, hf->HM_ModVal, hf->HM_Offset, (hval % hf->HM_ModVal + 1) >= hf->HM_Start && (hval % hf->HM_ModVal + 1) <= hf->HM_End); 
#endif
		if ((hval % hf->HM_ModVal + 1) >= hf->HM_Start &&
		    (hval % hf->HM_ModVal + 1) <= hf->HM_End) {
			if (hmoper == HMOPER_MATCHONE) {
				return(1);
			}
		} else {
			if (hmoper == HMOPER_MATCHALL) {
				return(0);
			}
		}
		hf = hf->HM_Next;
	}
	if (hmoper == HMOPER_MATCHALL) {
		return(1);
	}
	return(0);
}





/*
 * If you are going to do a lot of work on a single Message-ID,
 * like comparing it to all your newsfeed objects, it makes sense
 * to avoid calling HM_CheckForMatch which will call HM_GetHashInt
 * which will call HM_MD5MessageID which will call MD5Update.
 *
 * Use these PreComputing functions instead.  Call HM_MD5MessageID 
 * to set up the MD5 result first, then you can repeatedly call
 * HM_CheckForMatch_PC which will call HM_GetHashInt_PC, saving the
 * repeated MD5 calls.
 */

/*
 * Pull out a 32-bit value from offset offloc (0-12)
 * for the provided MID (PreCompute version)
 */

hashint_t
HM_GetHashInt_PC(unsigned char *res, int offloc)
{
	hashint_t rval;

	if (offloc < 0 || offloc > 12) {
		return((hashint_t)0);
	}

	memcpy((void *)&rval, (void *)&res[12 - offloc], sizeof(rval));

	return(ntohl(rval));
}





/*
 * Check for a match in the referenced HashFeed_MatchList
 * (PreCompute version)
 */

int 
HM_CheckForMatch_PC(HashFeed_MatchList *hf, mid_t mid, unsigned char *res, int hmoper)
{
	hashint_t hval;

	while (hf) {
		switch (hf->HM_Type) {
			case	HMTYPE_OLD:	hval = HM_OldQuickhash(mid);
						break;
			case	HMTYPE_MD5:	hval = HM_GetHashInt_PC(res, hf->HM_Offset);
						break;
			default:		return(-1);
		}
#ifdef	HM_DEBUG
		fprintf(stderr, "hval %u, %s%d-%d/%d:%d, res %d\n", hval, (hf->HM_Type == HMTYPE_OLD) ? "@" : "", hf->HM_Start, hf->HM_End, hf->HM_ModVal, hf->HM_Offset, (hval % hf->HM_ModVal + 1) >= hf->HM_Start && (hval % hf->HM_ModVal + 1) <= hf->HM_End); 
#endif
		if ((hval % hf->HM_ModVal + 1) >= hf->HM_Start &&
		    (hval % hf->HM_ModVal + 1) <= hf->HM_End) {
			if (hmoper == HMOPER_MATCHONE) {
				return(1);
			}
		} else {
			if (hmoper == HMOPER_MATCHALL) {
				return(0);
			}
		}
		hf = hf->HM_Next;
	}
	if (hmoper == HMOPER_MATCHALL) {
		return(1);
	}
	return(0);
}















/*
 * Construct a HashFeed_MatchList node from a single element
 * of configuration data.  Due to the way Diablo has its
 * own preferred memory allocator, this function assumes
 * that it will be called iteratively to construct a
 * matchlist, with the upper level process allocating the
 * structures as we go.
 */

HashFeed_MatchList * 
HM_ConfigNode_Sub(HashFeed_MatchList *hf, HashFeed_MatchList *next, char *conf)
{
	char *ptr;

	if (! conf || ! *conf) {
		return(NULL);
	}

	bzero(hf, sizeof(HashFeed_MatchList));

	hf->HM_Next = next;
	
	hf->HM_Type = HMTYPE_MD5;
	if (*conf == '@') {
		conf++;
		hf->HM_Type = HMTYPE_OLD;
	}
	if ((ptr = strchr(conf, '-'))) {
		hf->HM_Start = strtol(conf, NULL, 10);
		hf->HM_End   = strtol(++ptr, NULL, 10);
	} else {
		hf->HM_Start = hf->HM_End = strtol(conf, NULL, 10);
	}
	if ((ptr = strchr(conf, ':'))) {
		hf->HM_Offset = strtol(++ptr, NULL, 10);
	}
	if ((ptr = strchr(conf, '/'))) {
		hf->HM_ModVal = strtol(++ptr, NULL, 10);
	}

	if (! hf->HM_Start || ! hf->HM_End || ! hf->HM_ModVal || (hf->HM_Offset < 0 || hf->HM_Offset > 12)) {
		return(NULL);
	}
#ifdef	HM_DEBUG
	fprintf(stderr, "confignode %s%d-%d/%d:%d\n", (hf->HM_Type == HMTYPE_OLD) ? "@" : "", hf->HM_Start, hf->HM_End, hf->HM_ModVal, hf->HM_Offset);
#endif

	return(hf);
}
