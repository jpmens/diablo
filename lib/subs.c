
/*
 * LIB/SUBS.C
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype const char *PatExpand(const char *pat);
Prototype void diablo_strlcpy(char *d, const char *s, int ssize, int dsize);
Prototype void diablo_strlcpynl(char *d, const char *s, int ssize, int dsize);
Prototype void diablo_strlcpynl2(char *d, int trimwsafter, const char *s, int ssize, int dsize);
Prototype const char *PatExpand(const char *pat);
Prototype const char *PatLibExpand(const char *pat);
Prototype const char *PatDbExpand(const char *pat);
Prototype const char *PatLogExpand(const char *pat);
Prototype const char *PatSpoolExpand(const char *pat);
Prototype const char *PatRunExpand(const char *pat);
Prototype int CidrMatch(const char *cidr, const char *ip);
Prototype int IsIpAddr(char *a);
Prototype char *SanitiseAddr(char *addr);
Prototype void GetIPHost(struct sockaddr *sa, char *hname, int hnamelen, char *ipname, int iplen);
Prototype int TestForwardLookup(const char *hostName, const char *ipst, const struct sockaddr *res);
Prototype char *NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec);
Prototype int ValidGroupName(const char *name);
Prototype void SanitizeString(char *name);
Prototype void SanitizeDescString(char *name);
Prototype char *StrnCpyNull(char *dst, const char *src, size_t maxlen);
Prototype const char *ftos(double d);
Prototype long bsizetol(char *p);
Prototype double bsizektod(char *p);
Prototype long btimetol(char *p);
Prototype char *dtlenstr(time_t t);
Prototype int MakeGroupDirectory(char *path);
Prototype char *strdupfree(char **st, char *val, char *empty);
Prototype char *safestr(char *st, char *noval);
Prototype int enabled(char *st);
Prototype HashFeed_MatchList *DiabHashFeedParse(MemPool **mp, char *hashconfig);
Prototype int MoveFile(char *from, char *to);
Prototype int TimeSpec(char *t, char *def);

u_long ascToAddr(const char *str);
u_long ascToMask(const char *str);

void 
diablo_strlcpy(char *d, const char *s, int ssize, int dsize)
{
    while (ssize && dsize > 1 && *s) {
	*d = *s;
	--ssize;
	--dsize;
	++s;
	++d;
    }
    *d = 0;
}

void 
diablo_strlcpynl(char *d, const char *s, int ssize, int dsize)
{
    diablo_strlcpynl2(d, 0, s, ssize, dsize);
}

void 
diablo_strlcpynl2(char *d, int trimwsafter, const char *s, int ssize, int dsize)
{
    char *dold = d;
    int trimws = 0;

    while (ssize && *s == ' ') {
	++s;
	--ssize;
    }

    for (; ssize > 0 && dsize > 1 && *s; s++, ssize--) {
	if (*s == '\r' || *s == '\n')
	   continue;
	if (trimws && (*s == ' ' || *s == '\t'))
	   continue;
	trimws = (*s == trimwsafter);
	*d = *s;
	--dsize;
	++d;
    }
    *d = 0;

    /*
     * remove trailing tabs and spaces
     */
    while (d != dold && (d[-1] == '\t' || d[-1] == ' ')) {
	--d;
	*d = 0;
    }
}

static char PatPath1[128];
static char PatPath2[128];

const char *
PatExpand(const char *pat)
{
    snprintf(PatPath1, sizeof(PatPath1), pat, NewsHome);

    return(PatPath1);
}


const char *
PatLibExpand(const char *pat)
{
    snprintf(PatPath2, sizeof(PatPath2), pat, PatExpand(LibHomePat));
    return(PatPath2);
}


const char *
PatDbExpand(const char *pat)
{
    snprintf(PatPath2, sizeof(PatPath2), pat, PatExpand(DbHomePat));
    return(PatPath2);
}

const char *
PatLogExpand(const char *pat)
{
    snprintf(PatPath2, sizeof(PatPath2), pat, PatExpand(LogHomePat));
    return(PatPath2);
}

const char *
PatSpoolExpand(const char *pat)
{
    snprintf(PatPath2, sizeof(PatPath2), pat, PatExpand(SpoolHomePat));
    return(PatPath2);
}

const char *
PatRunExpand(const char *pat)
{
    snprintf(PatPath2, sizeof(PatPath2), pat, PatExpand(RunHomePat));
    return(PatPath2);
}

int 
IsIpAddr(char *a)
{
    const char *spanstr;

#ifdef INET6
    if (strchr(a, ':') != NULL)
	spanstr = "0123456789abcdefgh.:/*?[]";
    else
#endif
	spanstr = "0123456789./*?";
    if (strspn(a, spanstr) == strlen(a))
	return(1);
    else
	return(0);
}

