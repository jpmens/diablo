
/*
 * DREADERD/SUBS.C
 *
 *	Misc subroutines.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype char *parseword(char **pptr, char *toks);
Prototype void SetCurrentGroup(Connection *conn, const char *group);
Prototype int shash(const char *s);
Prototype int SetTimeRestrict(TimeRestrict *tr, const char *yymmdd, const char *hhmmss, const char *gmt);
Prototype void GenerateMessageID(Connection *conn);
Prototype void dumpConnectionInfo(Connection *conn);
Prototype artno_t artno_nb(artno_t nb, artno_t ne, int numberingmode);
Prototype artno_t artno_ne(artno_t nb, artno_t ne, int numberingmode);
Prototype artno_t artno_art(artno_t nb, artno_t ne, artno_t art, int numberingmode);
Prototype artno_t artno_input(artno_t nb, artno_t ne, artno_t in, int numberingmode);


artno_t
artno_nb(artno_t nb, artno_t ne, int numberingmode) {
	if (numberingmode == CON_RFC3977) {
		if (nb > 0x7FFFFFFFLL) {
			nb = 0x7FFFFFFFLL;
		}
	}
	if (numberingmode == CON_RFC977) {
		/* no action */
	}
	if (numberingmode == CON_WINDOW) {
		artno_t reduce;
		reduce = ne / 1500000000;
		reduce *= 1500000000;
		if (reduce) {
			reduce -= 500000000;
		}
		if (nb < ne - 500000000) {
			nb = ne - 500000000;
		}
		nb -= reduce;
	}
	return(nb);
}

artno_t
artno_ne(artno_t nb, artno_t ne, int numberingmode) {
	if (numberingmode == CON_RFC3977) {
		if (ne > 0x7FFFFFFFLL) {
			ne = 0x7FFFFFFFLL;
		}
	}
	if (numberingmode == CON_RFC977) {
		/* no action */
	}
	if (numberingmode == CON_WINDOW) {
		artno_t reduce;
		reduce = ne / 1500000000;
		reduce *= 1500000000;
		if (reduce) {
			reduce -= 500000000;
		}
		ne -= reduce;
	}
	return(ne);
}

artno_t
artno_art(artno_t nb, artno_t ne, artno_t art, int numberingmode) {
	if (numberingmode == CON_RFC3977) {
		if (art > 0x7FFFFFFFLL) {
			art = 0x7FFFFFFFLL;
		}
	}
	if (numberingmode == CON_RFC977) {
		/* no action */
	}
	if (numberingmode == CON_WINDOW) {
		artno_t reduce;
		reduce = ne / 1500000000;
		reduce *= 1500000000;
		if (reduce) {
			reduce -= 500000000;
		}
		if (nb < ne - 500000000) {
			nb = ne - 500000000;
		}
		nb -= reduce;
		ne -= reduce;
		art -= reduce;
		if (art < nb) {
			art = nb;
		}
	}
	return(art);
}

artno_t
artno_input(artno_t nb, artno_t ne, artno_t in, int numberingmode) {
	/*
	 * We may need to adjust user-supplied artnos in the 
	 * case of CON_WINDOW and CON_RFC3977.
	 */

	if (numberingmode == CON_RFC3977) {
		if (in > 0x7FFFFFFFLL) {
			return(-1);
		}
	}

	in += (nb - artno_nb(nb, ne, numberingmode));

	return(in);
}

char *
parseword(char **pptr, char *toks)
{
    char *base = *pptr;
    int i;

    for (i = 0; base[i] && strchr(toks, base[i]) == NULL; ++i)
	;
    if (base[i]) {
	int j;

	base[i] = 0;
	for (j = i + 1; base[j] && strchr(toks, base[j]) != NULL; ++j)
	    ;
	*pptr = base + j;
    } else {
	*pptr = base + i;
	if (i == 0)
	    base = NULL;
    }
    return(base);
}

void
SetCurrentGroup(Connection *conn, const char *group)
{
    zfreeStr(&conn->co_MemPool, &conn->co_GroupName);
    conn->co_GroupName = zallocStr(&conn->co_MemPool, group);
}

int
shash(const char *s)
{
    unsigned int hv = 0xAFC32344;

    if (s == NULL)
	return(0);
    while (*s) {
	hv = (hv << 5) ^ *s ^ (hv >> 23);
	++s;
    }
    return(hv ^ (hv >> 16));
}

