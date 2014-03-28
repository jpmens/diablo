
/*
 * LIB/HOSTAUTH.C
 *
 * (c)Copyright 1997-1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype int LoadHostAccess(time_t t, int force, int rebuildtime);
Prototype char *Authenticate(int fd, struct sockaddr *res, char *ipst, char *label);
Prototype void DumpHostCache(const char *fname);

#define	TESTTYPE_NONE	0
#define	TESTTYPE_ANY	1
#define	TESTTYPE_IP	2
#define	TESTTYPE_FQDN	4

typedef struct HostAccess {
    char		entry[256];
    int			testtype;
    char 		addrst[NI_MAXHOST];
    char		label[256];
} HostAccess;

time_t HATDSec = 0;
time_t HAMTime = 0;
time_t HACMTime = 0;
time_t DNMTime = 0;

int convHost(char *host, HostAccess *HA, FILE *fo);
void loadDiabloHosts(FILE *fo, int t, int *count);
void loadDnewsFeeds(FILE *fo, int t, int *count);

/*
 * LoadAccess() - [re]load diablo.hosts file
 *
 * This load is to create a cache file of the diablo.hosts file
 * that contains the IP addresses of all the hosts found. This
 * makes authentication a lot quicker and easier.
 *
 * If a time of '0' is passed, we don't fork and don't use syslog
 * 
 */

int
LoadHostAccess(time_t t, int force, int rebuildtime)
{
    int timeChanged = 0;
    struct stat st = { 0 };
    pid_t pid;

    /*
     * check for diablo.hosts file modified once a minute
     */

    if (!force  && (t / 10 <= HATDSec))
	return(0);

    timeChanged = 1;

    HATDSec = t / 10;

    /* Don't do this if diablo.hosts hasn't been modified  or we are
     * within the rebuild time limit
     */
    if (!force && (t - HACMTime) < rebuildtime &&
		stat(PatDbExpand(DHostsCachePat), &st) == 0) {
	int ok = 1;
	if (stat(PatLibExpand(DiabloHostsPat), &st) == 0) {
	    if (st.st_mtime != HAMTime)
		ok = 0;
	} else {
	    if (HAMTime != 0)	/* File deleted */
		ok = 0;
	}
	if (stat(PatLibExpand(DNewsfeedsPat), &st) == 0) {
	    if (st.st_mtime != DNMTime)
		ok = 0;
	} else {
	    if (DNMTime != 0)	/* File deleted */
		ok = 0;
	}
	if (ok)
	    return(0);
    }

    if (stat(PatLibExpand(DiabloHostsPat), &st) == 0)
	HAMTime = st.st_mtime;
    else
	HAMTime = 0;
    if (stat(PatLibExpand(DNewsfeedsPat), &st) == 0)
	DNMTime = st.st_mtime;
    else
	DNMTime = 0;

    HACMTime = t;

    /*
     * Do the DNS lookups in the background
     * The main loop does a wait to cleanup the children
     */
    if (t != 0)
	pid = fork();
    else
	pid = 0;
    if (pid == 0) {
	FILE *fo;
        int lockfd;
	char buf[PATH_MAX];
	int count = 0;

	if ((lockfd = open(PatDbExpand(DHostsLockPat), O_RDWR|O_CREAT, 0644)) < 0) {
	    if (t != 0)
		logit(LOG_INFO, "%s : Unable to create lockfile (%s)",
				PatDbExpand(DHostsLockPat), strerror(errno));
	    else
		printf("%s : Unable to create lockfile (%s)\n",
				PatDbExpand(DHostsLockPat), strerror(errno));
	    exit(0);
	    
	}
	if (xflock(lockfd, XLOCK_EX|XLOCK_NB) == -1) {
	    if (t != 0)
		logit(LOG_INFO, "%s : Rebuild already in progress",
						PatDbExpand(DHostsCachePat));
	    else
		printf("%s : Rebuild already in progress\n",
						PatDbExpand(DHostsCachePat));
	    close(lockfd);
	    exit(0);
	}
	sprintf(buf, "%d", (int)getpid());
	write(lockfd, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "%s.new", PatDbExpand(DHostsCachePat));

	fo = fopen(buf, "w");
	if (fo == NULL) {
	    if (t != 0)
		logit(LOG_CRIT, "%s: %s", buf, strerror(errno));
	    else
		fprintf(stderr, "%s: %s\n", buf, strerror(errno));
	    xflock(lockfd, XLOCK_UN);
	    exit(1);
	}

	if (t != 0)
	    logit(LOG_INFO, "Rebuilding host cache");
	else
	    printf("Rebuilding host cache\n");

	loadDnewsFeeds(fo, t, &count);
	loadDiabloHosts(fo, t, &count);
	if (count == 0)
	    logit(LOG_INFO, "No incoming hosts found");
	else if (DebugOpt)
	    printf("Found %d incoming hosts\n", count);

	fclose(fo);

	/*
	 * Now rename the newly created cache to its correct name
	 */
	snprintf(buf, sizeof(buf), "%s.new", PatDbExpand(DHostsCachePat));
	if ((stat(buf, &st) == 0 && st.st_size > 0) ||
		stat(PatDbExpand(DHostsCachePat), &st) != 0) {
	    rename(buf, PatDbExpand(DHostsCachePat));
	} else {
	    unlink(buf);
	}
	xflock(lockfd, XLOCK_UN);
	close(lockfd);
	if (t != 0)
	    exit(0);
    }
    return(pid);
}

