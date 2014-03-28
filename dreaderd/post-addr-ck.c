/*-
 * Copyright (c) 1996-2000 Joe Greco and sol.net Network Services
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
 *	$Id: post-addr-ck.c,v 1.6 2006/03/21 19:01:15 jgreco Exp $
 */

/*
 * Note: this is still broken in that it won't take some very-odd-but-legit
 * stuff, such as "user@cc".  "cc" is a TLD which has an A record.  Foo.
 *
 * It would probably be most correct to actually perform a DNS lookup except
 * in the event of *.invalid, but that would require putzing with threads to
 * talk to the DNS resolver processes.  This is a nifty little project for
 * someone to take up.  JG20000810
 */

#include	<stdio.h>
#include	<string.h>
#include	<ctype.h>

#include	"defs.h"

Prototype int ckaddress(char *addr);

#define		E_NOHOST	-1
#define		E_ALLUPPER	-2
#define		E_UNAME2LONG	-3
#define		E_UNAMEALPHA	-4
#define		E_UNAME2SHORT	-5
#define		E_UNAMEILLEGAL	-6
#define		E_HNAME2LONG	-7
#define		E_HNAMENODOTS	-8
#define		E_HNAMETLD	-9
#define		E_UNAMESPECSEP	-10
#define		E_HNAMEILLEGAL	-11
#define		E_HNAMESPECSEP	-12
#define		E_HNAME2LDILLEG	-13
#define		E_HNAMEFORBID	-14
#define		E_HNAMEDIGIT	-15

int badstring(char *str)
{
	if (	strcasecmp(str, "pussy") &&
		strcasecmp(str, "sex") &&
		strcasecmp(str, "slut") &&
		strcasecmp(str, "sperm") &&
		strcasecmp(str, "anal") &&
		strcasecmp(str, "panties") &&
		strcasecmp(str, "dream") &&
		strcasecmp(str, "stud") &&
		strcasecmp(str, "clit") &&
		strcasecmp(str, "myplace") &&
		strcasecmp(str, "suckcock") &&
		strcasecmp(str, "lust") &&
		strcasecmp(str, "teen") &&
		strcasecmp(str, "fuck") &&
		strcasecmp(str, "cum") &&
		strcasecmp(str, "cunt") &&
		strcasecmp(str, "semen") &&
		strcasecmp(str, "slit") &&
		strcasecmp(str, "ass") &&
		strcasecmp(str, "xxx")
	) {
		return(0);
	}
	return(1);
}





int ck2lddomainname(char *name)
{
	if (strlen(name) < 2) {
		return(E_HNAME2LDILLEG);
	}
	if (badstring(name)) {
		return(E_HNAMEFORBID);
	}
	return(0);
}





