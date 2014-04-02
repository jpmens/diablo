/*
 * DREADERD/DNS.C - dns resolution and authentication task
 *
 *	DNS authenticator for new connections.
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

#ifdef LDAP_ENABLED
#define LDAP_COMPARE
#include <lber.h>
#include <ldap.h>
#endif
#ifdef DB_ENABLED
#  ifdef __linux__
#    include <db_185.h>
#  else
#    include <db.h>
#  endif
#endif
#ifdef PERL_ENABLED
#include <EXTERN.h>
#include <perl.h>
#endif

Prototype void DnsTask(int fd, const char *id);
Prototype void InstallAccessCache(void);
Prototype int ReadAccessCache(void);
Prototype void UpdateAuthDetails(ForkDesc *desc);
Prototype void SetAuthDetails(DnsRes *dres, char *which);
Prototype void PrintAuthDetails(DnsRes *dres);
Prototype void ClearOldAccessMap(void);

void DnsTest(DnsReq *dreq, DnsRes *dres, char *fqdn, char **ary, const char *ipname);
void getAuthUser(const struct sockaddr *lsin, const struct sockaddr *rsin, char *ubuf, int ulen, int timeout);
int readExactly(int fd, void *buf, int bytes);
void sigSegVDNS(int sigNo);
int readtoeof(int fd, void *buf, int bytes, int secs);
int readAccessLine(FILE *af, char *fname, AccessDef *adef);
int xread(int fd, char *buf, int siz);
void getRateLimitCmds(char *opt, ReaderDef *rd);
void sigHupDns(int SigNo);

/*
 * The various authentication methods
 *
 * The current interface to the authentication methods is the passing
 * of 3 strings as the parms: user, passwd, conf string from authdef
 * Returns a string:
 *	NULL = failed
 *	100 Success - use readerdef from access file
 *	110 readerdef = Use this readerdef
 */
char *fileAuthenticate(char *user, char *pass, char *conf);
#ifdef RADIUS_ENABLED
char *radiusAuthenticate(char *user, char *pass, char *conf);
#endif
#ifdef CDB_ENABLED
extern int cdb_seek(int, char*, unsigned int, unsigned int*);
char *cdbAuthenticate(char *user, char *pass, char *conf);
#endif
#ifdef DB_ENABLED
char *dbAuthenticate(char *user, char *pass, char *conf);
#endif
#ifdef NETREMOTE_ENABLED
void netRemoteAccounting(DnsReq *dreq);
char *netRemoteAuthenticate(char *user, char *pass, char *conf, char *localip, char *remoteip);
#endif
#ifdef LDAP_ENABLED
#ifdef NEW_LDAP
char * LDAPAuthenticate(char *user, char *pass, char *realm, char *conf);
#else
char * LDAPAuthenticate(char *user, char *pass, char *conf);
#endif
#endif  /* LDAP_ENABLED */
#ifdef PERL_ENABLED
char * PerlAuthenticate(char *user, char *pass, char *conf);
#endif
#ifdef PAM_ENABLED
#include <security/pam_appl.h>
char *PamAuthenticate(char *user, char *pass, char *conf);
#endif

int DFd = -1;
MemPool	*DnsMemPool;

time_t Access_LastUpdate = 0;
time_t Cache_LastUpdate = 0;

AccessMap accessmap;
AccessMap oldaccessmap;

int DnsTerminate = 0;

void
sigHupDns(int SigNo)
{
    DnsTerminate++;
}

/* -------------------------------------------------------------------*/

/*
 * This is the process that loops handling DNS and authentication requests
 *
 * It listens in a UNIX SOCKET for a dreq structure and responds with a
 * dres structure.
 *
 */

void
DnsTask(int fd, const char *id)
{
    /*
     * read remote address to resolve.  Note: fd is not set to non-blocking
     */
    DnsReq dreq;
    int n;

    DFd = fd;

    signal(SIGHUP, sigHupDns);

    if (CoreDebugOpt)
        signal(SIGSEGV, sigSegVDNS);

    /*
     * Make dummy call to lock buffer so we do not inefficiently free and
     * reallocate it for every DNS request.
     */
    (void)zalloc(&DnsMemPool, 8);

    
    /*
     * Loop on DNS requests, resolve them, and respond.
     */

    while ((n = readExactly(fd, &dreq, sizeof(dreq))) == sizeof(dreq)) {
	DnsRes dres;

	if (DnsTerminate > 0)
	    break;

	bzero(&dres, sizeof(dres));

	stprintf("reverse auth %s", NetAddrToSt(0, (struct sockaddr *)&dreq.dr_RSin, 1, 0, 1));

	if (ReadAccessCache() == 1)
	    ClearOldAccessMap();
	if (dreq.dr_ResultFlags == DR_SESSEXIT_RPT) {
		/*
		 * This handles session termination reporting, from a
		 * dreaderd NNTP thread.  The idea is to allow one of the
		 * authentication methods to get information upon the
		 * termination of a session.  Currently, most auth methods
		 * do not support this - NetRemote does.  JG200106061320
		 *
		 * Note that this all requires some additional work anyways
		 */
		dres.dr_ResultFlags = DR_SESSEXIT_RPT;
#ifdef	NETREMOTE_ENABLED
		if (*dreq.dr_AuthUser && *dreq.dr_AuthPass) {
		    netRemoteAccounting(&dreq);
		}
#endif
	} else {
		/*
		 * This handles both initial connect and AUTHINFO stuff
		 * by means that should probably be explicitly clarified
		 * at some point.  JG200106061204
		 */

	    char hname[NI_MAXHOST];
	    char ipname[NI_MAXHOST];

	    GetIPHost((struct sockaddr *)&dreq.dr_RSin, hname, sizeof(hname),
						ipname, sizeof(ipname));

	    bcopy(&dreq.dr_RSin, &dres.dr_Addr, sizeof(dres.dr_Addr));

	    /* Do forward DNS doublecheck */
	    if (hname[0] == 0)
		snprintf(dres.dr_Host, sizeof(dres.dr_Host), "%s", ipname);
	    else if (TestForwardLookup(hname, ipname,
					(struct sockaddr *)&dreq.dr_RSin) < 0)
		dres.dr_DnsMismatch = 1;
	    else
		snprintf(dres.dr_Host, sizeof(dres.dr_Host), "%s", hname);

	    /*
	     * call test
	     */
	    if (DebugOpt)
		printf("call test, DnsTest %s (%s)\n", ipname, hname);
	    if (hname[0])
		DnsTest(&dreq, &dres, hname, NULL, ipname);
	    else
		DnsTest(&dreq, &dres, NULL, NULL, ipname);
	}
	write(fd, &dres, sizeof(dres));
	stprintf("dns idle");
    }
    logit(LOG_NOTICE, "DNS process exiting n=%d/%d", n, sizeof(dreq));
}

/*
 * Search a file of username:password pairs for a matching username/passord
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 *
 */
char *
fileAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    char *r = NULL;
    FILE *fp;
    char buffer[1024];
    char *bptr;
    char *ptr;
    int negate = 0;

    if (*conf == '!') {
	negate = 1;
	conf++;
    }
    if (! ((fp = fopen(conf, "r")))) {
	logit(LOG_ERR, "unable to open auth file %s", conf);
	return(NULL);
    }
    while (!feof(fp)) {
	fgets(buffer, sizeof(buffer), fp);
	if ((ptr = strrchr(buffer, '\n'))) {
	    *ptr = '\0';
	}
	bptr = buffer;
	ptr = strsep(&bptr, ":");
	if (ptr && ! strcmp(ptr, user)) {
	    if (negate)
		return(NULL);
	    ptr = strsep(&bptr, ":");
	    if (ptr && !strcmp(ptr, pass)) {
		ptr = strsep(&bptr, ":");
		if (ptr)
		    snprintf(result, sizeof(result), "110 %s", ptr);
		else
		    strcpy(result, "100 Success");
		r = result;
	    }
	    break;
	}
    }
    fclose(fp);
    if (r == NULL && negate) {
	strcpy(result, "100 Success");
	r = result;
    }
    return(r);
}

#ifdef		RADIUS_ENABLED

/*
 * Send an authenticate request to a radius server
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 */
char *
radiusAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    char *r = NULL;
    struct rad_handle *h;
    int rval;

    if (! ((h = rad_open()))) {
	logit(LOG_ERR, "unable to rad_auth_open");
	return(NULL);
    }
    if (rad_config(h, conf) < 0) {
	logit(LOG_ERR, "rad_config(%s) failed: %s", conf, rad_strerror(h));
	rad_close(h);
	return(NULL);
    }
    if (rad_create_request(h, RAD_ACCESS_REQUEST) < 0) {
	logit(LOG_ERR, "rad_create_request failed: %s", rad_strerror(h));
	rad_close(h);
	return(NULL);
    }
    if (rad_put_string(h, RAD_USER_NAME, user) < 0) {
	logit(LOG_ERR, "rad_put_string(RAD_USER_NAME) failed: %s", rad_strerror(h));
	rad_close(h);
	return(NULL);
    }
    if (rad_put_string(h, RAD_USER_PASSWORD, pass) < 0) {
	logit(LOG_ERR, "rad_put_string(RAD_USER_PASSWORD) failed: %s", rad_strerror(h));
	rad_close(h);
	return(NULL);
    }
    if (((rval = rad_send_request(h))) < 0) {
	logit(LOG_ERR, "rad_send_request failed: %s", rad_strerror(h));
	rad_close(h);
	return(NULL);
    }
    rad_close(h);

    if (rval == RAD_ACCESS_ACCEPT) {
	strcpy(result, "100 Success");
	r = result;
    } else
	r = NULL;
    return(r);
}
#endif

#ifdef CDB_ENABLED

/*
 * Search a CDB database for a matching username/passord
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 *
 */
char *
cdbAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    char *r = NULL;
    unsigned int pwlen = 0;
    int fd;
    int negate = 0;

    if (*conf == '!')
	negate = 1;
    fd = open(conf, O_RDONLY);
    if (fd < 0) {
	logit(LOG_ERR, "unable to open auth cdb %s", conf);
	return(NULL);
    }
    if (cdb_seek(fd, user, strlen(user), &pwlen) > 0) {
	if (negate)
	    return(NULL);
	if (pwlen > 0) {
	    char pw[64];
	    if (pwlen > sizeof(pw) - 1) pwlen = sizeof(pw) - 1;
	    if (read(fd, pw, pwlen) == pwlen) {
		pw[pwlen] = 0;
		if (!strcmp(pw, pass)) {
		    strcpy(result, "100 Success");
		    r = result;
		}
	    }
	}
    }
    close(fd);
    if (r == NULL && negate) {
	strcpy(result, "100 Success");
	r = result;
    }
    return(r);
}

#endif /* CDB_ENABLED */

#ifdef DB_ENABLED

/*
 * Search a DB database for a matching username/passord
 *
 * Returns:	NULL	failure
 *		"100 Sucess"	success
 *
 */
char *
dbAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    char *r = NULL;
    DB *db = NULL;
    DBT key;
    DBT data;
    int negate = 0;
    int dbtype = DB_BTREE;

    if (*conf == '!') {
	negate = 1;
	conf++;
    }
    if (strncmp(conf, "hash_", 5) == 0) {
	dbtype = DB_HASH;
	conf += 5;
    } else if (strncmp(conf, "btree_", 6) == 0) {
	dbtype = DB_BTREE;
	conf += 6;
    } else if (strncmp(conf, "recno_", 6) == 0) {
	dbtype = DB_RECNO;
	conf += 6;
    }
    db = dbopen(conf, O_RDONLY, 0, dbtype, NULL);
    if (db == NULL) {
	logit(LOG_ERR, "unable to open auth db %s (%s)", conf, strerror(errno));
	return(NULL);
    }
    key.data = user;
    key.size = strlen(user);
    if (db->get(db, &key, &data, 0) == 0) {
	char *p = data.data;
	int i = 0;

	if (negate)
	    return(NULL);
	if (*pass) {
	    if (strcmp(pass, p) != 0)
		return(NULL);
	    while (i < data.size && *p) {
		p++;
		i++;
	    }
	    if (!*p && i < data.size)
		p++;
	    i++;
	    i = data.size - i;
	} else {
	    i = data.size;
	}
	if (i > 0 && *p) {
	    if (i > sizeof(result) - 5)
		i = sizeof(result) - 5;
	    snprintf(result, i + 5, "110 %s", p);
	} else {
	    strcpy(result, "100 Success");
	}
	r = result;
    } else if (negate) {
	strcpy(result, "100 Success");
	r = result;
    }
    db->close(db);
    return(r);
}

#endif /* DB_ENABLED */

#ifdef		NETREMOTE_ENABLED

/*
 * Send an authenticate request to a NetRemote server
 *
 * This is Yet Another Famous Joe Greco Simple Stupid Protocol
 *
 * My intended use was, due to a large number of reader machines that
 * maintained strong firewalls, to be able to centralize authentication
 * operations on a core set of servers so that maintenance would be
 * easier.
 *
 * Protocol is simple.  Server starts by sending the time as a 12-digit
 * integer.  Client responds with a packet length as a 12-digit integer,
 * and a DES-encrypted packet containing a random number, the server-sent 
 * time, the encryption password, username being auth'd, and password 
 * being auth'd.  The server then returns 1-digit integer, 0 or 1, for
 * status.
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 */

/* really needs to be a multiple of 8 for DES encrypt */
#define NETREMOTE_DATASIZE	1432

int xread(fd, buf, siz)
int fd;
char *buf;
int siz;
{
    int rval;
    int chrs = 0;

    while (siz) {
        if ((rval = read(fd, buf, siz)) <= 0) {
            return(rval);
        }
        chrs += rval;
        siz -= rval;
        buf += rval;
    }
    return(chrs);
}

/*
 * This needs a rework.
 *
 * Since we do not record what authenticator gave us access, or
 * the parameters to it, this really stinks.
 *
 * Ideally the whole dns module should be retrofitted a bit to
 * allow future callbacks...
 */

