
/*
 * LIB/MSGID.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype const char *MsgId(const char *s, const char **badmsgid);

/*
 * MsgId() - The message id must begin with a '<', end with a '>', 
 *	     and not contain any embedded '<', space, or TAB.
 *
 *	     Additionally, we require either nul or whitespace terminator
 *	     after the logical end of the message-id.
 *
 *	     WARNING!  Returned storage only good until next MsgId() call!
 *
 *	     If badmsgid is non-NULL, then an attempt is made to write
 *	     the bad message-id into that buffer
 */

const char *
MsgId(const char *s, const char **badmsgid)
{
    int i;
    static char LMsgId[MAXMSGIDLEN + 1];
    const char *badId = "<>";

    if (s == NULL) {
	if (badmsgid)
	    *badmsgid = badId;
	return(badId);
    }

    /*
     * Locate beginning of of message-id
     */
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
	++s;
    if (*s != '<') {
	if (badmsgid)
	    *badmsgid = s;
	return(badId);
    }

    /*
     * Locate end of message-id
     */

    for (i = 1; s[i] && s[i] != '>'; ++i) {
	if (s[i] == '<' || s[i] == '\t' || s[i] == ' ' || *s == '\r' || *s == '\n') {
	    if (badmsgid)
		*badmsgid = s;
	    return(badId);
	}
    }

    if (s[i] != '>') {
	if (badmsgid)
	    *badmsgid = s;
	return(badId);
    }

    /*
     * Last character cannot be a '.'
     */
    if (i > 2 && s[i - 1] == '.') {
	if (badmsgid)
	    *badmsgid = s;
	return(badId);
    }

    /*
     * check length, check for garbage (note: whitespace after msgid 
     * is ok and may indicate a tagged-on comment).
     */

    ++i;
    if (i >= MAXMSGIDLEN || (s[i] && s[i] != ' ' && s[i] != '\t' &&
					s[i] != '\r' && s[i] != '\n')) {
	if (badmsgid)
	    *badmsgid = s;
	return(badId);
    }

    bcopy(s, LMsgId, i);
    LMsgId[i] = 0;
    if (badmsgid)
	*badmsgid = LMsgId;
    return(LMsgId);
}