int
convHost(char *host, HostAccess *HA, FILE *fo)
{
    int found = 0;

    /*
     * Handle the forcing of entry type
     */
    HA->testtype = TESTTYPE_NONE;
    if (strncmp(host, "IP:", 3) == 0) {
	HA->testtype |= TESTTYPE_IP;
	host += 3;
    } else if (strncmp(host, "FQDN:", 5) == 0) {
	HA->testtype |= TESTTYPE_FQDN;
	host += 5;
    }

    /*
     * If this entry is an IP, then only match against an IP
     * otherwise only match against the hostname
     */
    if (HA->testtype == TESTTYPE_NONE) {
	if (IsIpAddr(host))
	    HA->testtype |= TESTTYPE_IP;
	else
	    HA->testtype |= TESTTYPE_FQDN;
    }

    strcpy(HA->entry, host);
    HA->addrst[0] = 0;

    /*
     * Don't lookup IP for wildcards/CIDR
     */
    if (strchr(host, '*') != NULL || strchr(host, '?') != NULL ||
						strchr(host, '/') != NULL) {
	fwrite(HA, sizeof(*HA), 1, fo);
	return(1);
    }

#ifdef INET6
    {
	struct addrinfo *ai0;

	if (getaddrinfo(host, NULL, NULL, &ai0) == 0) {
	    struct addrinfo *ai;

	    for (ai = ai0; ai; ai = ai->ai_next) {
		if (getnameinfo(ai->ai_addr, ai->ai_addrlen, HA->addrst,
					sizeof(HA->addrst), NULL,
					0, NI_NUMERICHOST) == 0)
		    HA->testtype |= TESTTYPE_IP;
		    fwrite(HA, sizeof(*HA), 1, fo);
		    found++;
	    }
	    freeaddrinfo(ai0);
	}
    }
#else
    {
	struct hostent *he;
	int i;

	if ((he = gethostbyname(host)) != NULL)
	    for (i = 0; he->h_addr_list && he->h_addr_list[i] != NULL; ++i) {
		struct in_addr *haddr = (void *)he->h_addr_list[i];
		strcpy(HA->addrst, inet_ntoa(*haddr));
		HA->testtype |= TESTTYPE_IP;
		fwrite(HA, sizeof(*HA), 1, fo);
		found++;
	    }
    }
#endif
    return(found);
}