/*
 * Sanitise an IP[v6] address by removing the '[]'
 */
char *
SanitiseAddr(char *addr)
{
    static char st[64];
    char *p;
    int len;

    if (addr == NULL)
	return(NULL);
    if (*addr == '[')
	addr++;
    len = strlen(addr);
    if ((p = strchr(addr, ']')) != NULL)
	len = p - addr;
    if (len >= sizeof(st))
	len = sizeof(st) - 1;
    strncpy(st, addr, len);
    st[len] = '\0';
    return(st);
}

u_long
ascToAddr(const char *str)
{
    char string[32];
    char *ptr;
    u_long i, a, b, c, d;

    snprintf(string, sizeof(string), "%s", str);
    if ((ptr = strchr(string, '/')))
	*ptr = '\0';

    /* Count the dots. */
    ptr = string;
    i = 0;
    while (*ptr)
	if (*ptr++ == '.')
	    i++;

    if (i == 0) {
	sscanf(string, "%ld", &a);
	return(a << 24);
    } else if (i == 1) {
	sscanf(string, "%ld.%ld", &a, &b);
	return((a << 24) + (b << 16));
    } else if (i == 2) {
	sscanf(string, "%ld.%ld.%ld", &a, &b, &c);
	return((a << 24) + (b << 16) + (c << 8));
    } else {
	sscanf(string, "%ld.%ld.%ld.%ld", &a, &b, &c, &d);
	return((a << 24) + (b << 16) + (c << 8) + d);
    }
}

u_long
ascToMask(const char *str)
{
    char *ptr;
    u_long i;

    if ((ptr = strchr(str, '/')))
	ptr++;
    else
	return(0xffffffff);

    /* Accept either CIDR /nn or /xxx.xxx.xxx.xxx */
    if (strstr(ptr, ".")) {
	return(ntohl(inet_addr(ptr)));
    } else {
	i = atoi(ptr);
	if (!i)
	    return(0);
	else
	    return((u_long) 0xffffffff << (32 - i));
    }
}

/*
 * Match CIDR address - return 1 for sucess else 0
 *
 * This is really messy
 */
int
CidrMatch(const char *cidr, const char *ip)
{
#ifdef INET6
    char addr[256];
    char mask[256];
    uint8 maskbits = 128;
    char maskst[NI_MAXHOST];
    char *p;
    int i;

    if (strchr(ip, ':') != NULL) {
	uint8 b8;
	uint8 b;

	if (strchr(cidr, ':') == NULL)	/* Don't compare against non v6 CIDR */
	    return(0);
	strcpy(maskst, cidr);
	if ((p = strchr(maskst, '/')) != NULL) {
	    *p = '\0';
	    maskbits = atoi(++p);
	}
	if (inet_pton(AF_INET6, ip, addr) != 1) {
	    logit(LOG_ERR, "Invalid IPv6 address: %s", ip);
	    return(0);
	}
	if (inet_pton(AF_INET6, maskst, mask) != 1) {
	    logit(LOG_ERR, "Invalid IPv6 address: %s", cidr);
	    return(0);
	}
	
	for (i = 0; i * 8 < maskbits; i++) {
	    if (i < maskbits / 8)
		b8 = 255;
	    else
		for (b8 = 0, b = 0; b < maskbits % 8; b++)
		    b8 |= (1 << (7 - b));
	    if ((addr[i] & b8) != (mask[i] & b8))
		return(0);
	}
	return(1);
    } else
#endif
    {
	u_long net;
	u_long mask;
	u_long addr;

	if (strchr(cidr, ':') != NULL)	/* Don't compare against v6 CIDR */
	    return(0);
	addr = ascToAddr(ip);
	net = ascToAddr(cidr);
	mask = ascToMask(cidr);
	if ((addr & mask) != (net & mask))
	    return(0);
	return(1);
    }
}

void
GetIPHost(struct sockaddr *sa, char *hname, int hnamelen, char *ipname, int iplen)
{
    strncpy(ipname, NetAddrToSt(0, sa, 1, 0, 0), iplen - 1);
    ipname[iplen - 1] = '\0';

    *hname = '\0';
    {
#ifdef INET6
	int error;

	if ((error = getnameinfo(sa, SA_LEN(sa), hname, hnamelen,
					NULL, 0, NI_NAMEREQD)) != 0)
		*hname = '\0';
#else
	struct hostent *he;
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;

	he = gethostbyaddr(
	    (char *)&sin->sin_addr,
	    sizeof(sin->sin_addr),
	    AF_INET
	);

	if (he != NULL) {
	    strncpy(hname, he->h_name, hnamelen - 1);
	    hname[hnamelen - 1] = '\0';
	}
#endif /* INET6 */
    }

}