int ckhostname(char *name, int len)
{
	char buffer[64];
	char *ptr, *dot;
	int rval;

	/* Host name exceeds buffer */
	if (len > sizeof(buffer) - 1) {
		return(E_HNAME2LONG);
	}

	bzero(buffer, sizeof(buffer));
	memcpy(buffer, name, len);

	/* Check last part.  Needs to be legit. */
	if (! (ptr = strrchr(buffer, '.'))) {
		return(E_HNAMENODOTS);
	}
	ptr++;

	/* ISO-3166 country codes and other TLD's */
	/* http://www.bcpl.net/~jspath/isocodes.html */
	/* new: http://data.iana.org/TLD/tlds-alpha-by-domain.txt */
	if (	strcasecmp(ptr, "com") &&
		strcasecmp(ptr, "edu") &&
		strcasecmp(ptr, "gov") &&
		strcasecmp(ptr, "int") &&
		strcasecmp(ptr, "mil") &&
		strcasecmp(ptr, "net") &&
		strcasecmp(ptr, "org") &&
		strcasecmp(ptr, "aero") &&
		strcasecmp(ptr, "biz") &&
		strcasecmp(ptr, "coop") &&
		strcasecmp(ptr, "info") &&
		strcasecmp(ptr, "museum") &&
		strcasecmp(ptr, "name") &&
		strcasecmp(ptr, "pro") &&
		strcasecmp(ptr, "invalid") &&
		strcasecmp(ptr, "ad") &&
		strcasecmp(ptr, "ae") &&
		strcasecmp(ptr, "af") &&
		strcasecmp(ptr, "ag") &&
		strcasecmp(ptr, "ai") &&
		strcasecmp(ptr, "al") &&
		strcasecmp(ptr, "am") &&
		strcasecmp(ptr, "an") &&
		strcasecmp(ptr, "ao") &&
		strcasecmp(ptr, "aq") &&
		strcasecmp(ptr, "ar") &&
		strcasecmp(ptr, "as") &&
		strcasecmp(ptr, "at") &&
		strcasecmp(ptr, "au") &&
		strcasecmp(ptr, "aw") &&
		strcasecmp(ptr, "az") &&
		strcasecmp(ptr, "ba") &&
		strcasecmp(ptr, "bb") &&
		strcasecmp(ptr, "bd") &&
		strcasecmp(ptr, "be") &&
		strcasecmp(ptr, "bf") &&
		strcasecmp(ptr, "bg") &&
		strcasecmp(ptr, "bh") &&
		strcasecmp(ptr, "bi") &&
		strcasecmp(ptr, "bj") &&
		strcasecmp(ptr, "bm") &&
		strcasecmp(ptr, "bn") &&
		strcasecmp(ptr, "bo") &&
		strcasecmp(ptr, "br") &&
		strcasecmp(ptr, "bs") &&
		strcasecmp(ptr, "bt") &&
		strcasecmp(ptr, "bv") &&
		strcasecmp(ptr, "bw") &&
		strcasecmp(ptr, "by") &&
		strcasecmp(ptr, "bz") &&
		strcasecmp(ptr, "ca") &&
		strcasecmp(ptr, "cc") &&
		strcasecmp(ptr, "cf") &&
		strcasecmp(ptr, "cd") &&
		strcasecmp(ptr, "cg") &&
		strcasecmp(ptr, "ch") &&
		strcasecmp(ptr, "ci") &&
		strcasecmp(ptr, "ck") &&
		strcasecmp(ptr, "cl") &&
		strcasecmp(ptr, "cm") &&
		strcasecmp(ptr, "cn") &&
		strcasecmp(ptr, "co") &&
		strcasecmp(ptr, "cr") &&
		strcasecmp(ptr, "cs") &&
		strcasecmp(ptr, "cu") &&
		strcasecmp(ptr, "cv") &&
		strcasecmp(ptr, "cx") &&
		strcasecmp(ptr, "cy") &&
		strcasecmp(ptr, "cz") &&
		strcasecmp(ptr, "de") &&
		strcasecmp(ptr, "dj") &&
		strcasecmp(ptr, "dk") &&
		strcasecmp(ptr, "dm") &&
		strcasecmp(ptr, "do") &&
		strcasecmp(ptr, "dz") &&
		strcasecmp(ptr, "ec") &&
		strcasecmp(ptr, "ee") &&
		strcasecmp(ptr, "eg") &&
		strcasecmp(ptr, "eh") &&
		strcasecmp(ptr, "er") &&
		strcasecmp(ptr, "es") &&
		strcasecmp(ptr, "et") &&
		strcasecmp(ptr, "fi") &&
		strcasecmp(ptr, "fj") &&
		strcasecmp(ptr, "fk") &&
		strcasecmp(ptr, "fm") &&
		strcasecmp(ptr, "fo") &&
		strcasecmp(ptr, "fr") &&
		strcasecmp(ptr, "fx") &&
		strcasecmp(ptr, "ga") &&
		strcasecmp(ptr, "gb") &&
		strcasecmp(ptr, "gd") &&
		strcasecmp(ptr, "ge") &&
		strcasecmp(ptr, "gf") &&
		strcasecmp(ptr, "gh") &&
		strcasecmp(ptr, "gi") &&
		strcasecmp(ptr, "gl") &&
		strcasecmp(ptr, "gm") &&
		strcasecmp(ptr, "gn") &&
		strcasecmp(ptr, "gp") &&
		strcasecmp(ptr, "gq") &&
		strcasecmp(ptr, "gr") &&
		strcasecmp(ptr, "gs") &&
		strcasecmp(ptr, "gt") &&
		strcasecmp(ptr, "gu") &&
		strcasecmp(ptr, "gw") &&
		strcasecmp(ptr, "gy") &&
		strcasecmp(ptr, "hk") &&
		strcasecmp(ptr, "hm") &&
		strcasecmp(ptr, "hn") &&
		strcasecmp(ptr, "hr") &&
		strcasecmp(ptr, "ht") &&
		strcasecmp(ptr, "hu") &&
		strcasecmp(ptr, "id") &&
		strcasecmp(ptr, "ie") &&
		strcasecmp(ptr, "il") &&
		strcasecmp(ptr, "in") &&
		strcasecmp(ptr, "io") &&
		strcasecmp(ptr, "iq") &&
		strcasecmp(ptr, "ir") &&
		strcasecmp(ptr, "is") &&
		strcasecmp(ptr, "it") &&
		strcasecmp(ptr, "jm") &&
		strcasecmp(ptr, "jo") &&
		strcasecmp(ptr, "jp") &&
		strcasecmp(ptr, "ke") &&
		strcasecmp(ptr, "kg") &&
		strcasecmp(ptr, "kh") &&
		strcasecmp(ptr, "ki") &&
		strcasecmp(ptr, "km") &&
		strcasecmp(ptr, "kn") &&
		strcasecmp(ptr, "kp") &&
		strcasecmp(ptr, "kr") &&
		strcasecmp(ptr, "kw") &&
		strcasecmp(ptr, "ky") &&
		strcasecmp(ptr, "kz") &&
		strcasecmp(ptr, "la") &&
		strcasecmp(ptr, "lb") &&
		strcasecmp(ptr, "lc") &&
		strcasecmp(ptr, "li") &&
		strcasecmp(ptr, "lk") &&
		strcasecmp(ptr, "lr") &&
		strcasecmp(ptr, "ls") &&
		strcasecmp(ptr, "lt") &&
		strcasecmp(ptr, "lu") &&
		strcasecmp(ptr, "lv") &&
		strcasecmp(ptr, "ly") &&
		strcasecmp(ptr, "ma") &&
		strcasecmp(ptr, "mc") &&
		strcasecmp(ptr, "md") &&
		strcasecmp(ptr, "mg") &&
		strcasecmp(ptr, "mh") &&
		strcasecmp(ptr, "mk") &&
		strcasecmp(ptr, "ml") &&
		strcasecmp(ptr, "mm") &&
		strcasecmp(ptr, "mn") &&
		strcasecmp(ptr, "mo") &&
		strcasecmp(ptr, "mp") &&
		strcasecmp(ptr, "mq") &&
		strcasecmp(ptr, "mr") &&
		strcasecmp(ptr, "ms") &&
		strcasecmp(ptr, "mt") &&
		strcasecmp(ptr, "mu") &&
		strcasecmp(ptr, "mv") &&
		strcasecmp(ptr, "mw") &&
		strcasecmp(ptr, "mx") &&
		strcasecmp(ptr, "my") &&
		strcasecmp(ptr, "mz") &&
		strcasecmp(ptr, "na") &&
		strcasecmp(ptr, "nc") &&
		strcasecmp(ptr, "ne") &&
		strcasecmp(ptr, "nf") &&
		strcasecmp(ptr, "ng") &&
		strcasecmp(ptr, "ni") &&
		strcasecmp(ptr, "nl") &&
		strcasecmp(ptr, "no") &&
		strcasecmp(ptr, "np") &&
		strcasecmp(ptr, "nr") &&
		strcasecmp(ptr, "nt") &&
		strcasecmp(ptr, "nu") &&
		strcasecmp(ptr, "nz") &&
		strcasecmp(ptr, "om") &&
		strcasecmp(ptr, "pa") &&
		strcasecmp(ptr, "pe") &&
		strcasecmp(ptr, "pf") &&
		strcasecmp(ptr, "pg") &&
		strcasecmp(ptr, "ph") &&
		strcasecmp(ptr, "pk") &&
		strcasecmp(ptr, "pl") &&
		strcasecmp(ptr, "pm") &&
		strcasecmp(ptr, "pn") &&
		strcasecmp(ptr, "pr") &&
		strcasecmp(ptr, "pt") &&
		strcasecmp(ptr, "pw") &&
		strcasecmp(ptr, "py") &&
		strcasecmp(ptr, "qa") &&
		strcasecmp(ptr, "re") &&
		strcasecmp(ptr, "ro") &&
		strcasecmp(ptr, "ru") &&
		strcasecmp(ptr, "rw") &&
		strcasecmp(ptr, "sa") &&
		strcasecmp(ptr, "sb") &&
		strcasecmp(ptr, "sc") &&
		strcasecmp(ptr, "sd") &&
		strcasecmp(ptr, "se") &&
		strcasecmp(ptr, "sg") &&
		strcasecmp(ptr, "sh") &&
		strcasecmp(ptr, "si") &&
		strcasecmp(ptr, "sj") &&
		strcasecmp(ptr, "sk") &&
		strcasecmp(ptr, "sl") &&
		strcasecmp(ptr, "sm") &&
		strcasecmp(ptr, "sn") &&
		strcasecmp(ptr, "so") &&
		strcasecmp(ptr, "sr") &&
		strcasecmp(ptr, "st") &&
		strcasecmp(ptr, "su") &&
		strcasecmp(ptr, "sv") &&
		strcasecmp(ptr, "sy") &&
		strcasecmp(ptr, "sz") &&
		strcasecmp(ptr, "tc") &&
		strcasecmp(ptr, "td") &&
		strcasecmp(ptr, "tf") &&
		strcasecmp(ptr, "tg") &&
		strcasecmp(ptr, "th") &&
		strcasecmp(ptr, "tj") &&
		strcasecmp(ptr, "tk") &&
		strcasecmp(ptr, "tm") &&
		strcasecmp(ptr, "tn") &&
		strcasecmp(ptr, "to") &&
		strcasecmp(ptr, "tp") &&
		strcasecmp(ptr, "tr") &&
		strcasecmp(ptr, "tt") &&
		strcasecmp(ptr, "tv") &&
		strcasecmp(ptr, "tw") &&
		strcasecmp(ptr, "tz") &&
		strcasecmp(ptr, "ua") &&
		strcasecmp(ptr, "ug") &&
		strcasecmp(ptr, "uk") &&
		strcasecmp(ptr, "um") &&
		strcasecmp(ptr, "us") &&
		strcasecmp(ptr, "uy") &&
		strcasecmp(ptr, "uz") &&
		strcasecmp(ptr, "va") &&
		strcasecmp(ptr, "vc") &&
		strcasecmp(ptr, "ve") &&
		strcasecmp(ptr, "vg") &&
		strcasecmp(ptr, "vi") &&
		strcasecmp(ptr, "vn") &&
		strcasecmp(ptr, "vu") &&
		strcasecmp(ptr, "wf") &&
		strcasecmp(ptr, "ws") &&
		strcasecmp(ptr, "ye") &&
		strcasecmp(ptr, "yt") &&
		strcasecmp(ptr, "yu") &&
		strcasecmp(ptr, "za") &&
		strcasecmp(ptr, "zm") &&
		strcasecmp(ptr, "zr") &&
		strcasecmp(ptr, "zw")
	) {
		return(E_HNAMETLD);
	}
	*--ptr = '\0';

	/* We now have the host (and maybe domain) name, without the TLD. */

	/* Basic valid character checking */
	for (ptr = buffer; *ptr; ptr++) {
		/* All characters must be alnum, -, or . */ 
		if (! isalnum((int)*ptr) && ! (*ptr == '-') && ! (*ptr == '.')) {
			return(E_HNAMEILLEGAL);
		}

		/* Symbols must be separated by alnum */
		if (((ptr != buffer) && ((*ptr == '-') || (*ptr == '.')) && ! isalnum((int)*(ptr - 1)))) {
			return(E_HNAMESPECSEP);
		}
	}

	/* Check if it contains dots.  If so - we have both hostname
	   and domain name */
	if ((dot = strrchr(buffer, '.'))) {
		/* Host name case */
		if ((rval = ck2lddomainname(dot + 1))) {
			return(rval);
		}

		/* Host name leading char can not be a digit */
		if (isdigit((int)*buffer)) {
			return(E_HNAMEDIGIT);
		}

		/* Remove 2LD name */
		*dot = '\0';

		/* XXX this really sucks */
		if (badstring(buffer)) {
			return(E_HNAMEFORBID);
		}
	} else {
		/* Domain name only case */
		if ((rval = ck2lddomainname(buffer))) {
			return(rval);
		}
	}
	return(0);
}