void
netRemoteAccounting(DnsReq *dreq)
{
    char *conf = "Password@accounting.server:8998";
    int fd = -1, port = 8998;
    char buffer[NETREMOTE_DATASIZE], output[NETREMOTE_DATASIZE], *ptr, *optr;
    char confpw[1024], confhn[1024], *colon;
    long theirtime;
    des_cblock key;
    des_key_schedule sched;
    struct sockaddr_in sin;
    struct hostent *hptr;

    snprintf(confpw, sizeof(confpw), "%s", conf);
    if ((ptr = strchr(confpw, '@'))) {
	*ptr = '\0';
	snprintf(confhn, sizeof(confhn), "%s", ptr + 1);
    } else {
	snprintf(confpw, sizeof(confpw), "%s", "none");
	snprintf(confhn, sizeof(confhn), "%s", conf);
    }
    if ((colon = strrchr(confhn, ':'))) {
	int bad = 0;

	*colon = '\0';
	ptr = colon + 1;
	if (! *ptr) {
	    bad++;
	}
	while (*ptr) {
	    if (! isdigit(*ptr)) {
		bad++;
	    }
	    ptr++;
	}
	if (! bad) {
	    port = atoi(colon + 1);
	} else {
	    logit(LOG_ERR, "Invalid NetRemote port %s", colon + 1);
	}
    }

    /* attach to remote server(s) */
    if (isdigit(*confhn)) {
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            logit(LOG_ERR, "NetRemote socket create: %m");
	    return;
        }
        bzero((void *)&sin, sizeof(&sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons(0);

        if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    close(fd);
            logit(LOG_ERR, "NetRemote socket bind: %m");
	    return;
        }

        bzero((void *)&sin, sizeof(&sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(confhn);

	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    close(fd);
	    logit(LOG_ERR, "NetRemote socket connect: %s(%s:%d): %m", confhn, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	    return;
	}
    } else {
	int i = 0, connected = 0;

	if (! (hptr = gethostbyname(confhn))) {
	    close(fd);
            logit(LOG_ERR, "NetRemote gethostbyname: %s: %m", confhn);
	    return;
	}
	while (! connected) {
	    if (! hptr->h_addr_list[i]) {
		return;
	    }

            if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                logit(LOG_ERR, "NetRemote socket create: %m");
	        return;
            }
            bzero((void *)&sin, sizeof(&sin));
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = INADDR_ANY;
            sin.sin_port = htons(0);

            if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	        close(fd);
                logit(LOG_ERR, "NetRemote socket bind: %m");
	        return;
            }

            bzero((void *)&sin, sizeof(&sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
	    bcopy(hptr->h_addr_list[i], (char *)&sin.sin_addr.s_addr, sizeof(sin.sin_addr.s_addr));

	    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		if (errno != ETIMEDOUT && errno != ECONNREFUSED && errno != ENETUNREACH) {
		    close(fd);
		    logit(LOG_ERR, "NetRemote fatal socket connect: %s: %m", confhn);
		    return;
		}
		close(fd);
	        logit(LOG_ERR, "NetRemote socket connect: %s(%s:%d): %m", confhn, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	        i++;
	    } else {
		connected++;
	    }
	}
    }

    /* Read the server's idea of time.  12 byte int plus \n */
    if (xread(fd, buffer, 13) != 13) {
        close(fd);
	return;
    }

    theirtime = atol(buffer);

    des_string_to_key(confpw, &key);
    des_set_key(&key, sched);

    bzero(buffer, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%ld\n%ld\n%s\n%c\n%s\n%.0f\n%d\n%d\n%.0f\n%d\n%d\n%.0f\n%.0f\n%.0f\n%.0f\n%.0f\n%.0f\n%.0f\n%d\n%s\n%s\n", random(), theirtime, confpw, '2',
	dreq->dr_AuthUser,
	dreq->dr_ByteCount,
	dreq->dr_GrpCount,
	dreq->dr_ArtCount,
	dreq->dr_PostBytes,
	dreq->dr_PostCount,
	dreq->dr_PostFailCount,
	dreq->dr_ByteCountArticle,
	dreq->dr_ByteCountHead,
	dreq->dr_ByteCountBody,
	dreq->dr_ByteCountList,
	dreq->dr_ByteCountXover,
	dreq->dr_ByteCountXhdr,
	dreq->dr_ByteCountOther,
	(int)dreq->dr_SessionLength,
	NetAddrToSt(0, (struct sockaddr *)&dreq->dr_LSin, 1, 0, 1),
	NetAddrToSt(0, (struct sockaddr *)&dreq->dr_RSin, 1, 0, 1));
    ptr = buffer;
    optr = output;
    while (ptr < buffer + sizeof(buffer)) {
	des_ecb_encrypt((des_cblock *)ptr,(des_cblock *)optr, sched, 1);
	bzero(ptr, 8);
	ptr += 8;
	optr += 8;
    }

    /* Send the encrypted request, prefixed by size of packet */
    snprintf(buffer, sizeof(buffer), "%012d\n", sizeof(output));
    write(fd, buffer, strlen(buffer));
    write(fd, output, sizeof(output));

    /* No response to read */
    close(fd);

    return;
}

char *
netRemoteAuthenticate(char *user, char *pass, char *conf, char *localip, char *remoteip)
{
    static char result[DEFNAMELEN + 16];
    char *r = NULL;
    int fd = -1, port = 8998;
    char buffer[NETREMOTE_DATASIZE], output[NETREMOTE_DATASIZE], *ptr, *optr;
    char confpw[1024], confhn[1024], *colon;
    long theirtime;
    des_cblock key;
    des_key_schedule sched;
    struct sockaddr_in sin;
    struct hostent *hptr;

    snprintf(confpw, sizeof(confpw), "%s", conf);
    if ((ptr = strchr(confpw, '@'))) {
	*ptr = '\0';
	snprintf(confhn, sizeof(confhn), "%s", ptr + 1);
    } else {
	snprintf(confpw, sizeof(confpw), "%s", "none");
	snprintf(confhn, sizeof(confhn), "%s", conf);
    }
    if ((colon = strrchr(confhn, ':'))) {
	int bad = 0;

	*colon = '\0';
	ptr = colon + 1;
	if (! *ptr) {
	    bad++;
	}
	while (*ptr) {
	    if (! isdigit(*ptr)) {
		bad++;
	    }
	    ptr++;
	}
	if (! bad) {
	    port = atoi(colon + 1);
	} else {
	    logit(LOG_ERR, "Invalid NetRemote port %s", colon + 1);
	}
    }

    /* attach to remote server(s) */
    if (isdigit(*confhn)) {
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            logit(LOG_ERR, "NetRemote socket create: %m");
	    return(NULL);
        }
        bzero((void *)&sin, sizeof(&sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons(0);

        if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    close(fd);
            logit(LOG_ERR, "NetRemote socket bind: %m");
	    return(NULL);
        }

        bzero((void *)&sin, sizeof(&sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(confhn);

	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    close(fd);
	    logit(LOG_ERR, "NetRemote socket connect: %s(%s:%d): %m", confhn, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	    return(NULL);
	}
    } else {
	int i = 0, connected = 0;

	if (! (hptr = gethostbyname(confhn))) {
	    close(fd);
            logit(LOG_ERR, "NetRemote gethostbyname: %s: %m", confhn);
	    return(NULL);
	}
	while (! connected) {
	    if (! hptr->h_addr_list[i]) {
		return(NULL);
	    }

            if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                logit(LOG_ERR, "NetRemote socket create: %m");
	        return(NULL);
            }
            bzero((void *)&sin, sizeof(&sin));
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = INADDR_ANY;
            sin.sin_port = htons(0);

            if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	        close(fd);
                logit(LOG_ERR, "NetRemote socket bind: %m");
	        return(NULL);
            }

            bzero((void *)&sin, sizeof(&sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
	    bcopy(hptr->h_addr_list[i], (char *)&sin.sin_addr.s_addr, sizeof(sin.sin_addr.s_addr));

	    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		if (errno != ETIMEDOUT && errno != ECONNREFUSED && errno != ENETUNREACH) {
		    close(fd);
		    logit(LOG_ERR, "NetRemote fatal socket connect: %s: %m", confhn);
		    return(NULL);
		}
		close(fd);
	        logit(LOG_ERR, "NetRemote socket connect: %s(%s:%d): %m", confhn, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	        i++;
	    } else {
		connected++;
	    }
	}
    }

    /* Read the server's idea of time.  12 byte int plus \n */
    if (xread(fd, buffer, 13) != 13) {
        close(fd);
	return(NULL);
    }

    theirtime = atol(buffer);

    des_string_to_key(confpw, &key);
    des_set_key(&key, sched);

    bzero(buffer, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%ld\n%ld\n%s\n%c\n%s\n%s\n%s\n%s\n", random(), theirtime, confpw, '1', user, pass, localip, remoteip);
    ptr = buffer;
    optr = output;
    while (ptr < buffer + sizeof(buffer)) {
	des_ecb_encrypt((des_cblock *)ptr,(des_cblock *)optr, sched, 1);
	bzero(ptr, 8);
	ptr += 8;
	optr += 8;
    }

    /* Send the encrypted request, prefixed by size of packet */
    snprintf(buffer, sizeof(buffer), "%012d\n", sizeof(output));
    write(fd, buffer, strlen(buffer));
    write(fd, output, sizeof(output));

    /* Read the server's response.  1 byte int plus \n */
    if (read(fd, buffer, sizeof(buffer)) < 2) {
        close(fd);
	return(NULL);
    }
    close(fd);

    if (*buffer == '1') {
	snprintf(result, sizeof(result), "%s", buffer);
	if ((r = strchr(result, '\n'))) {
		*r = '\0';
	}
	r = result;
    } else
	r = NULL;
    return(r);
}
#endif

#ifdef		LDAP_ENABLED

#ifdef NEW_LDAP
/*
 * Send an authenticate request to an LDAP server
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 *
 * Configuration is done via an LDAP URL of the form:
 *
 * ldap://hostname[:port]/dn?userPassword?scope?filter [username:passwd]
 *
 * where:
 *	userPassword is the attribute that contains the clear text password,
 *	usually called "userPassword".
 *
 *	scope is probably normally "sub" or "base".
 *
 * 	filter consists of the username and password attributes and any
 *	other site specific search options required to narrow a search
 *	to a single user. The following values can be used and will
 *	be replaced by the corresponding value supplied from Diablo.
 *
 *		$USER$		username
 *		$PASS$		password
 *		$REALM$		authentication domain, if realms are used
 *
 *	username:passwd is the username and password with which to
 *	authenticate to the LDAP server. If they are not specified,
 *	anonymous authentication is done.
 *
 * Example:
 *
 * ldap://host.example.com/dc=$REALM$,dn=example.com?userPassword?sub?(userName=$USER$)
 *
 */
char *
LDAPAuthenticate(char *user, char *pass, char *realm, char *conf)
{
    static LDAP *ld = NULL;
    char *result = NULL;
    char url[1024];
    LDAPURLDesc *ludp;
    char *confst = NULL;
    char *r;
    char *p;
    LDAPMessage *res;
    LDAPMessage *m;
    int msgid;
    int rcode;
    char *ldapuser = NULL;
    char *ldappass = NULL;

    /*
     * Parse the configuration options to extract the URL and the
     * LDAP auth options
     */
    r = strdup(conf);
    confst = strsep(&r, " \t");
    if (confst != NULL) {
	p = r;
	while ((p = strsep(&r, " \t")) != NULL) {
	    ldapuser = p;
	    p = strchr(p, ':');
	    if (p != NULL) {
		   *p++ = 0;
		   ldappass = p;
	    }
	}
    } else {
	confst = r;
    }
    if (ld != NULL && (confst == NULL || strcmp(confst, conf) != 0)) {
	ldap_unbind_s(ld);
	ld = NULL;
    }

    /*
     * Make a new connection to the LDAP server if one isn't already there
     */
    if (ld == NULL) {
	if (ldap_is_ldap_url(confst) == 0) {
	    logit(LOG_ERR, "Not an LDAP URL: %s\n", conf);
	    free(confst);
	    return(NULL);
	}

	if (ldap_url_parse(confst, &ludp) != 0) {
	    logit(LOG_ERR,"Unable to parse LDAP URL: %s\n", confst);
	    free(confst);
	    return(NULL);
	}

	/* Open LDAP connection */
	if ((ld = ldap_init(ludp->lud_host, ludp->lud_port)) == NULL) {
	    /* Couldn't contact server - fail authentication attempt */
	    logit(LOG_ERR,"Unable to connect to LDAP server: %s\n", confst);
	    free(confst);
	    ldap_free_urldesc(ludp);
	    return(NULL);
	}

        ldap_free_urldesc(ludp);

	if ((msgid = ldap_bind_s(ld, ldapuser, ldappass, LDAP_AUTH_SIMPLE)) !=
								LDAP_SUCCESS) {
	    /* Bind failed - fail auth attempt */
	    logit(LOG_ERR,"LDAP bind failed: %s\n", confst);
	    free(confst);
	    ldap_unbind_s(ld);
	    ld = NULL;
	    return(NULL);
	}
	if (DebugOpt)
	    printf("Connection to LDAP server established\n");
    }

    /*
     * Setup the search parameters and substitute the user, pass and domain
     * fields into the search URL
     */
    {
	char *src = confst;
	char *dst = url;

	while (*src) {
	    if (user && strncmp(src, "$USER$", 6) == 0) {
		src += 6;
		strcpy(dst, user);
		dst += strlen(user);
	    } else if  (pass && strncmp(src, "$PASSWD$", 8) == 0) {
		src += 8;
		strcpy(dst, pass);
		dst += strlen(pass);
	    } else if  (realm && strncmp(src, "$REALM$", 7) == 0) {
		src += 7;
		strcpy(dst, realm);
		dst += strlen(realm);
	    } else {
		*dst++ = *src++;
	    }
	}
	*dst = 0;
	if (DebugOpt)
	    printf("URL: %s\n", url);
    }

    if (ldap_url_parse(url, &ludp) != 0) {
	logit(LOG_ERR,"Unable to parse LDAP URL: %s\n", url);
        free(confst);
	return(NULL);
    }
    
    /*
     * Perform the search
     */
    if ((rcode = ldap_search_s(ld, ludp->lud_dn, ludp->lud_scope, ludp->lud_filter, ludp->lud_attrs, 0, &res)) != LDAP_SUCCESS) {
	logit(LOG_ERR,"LDAP search for %s failed: %s\n", url, ldap_err2string(rcode));
	free(confst);
	ldap_unbind_s(ld);
	ldap_free_urldesc(ludp);
	ld = NULL;
	return(NULL);
    }

    ldap_free_urldesc(ludp);

    /*
     * Parse the results.
     */
    if (DebugOpt)
	printf("Results:\n");
    for (m = ldap_first_entry(ld, res); m != NULL;
					m = ldap_next_entry(ld, m)) {
	BerElement *ber;
	char *attr;
	int i;
	
	for (attr = ldap_first_attribute(ld, m, &ber); attr != NULL;
				attr = ldap_next_attribute(ld, m, ber)) {
	    char **vals;

	    if (DebugOpt)
		printf("attr: %s\n", attr);
	    if ((vals = ldap_get_values(ld, m, attr)) != NULL) {
		for (i = 0; vals[i] != NULL; ++i) {
		    if (strcmp(pass,vals[i])==0) result="100 Success";
		    if (DebugOpt)
			printf("   val: %s\n", vals[i]);
		}
		ldap_value_free(vals);
	    }
	}
    }
    ldap_msgfree(res);
#ifdef LDAP_DO_UNBIND
    ldap_unbind_s(ld);
    ld = NULL;
#endif
    free(confst);
    return(result);
}
#else
/*
 * Send an authenticate request to an LDAP server
 *
 * Returns:	NULL	failure
 *		 "100 Sucess"	success
 */
char *
LDAPAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    char *r;
    static LDAP *ld = NULL;
    static LDAPURLDesc *ludp;
    LDAPMessage *res;
    LDAPMessage *mptr;
    char *ldappassattr = "userpassword";
    int msgid;
    int rcode;
    char *filter;
#ifndef LDAP_COMPARE
    char **bv_val;
#else
    char *dn;
#endif

    if (ld == NULL) {
	if (ldap_is_ldap_url(conf) == 0) {
	    logit(LOG_ERR, "No LDAP URL: %s\n", conf);
	    ld = NULL;
	    return(NULL);
	}

	if (ldap_url_parse(conf, &ludp) != 0) {
	    logit(LOG_ERR, "Unable to parse LDAP URL: %s\n", conf);
	    ldap_free_urldesc(ludp);
	    ld = NULL;
	    return(NULL);
	}

	/* Open LDAP connection */
	if ((ld = ldap_init(ludp->lud_host, ludp->lud_port)) == NULL) {
	    /* Couldn't contact server - fail authentication attempt */
	    logit(LOG_ERR, "Unable to connect to LDAP server: %s\n", conf);
	    ldap_free_urldesc(ludp);
	    ld = NULL;
	    return(NULL);
	}

	if ((msgid = ldap_bind_s(ld, NULL, NULL, LDAP_AUTH_SIMPLE)) !=
								LDAP_SUCCESS) {
	    /* Bind failed - fail auth attempt */
	    logit(LOG_ERR, "LDAP bind failed: %s\n", conf);
	    ldap_free_urldesc(ludp);
	    ld = NULL;
	    return(NULL);
	}
	if (DebugOpt)
	    printf("Connection to LDAP server established\n");
    }

    /* Build the filter string and attributes */
    if ((filter = (char *)malloc(strlen(ludp->lud_filter) + strlen(user) + 1)) == NULL) {
	/* Malloc failed - fail auth attempt */
	logit(LOG_ERR, "LDAP malloc failed: %s\n", conf);
	ldap_unbind_s(ld);
	ldap_free_urldesc(ludp);
	ld = NULL;
	return(NULL);
    }
    sprintf(filter, ludp->lud_filter, user);

    /* Perform the search */
    if ((rcode = ldap_search_s(ld, ludp->lud_dn, ludp->lud_scope, filter, ludp->lud_attrs, 0, &res)) != LDAP_SUCCESS) {
	/* Search failed - fail auth attempt */
	logit(LOG_INFO, "LDAP search failed (%s): %s\n",
						user, ldap_err2string(rcode));
	free(filter);
	ldap_unbind_s(ld);
	ldap_free_urldesc(ludp);
	ld = NULL;
	return(NULL);
    }

    /* See what we got back - we only care about the first response */
    if ((mptr = ldap_first_entry(ld, res)) == NULL) {
	/* Error - fail auth attempt */
	logit(LOG_INFO, "LDAP auth failed: %s\n", user);
	free(filter);
	ldap_unbind_s(ld);
	ldap_free_urldesc(ludp);
	return(NULL);
    }

    r = NULL;
#ifdef LDAP_COMPARE
    dn = ldap_get_dn(ld, mptr);
    if (ldap_compare_s(ld, dn, ldappassattr, pass) == LDAP_COMPARE_TRUE) {
	strcpy(result, "100 Success");
	r = result;
    } else {
	logit(LOG_INFO, "LDAP auth failed: %s\n", user);
	r = NULL;
    }
    free(dn);
#else
    /* Read the password and compare it */
    bv_val = ldap_get_values(ld, mptr, ldappassattr);
    if (ldap_count_values(bv_val) == 0) {
	/* No password value returned - fail auth attempt */
	logit(LOG_INFO, "LDAP auth failed: %s\n", user);
	r = NULL;
	ldap_value_free(bv_val);
    }

    if (strcmp(bv_val[0], pass) == 0) {
	strcpy(result, "100 Success");
	r = result;
    } else {
	r = NULL;
    }
#endif
    return(r);
}
#endif	/* NEW_LDAP */
#endif	/* LDAP_ENABLED */

#ifdef PERL_ENABLED

EXTERN_C void xs_init (pTHXo);
EXTERN_C void boot_DynaLoader (pTHXo_ CV* cv);

EXTERN_C void xs_init(pTHXo)
{
    char *file = __FILE__;
    dXSUB_SYS;

    /* DynaLoader is a special case */
    newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
}
char *call_checkuser(char *auth_args[])
{
    static char buff[DEFNAMELEN + 16];

    dSP;
    SV *sv;
    char *str;
    STRLEN len;
    char *result = NULL;

    ENTER;
    SAVETMPS;

    call_argv("checkuser", G_SCALAR, auth_args);

    SPAGAIN;

    sv = POPs;
    if (SvTRUE(sv) && (str = SvPV(sv, len)) && len < DEFNAMELEN + 16) 
        result = strcpy(buff, str);

    FREETMPS;
    LEAVE;
    return result;
}

char *PerlAuthenticate(char *user, char *pass, char *conf)
{
    char *embedding[] = { "", conf };
    char *auth_args[] = { user, pass, NULL };
    char *result;
    char *reslog;

    static PerlInterpreter *perl = NULL;
    int exitstatus = 0;

    if (perl == NULL) {
        if((perl = perl_alloc()) == NULL) {
            fprintf(stderr, "perl_alloc no memory\n");
            exit(1);
        }
        PL_perl_destruct_level = 0;
        perl_construct(perl);
        exitstatus = perl_parse(perl, xs_init, 2, embedding, NULL);
        if (!exitstatus) {
            exitstatus = perl_run(perl);
        }
        if (exitstatus) {
            fprintf(stderr, "perl_parse or perl_run failed\n");
            perl_destruct(perl);
            perl_free(perl);
            exit(exitstatus);
        }
    }
    reslog = result = call_checkuser(auth_args);
    if (reslog == NULL) 
	reslog = "FAILED";
    logit(LOG_INFO, "PERL auth request for %s: %s\n", user, reslog);
    return result;
}
#endif	/* PERL_ENABLED */

#ifdef PAM_ENABLED
static char *PAM_user;
static char *PAM_password;
static int PAM_error = 0;

static int PAM_conv(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata)
{
    int count;
    int replies = 0;
    struct pam_response *reply = NULL;
    
    for (count = 0; count < num_msg; count++) {
	switch (msg[count]->msg_style) {
	    case PAM_PROMPT_ECHO_ON:
		reply = (struct pam_response *)realloc(reply,
						sizeof(struct pam_response));
		reply[replies].resp_retcode = PAM_SUCCESS;
		reply[replies++].resp = strdup(PAM_user);
		break;
	    case PAM_PROMPT_ECHO_OFF:
		reply = (struct pam_response *)realloc(reply,
						sizeof(struct pam_response));
		reply[replies].resp_retcode = PAM_SUCCESS;
		reply[replies++].resp = strdup(PAM_password);
		break;
	    case PAM_TEXT_INFO:
		break;
	    case PAM_ERROR_MSG:
		default:
		PAM_error = 1;
		if (reply != NULL) {
		    free(reply);
		    reply = NULL;
		}
		return PAM_CONV_ERR;
      }
    }
    if (reply != NULL)
	*resp = reply;
    return PAM_SUCCESS;
}

static struct pam_conv conv = {
    PAM_conv, NULL
};

char *PamAuthenticate(char *user, char *pass, char *conf)
{
    static char result[DEFNAMELEN + 16];
    pam_handle_t *pamh = NULL;
    int retval;
    char *r = NULL;
    
    PAM_user = user;
    PAM_password = pass;

    retval = pam_start(conf, PAM_user, &conv, &pamh);

    if (retval == PAM_SUCCESS)
	retval = pam_authenticate(pamh, 0);

    if (retval == PAM_SUCCESS) {
	strcpy(result, "100 Success");
	r = result;
    }

    if (pam_end(pamh, retval) != PAM_SUCCESS) {
	pamh = NULL;
	logit(LOG_CRIT, "Unable to release PAM authentication\n");
    }

    return(r);
}
#endif /* PAM_ENABLE */

/*
 * DnsTest() - test access file entry against host.
 *
 *	fqdn	reverse lookup of name, if known, or NULL
 *	ary	aliases, if known, or NULL
 *	ipname	name of host as dotted quad
 */

void DnsTest(DnsReq *dreq, DnsRes *dres, char *fqdn, char **ary, const char *ipname)
{
    int AuthNeeded;
    int MatchedAuthNeeded = 0;
    int Authenticated = 0;
    int MatchedAuthenticated = 0;
    int MatchType;
    char *AuthReader = NULL;
    Vserver *vsp = NULL;
    AccessDef *alp;
    AccessDef *matchedalp = NULL;
    AccessDef accessdef;
    AccessDef matchedaccessdef;
    int acount;
    int DoneIdent = 0;
    int accessok;
    int accessfile;
    int firstmatch;
    FILE *af = NULL;
    char *realm = NULL;

    /* This got trashed, so reset it */
    if (*dreq->dr_AuthUser) {
	dres->dr_ResultFlags = DR_REQUIRE_DNS;
	strcpy(dres->dr_AuthUser, dreq->dr_AuthUser);
	strcpy(dres->dr_AuthPass, dreq->dr_AuthPass);
    }

	/* XXX Doesn't work after INET6 addition!! */
    for (vsp = accessmap.VServerList, acount = 0;
		acount < accessmap.VServerCount; acount++, vsp++) {
#ifdef INET6
	struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&dreq->dr_LSin;
	struct sockaddr_in6 *vsa = (struct sockaddr_in6 *)&vsp->vs_Interface;
	if (memcmp(&sa->sin6_addr, &vsa->sin6_addr,
				sizeof(sa->sin6_addr))  == 0) {
#else
	struct sockaddr_in *sa = (struct sockaddr_in *)&dreq->dr_LSin;
	struct sockaddr_in *vsa = (struct sockaddr_in *)&vsp->vs_Interface;
	if (sa->sin_addr.s_addr == vsa->sin_addr.s_addr) {
#endif
	    strcpy(dres->dr_VServer, vsp->vs_Name);
	    dres->dr_VServerDef = vsp;
            break;
	}
    }

    accessok = 1;
    accessfile = 0;

    /*
     * Not sure if this is still needed as we only ever call DnsTest()
     * from DnsTask(), which hopefully sets this up correctly.
     */
    if (!ipname)
	if (fqdn && IsIpAddr(fqdn))
	    ipname = fqdn;

    /*
     * For logging purposes, put something here
     */
    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", ipname);

    /*
     * If we have specified a separate access file for a vserver,
     * then load it into memory for scanning.
     */
    if (*dres->dr_VServer && *dres->dr_VServerDef->vs_AccessFile) {
	accessfile = 1;
	strcpy(accessdef.ad_Reader, "DEFAULT");
	if (DebugOpt)
	    printf("Using accessfile: %s\n",
		PatLibExpand(dres->dr_VServerDef->vs_AccessFile));
	if ((af = fopen(PatLibExpand(dres->dr_VServerDef->vs_AccessFile), "r")) == NULL) {
	    logit(LOG_CRIT, "Unable to open %s",	
		PatLibExpand(dres->dr_VServerDef->vs_AccessFile));
	    accessok = 0;
	}
    }

    /*
     * Go through interations of the access list or read a vserver
     * access file and look for matches. Last match wins.
     *
     * We use the following logic for a match:
     *  - if no match -> fail
     *  - if last match with no auth required -> pass
     *  - if last match with auth required (and no auth details) and no
     *		previous match -> pass (requires auth to do things)
     *  - if last match with auth required (and no auth details) and prev
     *		match -> pass with prev match
     *  - if last match with auth required (with matching auth details) -> pass
     *  - if no auth required and no read,post,feed or status -> fail
     */

    alp = accessmap.AccessList;
    matchedalp = NULL;
    acount = 0;
    firstmatch = 1;

    while (accessok) {

	stprintf("dns auth search %s", ipname);

	if (accessfile == 1) {
	    if (readAccessLine(af,
		    (char *)PatLibExpand(dres->dr_VServerDef->vs_AccessFile),
		    &accessdef) != 1)
		break;
	    alp = &accessdef;
	} else if (accessfile == 2) {
	    if (acount++ > 1)
		break;
	} else {
	    if (!firstmatch)
		alp++;
	    firstmatch = 0;
	    if (acount++ >= accessmap.AccessCount || alp == NULL)
		break;
	}

	/*
	 * First we test the hostname or IP.
	 */

	MatchType = 0;
	if (IsIpAddr(alp->ad_Pattern)) {
	    if (!ipname || !*ipname)
		continue;
	    if (strchr(alp->ad_Pattern, '*') ||
				strchr(alp->ad_Pattern, '?')) {
		/* Wildmatch */
		if (WildCaseCmp(alp->ad_Pattern, ipname) != 0)
		    continue;
	    } else if (strchr(alp->ad_Pattern, '/')) {
		/* CIDR notation */
		if (!CidrMatch(alp->ad_Pattern, ipname))
		    continue;
	    } else {
		    /* Default - exact match */
		if (strcasecmp(ipname, alp->ad_Pattern) != 0)
		    continue;
	    }
	    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", ipname);
	    MatchType = 1;
	} else if (strncmp(alp->ad_Pattern, "db:", 3) == 0) {
	/*
	 * If we have specified a DB for IP address lookup, then do
	 * the lookup.
	 */
#ifdef DB_ENABLED
	    char *conf = alp->ad_Pattern + 3;
	    char *res;
	    if (DebugOpt)
		printf("Using accessdb: %s\n", conf);
	    res = dbAuthenticate((char *)ipname, "", conf);
	    if (DebugOpt)
		printf("accessdb result: %s\n", res ? res : "(NULL)");
	    if (res == NULL || strcmp(res, "110 0") == 0)
		continue;
	    MatchType = 1;
	    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", ipname);
#else
	    logit(LOG_ERR, "db: option in dreader.access not enabled - ignoring");
	    continue;
#endif
	} else {
	    if (!fqdn || !*fqdn)
		continue;
	    if (strchr(alp->ad_Pattern, '*') ||
				strchr(alp->ad_Pattern, '?')) {
		/* Wildmatch */
		if (WildCaseCmp(alp->ad_Pattern, fqdn) != 0)
		    continue;
	    } else {
		/* Default - exact match */
		if (strcasecmp(fqdn, alp->ad_Pattern) != 0)
		    continue;
	    }
	    MatchType = 2;
	    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", fqdn);
	}
	if (*alp->ad_IdentUser) {
	    if (!DoneIdent && !*dres->dr_IdentUser) {
		/*
		 * this isn't an option where the user can actually
		 * enter something to pass the test; it is gained from
		 * identd info.  therefore it is an all-pass or all-fail
		 * type thing.
		 */
		getAuthUser((struct sockaddr *)&dreq->dr_LSin,
			(struct sockaddr *)&dreq->dr_RSin, dres->dr_IdentUser,
			sizeof(dres->dr_IdentUser), DOpts.ReaderIdentTimeout);
		stprintf("dns auth ident %s", ipname);
		stprintf("dns auth search %s", ipname);
		DoneIdent = 1;
	    }
	    /* We don't need to go further if we fail the ident check */
	    if (strcmp(alp->ad_IdentUser, dres->dr_IdentUser) != 0)
		continue;
	}

	/*
	 *  Now me are matching an access line, check some of the
	 *  user details
	 */

	if (DebugOpt)
	    printf("Matched access line: %s%s%s (%s)\n",
			*alp->ad_IdentUser ? alp->ad_IdentUser : "",
			*alp->ad_IdentUser ? "@" : "",
			alp->ad_Pattern,
			alp->ad_Reader);

	SetAuthDetails(dres, alp->ad_Reader);

	/*
	 * Do we log with the hostname
	 */
	if (dres->dr_ReaderDef->rd_UseVerifiedDns && fqdn && *fqdn)
	    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", fqdn);

	/*
	 *  We can optionally deny an entry that doesn't have a DNS entry
	 */
	if (dres->dr_ReaderDef->rd_DenyNoDns && (!fqdn || !*fqdn)) {
	    logit(LOG_ERR, "Denying host with no DNS entry: %s", ipname);
	    continue;
	}

	/*
	 * If we mismatch Fwd/Rev and we matched against a domain
	 * then reject the connection for security reasons
	 */
	if (dres->dr_DnsMismatch && (dres->dr_ReaderDef->rd_DenyMismatchedDns
			 || (dres->dr_DnsMismatch && MatchType != 1))) {
	    if (DebugOpt)
		printf("Not allowing DNS Fwd/Rev Mismatch for: %s matching %s%s%s\n",
			ipname,
			*alp->ad_IdentUser ? alp->ad_IdentUser : "",
			*alp->ad_IdentUser ? "@" : "",
			alp->ad_Pattern);
	    continue;
	}

	/*
	 * We can optionally use the FQDN for logging even when
	 * matched with an IP
	 */
	if (dres->dr_ReaderDef->rd_UseVerifiedDns && !dres->dr_DnsMismatch &&
						fqdn && *fqdn)
	    snprintf(dres->dr_Host, sizeof(dres->dr_Host), "%s", fqdn);
		

	/*
	 * Ok.  Now at this point, we either don't have a match on this
	 * line, in which case we've already skipped to the next line,
	 * or we have a match...  but we may still need to authenticate.
	 */

	Authenticated = 0;
	AuthNeeded = 0;

	if (*dres->dr_AuthDef->au_Radius ||
		*dres->dr_AuthDef->au_LDAP ||
		*dres->dr_AuthDef->au_Perl ||
		*dres->dr_AuthDef->au_PAM ||
		*dres->dr_AuthDef->au_NetRemote ||
		*dres->dr_AuthDef->au_File ||
		*dres->dr_AuthDef->au_Cdb ||
		*dres->dr_AuthDef->au_Db ||
		*dres->dr_AuthDef->au_External ||
		*dres->dr_AuthDef->au_User)
	    AuthNeeded = 1;

	if (dres->dr_AuthDef->au_Ident > 0 && !DoneIdent &&
					!*dres->dr_IdentUser) {
	    /*
	     * this isn't an option where the user can actually
	     * enter something to pass the test; it is gained from
	     * identd info.  therefore it is an all-pass or all-fail
	     * type thing.
	     */
	    getAuthUser((struct sockaddr *)&dreq->dr_LSin,
			(struct sockaddr *)&dreq->dr_RSin, dres->dr_IdentUser,
			sizeof(dres->dr_IdentUser), dres->dr_AuthDef->au_Ident);
	    stprintf("dns auth search %s", ipname);
	    DoneIdent = 1;
	}

	/*
	 * The user has used the AUTHINFO comamnd - check the details
	 * against various authentication mechanisms.
	 *
	 * All methods specified in the auth block are used and any of
	 * them can match
	 */

	if (AuthNeeded) {

	    char user[128];
	    int authok;

	    /*
	     * Add a default realm, if one not already specified
	     */

	    if (*dres->dr_AuthDef->au_AddRealm) {
		realm = strchr(dreq->dr_AuthUser, '@');
		if (*dreq->dr_AuthUser && ! realm) {
		    int offset;

		    offset = strlen(dres->dr_AuthUser);
		    if (offset < sizeof(dres->dr_AuthUser) - 4) {
			snprintf(dres->dr_AuthUser + offset,
				sizeof(dres->dr_AuthUser) - offset,
				"@%s", dres->dr_AuthDef->au_AddRealm);
			snprintf(dreq->dr_AuthUser, sizeof(dreq->dr_AuthUser),
						"%s", dres->dr_AuthUser);
		    }
		}
	    }

	    strncpy(user, dreq->dr_AuthUser, sizeof(user) - 1 );
	    user[sizeof(user) - 1] = '\0';
	    authok = 1;

	    /*
	     * The realm is anything after the '@' in a username
	     * Make sure this matches, otherwise the others won't
	     * match.  Note: this strips the realm
	     */

	    if (*dres->dr_AuthDef->au_Realm) {
		authok = 0;
		realm = strchr(user, '@');
		if (realm != NULL) {
		    *realm++ = '\0';
		    if (strcmp(dres->dr_AuthDef->au_Realm,"*") == 0 ||
				strcmp(dres->dr_AuthDef->au_Realm, realm) == 0)
			authok = 1;
		}
	    }

	    /*
	     * If the user was set in auth and we have Ident, we just compare
	     */

	    if (authok && DoneIdent && *dres->dr_IdentUser &&
			*dres->dr_AuthDef->au_User &&
			!strcmp(dres->dr_IdentUser, dres->dr_AuthDef->au_User))  {
		Authenticated = 2;
		if (DebugOpt)
		    printf("Authenticated via ident+user\n");
	    }


#ifdef REJECT_DB

	    {
		DBT key;
		DBT data;
		DB *db = NULL;

		if((db = dbopen(REJECT_DB, O_RDONLY , 0, DB_BTREE, NULL)) != NULL) {
		    key.data = user;
		    key.size = strlen(user);

		    if((db->get(db, &key, &data, 0) == 0)) {
			logit(LOG_NOTICE, "User %s found in reject db, denying access",user);
			if(DebugOpt) 
				printf("User %s found in reject db, denying access\n",user);
			Authenticated = -1;
			authok = 0;
		    }
		    db->close(db);
		}
	    }
#endif	/* REJECT_DB */

	    /*
	     * Check for the user+pass in a file
	     */

	    if (authok && *dres->dr_AuthDef->au_File && *user &&
			*dreq->dr_AuthPass) {
		stprintf("dns auth file %s@%s", user, ipname);
		AuthReader = fileAuthenticate(user, dreq->dr_AuthPass,
						dres->dr_AuthDef->au_File);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    if (*dres->dr_AuthDef->au_File != '!')
			Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via file (%s)\n", AuthReader);
		} else if (*dres->dr_AuthDef->au_File == '!') {
		    Authenticated = -1;
		    authok = 0;
		    if (DebugOpt)
			printf("Authentication via file REJECTED\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via file FAILED\n");
		}
	    }

#ifdef	RADIUS_ENABLED

	    /*
	     * Check user+pass with radius server
	     */

	    if (authok && *dres->dr_AuthDef->au_Radius && *user &&
				*dreq->dr_AuthPass) {
		stprintf("dns auth radius %s@%s", user, ipname);
		AuthReader = radiusAuthenticate(user, dreq->dr_AuthPass,
			dres->dr_AuthDef->au_Radius);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via radius\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via radius FAILED\n");
		}
	    }
#else
	    if (authok && *dres->dr_AuthDef->au_Radius && *user &&
			*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via radius FAILED\n");
		logit(LOG_ERR, "Radius authentication requested, but not enabled");
	    }
#endif

#ifdef CDB_ENABLED

	    /*
	     * Check for the user+pass in a CDB
	     */

	    if (authok && *dres->dr_AuthDef->au_Cdb && *user &&
			*dreq->dr_AuthPass) {
		stprintf("dns auth cdb %s@%s", user, ipname);
		AuthReader = cdbAuthenticate(user, dreq->dr_AuthPass,
					dres->dr_AuthDef->au_Cdb);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    if (*dres->dr_AuthDef->au_Cdb != '!')
			Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via cdb\n");
		} else if (*dres->dr_AuthDef->au_Cdb == '!') {
		    authok = 0;
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via cdb REJECTED\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via cdb FAILED\n");
		}
	    }

#else
	    if (authok && *dres->dr_AuthDef->au_Cdb && *user &&
			*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via cdb FAILED\n");
		logit(LOG_ERR, "CDB authentication requested, but not enabled");
	    }
#endif /* CDB_ENABLED */

#ifdef DB_ENABLED

	    /*
	     * Check for the user+pass in a DB
	     */

	    if (authok && *dres->dr_AuthDef->au_Db && *user &&
			*dreq->dr_AuthPass) {
		stprintf("dns auth db %s@%s", user, ipname);
		AuthReader = dbAuthenticate(user, dreq->dr_AuthPass,
					dres->dr_AuthDef->au_Db);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    if (*dres->dr_AuthDef->au_Db != '!')
			Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via db\n");
		} else if (*dres->dr_AuthDef->au_Db == '!') {
		    authok = 0;
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via db REJECTED\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via db FAILED\n");
		}
	    }

#else
	    if (authok && *dres->dr_AuthDef->au_Db && *user &&
			*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via db FAILED\n");
		logit(LOG_ERR, "DB authentication requested, but not enabled");
	    }
#endif /* DB_ENABLED */

#ifdef	NETREMOTE_ENABLED

	    /*
	     * Check user+pass with NetRemote server
	     */

	    if (authok && *dres->dr_AuthDef->au_NetRemote && *user &&
				*dreq->dr_AuthPass) {
		stprintf("dns auth netremote %s@%s", user, ipname);
		AuthReader = netRemoteAuthenticate(user, dreq->dr_AuthPass,
				dres->dr_AuthDef->au_NetRemote,
				NetAddrToSt(0, (struct sockaddr *)&dreq->dr_LSin, 1, 0, 1),
				ipname);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via NetRemote: %s\n", AuthReader);
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via NetRemote FAILED\n");
		}
	    }
#else
	    if (authok && *dres->dr_AuthDef->au_NetRemote && *user &&
			*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via NetRemote FAILED\n");
		logit(LOG_ERR, "NetRemote authentication requested, but not enabled");
	    }
#endif

#ifdef	LDAP_ENABLED

	    /*
	     * Check user+pass with LDAP server
	     */

	    if (authok && *dres->dr_AuthDef->au_LDAP && *user &&
				*dreq->dr_AuthPass) {
		stprintf("dns auth ldap %s@%s", user, ipname);
#ifdef NEW_LDAP
		AuthReader = LDAPAuthenticate(user, dreq->dr_AuthPass, realm,
						dres->dr_AuthDef->au_LDAP);
#else
		AuthReader = LDAPAuthenticate(user, dreq->dr_AuthPass,
						dres->dr_AuthDef->au_LDAP);
#endif
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via LDAP\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via LDAP FAILED\n");
		}
	    }
#else
	    if (authok && *dres->dr_AuthDef->au_LDAP && *user &&
			*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via LDAP FAILED\n");
		logit(LOG_ERR, "LDAP authentication requested, but not enabled");
	    }
#endif

#ifdef	PERL_ENABLED
	    if (authok && *dres->dr_AuthDef->au_Perl && *user &&
				*dreq->dr_AuthPass) {
		stprintf("dns auth perl %s@%s", user, ipname);
		AuthReader = PerlAuthenticate(user, dreq->dr_AuthPass,
						dres->dr_AuthDef->au_Perl);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via Perl\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via Perl FAILED\n");
		}
	    }
#else
	    if (authok && *dres->dr_AuthDef->au_Perl && *user &&
							*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via Perl FAILED\n");
		logit(LOG_ERR, "Perl authentication requested, but not enabled");
	    }
#endif

#ifdef PAM_ENABLED

	    /*
	     * Check user+pass with radius server
	     */

	    if (authok && *dres->dr_AuthDef->au_PAM && *user &&
						*dreq->dr_AuthPass) {
		stprintf("dns auth PAM %s@%s", user, ipname);
		AuthReader = PamAuthenticate(user, dreq->dr_AuthPass,
						dres->dr_AuthDef->au_PAM);
		stprintf("dns auth search %s", ipname);
		if (AuthReader != NULL && *AuthReader == '1') {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via PAM\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via PAM FAILED\n");
		}
	    }
#else
	    if (authok && *dres->dr_AuthDef->au_PAM && *user &&
							*dreq->dr_AuthPass) {
		Authenticated = -1;
		if (DebugOpt)
		    printf("Authentication via PAM FAILED\n");
		logit(LOG_ERR, "PAM authentication requested, but not enabled");
	    }
#endif
	    /*
	     * Check a user+pass specified in the auth config
	     */

	    if (authok && *dres->dr_AuthDef->au_User && *user &&
			*dres->dr_AuthDef->au_Pass && *dreq->dr_AuthPass) {
		if (!strcmp(user, dres->dr_AuthDef->au_User) &&
			!strcmp(dreq->dr_AuthPass, dres->dr_AuthDef->au_Pass)) {
		    Authenticated = 1;
		    if (DebugOpt)
			printf("Authenticated via user+pass\n");
		} else {
		    Authenticated = -1;
		    if (DebugOpt)
			printf("Authentication via user+pass FAILED\n");
		}
	    }

	}
	if (!AuthNeeded ||
		/* AUTHINFO and matched */
		(Authenticated > 0 && *dreq->dr_AuthPass) ||
		/* Ident + user only */
		(Authenticated == 2) ||
		/* AUTHINFO and not failed auth, but no other match */
		(Authenticated == 0 && (matchedalp == NULL))
	) {
	    if (accessfile == 1) {
		memcpy(&matchedaccessdef, &accessdef, sizeof(accessdef));
		matchedalp = &matchedaccessdef;
		MatchedAuthenticated = Authenticated;
		MatchedAuthNeeded = AuthNeeded;
	    } else {
		matchedalp = alp;
		MatchedAuthenticated = Authenticated;
		MatchedAuthNeeded = AuthNeeded;
	    }
	}
	if (matchedalp && matchedalp->ad_MatchExit)
	    break;
    }
    if (accessfile == 1)
	fclose(af);
    if (matchedalp == NULL) {
	if (DebugOpt)
	    printf("No access match\n");
	dres->dr_Code = 0;
	return;
    }
    if (!dres->dr_ReaderDef->rd_IgnoreAuthInfo && *dreq->dr_AuthPass &&
						MatchedAuthenticated <= 0) {
	if (DebugOpt)
	    printf("Authentication failed\n");
	dres->dr_Code = 0;
	return;
    }


    if (AuthReader && strncmp(AuthReader, "110 ", 4) == 0) {
	AuthReader += 4;
	strncpy(dres->dr_ReaderName, AuthReader, sizeof(dres->dr_ReaderName) - 1);
	dres->dr_ReaderName[sizeof(dres->dr_ReaderName) - 1] = '\0';
    } else {
	strncpy(dres->dr_ReaderName, matchedalp->ad_Reader, sizeof(dres->dr_ReaderName) - 1);
	dres->dr_ReaderName[sizeof(dres->dr_ReaderName) - 1] = '\0';
    }

    SetAuthDetails(dres, dres->dr_ReaderName);

    /*
     * Now we have checked against all the access lines and matchedalp
     * points to the last match
     */

    if (DebugOpt > 1)
	PrintAuthDetails(dres);

    /*
     * okay, if this was an authentication request, and we didn't auth,
     * then return a failure
     */
    dres->dr_Flags &= ~DF_AUTHREQUIRED;
    if (MatchedAuthNeeded && !MatchedAuthenticated) {
	dres->dr_Flags |= DF_AUTHREQUIRED;
    }

    /*
     * If we authenticated with AUTHUSER, mark that we have done so as
     * we would like to log the correct info in log lines
     */
    if (MatchedAuthenticated <= 0) {
	dres->dr_AuthUser[0] = 0;
	dres->dr_Flags &= ~DF_AUTH;
    } else {
	dres->dr_Flags |= DF_AUTH;
    }

    dres->dr_Code = 1;
    if ((dres->dr_Flags & (DF_FEED|DF_READ|DF_POST|DF_STATUS|DF_AUTHREQUIRED)) == 0)
	dres->dr_Code = 0;
}