/*
 * TestForwardLookup(). 
 *
 * Check that Fwd and Rev DNS entries match up
 */

int
TestForwardLookup(const char *hostName, const char *ipst, const struct sockaddr *sa)
{
#ifdef INET6
    struct addrinfo *ai0;
    int r = -1;

    stprintf("forward auth %s", hostName);
    if (getaddrinfo(hostName, NULL, NULL, &ai0) == 0) {
	struct addrinfo *ai;

	for (ai = ai0; ai; ai = ai->ai_next) {
	    char *st = NetAddrToSt(0, ai->ai_addr, 1, 0, 0);
	    if (st != NULL && strcmp(ipst, st) == 0) {
		r = 0;
		break;
	    }
	}
	if (ai == NULL)
	    logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: %s/%s", hostName, ipst);
	freeaddrinfo(ai0);
    } else {
	logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: lookup of %s failed", hostName);
    }
    return(r);
#else
    struct hostent *he;
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    int r = -1;

    if ((he = gethostbyname(hostName)) != NULL) {
	int i;

	for (i = 0; he->h_addr_list && he->h_addr_list[i]; ++i) {
	    const struct in_addr *haddr = (const void *)he->h_addr_list[i];
	    if (sin->sin_addr.s_addr == haddr->s_addr) {
		r = 0;
		break;
	    }
	}
	if (he->h_addr_list[i] == NULL)
	    logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: %s/%s", hostName, ipst);
    } else {
	logit(LOG_NOTICE, "DNS Fwd/Rev mismatch: lookup of %s failed", hostName);
    }
    return(r);
#endif
}

int 
ValidGroupName(const char *name)
{
    int r = 0;

    if (name[0] == 0 || name[0] == '.') {
	r = -1;
    } else {
	for (; *name; ++name) {
	    if (*name == ' ' || ! isprint((int)*name)) {
		r = -1;
	    }
	    if (*name >= 'a' && *name <= 'z')
		continue;
	    if (*name >= 'A' && *name <= 'Z')
		continue;
	    if (*name >= '0' && *name <= '9')
		continue;
	    if (*name == '+')
		continue;
	    if (*name == '-')
		continue;
	    if (*name == '.') {
		if (name[-1] == '.' || name[1] == '.' || name[1] == 0)
		    r = -1;
	    }
	}
    }
    return(r);
}

void
SanitizeString(char *name)
{
    for (; *name; ++name) {
	if (*name >= 'a' && *name <= 'z')
	    continue;
	if (*name >= 'A' && *name <= 'Z')
	    continue;
	if (*name >= '0' && *name <= '9')
	    continue;
	if (*name == '\t')
	    *name = ' ';
	if (*name == '@' ||
	    *name == '-' ||
	    *name == '+' ||
	    *name == '_' ||
	    *name == '.' ||
	    *name == ',' ||
	    *name == ' '
	) {
	    continue;
	}
	*name = '?';
    }
}

/*
 * Sanitise a group description
 */
void
SanitizeDescString(char *name)
{
    for (; *name; ++name) {
	if (*name == '\t')
	    *name = ' ';
	if (!iscntrl((int)*name))  
	    continue;
	*name = '?';
    }
}

/*
 * StrnCpyNull()
 *
 * Like strcpy, but gracefully handles null pointers and limits the length
 * of the string copied.
 */
char *
StrnCpyNull(char *dst, const char *src, size_t maxlen)
{
    if (dst) {
    	if (src) {
	    if (strlen(src) >= maxlen) {
		strncpy(dst, src, maxlen - 1);
		dst[maxlen - 1] = '\0';
	    }
	    else {
		strcpy(dst, src);
	    }
    	}
    	else {
	    *dst = '\0';
    	}
    }
    return dst;
}

#define ONE_K	1024.0	
#define ONE_M	(1024.0*1024.0)
#define ONE_G	(1024.0*1024.0*1024.0)
#define ONE_T	(1024.0*1024.0*1024.0*1024.0)