void
loadDiabloHosts(FILE *fo, int t, int *count)
{
    FILE *fi;
    char buf[PATH_MAX];
    HostAccess HA;
    char *s;
    char *l;
    char *p;

    if (DebugOpt)
	printf("Search for hosts in %s\n", PatLibExpand(DiabloHostsPat));

    fi = fopen(PatLibExpand(DiabloHostsPat), "r");
    if (fi == NULL)
	return;

    bzero(&HA, sizeof(HA));
    while (fgets(buf, sizeof(buf), fi) != NULL) {
	p = buf;
	if (*p == '\n' || *p == '#')
	    continue;
	HA.testtype = TESTTYPE_NONE;
	if (strncmp(p, "IP:", 3) == 0) {
	    HA.testtype |= TESTTYPE_IP;
	    p += 3;
	} else if (strncmp(p, "FQDN:", 5) == 0) {
	    HA.testtype |= TESTTYPE_FQDN;
	    p += 5;
	}
	s = strtok(p, ": \t\r\n");	/* hostname	*/
	if (s == NULL || !*s)
	    continue;
        l = strtok(NULL, ": \t\r\n");	/* label	*/
	if (l == NULL || !*l)
	    continue;

	if (strlen(s) > 255 || strlen(l) > 255)
	    continue;

	strcpy(HA.label, l);
	if (convHost(s, &HA, fo))
	    (*count)++;
	bzero(&HA, sizeof(HA));
    }
    fclose(fi);
}

void
loadDnewsFeeds(FILE *fo, int t, int *count)
{
    FILE *fi;
    char buf[512];
    HostAccess HA;
    char label[255];

    if (DebugOpt)
	printf("Search for hosts in %s\n", PatLibExpand(DNewsfeedsPat));

    fi = iopen(PatLibExpand(DNewsfeedsPat), "r");
    if (fi == NULL) {
	if (t != 0)
	    logit(LOG_CRIT, "%s: %s", PatLibExpand(DNewsfeedsPat),
							strerror(errno));
	else
	    fprintf(stderr, "%s: %s\n", PatLibExpand(DNewsfeedsPat),
							strerror(errno));
	exit(1);
    }

    bzero(&HA, sizeof(HA));
    while (igets(buf, sizeof(buf), fi) != NULL) {
	char *cmd = strtok(buf, " \t\n");
	char *p = (cmd != NULL) ? strtok(NULL, " \t\n") : NULL;
	if (cmd == NULL || p == NULL || *cmd == '#')
	    continue;

	if (strcmp(cmd, "end") == 0) {
	    label[0] = 0;
	}
	if (strcmp(cmd, "label") == 0) {
	    strcpy(label, p);
	    continue;
	}
	if (!label[0])
	    continue;

	if (strcmp(cmd, "inhost") == 0 || strcmp(cmd, "host") == 0) {
	    strcpy(HA.label, label);
	    if (convHost(p, &HA, fo))
		(*count)++;
	    bzero(&HA, sizeof(HA));
	    continue;
	}
    }
    iclose(fi);
}

/*
 * AUTHENTICATION()	- authenticate a new connection
 *
 *   We follow the following procedure:
 *
 *	- if access entry is IP, only match with IP
 *	- if access entry is FQDN, only match with FQDN
 *	- if IP has no hostname, only match IP
 *	- if IP Fwd/Rev mismatch, only match IP
 *	- if IP has valid hname, match with IP or hname
 *
 *	Returns the hostname or IP if hostname not found/invalid
 */