/*
 * IDENT
 *
 * getAuthUser() - authenticate the remote username by connecting to port
 *		   113 and requesting the user id of the remote port.  This
 *		   is used by X-Trace: and some authentication.
 */

void
getAuthUser(const struct sockaddr *plsin, const struct sockaddr *prsin, char *ubuf, int ulen, int timeout)
{
#ifdef INET6
    /*
     * XXX This needs to be updated to do INET6 ident requests
     */
#else
    int cfd;
    int lport;
    int rport;
    int n;
    char buf[256];
    struct sockaddr_in lsin;
    struct sockaddr_in rsin;

    memcpy(&lsin, plsin, sizeof(lsin));
    memcpy(&rsin, prsin, sizeof(rsin));

    rport = ntohs(rsin.sin_port);
    lport = ntohs(lsin.sin_port);

    lsin.sin_port = 0;
    lsin.sin_family = AF_INET;

    if ((cfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return;
    }
    if (bind(cfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
	perror("bind");
        close(cfd);
        return;
    }
    rsin.sin_port = htons(113);
    rsin.sin_family = AF_INET;

    /*
     * Do asynchronous connection with 10 second timeout.  The timeout is
     * necessary to deal with broken clients or firewalls.
     */

    fcntl(cfd, F_SETFL, O_NONBLOCK);
    errno = 0;
    if (connect(cfd, (struct sockaddr *)&rsin, sizeof(rsin)) < 0) {
	fd_set wfds;
	struct timeval tv = { timeout, 0 };

	if (errno != EINPROGRESS) {
	    close(cfd);
	    return;
	}
	FD_ZERO(&wfds);
	FD_SET(cfd, &wfds);
	if (select(cfd + 1, NULL, &wfds, NULL, &tv) == 0) {
	    close(cfd);
	    cfd = -1;
	}
    }

    /*
     * Send query, interpret response
     *
     * write() may fail if connect() failed to establish a connection.
     */

    if (cfd >= 0) {
	fcntl(cfd, F_SETFL, 0);
	sprintf(buf, "%d, %d\r\n", rport, lport);
	if (DebugOpt)
	    printf("IDENTD COMMAND %s\n", buf);
	if (write(cfd, buf, strlen(buf)) != strlen(buf)) {
	    n = -1;
	} else {
	    fcntl(cfd, F_SETFL, O_NONBLOCK);
	    n = readtoeof(cfd, buf, sizeof(buf) - 1, 10);
	}
    } else {
	n = -1;
    }
    if (n > 0) {
	char *uid;

        buf[n] = 0;

	if (DebugOpt)
	    printf("IDENTD RESPONSE %s\n", buf);

        if ((uid = strstr(buf, "USERID")) != NULL) {
            uid = strchr(uid + 1, ':');
            if (uid) {
                uid = strchr(uid + 1, ':');
                if (uid) {
                    uid = strtok(uid + 1, "\r\n: \t");
                    if (uid && strlen(uid) < ulen) {
                        strncpy(ubuf, uid, ulen - 1);
			ubuf[ulen - 1] = '\0';
			SanitizeString(ubuf);
                    }
                }
            }
        }
    }
    if (cfd >= 0)
	close(cfd);
#endif
}

int
readExactly(int fd, void *buf, int bytes)
{
    int r = 0;

    while (bytes > 0) {
	int n = read(fd, buf, bytes);

	if (n <= 0) {
	    if (n < 0 && r == 0)
		r = -1;
	    break;
	}
	buf = (char *)buf + n;
	bytes -= n;
	r += n;
    }
    return(r);
}

int
readtoeof(int fd, void *buf, int bytes, int secs)
{
    fd_set rfds;
    int r = 0;

    FD_ZERO(&rfds);

    for (;;) {
	struct timeval tv = { 0, 0 };
	int n;

	tv.tv_sec = secs;
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, &tv) == 0)
	    break;
	errno = 0;
	n = read(fd, buf, bytes);
	if (n == 0)
	    break;
	if (n < 0) {
	    if (errno == EWOULDBLOCK ||
		errno == EINTR ||
		errno == EAGAIN
	    ) {
		continue;
	    }
	    break;
	}
	r += n;
	buf = (char *)buf + n;
	bytes -= n;
    }
    return(r);
}