int 
SetTimeRestrict(TimeRestrict *tr, const char *yymmdd, const char *hhmmss, const char *gmt)
{
    time_t t = time(NULL);
    struct tm tmv = { 0 };
    struct tm *tp = localtime(&t);

    if (yymmdd == NULL || hhmmss == NULL)
	return(-1);

    /*
     * Handle various cases of year specification:
     *
     * 20yymmdd	= 4 digit year, only valid until 2099    XXX
     * 1yymmdd  = 3 digit year from non-y2k clients
     * yymmdd   = 2 digit year that conforms to NNTP spec
     *
     */
    switch(strlen(yymmdd)) {
    case 8:
	if (sscanf(yymmdd, "20%2d%2d%2d", &tmv.tm_year,
					&tmv.tm_mon, &tmv.tm_mday) != 3)
	    return(-1);
	break;
    case 7:
	if(*yymmdd=='1' && *(yymmdd+1)=='0') {
	    if (sscanf(yymmdd, "1%2d%2d%2d", &tmv.tm_year,
					&tmv.tm_mon,&tmv.tm_mday) != 3)
		return(-1);
	} else {
	    return (-1);
	}
	break;
    case 6:
	if (sscanf(yymmdd, "%2d%2d%2d", &tmv.tm_year,
					&tmv.tm_mon,&tmv.tm_mday) != 3)
	    return(-1);
	break;
    default:
	return (-1);
    }

    if (sscanf(hhmmss, "%2d%2d%2d", &tmv.tm_hour,&tmv.tm_min,&tmv.tm_sec) != 3)
	return(-1);
    if (--tmv.tm_mon < 0 || tmv.tm_mday <= 0)
	return(-1);

    /*
     * Handle year rollover, aka '00 - 99' == -99.
     */
    tmv.tm_year += (tp->tm_year / 100) * 100;

    if (tp->tm_year - tmv.tm_year < -50)
	tmv.tm_year -= 100;
    else if (tp->tm_year - tmv.tm_year > 50)
	tmv.tm_year += 100;

    tmv.tm_isdst = -1;

    t = mktime(&tmv);

    /*
     * don't use localtime, use gmt.  Figure out the GMT offset.
     */

    if (gmt && strcmp(gmt, "GMT") == 0) {
        struct tm lt = *localtime(&t);
        struct tm gt = *gmtime(&t);
        int dt =
            (lt.tm_sec + lt.tm_min * 60 + lt.tm_hour * (60 * 60)) -
            (gt.tm_sec + gt.tm_min * 60 + gt.tm_hour * (60 * 60));
        if (lt.tm_mday > gt.tm_mday)
            dt += 24 * 60 * 60;
        if (lt.tm_mday < gt.tm_mday)
            dt -= 24 * 60 * 60;
        t += dt;
    }
    tr->tr_Time = t;
    return(0);
}

void
GenerateMessageID(Connection *conn)
{
    char msgid[512];
    static time_t LastT;
    static int MsgIdCounter;
    time_t t;
  
    t = time(NULL);
    if (LastT != t) {
	LastT = t;
	MsgIdCounter = 0;
    }

    sprintf(msgid, "<%08lx$%d$%d$%x@%s>",
	(long)t,
	MsgIdCounter,
	(int)getpid(),
	shash(conn->co_Auth.dr_VServerDef->vs_HostName),
	conn->co_Auth.dr_VServerDef->vs_ClusterName
    );
    ++MsgIdCounter;
    if (DebugOpt)
	printf("Generated Message-ID: %s\n", msgid);
    conn->co_IHaveMsgId = zallocStr(&conn->co_MemPool, msgid);
}

void
dumpConnectionInfo(Connection *conn)
{
    if (conn->co_Desc) {
    printf("ForkDesc ID            : %s\n", conn->co_Desc->d_Id);
    printf("         pid           : %d\n", conn->co_Desc->d_Pid);
    printf("         slot          : %d\n", conn->co_Desc->d_Slot);
    printf("         fd            : %d\n", conn->co_Desc->d_Fd);
    printf("         type          : 0x%x\n", conn->co_Desc->d_Type);
    printf("         active threads: %d\n", conn->co_Desc->d_Count);
    printf("         pri           : %d\n", conn->co_Desc->d_Pri);
    }
    printf(" Conn    state         : %s\n", conn->co_State);
    printf("         starttime     : %d\n", (int)conn->co_SessionStartTime);
    printf("         lastactivetime: %d\n", (int)conn->co_LastActiveTime);
    printf("         fcounter      : %d\n", conn->co_FCounter);
    printf("         bytecounter   : %.0f\n", conn->co_ByteCounter);
    printf("         bytesheader   : %d\n", conn->co_BytesHeader);
    printf("         flags         : 0x%x\n", conn->co_Flags);
    printf("         numbering     : %d\n", conn->co_Numbering);
    printf("         auth          : %s\n", conn->co_Auth.dr_ReaderName);
    printf("         group         : %s\n", conn->co_GroupName);
    printf("         ihavemsgid    : %s\n", conn->co_IHaveMsgId);
    printf("         listrec       : %d\n", conn->co_ListRec);
    printf("         listpat       : %s\n", conn->co_ListPat);
    printf("         listhdrs      : %s\n", conn->co_ListHdrs);
    printf("         listbegno     : %lld\n", conn->co_ListBegNo);
    printf("         listendno     : %lld\n", conn->co_ListEndNo);
    printf("         listcachemode : 0x%x\n", conn->co_ListCacheMode);
    printf("         artmode       : 0x%x\n", conn->co_ArtMode);
    printf("         artno         : %lld\n", conn->co_ArtNo);
    printf("         artbeg        : %lld\n", conn->co_ArtBeg);
    printf("         artend        : %lld\n", conn->co_ArtEnd);
    printf("         ratecounter   : %d\n", conn->co_RateCounter);
}
