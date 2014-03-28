/*  $Revision: 1.1.1.1 $
**
*/
#include <stdio.h>
#include <sys/types.h>

#include	"defs.h"

/* Stolen from INN */

Prototype void HeaderCleanFrom(char *from);

#define LPAREN	'('
#define RPAREN	')'


/*
**  Clean up a from line, making the following transformations:
**	address			address
**	address (stuff)		address
**	stuff <address>		address
*/
void
HeaderCleanFrom(from)
    char		*from;
{
    register char	*p;
    register char	*end;

    /* Do the equivalent of sed's "1q" */
    if ((p = strchr(from, '\n')) != NULL)
	*p = '\0';

    /* Do pretty much the equivalent of sed's "s/ (.*)//"; doesn't
     * work for "(save (delete this)" but that's okay. */
    if ((p = strchr(from, LPAREN))
     && p > from
     && *--p == ' '
     && (end = strrchr(p, RPAREN))) {
	while (*++end)
	    *p++ = *end;
	*p = '\0';
    }

    /* Do the equivalent of sed's "s/.*<\(.*\)>/\1/" */
    if ((p = strrchr(from, '<')) && (end = strrchr(p, '>'))) {
	while (++p < end)
	    *from++ = *p;
	*from = '\0';
    }
}