void
sigSegVDNS(int sigNo)
{
    if (DFd >= 0)
        close(DFd);
    nice(20);
    for (;;)
        ;
}   

int
readAccessLine(FILE *af, char *fname, AccessDef *adef)
{
    static int needread = 1;
    static int linecount = 0;
    static char buf[8192] = "";
    static char *opt;
    static char *val;
    char *p;

    while (needread) {
    	if (fgets(buf, sizeof(buf), af) == NULL)
	    return(0);
	linecount++;
	opt = buf;
	while (isspace((int)*opt))
	    opt++;
	if (*opt == '#' || *opt == '\n' || *opt == '\0')
	    continue;
	p = opt + 6;
	*p = '\0';
	if (strcmp(opt, "access") == 0) {
	   opt += 7;
	   while (isspace((int)*opt))
		opt++;
	   bzero(adef, sizeof(AccessDef));
	   break;
	} else {
	    logit(LOG_ERR, "%s: Invalid command %s in line %d",
				fname, opt, linecount);
	    return(0);
	}
    }
    /*
     * At this point we should be pointing to the wildmat pattern
     */
    if (opt == NULL) {
	logit(LOG_ERR, "%s: Missing wildmat in line %d",
				fname, linecount);
	needread = 1;
	return(0);
    }
    if (needread) {
	val = strtok(opt, " \t");
	val = strtok(NULL, " \t");
	if (val == NULL) {
	    logit(LOG_ERR, "%s: Invalid readerdef in line %d",
					fname, linecount);
	    needread = 1;
	    return(0);
	}
	while (isspace((int)*val))
	    val++;
	if (*val == '#' || *val == '\n' || *val == '\0') {
	    logit(LOG_ERR, "%s: Invalid readerdef in line %d",
					fname, linecount);
	    needread = 1;
	    return(0);
	}
	p = strchr(val, '\n');
	if (p)
	    *p = '\0';
	strncpy(adef->ad_Reader, val, sizeof(adef->ad_Reader) - 1);
	adef->ad_Reader[sizeof(adef->ad_Reader) - 1] = '\0';
    }
    p = strchr(opt, ',');
    if (p != NULL) {
	*p++ = '\0';
	needread = 1;
    } else {
	p = opt;
    }
    {
	/*
	 *  Check for identuser@
	 */
	char *q;
	if (*opt && (q = strchr(opt, '@')) != NULL) {
	    *q++ = '\0';
	    strncpy(adef->ad_IdentUser, opt, sizeof(adef->ad_IdentUser) - 1);
	    adef->ad_IdentUser[sizeof(adef->ad_IdentUser) - 1] = '\0';
	    opt = q;
	}
    }
    if (*opt && strlen(opt) < sizeof(adef->ad_Pattern)) {
	strncpy(adef->ad_Pattern, opt, sizeof(adef->ad_Pattern) - 1);
	adef->ad_Pattern[sizeof(adef->ad_Pattern) - 1] = '\0';
	opt = p;
	return(1);
    }
    logit(LOG_ERR, "%s: Bad length for access %s in line %d",
					fname, opt, linecount);
    needread = 1;
    return(0);
}

