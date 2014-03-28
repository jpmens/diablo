
/*
 * MISC.C	- general nntp reader commands
 *
 *	Misc NNTP commands
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void NNTPSlave(Connection *conn, char **pptr);
Prototype void NNTPNewNews(Connection *conn, char **pptr);
Prototype void NNTPDate(Connection *conn, char **pptr);

void 
NNTPSlave(Connection *conn, char **pptr)
{
    if (conn->co_Flags & COF_SERVER)
	MBLogPrintf(conn, &conn->co_TMBuf, "500 \"slave\" not implemented\r\n");
    else
	MBLogPrintf(conn, &conn->co_TMBuf, "202 Unsupported\r\n");
    NNCommand(conn);
}

#if 0
void 
NNTPNewNews(Connection *conn, char **pptr)
{
    MBLogPrintf(conn, &conn->co_TMBuf, "500 \"newnews\" not implemented\r\n");
    NNCommand(conn);
}
#endif

void
NNTPDate(Connection *conn, char **pptr)
{
    time_t t = time(NULL);
    struct tm *tp = gmtime(&t);

    MBLogPrintf(conn, &conn->co_TMBuf, "111 %04d%02d%02d%02d%02d%02d\r\n",
	tp->tm_year + 1900,
	tp->tm_mon + 1,
	tp->tm_mday,
	tp->tm_hour,
	tp->tm_min,
	tp->tm_sec
    );
    NNCommand(conn);
}

