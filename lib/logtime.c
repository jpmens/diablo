
/*
 * LIB/LOGTIME.C
 *
 */

#include "defs.h"

Prototype const char *LogTime(void);

/*
 * Return the current local time in msec resolution in a format usable for log entries:
 *
 * 	2000-04-14 13:24:59.296.
 *
 * This function is not thread safe.
 */

const char
*LogTime(void)
{
    static char result[30];
    struct timeval tv;
    struct tm *t;

    gettimeofday(&tv, NULL);
    t = localtime((const time_t *)&tv.tv_sec);

    sprintf(result, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
	t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
	t->tm_hour, t->tm_min, t->tm_sec, (int)(tv.tv_usec / 1000));
    return result;
}