/*
 * Install a new copy of the access cache
 * This is done by creating the new cache files first, locking and then
 * renaming the files.
 */

void
InstallAccessCache() {
    struct stat st;
    char buf[1024];
    int linecount = 0;
    int tfp;
    FILE *fp;
    FILE *vsf;
    FILE *grf;
    FILE *auf;
    FILE *rdf;
    FILE *raf;
    char *opt;
    char *val;
    Vserver vserver;
    GroupDef groupdef;
    AuthDef authdef;
    ReaderDef readerdef;
    GroupList *groupptr = NULL;
    AccessDef accessdef;
    int copt = OPT_NONE;
    char vsnewbuf[PATH_MAX];
    char grnewbuf[PATH_MAX];
    char aunewbuf[PATH_MAX];
    char rdnewbuf[PATH_MAX];
    char ranewbuf[PATH_MAX];

    if ((stat(PatLibExpand(DReaderAccessPat), &st) == 0) &&
		(st.st_mtime <= Access_LastUpdate))
	return;

    logit(LOG_INFO, "Installing access cache");

    if ((fp = fopen(PatLibExpand(DReaderAccessPat), "r")) == NULL) {
	logit(LOG_CRIT, "Unable to open %s (%s)",
			PatLibExpand(DReaderAccessPat), strerror(errno));
	return;
    }
    if (fstat(fileno(fp), &st) != 0) {
	fclose(fp);
	return;
    }
    Access_LastUpdate = st.st_mtime;

    snprintf(vsnewbuf, sizeof(vsnewbuf), "%s.new",
				PatDbExpand(DRVserverCachePat));
    snprintf(grnewbuf, sizeof(grnewbuf), "%s.new",
				PatDbExpand(DRGroupCachePat));
    snprintf(aunewbuf, sizeof(aunewbuf), "%s.new",
				PatDbExpand(DRAuthCachePat));
    snprintf(rdnewbuf, sizeof(rdnewbuf), "%s.new",
				PatDbExpand(DRReaderCachePat));
    snprintf(ranewbuf, sizeof(ranewbuf), "%s.new",
				PatDbExpand(DRAccessCachePat));

    if ((vsf = fopen(vsnewbuf, "w+")) == NULL) {
	logit(LOG_CRIT, "Unable to create %s (%s)", vsnewbuf, strerror(errno));
	fclose(fp);
	return;
    }
    if ((grf = fopen(grnewbuf, "w+")) == NULL) {
	logit(LOG_CRIT, "Unable to create %s (%s)", grnewbuf, strerror(errno));
	fclose(fp);
	fclose(vsf);
	return;
    }
    if ((auf = fopen(aunewbuf , "w+")) == NULL) {
	logit(LOG_CRIT, "Unable to create %s (%s)", aunewbuf, strerror(errno));
	fclose(fp);
	fclose(vsf);
	fclose(grf);
	return;
    }
    if ((rdf = fopen(rdnewbuf, "w+")) == NULL) {
	logit(LOG_CRIT, "Unable to create %s (%s)", rdnewbuf, strerror(errno));
	fclose(fp);
	fclose(vsf);
	fclose(grf);
	fclose(auf);
	return;
    }

    if ((raf = fopen(ranewbuf, "w+")) == NULL) {
	logit(LOG_CRIT, "Unable to create %s (%s)", ranewbuf, strerror(errno));
	fclose(fp);
	fclose(vsf);
	fclose(grf);
	fclose(auf);
	fclose(rdf);
	return;
    }

    /* The first entry is the default */

    bzero(&vserver, sizeof(vserver));
    strcpy(vserver.vs_Name, "DEFAULT");
    if (DOpts.ReaderHostName != NULL)
    strncpy(vserver.vs_HostName, DOpts.ReaderHostName, sizeof(vserver.vs_HostName) - 1);
    if (DOpts.ReaderHostName != NULL)
    strncpy(vserver.vs_ClusterName, DOpts.ReaderHostName, sizeof(vserver.vs_ClusterName) - 1);
    if (DOpts.ReaderPathHost != NULL)
    strncpy(vserver.vs_PostPath, DOpts.ReaderPathHost, sizeof(vserver.vs_PostPath) - 1);
    if (DOpts.NewsAdmin != NULL)
	strncpy(vserver.vs_NewsAdm, DOpts.NewsAdmin, sizeof(vserver.vs_NewsAdm) - 1);
    fwrite(&vserver, sizeof(vserver), 1, vsf);

    fwrite("DEFAULT\0", 8, 1, grf);
    groupdef.gr_Count = 1;
    fwrite("1\0", 2, 1, grf);
    fwrite("*\0", 2, 1, grf);

    bzero(&authdef, sizeof(authdef));
    strcpy(authdef.au_Name, "DEFAULT");
    fwrite(&authdef, sizeof(authdef), 1, auf);

    bzero(&readerdef, sizeof(readerdef));
    strcpy(readerdef.rd_Name, "DEFAULT");
    strcpy(readerdef.rd_Auth, "DEFAULT");
    strcpy(readerdef.rd_Groups, "DEFAULT");
    strcpy(readerdef.rd_ListGroups, "DEFAULT");
    strcpy(readerdef.rd_PostGroups, "DEFAULT");
    strcpy(readerdef.rd_Vserver, "DEFAULT");
    fwrite(&readerdef, sizeof(readerdef), 1, rdf);

    while (fgets(buf, sizeof(buf), fp) != NULL) {
	linecount++;
	opt = buf;
	while (isspace((int)*opt))
	    opt++;
	opt = strtok(opt, " \t\n");
	if (opt == NULL || *opt == '#' || *opt == '\n' || *opt == '\0')
	    continue;
	val = strtok(NULL, "\n");
	if (val == NULL || *val == '#')
	    val = "";
	else
	    while (isspace((int)*val))
		val++;

	if (strcmp(opt, "end") == 0) {
	    switch (copt) {
		case OPT_NONE:
		    logit(LOG_ERR, "Found 'end' without define in line %d", linecount);
		    break;
		case OPT_VSERVER:
		    if (!*vserver.vs_ClusterName)
			strcpy(vserver.vs_ClusterName, vserver.vs_HostName);
		    if (strcmp(vserver.vs_Name, "DEFAULT") == 0) {
			long fpos;
			fpos = ftell(vsf);
			fseek(vsf, 0L, SEEK_SET);
			fwrite(&vserver, sizeof(vserver), 1, vsf);
			fseek(vsf, fpos, SEEK_SET);
		    } else {
			fwrite(&vserver, sizeof(vserver), 1, vsf);
		    }
		    bzero(&vserver, sizeof(vserver));
		    break;
		case OPT_GROUPS:
		    fwrite(groupdef.gr_Name, strlen(groupdef.gr_Name) + 1, 1, grf);
		    zfreeStr(&DnsMemPool, &groupdef.gr_Name);
		    {
			char buf[10];
			struct GroupList *gpp;
			struct GroupList *gp;

			sprintf(buf, "%d", groupdef.gr_Count);
			fwrite(&buf, strlen(buf) + 1, 1, grf);
			gp = groupdef.gr_Groups;
			while (gp != NULL) {
			    fwrite(gp->group, strlen(gp->group) + 1, 1, grf);
			    gpp = gp;
			    gp = gp->next;
			    zfreeStr(&DnsMemPool, &gpp->group);
			    zfree(&DnsMemPool, gpp, sizeof(GroupList));
			}
		    }
		    bzero(&groupdef, sizeof(groupdef));
		    break;
		case OPT_AUTH:
		    if (strcmp(authdef.au_Name, "DEFAULT") == 0) {
			long fpos;
			fpos = ftell(auf);
			fseek(auf, 0L, SEEK_SET);
			fwrite(&authdef, sizeof(authdef), 1, auf);
			fseek(auf, fpos, SEEK_SET);
		    } else {
			fwrite(&authdef, sizeof(authdef), 1, auf);
		    }
		    bzero(&authdef, sizeof(authdef));
		    break;
		case OPT_READERGRP:
		    if (strcmp(readerdef.rd_Name, "DEFAULT") == 0) {
			long fpos;
			fpos = ftell(rdf);
			fseek(rdf, 0L, SEEK_SET);
			fwrite(&readerdef, sizeof(readerdef), 1, rdf);
			fseek(rdf, fpos, SEEK_SET);
		    } else {
			fwrite(&readerdef, sizeof(readerdef), 1, rdf);
		    }
		    bzero(&readerdef, sizeof(readerdef));
		    break;
	    }
	    copt = OPT_NONE;
	    continue;
	}
	if (!*val) {
	    logit(LOG_ERR, "dreader.access: Missing value/label for %s in line %d", opt, linecount);
	    Access_LastUpdate = 0;
	    continue;
	}
	/* Handle the definition options */
	switch (copt) {
	    case OPT_NONE: break;
	    case OPT_VSERVER:
		if (*val && strcmp(opt, "clustername") == 0) {
		    strncpy(vserver.vs_ClusterName, val, sizeof(vserver.vs_ClusterName) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "hostname") == 0) {
		    strncpy(vserver.vs_HostName, val, sizeof(vserver.vs_HostName) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "postpath") == 0) {
		    strncpy(vserver.vs_PostPath, val, sizeof(vserver.vs_PostPath) - 1);
		    if (strcmp(val, "0") == 0)
			vserver.vs_PostPath[0] = 0;
		    continue;
		}
		if (*val && strcmp(opt, "newsadmin") == 0) {
		    strncpy(vserver.vs_NewsAdm, val, sizeof(vserver.vs_NewsAdm) - 1);
		    continue;
		}
		if (*val && ((strcmp(opt, "organisation") == 0) ||
			(strcmp(opt, "organization") == 0))) {
		    strncpy(vserver.vs_Org, val, sizeof(vserver.vs_Org) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "abuseto") == 0) {
		    strncpy(vserver.vs_AbuseTo, val, sizeof(vserver.vs_AbuseTo) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "cryptpw") == 0) {
		    strncpy(vserver.vs_CryptPw, val, sizeof(vserver.vs_CryptPw) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "accessfile") == 0) {
		    strncpy(vserver.vs_AccessFile, val, sizeof(vserver.vs_AccessFile) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "welcome") == 0) {
		    strncpy(vserver.vs_Welcome, val, sizeof(vserver.vs_Welcome) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "interface") == 0) {
#ifdef INET6
		    int error;
		    struct addrinfo *res;
                     
		    error = getaddrinfo(val, 0, NULL, &res);
		    if (error != 0)
			logit(LOG_ERR, "getaddrinfo: %s: %s\n",
					val, gai_strerror(error));
		    else
			memcpy(&vserver.vs_Interface, &res->ai_addr,
							res->ai_addrlen);
#else
		    struct sockaddr_in *sin = (struct sockaddr_in *)&vserver.vs_Interface;

		    bzero(&vserver.vs_Interface, sizeof(vserver.vs_Interface));
		    sin->sin_family = AF_INET;
		    sin->sin_addr.s_addr = inet_addr(val);
#endif
		    continue;
		}
		if (*val && strcmp(opt, "noxrefhostupdate") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			vserver.vs_NoXrefHostUpdate = 1;
		    else
			vserver.vs_NoXrefHostUpdate = 0;
		    continue;
		}
		if (*val && strcmp(opt, "noreadpath") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			vserver.vs_NoReadPath = 1;
		    else
			vserver.vs_NoReadPath = 0;
		    continue;
		}
		if (*val && strcmp(opt, "postcomments") == 0) {
		    strncpy(vserver.vs_Comments, val, sizeof(vserver.vs_Comments) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "posttrailer") == 0) {
		    strncpy(vserver.vs_PostTrailer, val, sizeof(vserver.vs_PostTrailer) - 1);
		    continue;
		}
		logit(LOG_ERR, "dreader.access: Invalid vserverdef option : %s in line %d\n", opt, linecount);
		continue;
	    case OPT_GROUPS:
		if (*val && strcmp(opt, "group") == 0) {
		    if (groupdef.gr_Groups == NULL) {
			groupptr = zalloc(&DnsMemPool, sizeof(GroupList));
			groupptr->group = zallocStr(&DnsMemPool, val);
			groupptr->next = NULL;
			groupdef.gr_Groups = groupptr;
			groupdef.gr_Count++;
		    } else {
			groupptr->next = zalloc(&DnsMemPool, sizeof(GroupList));
			groupptr = groupptr->next;
			groupptr->group = zallocStr(&DnsMemPool, val);
			groupptr->next = NULL;
			groupdef.gr_Count++;
		    }
		    continue;
		}
		logit(LOG_ERR, "dreader.access: Invalid groupdef option : %s in line %d\n", opt, linecount);
		continue;
	    case OPT_AUTH:
		if (*val && strcmp(opt, "file") == 0) {
		    strncpy(authdef.au_File, val, sizeof(authdef.au_File) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "cdb") == 0) {
		    strncpy(authdef.au_Cdb, val, sizeof(authdef.au_Cdb) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "db") == 0) {
		    strncpy(authdef.au_Db, val, sizeof(authdef.au_Db) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "external") == 0) {
		    strncpy(authdef.au_External, val, sizeof(authdef.au_External) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "radius") == 0) {
		    strncpy(authdef.au_Radius, val, sizeof(authdef.au_Radius) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "netremote") == 0) {
		    strncpy(authdef.au_NetRemote, val, sizeof(authdef.au_NetRemote) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "ldap") == 0) {
		    strncpy(authdef.au_LDAP, val, sizeof(authdef.au_LDAP) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "perl") == 0) {
		    strncpy(authdef.au_Perl, val, sizeof(authdef.au_Perl) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "pam") == 0) {
		    strncpy(authdef.au_PAM, val, sizeof(authdef.au_PAM) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "user") == 0) {
		    strncpy(authdef.au_User, val, sizeof(authdef.au_User) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "pass") == 0) {
		    strncpy(authdef.au_Pass, val, sizeof(authdef.au_Pass) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "addrealm") == 0) {
		    strncpy(authdef.au_AddRealm, val, sizeof(authdef.au_AddRealm) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "realm") == 0) {
		    strncpy(authdef.au_Realm, val, sizeof(authdef.au_Realm) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "ident") == 0) {
		    if (*val == 'y' || *val == 'Y')
			authdef.au_Ident = DOpts.ReaderIdentTimeout;
		    else if (*val >= '0' && *val <= '9')
			authdef.au_Ident = btimetol(val);
		    else
			authdef.au_Ident = 0;
		    continue;
		}
		logit(LOG_ERR, "dreader.access: Invalid authdef option : %s in line %d\n", opt, linecount);
		continue;
	    case OPT_READERGRP:
		if (*val && strcmp(opt, "auth") == 0) {
		    strncpy(readerdef.rd_Auth, val, sizeof(readerdef.rd_Auth) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "groups") == 0) {
		    strncpy(readerdef.rd_Groups, val, sizeof(readerdef.rd_Groups) - 1);
		    if (strcmp(readerdef.rd_ListGroups, "DEFAULT") == 0)
			strncpy(readerdef.rd_ListGroups, val, sizeof(readerdef.rd_Groups) - 1);
		    if (strcmp(readerdef.rd_PostGroups, "DEFAULT") == 0)
			strncpy(readerdef.rd_PostGroups, val, sizeof(readerdef.rd_Groups) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "listgroups") == 0) {
		    strncpy(readerdef.rd_ListGroups, val, sizeof(readerdef.rd_Groups) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "postgroups") == 0) {
		    strncpy(readerdef.rd_PostGroups, val, sizeof(readerdef.rd_Groups) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "vserver") == 0) {
		    strncpy(readerdef.rd_Vserver, val, sizeof(readerdef.rd_Vserver) - 1);
		    continue;
		}
		if (*val && strcmp(opt, "read") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_Read = 1;
		    else
			readerdef.rd_Read = 0;
		    continue;
		}
		if (*val && strcmp(opt, "post") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_Post = 1;
		    else
			readerdef.rd_Post = 0;
		    continue;
		}
		if (*val && strcmp(opt, "feed") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_Feed = 1;
		    else
			readerdef.rd_Feed = 0;
		    continue;
		}
		if (*val && strcmp(opt, "status") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_Status = 1;
		    else
			readerdef.rd_Status = 0;
		    continue;
		}
		if (*val && strcmp(opt, "useproxied") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
		        readerdef.rd_UseProxied = 1;
		    else
		        readerdef.rd_UseProxied = 0;
		    continue;
		}
		if (*val && strcmp(opt, "quiet") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_Quiet = 1;
		    else
			readerdef.rd_Quiet = 0;
		    continue;
		}
		if (*val && strcmp(opt, "controlpost") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_ControlPost = 1;
		    else
			readerdef.rd_ControlPost = 0;
		    continue;
		}
		if (*val && strcmp(opt, "maxconn") == 0) {
		    readerdef.rd_MaxConnTotal = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "maxconnperhost") == 0) {
		    readerdef.rd_MaxConnPerHost = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "maxconnperuser") == 0) {
		    readerdef.rd_MaxConnPerUser = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "maxconnpergroup") == 0) {
		    readerdef.rd_MaxConnPerGroup = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "maxconnpervs") == 0) {
		    readerdef.rd_MaxConnPerVs = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "ratelimit") == 0) {
		    getRateLimitCmds(val, &readerdef);
		    continue;
		}
		if (*val && strcmp(opt, "ratelimittax") == 0) {
		    readerdef.rd_RateLimitTax = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "ratelimitrange") == 0) {
		    readerdef.rd_RateLimitRangeLow = atoi(val);
		    val = strchr(val, '-');
		    if (val && *val && *(val + 1)) {
			    val++;
			    readerdef.rd_RateLimitRangeHigh = atoi(val);
		    }
		    if (readerdef.rd_RateLimitRangeLow < 1 ||
			readerdef.rd_RateLimitRangeLow > 200 ||
			readerdef.rd_RateLimitRangeHigh < 1 ||
			readerdef.rd_RateLimitRangeHigh > 200) {
			    logit(LOG_NOTICE, "dreader.access: bad 'ratelimitrange' in line %d", linecount);
			    readerdef.rd_RateLimitRangeLow = 0;
			    readerdef.rd_RateLimitRangeHigh = 0;
		    }
		    continue;
		}
		if (*val && strcmp(opt, "bytelimit") == 0) {
		    readerdef.rd_ByteLimit = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "pathcomponents") == 0) {
		    readerdef.rd_PathComponents = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "idletimeout") == 0) {
		    readerdef.rd_IdleTimeout = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "sessiontimeout") == 0) {
		    readerdef.rd_SessionTimeout = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "allowdnsmismatch") == 0) {
		    /* Historical option - ignore it */
		    logit(LOG_NOTICE, "dreader.access: Ignoring historical option 'allowdnsmismatch' in line %d", linecount);
		    continue;
		}
		if (*val && strcmp(opt, "useverifieddns") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_UseVerifiedDns = 1;
		    else
			readerdef.rd_UseVerifiedDns = 0;
		    continue;
		}
		if (*val && strcmp(opt, "denymismatcheddns") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_DenyMismatchedDns = 1;
		    else
			readerdef.rd_DenyMismatchedDns = 0;
		    continue;
		}
		if (*val && strcmp(opt, "denynodns") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_DenyNoDns = 1;
		    else
			readerdef.rd_DenyNoDns = 0;
		    continue;
		}
		if (*val && strcmp(opt, "allownewnews") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_AllowNewnews = 1;
		    else
			readerdef.rd_AllowNewnews = 0;
		    continue;
		}
		if (*val && strcmp(opt, "grouplog") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_GroupLog = 1;
		    else
			readerdef.rd_GroupLog = 0;
		    continue;
		}
		if (*val && strcmp(opt, "spoolheaders") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_SpoolHeaders = 1;
		    else
			readerdef.rd_SpoolHeaders = 0;
		    continue;
		}
		if (*val && strcmp(opt, "ignoreauthinfo") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_IgnoreAuthInfo = 1;
		    else
			readerdef.rd_IgnoreAuthInfo = 0;
		    continue;
		}
		if (*val && strcmp(opt, "checkpostgroups") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_CheckPostGroups = 1;
		    else
			readerdef.rd_CheckPostGroups = 0;
		    continue;
		}
		if (*val && strcmp(opt, "logcmd") == 0) {
		    if (*val == 'y' || *val == 'Y' || *val == '1')
			readerdef.rd_LogCmd = 1;
		    else
			readerdef.rd_LogCmd = 0;
		    continue;
		}