const char *
ftos(double d)
{
    static char FBuf[8][32];
    static int FCnt;
    char *p = FBuf[FCnt];

    if (d < 1024.0) {
	sprintf(p, "%d", (int)d);
    } else if (d < ONE_M) {
	sprintf(p, "%d.%03dK", (int)(d / ONE_K), ((int)d % (int)ONE_K) * 1000 / (int)ONE_K);
    } else if (d < ONE_G) {
	sprintf(p, "%d.%03dM", (int)(d / ONE_M), ((int)(d / ONE_K) % (int)ONE_K) * 1000 / (int)ONE_K);
    } else if (d < ONE_T) {
	sprintf(p, "%d.%03dG", (int)(d / ONE_G), ((int)(d / ONE_M) % (int)ONE_K) * 1000 / (int)ONE_K);
    } else {
	sprintf(p, "%d.%03dT", (int)(d / ONE_T), ((int)(d / ONE_G) % (int)(d / ONE_M) % (int)ONE_K) * 1000 / (int)ONE_K);
    }

    FCnt = (FCnt + 1) & 7;
    return(p);
}

/*
 * Convert a string value to a number of bytes. The string can be
 * specified in kb, mb or gb
 */
long
bsizetol(char *p)
{
    long n;

    n = strtol(p, &p, 0);
    switch(*p) {
    case 'g':
    case 'G':
	n *= 1024;
	/* fall through */
    case 'm':
    case 'M':
	n *= 1024;
	/* fall through */
    case 'k':
    case 'K':
	n *= 1024;
	break;
    }
    return(n);
}

/*
 * Convert a string value to a number of kilobytes
 */
double
bsizektod(char *p)
{
    double n;

    n = strtol(p, &p, 0) * 1024.0;
    switch(*p) {
    case 'g':
    case 'G':
	n *= 1024.0;
	/* fall through */
    case 'm':
    case 'M':
	n *= 1024.0;
	break;
	/* fall through */
    case 't':
    case 'T':
	n *= 1024.0;
	break;
    }
    return(n);
}

/*
 * Convert a time specification in days, hours, mins and/or secs to a long
 * representing the number of secs. The specification can include
 * multiple time specifications.
 */
long
btimetol(char *p)
{
    long n;
    long res = 0;

    while (*p) {
	while (*p && !(int)isdigit(*p))
	    p++;
	if (*p == '\0')
	    break;
	n = strtol(p, &p, 0);
	while (*p && (int)isdigit(*p))
	    p++;
	if (*p == '\0') {
	    res += n;
	    break;
	}
	switch(*p) {
	    case 'd':
	    case 'D':
		n *= 24;
		/* fall through */
	    case 'h':
	    case 'H':
		n *= 60;
		/* fall through */
	    case 'm':
	    case 'M':
		n *= 60;
		break;
	    case 's':
	    case 'S':
	    case ' ':
		break;
	}
	p++;
	res += n;
    }
    return(res);
}

char *
dtlenstr(time_t t)
{
    int d = 0;
    int h = 0;
    int m = 0;
    static char tb[64];

    if (t > 24 * 60 * 60) {
	d = t / (24 * 60 * 60);
	t = t % (24 * 60 * 60);
    }
    if (t > 60 * 60) {
	h = t / (60 * 60);
	t = t % (60 * 60);
    }
    if (t > 60) {
	m = t / 60;
	t = t % 60;
    }
    sprintf(tb, "%d days %d hrs %d min %d sec", d, h, m, (int)t);
    return(tb);
}

#ifdef INET6
/*
 * A protocol independent conversion of network address to string
 *
 * Strips the initial portion of a IPv6 encoded IPv4 address if
 * requested
 *
 * If sa == NULL then the address is obtained from the fd passed
 * otherwise the fd is ignored.
 */
char *
NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec)
{
    static char st[NI_MAXHOST + 8];
    char addrst[NI_MAXHOST];
    char serv[16];
    int error;

    if (sa == NULL) {
	static struct sockaddr_storage res;
	int salen = sizeof(res);

	sa = (struct sockaddr *)&res;
	if (getpeername(fd, sa, &salen) != 0) {
	    logit(LOG_ERR, "%s", strerror(errno));
	    return(NULL);
	}
    }
    if (SA_LEN(sa) == 0)
	return(NULL);
    if ((error = getnameinfo(sa, SA_LEN(sa), addrst, sizeof(addrst),
					serv, sizeof(serv),
					NI_NUMERICHOST|NI_NUMERICSERV)) == 0) {
	if (v6spec && strchr(addrst, ':')) {
	    snprintf(st, sizeof(st), "[%s]", addrst);
	} else {
	    strncpy(st, addrst, sizeof(st) - 1);
	    st[sizeof(st) - 1] = '\0';
	}
	if (port) {
	    strncat(st, ":", sizeof(st) - 1);
	    strncat(st, serv, sizeof(st) - 1);
	}
	if (ip4conv && strncmp(st, "::ffff:", 7) == 0)
            return(st + 7);
	else
            return(st);
    } else {
            logit(LOG_ERR, "getnameinfo: %s\n", gai_strerror(error));
    }
    return(NULL);
}
#else
char *
NetAddrToSt(int fd, struct sockaddr *sa, int ip4conv, int port, int v6spec)
{
    static char st[128];
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;

    strncpy(st, inet_ntoa(sin->sin_addr), sizeof(st) - 1);
    st[sizeof(st) - 1] = '\0';
    if (port) {
	char port[16];
	snprintf(port, sizeof(port), ":%d", ntohs(sin->sin_port));
	strncat(st, port, sizeof(st) - 1);
    }
    return(st);
}