char *
Authenticate(int fd, struct sockaddr *res, char *ipst, char *label)
{
    char *hname = NULL;		/* authenticated hostname or NULL */
    char *pname = NULL;		/* IP address (as string)	  */
    int isValid = 0;

    /*
     * check reverse lookup verses forward lookup, set either hname or uname
     * or neither, but never both.
     */

#ifdef INET6
    {
	char *st;
	char hostst[NI_MAXHOST];
	int error;

	st = NetAddrToSt(0, res, 1, 0, 0);
	if (st != NULL) {
	    pname = zallocStr(&SysMemPool, st);
	} else {
	    logit(LOG_ERR, "getnameinfo: Unable to get numeric address");
	    return(NULL);
	}
	st[0]= 0;
	if ((error = getnameinfo(res, SA_LEN(res), hostst, sizeof(hostst),
					NULL, 0, NI_NAMEREQD)) == 0) {
	    if (TestForwardLookup(hostst, (const char *)ipst, res) >= 0)
		hname = zallocStr(&SysMemPool, hostst);
	} else {
	    fprintf(stderr, "getnameinfo: %d", error);
	}
    }
#else
    struct sockaddr_in *sin = (struct sockaddr_in *)res;

    /*
     * set pname
     */
    pname = zallocStr(&SysMemPool, inet_ntoa(sin->sin_addr));

    {
	struct hostent *he;

	he = gethostbyaddr(
	    (char *)&sin->sin_addr,
	    sizeof(sin->sin_addr),
	    AF_INET
	);

	if (he != NULL) {
	    hname = zallocStr(&SysMemPool, he->h_name);
	    if (TestForwardLookup(hname, (const char *)ipst, res) < 0) {
		zfreeStr(&SysMemPool, &hname);
		hname = NULL;
	    }
	}
    }

#endif /* INET6 */

    /*
     * Check IP against cache file of diablo.hosts. 
     */
    {
	FILE *fi = fopen(PatDbExpand(DHostsCachePat), "r");

	if (fi) {
	    HostAccess HA;

	    while (fread(&HA, sizeof(HA), 1, fi) == 1) {
		if (HA.addrst[0] && (HA.testtype & TESTTYPE_IP) && strcmp(ipst, HA.addrst) == 0) {
		    isValid = 1;
		    break;
		} else if ((strchr(HA.entry, '/') != NULL) && (HA.testtype & TESTTYPE_IP) && CidrMatch(HA.entry, pname)) {
		    isValid = 2;
		    break;
		} else if (pname && (HA.testtype & TESTTYPE_IP) && WildCaseCmp(HA.entry, pname) == 0) {
		    isValid = 3;
		    break;
		} else if (hname && (HA.testtype & TESTTYPE_FQDN) && strcasecmp(HA.entry, hname) == 0) {
		    isValid = 4;
		    break;
		} else if (hname && (HA.testtype & TESTTYPE_FQDN) && WildCaseCmp(HA.entry, hname) == 0) {
		    isValid = 5;
		    break;
		}
	    }
	    if (isValid) {
		char *method = "UNKNOWN";
		if (isValid == 1)
		    method = "Exact IP match";
		if (isValid == 2)
		    method = "CIDR match";
		if (isValid == 3)
		    method = "Wildcmp IP match";
		if (isValid == 4)
		    method = "Exact Hostname match";
		if (isValid == 5)
		    method = "Wildcmp hostname match";
		logit(LOG_DEBUG, "Matched %s (%s) to %s (label:%s) using %s",
					hname ? hname : "NOTFOUND", pname,
					HA.entry, HA.label,
					method);
		if (HA.label[0])
		    strcpy(label, HA.label);
		else
		    label[0] = 0;
	    }
	    fclose(fi);
	} else {
	    logit(LOG_ERR, "%s file not found", PatDbExpand(DHostsCachePat));
	}
    }

    /*
     * If we have a valid connection, but we were
     * unable to resolve the FQDN, we make the FQDN
     * the IP address.
     */
    if (isValid) {
	if (hname == NULL) {
	    hname = pname;
	    pname = NULL;
	}
    } else {
	if (hname)
	    zfreeStr(&SysMemPool, &hname);
    }
    zfreeStr(&SysMemPool, &pname);
    return(hname);
}

void
DumpHostCache(const char *fname)
{
   FILE *fi = fopen(fname, "r");

    if (fi) {
	HostAccess HA;
        char tt[20];

	while (fread(&HA, sizeof(HA), 1, fi) == 1) {
	    strcpy(tt, "");
	    if (HA.testtype & TESTTYPE_NONE)
		strcat(tt, "NONE:");
	    if (HA.testtype & TESTTYPE_IP)
		strcat(tt, "IP:");
	    if (HA.testtype & TESTTYPE_FQDN)
		strcat(tt, "H:");
	    if (HA.testtype & TESTTYPE_ANY)
		strcat(tt, "ANY:");
	    printf("%s label=%s type=%s ip=%s\n", HA.entry, HA.label, tt,
				(HA.testtype & TESTTYPE_IP) ? HA.addrst : "NONE");
	}
    } else {
	fprintf(stderr, "Unable to open %s\n", fname);
    }
}