#ifdef	IP_TOS
		if (*val && strcmp(opt, "settos") == 0) {
		    readerdef.rd_SetTOS = atoi(val);
		    continue;
		}
#endif
		if (*val && strcmp(opt, "rxbufsize") == 0) {
		    readerdef.rd_RxBufSize = atoi(val);
		    ValidateTcpBufferSize(&readerdef.rd_RxBufSize);
		    continue;
		}
		if (*val && strcmp(opt, "txbufsize") == 0) {
		    readerdef.rd_TxBufSize = atoi(val);
		    ValidateTcpBufferSize(&readerdef.rd_TxBufSize);
		    continue;
		}
		if (*val && strcmp(opt, "maxagepolicy") == 0) {
		    readerdef.rd_MaxAgePolicy = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "turnoffnnph") == 0) {
		    readerdef.rd_TurnOffNNPH = atoi(val);
		    continue;
		}
		if (*val && strcmp(opt, "xzver") == 0) {
		    if (*val == 'n') {
			readerdef.rd_XzverLevel = -1;
		    } else {
			readerdef.rd_XzverLevel = atoi(val);
		    }
		}
		logit(LOG_ERR, "dreader.access: Invalid readerdef option : %s in line %d\n", opt, linecount);
		continue;
	}
	/* Start of a new definition */
	if (strcmp(opt, "vserverdef") == 0) {
	    copt = OPT_VSERVER;
	    bzero(&vserver, sizeof(vserver));
	    strncpy(vserver.vs_Name, val, sizeof(vserver.vs_Name) - 1);
	    continue;
	}
	if (strcmp(opt, "groupdef") == 0) {
	    copt = OPT_GROUPS;
	    bzero(&groupdef, sizeof(groupdef));
	    groupdef.gr_Name = zallocStr(&DnsMemPool, val);
	    groupdef.gr_Count = 0;
	    groupdef.gr_Groups = NULL;
	    continue;
	}
	if (strcmp(opt, "authdef") == 0) {
	    copt = OPT_AUTH;
	    bzero(&authdef, sizeof(authdef));
	    strncpy(authdef.au_Name, val, sizeof(authdef.au_Name) - 1);
	    continue;
	}
	if (strcmp(opt, "readerdef") == 0) {
	    copt = OPT_READERGRP;
	    bzero(&readerdef, sizeof(readerdef));
	    strncpy(readerdef.rd_Name, val, sizeof(readerdef.rd_Name) - 1);
	    readerdef.rd_ControlPost = 1;
	    readerdef.rd_GroupLog = 1;
	    readerdef.rd_CheckPostGroups = 1;
	    readerdef.rd_LogCmd = -1;
#ifdef	IP_TOS
	    readerdef.rd_SetTOS = -1;
#endif
	    strcpy(readerdef.rd_Auth, "DEFAULT");
	    strcpy(readerdef.rd_Groups, "DEFAULT");
	    strcpy(readerdef.rd_ListGroups, "DEFAULT");
	    strcpy(readerdef.rd_PostGroups, "DEFAULT");
	    strcpy(readerdef.rd_Vserver, "DEFAULT");
	    continue;
	}
	if (strcmp(opt, "access") == 0) {
	    char *p;
	    char *q;

	    bzero(&accessdef, sizeof(accessdef));
	    copt = OPT_NONE;
	    p = strtok(val, " \t");
	    p = strtok(NULL, " \t");
	    if (p == NULL) {
		logit(LOG_ERR, "dreader.access: Invalid readerdef for access %s in line %d", val, linecount);
		continue;
	    }
	    while (*p == ' ' || *p == '\t')
		p++;
	    if (!*p || strlen(p) > sizeof(accessdef.ad_Reader)) {
		logit(LOG_ERR, "dreader.access: Invalid readerdef for access %s in line %d", val, linecount);
		continue;
	    }
	    if (*p == '|') {
		p++;
		accessdef.ad_MatchExit = 1;
	    }
	    strncpy(accessdef.ad_Reader, p, sizeof(accessdef.ad_Reader) - 1);
	    p = strtok(val, ",");
	    p = strtok(NULL, ",");
	    for (q = val; q != NULL; q = p, p = strtok(NULL, ",")) {
		char *pp = q;
		if ((q = strchr(pp, '@')) != NULL) {
		    *q++ = '\0';
		    if (!*pp || strlen(pp) > sizeof(accessdef.ad_Pattern)) {
			logit(LOG_ERR, "dreader.access: Bad length for ident user %s in line %d", q, linecount);
			continue;
		    }
		    strncpy(accessdef.ad_IdentUser, pp,
					sizeof(accessdef.ad_IdentUser) - 1);
		} else {
		    q = pp;
		}
		if (!*q || strlen(q) > sizeof(accessdef.ad_Pattern)) {
		    logit(LOG_ERR, "dreader.access: Bad length for access %s in line %d", q, linecount);
		    continue;
		}
		strncpy(accessdef.ad_Pattern, q, sizeof(accessdef.ad_Pattern) - 1);
		fwrite(&accessdef, sizeof(accessdef), 1, raf);
	    }
	    continue;
	}
	logit(LOG_ERR, "dreader.access: Invalid access definition : %s in line %d\n", opt, linecount);
	Access_LastUpdate = 0;
    }
    fclose(fp);
    fclose(vsf);
    fclose(grf);
    fclose(auf);
    fclose(rdf);
    fclose(raf);
    if (Access_LastUpdate == 0) {
	logit(LOG_ERR, "Errors in dreader.access file - not updating");
	return;
    }
    /*
     * We create a lock file that means no other processes should make
     * updates from the cache. The lock file time is also checked to
     * cater for possible race conditions.
     */

    if ((tfp = open(PatDbExpand(DRAccessLockPat), O_RDWR|O_CREAT, 0644)) <= 0) {
	logit(LOG_CRIT, "Cannot create access lock %s",
		PatDbExpand(DRAccessLockPat));
	Access_LastUpdate = 0;
	return;
    }
    if (xflock(tfp, XLOCK_SH) < 0) {
	if (DebugOpt)
	    printf("Unable to obtain access lock (%s)\n", strerror(errno));
	Access_LastUpdate = 0;
	return;
    }

    rename(vsnewbuf, PatDbExpand(DRVserverCachePat));
    rename(grnewbuf, PatDbExpand(DRGroupCachePat));
    rename(aunewbuf, PatDbExpand(DRAuthCachePat));
    rename(rdnewbuf, PatDbExpand(DRReaderCachePat));
    rename(ranewbuf, PatDbExpand(DRAccessCachePat));

    write(tfp, &tfp, 1);
    xflock(tfp, XLOCK_UN);
    close(tfp);
}

