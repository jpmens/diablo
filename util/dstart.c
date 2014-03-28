
/*
 * UTIL/DSTART.C - start diablo or dreaderd
 *
 *	This is a startup program for the diablo/dreaderd daemons.
 *
 *	It opens the listen port as root, switches uid/gid and then
 *	executes diablo or dreaderd from the dbin directory. It is
 *	designed to be run suid, although there are no guarantees
 *	as to its security. It should be no worse than the current
 *	method of starting diablo as root.
 *
 *	Note that it only handles a single bind() address. It seems
 *	dreaderd allows multiple binds. Oh well, start dreaderd as
 *	root if that is needed.
 */

#include "defs.h"

int	TxBufSize;
int	RxBufSize;
char	*NewsBindHost = NULL;
char	*NewsService = "nntp";

int
main(int ac, char **av)
{
    char *argv[255];
    char *env[1];
    char buf[64];
    int i;
    int lfd;

    if (ac < 2 || (strcmp(av[1], "diablo") != 0 &&
					strcmp(av[1], "dreaderd") != 0)) {
	fprintf(stderr, "Usage: %s diablo|dreaderd [options]\n", av[0]);
	exit(1);
    }

    LoadDiabloConfig(ac, av);

    /*
     * Options
     */

    {
	int i;
	char *p;

	for (i = 2; i < ac; ++i) {
	    char *ptr = av[i];

	    if (*ptr != '-')
		continue;
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'B':
		if (*ptr == 0)
		    ptr = av[++i];
		if ((p = strrchr(ptr, ':')) != NULL) {
		    *p++ = 0;
		    NewsService = p;
		    NewsBindHost = strdup(SanitiseAddr(ptr));
		} else { 
		    NewsBindHost = strdup(SanitiseAddr(ptr));
		}
		break;
	    case 'C':
		if (*ptr == 0)
		    ++i;
		break;
	    case 'P':
		if (*ptr == 0)
		    ptr = av[++i];
		if ((p = strrchr(ptr, ':')) != NULL) {
		    *p++ = 0;
		    NewsService = p;
		    NewsBindHost = strdup(SanitiseAddr(ptr));
		} else { 
		    NewsService = ptr;
		}
		break;
	    case 'R':
		RxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		if (RxBufSize < 4096)
		    RxBufSize = 4096;
		break;
	    case 'T':
		TxBufSize = strtol(((*ptr) ? ptr : av[++i]), NULL, 0);
		break;
	    case 'V':
		PrintVersion();
		break;
	    }
	}
    }

    {
	struct sockaddr_in sin;
	/*
	 * listen socket for news
	 */
	memset(&sin, 0, sizeof(sin));
	lfd = socket(AF_INET, SOCK_STREAM, 0);

	if (lfd < 0) {
	    perror("socket");
	    exit(1);
	}

	{
	    int on = 1;
	    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
	    setsockopt(lfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	}
	if (TxBufSize) {
	    setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, (void *)&TxBufSize, sizeof(int));
	}
	if (RxBufSize) {
	    setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, (void *)&RxBufSize, sizeof(int));
	}

	if (NewsBindHost == NULL)
	    sin.sin_addr.s_addr = INADDR_ANY;
	else if (strtol(NewsBindHost, NULL, 0) > 0) {
	    sin.sin_addr.s_addr = inet_addr(NewsBindHost);
	} else {
	    struct hostent *he;

	    if ((he = gethostbyname(NewsBindHost)) != NULL) {
		sin.sin_addr = *(struct in_addr *)he->h_addr;
	    } else {
		fprintf(stderr, "Unknown host for bindhost option: %s\n",
								NewsBindHost);
		exit(1);
	    }
	}
        sin.sin_port = strtol(NewsService, NULL, 0);
	if (sin.sin_port == 0) {
	    struct servent *se;
	    se = getservbyname(NewsService, NULL);
	    if (se == NULL) {
		fprintf(stderr, "Unknown service name: %s\n", NewsService);
		exit(1);
	    }
	    sin.sin_port = se->s_port;
	} else {
	    sin.sin_port = htons(sin.sin_port);
	}
	sin.sin_family = AF_INET;
	if (bind(lfd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    perror("bind");
	    exit(1);
	}
    }
    printf("Listening on %s:%s\n",
			NewsBindHost ? NewsBindHost : "ALL", NewsService);

#if NONBLK_ACCEPT_BROKEN
    /* HPUX is broken, see lib/config.h */
#else
    fcntl(lfd, F_SETFL, O_NONBLOCK);
#endif

    /*
     * change my uid/gid
     */
 
    {
        struct passwd *pw = getpwnam("news");
        struct group *gr = getgrnam("news");
        gid_t gid;
 
        if (pw == NULL) {
            perror("getpwnam('news')");
            exit(1);
        }
        if (gr == NULL) {
            perror("getgrnam('news')");
            exit(1);
        }
        gid = gr->gr_gid;
        setgroups(1, &gid);
        setgid(gr->gr_gid);
        setuid(pw->pw_uid);
    }

    if (strcmp(av[1], "diablo") == 0)
	argv[0] = strdup(PatExpand("%s/dbin/diablo"));
    else
	argv[0] = strdup(PatExpand("%s/dbin/dreaderd"));
    sprintf(buf, "-b%d", lfd);
    argv[1] = buf;
    av += 2;
    for (i=2; i<ac; i++)
	argv[i] = *av++;
    argv[i] = NULL;
    env[0] = NULL;
    execve(argv[0], argv, env);
    fprintf(stderr, "diablostart cant exec %s: %s\n", argv[0], strerror(errno));
    _exit(1);
    return(0);
}