#endif

/*
 * Create the directories required for the file pathname specified in path
 *   path = the full path/filename which gets clobbered.
 */
int
MakeGroupDirectory(char *path)
{
    char dir[PATH_MAX];
    char *p;
    struct stat st;

    strcpy(dir, path);
    if (strchr(dir, '/') == NULL)
	return(0);
    if ((p = strrchr(dir, '/')) != NULL)
	*p = '\0';
    if (stat(dir, &st) == 0)
	return(0);
    if (MakeGroupDirectory(dir) == -1)
	return(-1);
    return(mkdir(dir, 0755));
}

/*
 * If the string exists, free it
 * strdup the value
 * if value is "0", then set string to NULL
 */
char *
strdupfree(char **st, char *val, char *empty)
{
    if (*st != NULL)
	free(*st);
    if (val == NULL || strcmp(val, "0") == 0)
	*st = (empty == NULL) ? NULL : strdup(empty);
    else
	*st = strdup(val);
    return(*st);
}

/*
 * Return a string that isn't NULL
 */
char *
safestr(char *st, char *noval)
{
    if (st == NULL)
	return((noval != NULL) ? noval : "NONE");
    return(st);
}

int
enabled(char *st)
{
    if (st == NULL || !*st || *st == 'y' || *st == 'Y' ||
				strcasecmp(st, "on") == 0 || *st == '1')
	return(1);
    else
	return(0);
}

HashFeed_MatchList *
DiabHashFeedParse(MemPool **pool, char *hashconfig)
{
    char *ptr;
    HashFeed_MatchList *new, *cfgnext = NULL;

    for (;;) {
	if (! hashconfig || ! *hashconfig) {
	    return(cfgnext);
	}

	if ((ptr = strchr(hashconfig, ','))) {
	    *ptr = '\0';
	}
	if (! ((new = zalloc(pool, sizeof(HashFeed_MatchList))))) {
	    return(NULL);
	}
	HM_ConfigNode_Sub(new, cfgnext, hashconfig);
	cfgnext = new;

	if (ptr) {
	    *ptr++ = ',';
	}
        hashconfig = ptr;
    }
}

int
MoveFile(char *from, char *to)
{
    int from_fd;
    int to_fd;
    static char buffer[32768];
    int rlen;

    if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
	printf("ERROR: Unable to open input file: %s\n", from);
	return(-1);
    }
    if ((to_fd = open(to, O_CREAT | O_TRUNC | O_WRONLY, 0644)) < 0) {
	printf("ERROR: Unable to create output file: %s\n", to);
	return(-1);
    }
    while ((rlen = read(from_fd, buffer, 32768)) > 0)
	if (write(to_fd, buffer, rlen) != rlen) {
	    printf("ERROR: Unable to writing to file: %s\n", to);
	    close(from_fd);
	    close(to_fd);
	    unlink(to);
	    return(-1);
	}
    if (rlen < 0) {
	printf("ERROR: Unable to read from file: %s\n", from);
	close(from_fd);
	close(to_fd);
	unlink(to);
	return(-1);
    }
    close(from_fd);
    if (close(to_fd) != 0) {
	printf("ERROR: Unable to close file: %s\n", to);
	unlink(to);
	return(-1);
    }
    unlink(from);
    return(0);
}

/*
 * Convert a string time specification in days, hours, minutes or seconds
 * into a integer of seconds.
 */
int
TimeSpec(char *t, char *def)
{
    char *endp;
    int tt;

    tt = strtol(t, &endp, 0);
    if (endp == NULL || !*endp)
	endp = def;
    switch (*endp) {
	case 's':
	    break;
	case 'm':
	    tt *= 60;
	    break;
	case 'h':
	    tt *= 60 * 60;
	    break;
	case 'd':
	    tt *= 24 * 60 * 60;
	    break;
	default:
	    fprintf(stderr, "Invalid time specification: %s \n", t);
	    tt = -1;
    }
    return(tt);
}