/*
 * ReadAccessCache(): mmap the various access cache files into memory
 *   and set the pointers in the accessmap structure. This will be
 *   used to set the pointers for each thread.
 *
 * This has amazing performance benefits and saves memory because
 * we use the static mmap area for all threads
 */
int
ReadAccessCache() {
    int vsf;
    int grf;
    int auf;
    int rdf;
    int raf;
    int tfp;
    struct stat st;
    int i;
    time_t lastmod;
    int fail;

    if (DebugOpt > 1)
	printf("Checking access cache\n");

    if (stat(PatDbExpand(DRAccessLockPat), &st) != 0 ||
		st.st_mtime <= Cache_LastUpdate) {
	if (DebugOpt)
	    printf("No lock or not changed\n");
	return(0);
    }
    lastmod = st.st_mtime;

    /* Obtain a lock so that we know it won't disappear from under us */

    if ((tfp = open(PatDbExpand(DRAccessLockPat), O_RDONLY)) <= 0) {
	logit(LOG_ERR, "Cannot open access lock %s",
		PatDbExpand(DRAccessLockPat));
	return(0);
    }
    if (hflock(tfp, 0, XLOCK_SH) < 0) {
	if (DebugOpt)
	    printf("No lock or not changed\n");
	return(0);
    }

    if (DebugOpt)
	printf("Loading access cache\n");

    fail = 0;
    /* We must be able to load all the files at	the same time */
    if ((vsf = open(PatDbExpand(DRVserverCachePat), O_RDONLY)) <= 0) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(DRVserverCachePat));
	fail = 1;
    }
    if ((grf = open(PatDbExpand(DRGroupCachePat), O_RDONLY)) <= 0) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(DRGroupCachePat));
	close(vsf);
	fail = 1;
    }
    if ((auf = open(PatDbExpand(DRAuthCachePat), O_RDONLY)) <= 0) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(DRAuthCachePat));
	close(vsf);
	close(grf);
	fail = 1;
    }
    if ((raf = open(PatDbExpand(DRAccessCachePat), O_RDONLY)) <= 0) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(DRAccessCachePat));
	close(vsf);
	close(grf);
	close(auf);
	fail = 1;
    }
    if ((rdf = open(PatDbExpand(DRReaderCachePat), O_RDONLY)) <= 0) {
	logit(LOG_CRIT, "Unable to open %s", PatDbExpand(DRReaderCachePat));
	close(vsf);
	close(grf);
	close(auf);
	close(raf);
	fail = 1;
    }

    /* Now that the files are open, we can clear the lock */
    hflock(tfp, 0, XLOCK_UN);
    close(tfp);

    if (fail) {
	if (accessmap.AccessList == NULL)
	    return(-2);
	return(-1);
    }

    /*
     * Preserve the old accessmap for later munmap
     * We do this in case there is a race condition that removes the
     * map too early.  (????)
     */
    memcpy(&oldaccessmap, &accessmap, sizeof(accessmap));
    oldaccessmap.VServerList = accessmap.VServerList;
    oldaccessmap.GroupsList = accessmap.GroupsList;
    oldaccessmap.AuthList = accessmap.AuthList;
    oldaccessmap.ReaderList = accessmap.ReaderList;
    oldaccessmap.GroupMap = accessmap.GroupMap;
    oldaccessmap.AccessList = accessmap.AccessList;

    accessmap.VServerList = NULL;
    accessmap.VServerCount = 0;
    accessmap.GroupsList = NULL;
    accessmap.GroupCount = 0;
    accessmap.GroupMap = NULL;
    accessmap.AuthList = NULL;
    accessmap.AuthCount = 0;
    accessmap.ReaderList = NULL;
    accessmap.ReaderCount = 0;
    accessmap.AccessList = NULL;
    accessmap.AccessCount = 0;

    fstat(vsf, &st);
    accessmap.VServerList = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, vsf, 0);
    accessmap.VServerCount = st.st_size / sizeof(Vserver);
    close(vsf);

    fstat(grf, &st);
    if (st.st_size > 0) {
	char *mapptr;
	GroupDef *gp;
	GroupDef *pgp = NULL;
	GroupList *gl;
	GroupList *pgl = NULL;

	accessmap.GroupMap = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, grf, 0);
	mapptr = accessmap.GroupMap;
	while (*mapptr) {
	    gp = zalloc(&DnsMemPool, sizeof(GroupDef));
	    gp->gr_Groups = NULL;
	    gp->gr_Next = NULL;
	    if (accessmap.GroupsList == NULL)
		accessmap.GroupsList = gp;
	    if (pgp)
		pgp->gr_Next = gp;
	    gp->gr_Name = mapptr;
	    mapptr += strlen(gp->gr_Name) + 1;
	    gp->gr_Count = atoi(mapptr);
	    mapptr += strlen(mapptr) + 1;
	    pgl = NULL;
	    for (i=0; i < gp->gr_Count; i++) {
		gl = zalloc(&DnsMemPool, sizeof(GroupList));
		if (gp->gr_Groups == NULL)
		    gp->gr_Groups = gl;
		if (pgl)
		    pgl->next = gl;
		gl->group = zallocStr(&DnsMemPool, mapptr);
		mapptr += strlen(gl->group) + 1;
		gl->next = NULL;
		pgl = gl;
	    }
	    pgp = gp;
	    accessmap.GroupCount++;
	}
    }
    close(grf);

    fstat(auf, &st);
    accessmap.AuthList = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, auf, 0);
    accessmap.AuthCount = st.st_size / sizeof(AuthDef);
    close(auf);

    fstat(rdf, &st);
    accessmap.ReaderList = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, rdf, 0);
    accessmap.ReaderCount = st.st_size / sizeof(ReaderDef);
    close(rdf);

    fstat(raf, &st);
    accessmap.AccessList = xmap(NULL, st.st_size, PROT_READ, MAP_SHARED, raf, 0);
    accessmap.AccessCount = st.st_size / sizeof(AccessDef);
    close(raf);

    Cache_LastUpdate = lastmod;
    return(1);
}

/*
 * Remove the old mmap entries now that all the threads have been updated
 */