int ckusername(char *name, int len)
{
	char buffer[32];
	char *ptr;

	/* User name exceeds buffer */
	if (len > sizeof(buffer) - 1) {
		return(E_UNAME2LONG);
	}
	if (len < 2) {
		return(E_UNAME2SHORT);
	}
	bzero(buffer, sizeof(buffer));
	memcpy(buffer, name, len);

	/* MUST start with alpha. */
	if (! isalpha((int)*buffer)) {
		return(E_UNAMEALPHA);
	}

	/* Basic valid character checking */
	for (ptr = buffer; *ptr; ptr++) {
		/* All characters must be alnum, -, _, or . */ 
		if (! isalnum((int)*ptr) && ! (*ptr == '-') && ! (*ptr == '_') && ! (*ptr == '.')) {
			return(E_UNAMEILLEGAL);
		}

		/* Symbols must be separated by alnum */
		if (((ptr != buffer) && ((*ptr == '-') || (*ptr == '_') || (*ptr == '.')) && ! isalnum((int)*(ptr - 1)))) {
			return(E_UNAMESPECSEP);
		}
	}

	return(0);
}





int ckaddress(char *addr)
{
	register char *ptr = addr;
	char *at;
	char *end;
	int flag, rval;
	
	end = addr + strlen(addr);

	/* Can't have an E-mail address without @ */
	if (! (at = strchr(addr, '@'))) {
		return(E_NOHOST);
	}

	/* Strip any trailing space */
	if (strlen(addr) > 1 && *(end - 1) == ' ') {
		*(end - 1) = '\0';
	}

	/* Component check: user name */
	if ((rval = ckusername(addr, at - addr))) {
		return(rval);
	}

	/* Component check: host name */
	if ((rval = ckhostname(at + 1, end - at - 1))) {
		return(rval);
	}

	/* Can't have an all-uppercase E-mail address */
	flag = 0;
	for (ptr = addr; *ptr; ptr++) {
		if (isalpha((int)*ptr) && islower((int)*ptr)) {
			flag = 1;
			break;
		}
	}
	if (! flag) {
		return(E_ALLUPPER);
	}
	return(0);
}

#if 0
int main()
{
	char buffer[8192];

	while (! feof(stdin)) {
		gets(buffer);
		if (! feof(stdin)) {
			printf("%d\t'%s'\n", ckaddress(buffer), buffer);
		}
	}
}
#endif
