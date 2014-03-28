
/*
 * LIB/SNPRINTF.C - snprintf module if we don't have it
 *
 * 	snprintf() is just too important, we MUST have it, so if the OS
 *	doesn't support this module will include it.  Unfortunately, to
 *	do it right also required me to write an equivalent pfmt(), so
 *	all the snprintf()'s in the code support only a small subset
 *	of valid '%' escapes:
 *
 *		%[-][0][n][l]d
 *		%[-][0][n][l]u
 *		%[-][0][n][l]x
 *		%[-][0][n]c
 *		%[-][n]s
 *
 * NOTE!!!!! this snprintf only supports %x, %d, %c, and %s
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

int MiniPfmt(const char *ctl, va_list va, void (*callback)(void *info, const char *buf, int bytes), void *info);

#ifdef TEST
#undef HAVE_SNPRINTF
#define HAVE_SNPRINTF	0
void fatal(const char *ctl, ...);
#endif

#if USE_INTERNAL_SNPRINTF
#undef HAVE_SNPRINTF
#define HAVE_SNPRINTF	0
#endif

#if HAVE_SNPRINTF == 0

/*
 * no prototypes, snprintf is extern'd in lib/defs.h since it may 
 * be conditionally compiled.
 */

int 
snprintf(char *str, size_t size, const char *ctl, ...)
{
    va_list va;
    int r;

    va_start(va, ctl);
    r = vsnprintf(str, size, ctl, va);
    va_end(va);
    return(r);
}

typedef struct SBuf {
    char *sb_Buf;
    int  sb_Len;
} SBuf;

void
vsncallback(void *info, const char *buf, int bytes)
{
    SBuf *sbuf = info;
    if (bytes > sbuf->sb_Len)
	bytes = sbuf->sb_Len;
    if (bytes) {
	memcpy(sbuf->sb_Buf, buf, bytes);
	sbuf->sb_Buf += bytes;
	sbuf->sb_Len -= bytes;
    }
}

int 
vsnprintf(char *str, size_t size, const char *ctl, va_list va)
{
    SBuf sbuf;
    int r;

    sbuf.sb_Buf = str;
    sbuf.sb_Len = size - 1;
    r = MiniPfmt(ctl, va, vsncallback, &sbuf);
    *sbuf.sb_Buf = 0;

    if (r > size)
	r = size;	
    return(r);
}

#define FMT_LONG	0x01
#define FMT_LEFT	0x02
#define FMT_ZFILL	0x04
#define FMT_RIGHT	0x08
#define FMT_ISNEG	0x10

int
MiniPfmt(const char *ctl, va_list va, void (*callback)(void *info, const char *buf, int bytes), void *info)
{
    int r;
    int i;
    int b;

    r = 0;

    for (i = b = 0; ctl[i]; ++i) {
	int flags;
	int fwidth;
	char tbuf[32];
	char *tptr;
	int tlen;

	if (ctl[i] != '%')
	    continue;
	if (i != b) {
	    callback(info, ctl + b, i - b);
	    r += i - b;
	}

	flags = FMT_RIGHT;
	fwidth = 0;	/* exact fit	*/

	/*
	 * pointer to temporary buffer (for numbers),
	 * one past so we can put a minus sign before
	 * the beginning later on.
	 */

	tptr = tbuf + 1;
	tlen = 0;

	for (++i; ;++i) {
	    char c = ctl[i];

	    if (c == 0) {
		fatal("MiniPfmt: unterminated % escape in %s", ctl);
		/* not reached */
	    }
	    if (c == 's') {	/* string  */
		 tptr = va_arg(va, char *);
		 tlen = strlen(tptr);
		 break;
	    }
	    if (c == 'd' || c == 'u') {	/* decimal */
		long v;

		if (flags & FMT_LONG)
		    v = va_arg(va, long);
		else
		    v = va_arg(va, int);

		if (c == 'd' && v < 0) {
		    v = -v;
		    flags |= FMT_ISNEG;
		}
		sprintf(tptr, "%lu", (unsigned long)v);
		tlen = strlen(tptr);
		break;
	    }
	    if (c == 'x') {	/* hex	   */
		if (flags & FMT_LONG) {
		    long v = va_arg(va, long);
		    sprintf(tptr, "%lx", v);
		} else {
		    int v = va_arg(va, int);
		    sprintf(tptr, "%x", v);
		}
		tlen = strlen(tptr);
		break;
	    }
	    if (c == 'c') {
		tptr[0] = va_arg(va, int);
		tlen = 1;
		break;
	    }
	    if (c == '%') {
		/*
		 * %% = '%'.  tptr is empty
		 */
		tptr[0] = '%';
		tlen = 1;
		break;
	    }

	    switch(c) {
	    case '-':
		flags |= FMT_LEFT;
		flags &= ~FMT_RIGHT;
		break;
	    case 'l':
		flags |= FMT_LONG;
		break;
	    case '0':
		flags |= FMT_ZFILL;
		/* fall through */
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		/*
		 * field width
		 */
		{
		    char *ptr;
		    fwidth = strtol(ctl + i, &ptr, 10);
		    i = ptr - ctl - 1;
		}
		break;
	    default:
		fatal("MiniPfmt: unknown ctl character '%c' in %s", c, ctl);
		/* NOT REACHED */
	    }
	}

	/*
	 * Handle negative sign.  It must occur prior to
	 * any zero-fill, but be next to a right-justified
	 * number.
	 */
	if (flags & FMT_ISNEG) {
	    if ((flags & FMT_ZFILL) && tlen < fwidth) {
		--fwidth;
		callback(info, "-", 1);
		++r;
	    } else {
		*--tptr = '-';
		++tlen;
	    }
	}

	/*
	 * handle (default) right justification
	 * handle zero-fill
	 * (neither is set if buffer is left justified)
	 */
	while (tlen < fwidth) {
	    int n = (fwidth - tlen > 16) ? 16 : fwidth - tlen;

	    if (flags & FMT_ZFILL) {
		callback(info, "0000000000000000", n);
		fwidth -= n;
		r += n;
		continue;
	    } else if (flags & FMT_RIGHT) {
		callback(info, "                ", n);
		fwidth -= n;
		r += n;
		continue;
	    }
	    break;
	}

	/*
	 * output tptr
	 */
	if (tlen) {
	    callback(info, tptr, tlen);
	    r += tlen;
	}

	/*
	 * handle left justification
	 */

	while (tlen < fwidth && (flags & FMT_LEFT)) {
	    int n = (fwidth - tlen > 16) ? 16 : fwidth - tlen;
	    callback(info, "                ", n);
	    fwidth -= n;
	    r += n;
	}
	/*
	 * i points to the control terminator, so allow the for() loop to 
	 * increment it.
	 */
	b = i + 1;
    }
    if (i != b) {
	callback(info, ctl + b, i - b);
	r += i - b;
    }
    return(r);
}

#endif

/*
 *
 *	TEST CODE
 *
 */

#ifdef TEST

int
main(int ac, char **av)
{
    char buf[32];
    int r;

    if (ac == 1)
	exit(0);

    r = snprintf(buf, sizeof(buf), av[1], 203, 11, -500, "charlie");
    printf("(%d): %s\n", r, buf);
    exit(0);
}

void
fatal(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(1);
}

#endif