void
ClearOldAccessMap(void) {
    if (DebugOpt)
	printf("Clearing old accessmap\n");
    if (oldaccessmap.VServerList != NULL)
	xunmap(oldaccessmap.VServerList, oldaccessmap.VServerCount * sizeof(Vserver));
    if (oldaccessmap.GroupMap != NULL) {
	xunmap(oldaccessmap.GroupMap, oldaccessmap.GroupMapSize);
	zfree(&DnsMemPool, oldaccessmap.GroupsList, sizeof(GroupDef));
    }
    if (oldaccessmap.AuthList != NULL)
	xunmap(oldaccessmap.AuthList, oldaccessmap.AuthCount * sizeof(AuthDef));
    if (oldaccessmap.ReaderList != NULL)
	xunmap(oldaccessmap.ReaderList, oldaccessmap.ReaderCount * sizeof(ReaderDef));
    if (oldaccessmap.AccessList != NULL)
	xunmap(oldaccessmap.AccessList, oldaccessmap.AccessCount * sizeof(AccessDef));
    bzero(&oldaccessmap, sizeof(oldaccessmap));
}

/*
 * Update all the mmap'ed authentication pointers because something chanaged
 */
void
SetAuthDetails(DnsRes *dres, char *which)
{
    int i;
    ReaderDef *rdp;
    Vserver *vsp;
    GroupDef *gdp;
    AuthDef *adp;

    if (DebugOpt)
	printf("Setting Auth Details for %s\n", which);
    for (rdp = accessmap.ReaderList, i = 0; i < accessmap.ReaderCount;
								i++, rdp++)
	if (strcmp(which, rdp->rd_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in accessmap\n", which);
	    break;
	}
    /* The first reader access line is the default */
    if (i >= accessmap.ReaderCount) {
        if (DebugOpt)
	    printf("Unable to locate %s in accessmap\n", which);
	rdp = accessmap.ReaderList;
    }
    dres->dr_ReaderDef = rdp;
    
    for (vsp = accessmap.VServerList, i = 0; i < accessmap.VServerCount;
								i++, vsp++)
	/*
	 * If we are on a virtual interface, only check for the specified
	 * vserver.
	 */
	if (*dres->dr_VServer) {
	    if (strcmp(dres->dr_VServer, vsp->vs_Name) == 0) {
		if (DebugOpt)
		    printf("Found %s in dres virtuals\n", vsp->vs_Name);
		break;
	    }
	} else if (strcmp(rdp->rd_Vserver, vsp->vs_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in rd virtuals\n", vsp->vs_Name);
	    break;
	}
    /* The first VServer is always the default one */
    if (i >= accessmap.VServerCount) {
        if (DebugOpt)
	    printf("Unable to locate %s in virtuals\n", vsp->vs_Name);
	vsp = accessmap.VServerList;
    }
    dres->dr_VServerDef = vsp;
    
    for (gdp = accessmap.GroupsList, i = 0; i < accessmap.GroupCount;
						i++, gdp = gdp->gr_Next) {
	if (strcmp(rdp->rd_Groups, gdp->gr_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in accessmap groupslist\n", gdp->gr_Name);
	    break;
	}
    }
    /* The first group list is always everything */
    if (i >= accessmap.GroupCount) {
	if (DebugOpt)
	    printf("Unable to locate %s in accessmap groupslist\n", gdp->gr_Name);
	gdp = accessmap.GroupsList;
    }
    dres->dr_GroupDef = gdp;
    
    for (gdp = accessmap.GroupsList, i = 0; i < accessmap.GroupCount;
						i++, gdp = gdp->gr_Next) {
	if (strcmp(rdp->rd_ListGroups, gdp->gr_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in accessmap listgroupslist\n", gdp->gr_Name);
	    break;
	}
    }
    /* The first group list is always everything */
    if (i >= accessmap.GroupCount) {
	if (DebugOpt)
	    printf("Unable to locate %s in accessmap listgroupslist\n", gdp->gr_Name);
	gdp = accessmap.GroupsList;
    }
    dres->dr_ListGroupDef = gdp;
  
    
    for (gdp = accessmap.GroupsList, i = 0; i < accessmap.GroupCount;
						i++, gdp = gdp->gr_Next) {
	if (strcmp(rdp->rd_PostGroups, gdp->gr_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in accessmap postgroupslist\n", gdp->gr_Name);
	    break;
	}
    }
    /* The first group list is always everything */
    if (i >= accessmap.GroupCount) {
	if (DebugOpt)
	    printf("Unable to locate %s in accessmap postgroupslist\n", gdp->gr_Name);
	gdp = accessmap.GroupsList;
    }
    dres->dr_PostGroupDef = gdp;
    
    for (adp = accessmap.AuthList, i = 0; i < accessmap.AuthCount; i++, adp++)
	if (strcmp(rdp->rd_Auth, adp->au_Name) == 0) {
	    if (DebugOpt)
		printf("Found %s in accessmap authlist\n", adp->au_Name);
	    break;
	}
    /* The first auth list is no access */
    if (i >= accessmap.AuthCount) {
	if (DebugOpt)
	    printf("Unable to locate %s in accessmap authlist - USING NO ACCESS\n", adp->au_Name);
	adp = accessmap.AuthList;
    }
    dres->dr_AuthDef = adp;

    if (dres->dr_ReaderDef->rd_Read)
	dres->dr_Flags |= DF_READ;
    else
	dres->dr_Flags &= ~DF_READ;
    if (dres->dr_ReaderDef->rd_Post)
	dres->dr_Flags |= DF_POST;
    else
	dres->dr_Flags &= ~DF_POST;
    if (dres->dr_ReaderDef->rd_Feed)
	dres->dr_Flags |= DF_FEED;
    else
	dres->dr_Flags &= ~DF_FEED;
    if (dres->dr_ReaderDef->rd_Status)
	dres->dr_Flags |= DF_STATUS;
    else
	dres->dr_Flags &= ~DF_STATUS;
    if (dres->dr_ReaderDef->rd_UseProxied)
      dres->dr_Flags |= DF_USEPROXIED;
    else
      dres->dr_Flags &= ~DF_USEPROXIED;
    if (dres->dr_ReaderDef->rd_Quiet)
	dres->dr_Flags |= DF_QUIET;
    else
	dres->dr_Flags &= ~DF_QUIET;
    if (dres->dr_ReaderDef->rd_GroupLog)
	dres->dr_Flags |= DF_GROUPLOG;
    else
	dres->dr_Flags &= ~DF_GROUPLOG;
    if (dres->dr_ReaderDef->rd_ControlPost)
	dres->dr_Flags |= DF_CONTROLPOST;
    else
	dres->dr_Flags &= ~DF_CONTROLPOST;
}

/*
 * This gets called for each thread to update the pointers if the
 * mmapped info had been updated
 */
void
UpdateAuthDetails(ForkDesc *desc)
{
    Connection *conn = desc->d_Data;

    if (DebugOpt)
	printf("Updating auth details\n");
    if (conn && !conn->co_Auth.dr_StaticAuth)
	SetAuthDetails(&conn->co_Auth, conn->co_Auth.dr_ReaderName);
}

void
getRateLimitCmds(char *opt, ReaderDef *rd)
{
   int i;
   int rl;
   char *p;

   rl = atoi(opt);
   while (*opt && !isspace((int)*opt))
	opt++;
   while (*opt && isspace((int)*opt))
	opt++;
   if (!*opt)
	for (i = 0; i < DRBC_XXXX; i++)
	    rd->rd_RateLimit[i] = rl;
   else for (p = strtok(opt, " ,"); p != NULL; p = strtok(NULL, " ,")) {
	if (strcmp(p, "other") == 0)
	    rd->rd_RateLimit[DRBC_NONE] = rl;
	else if (strcmp(p, "article") == 0)
	    rd->rd_RateLimit[DRBC_ARTICLE] = rl;
	else if (strcmp(p, "head") == 0)
	    rd->rd_RateLimit[DRBC_HEAD] = rl;
	else if (strcmp(p, "body") == 0)
	    rd->rd_RateLimit[DRBC_BODY] = rl;
	else if (strcmp(p, "list") == 0)
	    rd->rd_RateLimit[DRBC_LIST] = rl;
	else if (strcmp(p, "xover") == 0)
	    rd->rd_RateLimit[DRBC_XOVER] = rl;
	else if (strcmp(p, "xhdr") == 0)
	    rd->rd_RateLimit[DRBC_XHDR] = rl;
	else if (strcmp(p, "xzver") == 0)
	    rd->rd_RateLimit[DRBC_XZVER] = rl;
   }
}

/*
 * PrintAuthDetails(): Used for debugging - print all auth info for a thread
 */
void
PrintAuthDetails(DnsRes *dres)
{
    int i;
    printf("dr_ReaderName: %s\n", dres->dr_ReaderName);
    printf("dr_Flags: %x\n", dres->dr_Flags);
    printf("rd_Name: %s\n", dres->dr_ReaderDef->rd_Name);
    printf(" rd_Read: %d\n", dres->dr_ReaderDef->rd_Read);
    printf(" rd_Post: %d\n", dres->dr_ReaderDef->rd_Post);
    printf(" rd_Auth: %s\n", dres->dr_ReaderDef->rd_Auth);
    printf(" rd_Groups: %s\n", dres->dr_ReaderDef->rd_Groups);
    printf(" rd_ListGroups: %s\n", dres->dr_ReaderDef->rd_ListGroups);
    printf(" rd_PostGroups: %s\n", dres->dr_ReaderDef->rd_PostGroups);
    printf(" rd_Vserver: %s\n", dres->dr_ReaderDef->rd_Vserver);
    printf(" rd_MaxConnTotal: %d\n", dres->dr_ReaderDef->rd_MaxConnTotal);
    printf(" rd_MaxConnPerHost: %d\n", dres->dr_ReaderDef->rd_MaxConnPerHost);
    printf(" rd_MaxConnPerUser: %d\n", dres->dr_ReaderDef->rd_MaxConnPerUser);
    printf(" rd_MaxConnPerGroup: %d\n", dres->dr_ReaderDef->rd_MaxConnPerGroup);
    printf(" rd_MaxConnPerVs: %d\n", dres->dr_ReaderDef->rd_MaxConnPerVs);
    printf(" rd_RateLimitTax: %d\n", dres->dr_ReaderDef->rd_RateLimitTax);
    printf(" rd_RateLimitRange: %d-%d\n", dres->dr_ReaderDef->rd_RateLimitRangeLow, dres->dr_ReaderDef->rd_RateLimitRangeHigh);
    printf(" rd_RateLimit: ");
    for (i = 0; i < DRBC_XXXX; i++)
	printf("%d ", dres->dr_ReaderDef->rd_RateLimit[i]);
    printf("\n");
    printf(" rd_ByteLimit: %d\n", dres->dr_ReaderDef->rd_ByteLimit);
    printf(" rd_IdleTimeout: %d\n", (int)dres->dr_ReaderDef->rd_IdleTimeout);
    printf(" rd_SessionTimeout: %d\n", (int)dres->dr_ReaderDef->rd_SessionTimeout);
    printf(" rd_GroupLog: %d\n", dres->dr_ReaderDef->rd_GroupLog);
    printf(" rd_UseVerifiedDns: %d\n", dres->dr_ReaderDef->rd_UseVerifiedDns);
    printf(" rd_DenyMismatchedDns: %d\n", dres->dr_ReaderDef->rd_DenyMismatchedDns);
    printf(" rd_DenyNoDns: %d\n", dres->dr_ReaderDef->rd_DenyNoDns);
    printf(" rd_CheckPostGroups: %d\n", dres->dr_ReaderDef->rd_CheckPostGroups);
    printf(" rd_LogCmd: %d\n", dres->dr_ReaderDef->rd_LogCmd);
#ifdef	IP_TOS
    printf(" rd_SetTOS: %d\n", dres->dr_ReaderDef->rd_SetTOS);
#endif
    printf(" rd_RxBufSize: %d\n", dres->dr_ReaderDef->rd_RxBufSize);
    printf(" rd_TxBufSize: %d\n", dres->dr_ReaderDef->rd_TxBufSize);
   printf("vs_Name: %s\n", dres->dr_VServerDef->vs_Name);
    printf(" vs_HostName: %s\n", dres->dr_VServerDef->vs_HostName);
    printf(" vs_ClusterName: %s\n", dres->dr_VServerDef->vs_ClusterName);
    printf(" vs_PostPath: %s\n", dres->dr_VServerDef->vs_PostPath);
    printf(" vs_NoXrefHostUpdate: %d\n", dres->dr_VServerDef->vs_NoXrefHostUpdate);
    printf(" vs_NoReadPath: %d\n", dres->dr_VServerDef->vs_NoReadPath);
    printf(" vs_NewsAdm: %s\n", dres->dr_VServerDef->vs_NewsAdm);
    printf(" vs_Org: %s\n", dres->dr_VServerDef->vs_Org);
    printf(" vs_AbuseTo: %s\n", dres->dr_VServerDef->vs_AbuseTo);
    printf(" vs_CryptPw: %s\n", dres->dr_VServerDef->vs_CryptPw);
    printf(" vs_Interface: %s\n", "XXX"); /* dres->dr_VServerDef->vs_Interface); */
    printf(" vs_AccessFile: %s\n", dres->dr_VServerDef->vs_AccessFile);
   printf("au_Name: %s\n", dres->dr_AuthDef->au_Name);
   printf(" au_File: %s\n", dres->dr_AuthDef->au_File);
   printf(" au_Cdb: %s\n", dres->dr_AuthDef->au_Cdb);
   printf(" au_Db: %s\n", dres->dr_AuthDef->au_Db);
   printf(" au_External: %s\n", dres->dr_AuthDef->au_External);
   printf(" au_Radius: %s\n", dres->dr_AuthDef->au_Radius);
   printf(" au_NetRemote: %s\n", dres->dr_AuthDef->au_NetRemote);
   printf(" au_LDAP: %s\n", dres->dr_AuthDef->au_LDAP);
   printf(" au_Perl: %s\n", dres->dr_AuthDef->au_Perl);
   printf(" au_PAM: %s\n", dres->dr_AuthDef->au_PAM);
   printf(" au_User: %s\n", dres->dr_AuthDef->au_User);
   printf(" au_Pass: %s\n", dres->dr_AuthDef->au_Pass);
   printf(" au_AddRealm: %s\n", dres->dr_AuthDef->au_AddRealm);
   printf(" au_Realm: %s\n", dres->dr_AuthDef->au_Realm);
   printf(" au_Ident: %d\n", dres->dr_AuthDef->au_Ident);
   printf("gr_Name: %s (%d)\n", dres->dr_GroupDef->gr_Name, dres->dr_GroupDef->gr_Count);
   {
	GroupList *gp;
	int i;
	for (gp = dres->dr_GroupDef->gr_Groups, i = 0;
		 i < dres->dr_GroupDef->gr_Count; i++, gp = gp->next)
	    printf(" group: %s\n", gp->group);
   }
}
