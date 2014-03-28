
/*
 * LIB/PARSEDATE.C - approximate date parser
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype time_t parsedate(char *buf);

int parsemonth(char *d, char **pd);

time_t
parsedate(char *d)
{
    struct tm ti;

    memset(&ti, 0, sizeof(ti));

    if (strncmp(d, "Date:", 5) == 0)
	d += 5;
    while (*d == ' ' || *d == '\t')
	++d;
    while (*d && !isdigit((int)*d))
	++d;
    ti.tm_isdst = -1;
    ti.tm_mday = strtol(d, &d, 10);
    while (*d == ' ')
	++d;
    ti.tm_mon = parsemonth(d, &d);
    ti.tm_year = strtol(d, &d, 10);
    if (ti.tm_year < 100) {
	/*
	 * handle 2 digit years as +/- 50
	 * years from the current year.  Handles
	 * YEAR x000 FIX.
	 */
	time_t t = time(NULL);
	struct tm *tp = localtime(&t);
	int dYear = ti.tm_year - (tp->tm_year % 100);

	if (dYear > 50)
	    dYear -= 100;
	if (dYear < -50)
	    dYear += 100;
	ti.tm_year = tp->tm_year + dYear;
    } else if (ti.tm_year >= 1900) {
	ti.tm_year -= 1900;
    }
    ti.tm_hour = strtol(d, &d, 10);
    d++;
    ti.tm_min = strtol(d, &d, 10);
    if (ti.tm_mday && ti.tm_mon >= 0 && ti.tm_year)
	return(mktime(&ti));
    else
	return((time_t)-1);
}

int
parsemonth(char *d, char **pd)
{
    static char *MonAry[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int i;

    for (i = 11; i >= 0; --i) {
	if (strncasecmp(d, MonAry[i], 3) == 0)
	    break;
    }
    while (*d == ' ' || isalpha((int)*d))
	++d;
    *pd = d;
    return(i);
}

