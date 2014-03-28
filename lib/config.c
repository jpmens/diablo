
/*
 * LIB/CONFIG.C	- Scan configuration file
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void LoadDiabloConfig(int ac, char **av);
Prototype void PrintVersion(void);
Prototype void SetMyHostName(char **hostname);
Prototype void SetNewsAdmin(char **admin, char *host);
Prototype void AddBindList(BindList **blist, char *interface, char *port, int append);
Prototype int SetGroupHashMethod(const char *opt, GroupHashType *pmethod);
Prototype int SetCommand(FILE *fo, char *cmd, char *opt, int *cmdErr);
Prototype int CheckConfigOptions(int which);
Prototype void DumpConfigOptions(FILE *fo, char *cmd, int which);

Prototype struct DiabloOpts DOpts;

int SetHashMethod(const char *opt, int *pmethod);
int SetCacheDirs(const char *opt, CacheDirType *cachedir);

struct DiabloOpts DOpts;

void
PrintVersion(void)
{
    printf("DIABLO Version %s - %s\n", VERS, SUBREV);
    exit(0);
}

void
SetMyHostName(char **hostname)
{
    char buf[1024];

    buf[sizeof(buf)-1] = 0;
    if (gethostname(buf, sizeof(buf) - 1) == -1)
	strcpy(buf, "localhost");
    *hostname = strdup(buf);
} 

void
SetNewsAdmin(char **admin, char *host)
{
    char buf[1024];

    snprintf(buf, sizeof(buf), "news@%s", host);
    *admin = strdup(buf);
}

void
AddBindList(BindList **blist, char *interface, char *port, int append)
{
    BindList *bl;
    bl = (BindList *)malloc(sizeof(BindList));
    bl->bl_Host = strdup(interface);
    bl->bl_Port = strdup(port);
    bl->bl_Next = NULL;
    if (*blist == NULL) {
	*blist = bl;
	return;
    }
    if (!append && *blist != NULL) {
	BindList *tbl;
	while (*blist != NULL) {
	    tbl = *blist;
	    *blist = (*blist)->bl_Next;
	    if (tbl->bl_Host != NULL)
		free(tbl->bl_Host);
	    if (tbl->bl_Port != NULL)
		free(tbl->bl_Port);
	    free(tbl);
	}
	*blist = bl;
    } else {
	BindList *tbl = *blist;
	while (tbl->bl_Next != NULL)
	    tbl = tbl->bl_Next;
	tbl->bl_Next = bl;
    }
}

void
initDOpts(void)
{
    int i;
    /*
     * Set all the default options for the global config
     */
    memset(&DOpts, 0, sizeof(DOpts));
    DOpts.HashMethod = HASH_CRC;
    DOpts.CompatHashMethod = HASH_CRC;
    DOpts.ReaderGroupHashMethod.gh_type = HASHGRP_CRC;
    DOpts.ReaderGroupHashMethod.gh_sigbytes = 20;
    DOpts.ReaderGroupHashMethod.gh_dirtype = HASHGRP_DIR_BIT;
    DOpts.ReaderGroupHashMethod.gh_dirlvl = 1;
    DOpts.ReaderGroupHashMethod.gh_dirinfo[0] = 2;
    DOpts.HashSize = 16 * 1024 * 1024;
    DOpts.FeederBufferSize = 1;
    DOpts.FeederMaxArtSize = 10000000;
    DOpts.FeederArtTypes = 1;
    DOpts.FeederPreloadArt = 1;
    DOpts.SpoolPreloadArt = 1;
    DOpts.FeederMaxHeaderSize = 64 * 1024;
    DOpts.ReaderMaxArtSize = OVER_HMAPSIZE / 2;
    DOpts.ReaderForks = 10;
    DOpts.ReaderThreads = 40;
    DOpts.ReaderFeedForks = 2;
    DOpts.ReaderDns = 5;
    DOpts.ReaderCacheMode = 1;
    DOpts.ReaderCacheHashSize = 4096;
    DOpts.ReaderXOverMode = 1;
    DOpts.ReaderAutoAddToActive = 0;
    DOpts.FeederAutoAddToActive = 0;
    DOpts.ReaderDetailLog = 1;
    DOpts.ReaderIdentTimeout = 10;
    DOpts.RememberSecs = 14 * 24 * 60 * 60;
    DOpts.FeederMaxAcceptAge = DOpts.RememberSecs;
    DOpts.HostCacheRebuildTime = 60 * 60;
    DOpts.DisplayAdminVersion = 1;
    DOpts.FeederRTStats = RTSTATS_NONE;
    DOpts.SpamFilterOpt = NULL;
    DOpts.FeederXRefHost = NULL;
    DOpts.ReaderXRefHost = NULL;
    DOpts.ReaderXRefSlaveHost = NULL;
    DOpts.FeederPathHost = NULL;
    DOpts.FeederFilter = NULL;
    DOpts.ReaderPathHost = NULL;
    DOpts.NewsAdmin = NULL;
    DOpts.FeederHostName = NULL;
    DOpts.ReaderHostName = NULL;
    DOpts.NewsMaster = NULL;
    DOpts.ReaderCrashHandler = strdup("none");
    DOpts.PathList = NULL;
    DOpts.ReaderBan = NULL;
    DOpts.FeederBindHost = NULL;
    DOpts.FeederPort = strdup("nntp");
    DOpts.ReaderBindHost = NULL;
    DOpts.ReaderPort = strdup("nntp");
    DOpts.ReaderCacheDirs.dt_dirlvl = 1;
    DOpts.ReaderCacheDirs.dt_dirinfo[0] = 512;
    for (i = 0; i < 10; i++)
	DOpts.PgpVerifyArgs[i] = NULL;
    DOpts.PostXFilter = NULL;
}

void
LoadDiabloConfig(int ac, char **av)
{
    FILE *fi;
    char buf[256];
    int versOk = 0;
    int exitMe = 0;
    int i;
    const char *diabloConfig;

    /*
     * Figure out path to diablo.config.  Check environment first, then
     * look for -C option to override it.  If neither is given, use the
     * default.
     */

    if ((diabloConfig = getenv("DIABLO_CONFIG_PATH")) == NULL) {
        diabloConfig = "%s/diablo.config";
    }

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (ptr[0] == '-' && ptr[1] == 'C') {
	    diabloConfig = ptr[2] ? ptr + 2 : av[++i];
	}
    }
    if (diabloConfig == NULL) {
	fprintf(stderr, "Expected argument to -C option\n");
	exit(1);
    }

    initDOpts();

    if ((fi = xfopen("r", diabloConfig, NewsHome)) != NULL) {
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    char *cmd;
	    char *opt = buf;
	    int cmdErr = 1;
	    int i;

	    if (buf[0] == '\n' || buf[0] == '#')
		continue;
	    if ((cmd = strchr(buf, '#')) != NULL)
		*cmd = 0;
	    while (isspace((int)*opt))
		opt++;
	    while ((i = strlen(opt) - 1) >= 0 && isspace((int)opt[i]))
		opt[i] = 0;
	    if (strlen(opt) == 0)
		continue;
	    if ((cmd = strsep(&opt, " \t\n")) != NULL && (opt != NULL)) {
		int optErr = 1;

		while (isspace((int)*opt))
		    opt++;
		cmdErr = 0;
		if (strcasecmp(cmd, "version") == 0) {
		    optErr = 0;
		    if (strtod(opt, NULL) > strtod(VERS, NULL))
			versOk = 0;
		    else
			versOk = 1;
		} else if (versOk == 0) {
		    optErr = 0;
		} else if (strcasecmp(cmd, "expire") == 0) {
			/* Now unused */
		    optErr = 0;
		} else if (strcasecmp(cmd, "hash") == 0) {
		    if (opt) {
			char *p2;
			if ((p2 = strchr(opt, '/')) != NULL)
			    *p2++ = 0;
			optErr = SetHashMethod(opt, &DOpts.HashMethod);
			if (p2)
			    optErr = SetHashMethod(p2, &DOpts.CompatHashMethod);
		    }
		} else if (strcasecmp(cmd, "readergrouphash") == 0) {
		    if (opt) {
			optErr = SetGroupHashMethod(opt, &DOpts.ReaderGroupHashMethod);
			if (optErr != 0)
			    logit(LOG_CRIT, "Illegal reader group hash: %s", opt);
		    }
		} else if (strcasecmp(cmd, "hsize") == 0) {
		    if (opt) {
			uint32 n = bsizetol(opt);

			optErr = 0;
			if (n < 256 * 1024 || n > (uint32)512 * 1024 * 1024) {
			    optErr = 1;
			    fprintf(stderr, "Illegal Hash size: %d\n", n);
			    logit(LOG_CRIT, "Illegal Hash size: %d", n);
			}
			if ((n ^ (n - 1)) != (n << 1) - 1) {
			    optErr = 1;
			    fprintf(stderr, "Hash size not a power of 2: %d\n", n);
			    logit(LOG_CRIT, "Hash size not a power of 2: %d", n);
			}
			if (optErr == 0)
			    DOpts.HashSize = n;
		    }
		} else if (strcasecmp(cmd, "active") == 0) {
		    if (opt) {
			if (strcasecmp(opt, "on") == 0) {
			    optErr = 0;
			    DOpts.FeederActiveEnabled = 1;
			} else if (strcasecmp(opt, "off") == 0) {
			    optErr = 0;
			    DOpts.FeederActiveEnabled = 0;
			} else if (opt[0] == '/') {
			    optErr = 0;
			    DOpts.FeederActiveEnabled = 1;
			    ServerDActivePat = strdup(opt);
			}
		    }
		} else if (strcasecmp(cmd, "maxspool") == 0) {
		    /* No longer used */
		} else if (strcasecmp(cmd, "hiscachesize") == 0) {
		    if (opt) {
			SetPCommitExpire(-1, -1, atoi(opt));
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "wireformat") == 0) {
		    if (opt) {
			if (strcasecmp(opt, "on") == 0) {
			    optErr = 0;
			    DOpts.WireFormat = 1;
			} else if (strcasecmp(opt, "off") == 0) {
			    optErr = 0;
			    DOpts.WireFormat = 0;
			}
		    }
		} else if (strcasecmp(cmd, "readerdns") == 0) {
		    if (opt) {
			DOpts.ReaderDns = strtol(opt, NULL, 0);
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "readerforks") == 0) {
		    if (opt) {
			DOpts.ReaderForks = strtol(opt, NULL, 0);
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "readerfeedforks") == 0) {
		    if (opt) {
			DOpts.ReaderFeedForks = strtol(opt, NULL, 0);
			optErr = 0;
		    }
		} else if (strncasecmp(cmd, "path_", 5) == 0) {
		    /*
		     * Paths (defaults already preset in globals)
		     */
		    const char **pptr = NULL;
		    char *p = cmd;

		    /*
		     * path_ entries can only have one value, so be backward
		     * compatable with old diablo.config files that didn't
		     * have a '#' before a comment.
		     */
		    cmd = strsep(&p, " \t\n");
		    if (strcasecmp(cmd + 5, "home") == 0)
			pptr = &NewsHome;
		    else if (strcasecmp(cmd + 5, "spool") == 0) 
			pptr = &SpoolHomePat;
		    else if (strcasecmp(cmd + 5, "dqueue") == 0) 
			pptr = &DQueueHomePat;
		    else if (strcasecmp(cmd + 5, "group") == 0) 
			pptr = &GroupHomePat;
		    else if (strcasecmp(cmd + 5, "cache") == 0) 
			pptr = &CacheHomePat;
		    else if (strcasecmp(cmd + 5, "log") == 0) 
			pptr = &LogHomePat;
		    else if (strcasecmp(cmd + 5, "feeds") == 0) 
			pptr = &FeedsHomePat;
		    else if (strcasecmp(cmd + 5, "run") == 0) 
			pptr = &RunHomePat;
		    else if (strcasecmp(cmd + 5, "lib") == 0) 
			pptr = &LibHomePat;
		    else if (strcasecmp(cmd + 5, "db") == 0) 
			pptr = &DbHomePat;
		    else if (strcasecmp(cmd + 5, "server_dactive") == 0) 
			pptr = &ServerDActivePat;
		    else if (strcasecmp(cmd + 5, "reader_dactive") == 0) 
			pptr = &ReaderDActivePat;
		    else if (strcasecmp(cmd + 5, "diablo_socket") == 0) 
			pptr = &DiabloSocketPat;
		    else if (strcasecmp(cmd + 5, "dreader_socket") == 0) 
			pptr = &DReaderSocketPat;
		    else if (strcasecmp(cmd + 5, "feednotify_socket") == 0) 
			pptr = &DFeedNotifySocketPat;
		    else if (strcasecmp(cmd + 5, "feednotify_lock") == 0) 
			pptr = &DFeedNotifyLockPat;
		    else if (strcasecmp(cmd + 5, "dexpire") == 0) 
			pptr = &DExpireCtlPat;
		    else if (strcasecmp(cmd + 5, "dcontrol") == 0) 
			pptr = &DControlCtlPat;
		    else if (strcasecmp(cmd + 5, "dhistory") == 0) 
			pptr = &DHistoryPat;
		    else if (strcasecmp(cmd + 5, "dumphist") == 0) 
			pptr = &DumpHistPat;
		    else if (strcasecmp(cmd + 5, "diablo_hosts") == 0) 
			pptr = &DiabloHostsPat;
		    else if (strcasecmp(cmd + 5, "dserver_hosts") == 0) 
			pptr = &DServerHostsPat;
		    else if (strcasecmp(cmd + 5, "moderators") == 0) 
			pptr = &ModeratorsPat;
		    else if (strcasecmp(cmd + 5, "spam_body_cache") == 0) 
			pptr = &SpamBodyCachePat;
		    else if (strcasecmp(cmd + 5, "spam_nph_cache") == 0) 
			pptr = &SpamNphCachePat;
		    else if (strcasecmp(cmd + 5, "pcommit_cache") == 0) 
			pptr = &PCommitCachePat;
		    else if (strcasecmp(cmd + 5, "dexpover_list") == 0)
			pptr = &DExpireOverListPat;
		    else if (strcasecmp(cmd + 5, "dhosts_cache") == 0)
			pptr = &DHostsCachePat;
		    else if (strcasecmp(cmd + 5, "dnewsfeeds") == 0) 
			pptr = &DNewsfeedsPat;
		    else if (strcasecmp(cmd + 5, "dnntpspool") == 0) 
			pptr = &DNNTPSpoolCtlPat;
		    else if (strcasecmp(cmd + 5, "distrib_pats") == 0) 
			pptr = &DistribDotPatsPat;
		    else if (strcasecmp(cmd + 5, "distributions") == 0) 
			pptr = &DistributionsPat;
		    else if (strcasecmp(cmd + 5, "dspoolctl") == 0) 
			pptr = &DSpoolCtlPat;
		    else if (strcasecmp(cmd + 5, "generallog") == 0) 
			pptr = &GeneralLogPat;
		    else if (strcasecmp(cmd + 5, "incominglog") == 0) 
			pptr = &IncomingLogPat;
		    else if (strcasecmp(cmd + 5, "drincominglog") == 0) 
			pptr = &DRIncomingLogPat;
		    else if (strcasecmp(cmd + 5, "feedstats") == 0) 
			pptr = &DFeedStatsPat;
		    else if (strcasecmp(cmd + 5, "pathlog") == 0) 
			pptr = &FPathLogPat;
		    else if (strcasecmp(cmd + 5, "artlog") == 0) 
			pptr = &FArtLogPat;
		    else if (strcasecmp(cmd + 5, "shutdown_cleanup") == 0) 
			pptr = &ShutdownCleanup;
		    else if (strcasecmp(cmd + 5, "cachehits") == 0) 
			pptr = &CacheHitsPat;
		    else
			cmdErr = 1;

		    /*
		     * XXX check for zero or one '%s' in opt.
		     */
		    if (cmdErr == 0 && opt != NULL) {
			*pptr = strdup(opt);
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "spooldirs") == 0) {
		    /* No longer used */
		} else if (strcasecmp(cmd, "port") == 0) {
		    if (opt) {
			strdupfree(&DOpts.FeederPort, opt, "nntp");
			strdupfree(&DOpts.ReaderPort, opt, "nntp");
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "feederbindport") == 0) {
		    if (opt) {
			strdupfree(&DOpts.FeederPort, opt, "nntp");
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "feederbindhost") == 0) {
		    if (opt) {
			strdupfree(&DOpts.FeederBindHost, SanitiseAddr(opt), NULL);
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "readerbindport") == 0) {
		    if (opt) {
			strdupfree(&DOpts.ReaderPort, opt, "nntp");
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "readerbindhost") == 0) {
		    if (opt) {
			strdupfree(&DOpts.ReaderBindHost, SanitiseAddr(opt), NULL);
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "readerpath") == 0) {
		    if (opt) {
			strdupfree(&DOpts.ReaderPathHost, opt, "");
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "path") == 0) {
		    if (opt) {
			PathListType *pl = NULL;
	
			strdupfree(&DOpts.FeederPathHost, opt, "");
			strdupfree(&DOpts.ReaderPathHost, opt, "");
			pl = zalloc(&SysMemPool, sizeof(PathListType));
			pl->pathent = strdup(DOpts.FeederPathHost);
			pl->pathtype = 1;
			pl->next = DOpts.PathList;
			DOpts.PathList = pl;
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "feederpath") == 0) {
		    if (opt) {
			PathListType *pl = NULL;

			strdupfree(&DOpts.FeederPathHost, opt, "");
			pl = zalloc(&SysMemPool, sizeof(PathListType));
			pl->pathent = strdup(DOpts.FeederPathHost);
			pl->pathtype = 1;
			pl->next = DOpts.PathList;
			DOpts.PathList = pl;
			optErr = 0;
		    }
		} else if (strcasecmp(cmd, "commonpath") == 0) {
		    if (opt) {
			PathListType *pl = NULL;
	
			pl = zalloc(&SysMemPool, sizeof(PathListType));
			pl->pathent = strdup(opt);
			pl->pathtype = 2;
			pl->next = DOpts.PathList;
			DOpts.PathList = pl;
			optErr = 0;
		    }
		} else {
		    optErr = SetCommand(stderr, cmd, opt, &cmdErr);
		}
		if (optErr) {
		    exitMe = 1;
		    fprintf(stderr, "Unknown diablo.config command: %s %s\n",
						cmd, ((opt) ? opt : ""));
		    logit(LOG_CRIT, "Unknown diablo.config command: %s %s",
						cmd, ((opt) ? opt : ""));
		}
	    }
	    if (cmdErr && cmd) {
		exitMe = 1;
		fprintf(stderr, "Unknown diablo.config command: %s %s\n",
						cmd, ((opt) ? opt : ""));
		logit(LOG_CRIT, "Unknown diablo.config command: %s %s",
						cmd, ((opt) ? opt : ""));
	    }
	}
	fclose(fi);
	CheckConfigOptions(0);
    } else {
	fprintf(stderr, "Unable to open diablo.config using pattern %s\n",
							diabloConfig);
	logit(LOG_CRIT, "Unable to open diablo.config using pattern %s",
							diabloConfig);
	exitMe = 1;
    }
    if (exitMe) {
	fprintf(stderr, "critical error parsing diablo.config, exiting.\n");
	exit(1);
    }
}

/*
 *
 * Set various options that can also be set via dicmd/drcmd (runtime)
 *
 */
int
SetCommand(FILE *fo, char *cmd, char *opt, int *cmdErr)
{
    int optErr = 1;

    if (strcasecmp(cmd, "remember") == 0) {
	    if (opt) {
		DOpts.RememberSecs = TimeSpec(opt, "d");
		DOpts.FeederMaxAcceptAge = DOpts.RememberSecs;
		optErr = 0;
	    }
    } else if (strcasecmp(cmd, "feedermaxacceptage") == 0) {
	    if (opt) {
		DOpts.FeederMaxAcceptAge = TimeSpec(opt, "d");
		optErr = 0;
	    }
    } else if (strcasecmp(cmd, "maxconnect") == 0) {
	    if (opt) {
		DOpts.MaxPerRemote = strtol(opt, NULL, 0);
		optErr = 0;
	    }
    } else if (strcasecmp(cmd, "hostcacherebuildtime") == 0) {
	    if (opt) {
		DOpts.HostCacheRebuildTime = strtol(opt, NULL, 0);
		optErr = 0;
	    }
    } else if (strcasecmp(cmd, "activedrop") == 0) {
	    if (opt) {
		DOpts.FeederActiveDrop = enabled(opt);
		optErr = 0;
 	    }
    } else if (strcasecmp(cmd, "precommittime") == 0) {
	    if (opt) {
		SetPCommitExpire(atoi(opt), -1, -1);
		optErr = 0;
	    }
    } else if (strcasecmp(cmd, "postcommittime") == 0) {
	    if (opt) {
		SetPCommitExpire(-1, atoi(opt), -1);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "slave") == 0 ||
			strcasecmp(cmd, "feederxrefslave") == 0) {
	    if (opt) {
		DOpts.FeederXRefSlave = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederxrefsync") == 0) {
	    if (opt) {
		DOpts.FeederXRefSync = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "displayadminversion") == 0) {
	    if (opt) {
		DOpts.DisplayAdminVersion = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "autoaddtoactive") == 0) {
	    if (opt) {
		DOpts.FeederAutoAddToActive = enabled(opt);
		DOpts.ReaderAutoAddToActive = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederautoaddtoactive") == 0) {
	    if (opt) {
		DOpts.FeederAutoAddToActive = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feedermaxheadersize") == 0) {
	    if (opt) {
		DOpts.FeederMaxHeaderSize = bsizetol(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerautoaddtoactive") == 0) {
	    if (opt) {
		DOpts.ReaderAutoAddToActive = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerdetaillog") == 0) {
	    if (opt) {
		DOpts.ReaderDetailLog = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readeridenttimeout") == 0) {
	    if (opt) {
		DOpts.ReaderIdentTimeout = strtol(opt, NULL, 0);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerthreads") == 0) {
	    if (opt) {
		DOpts.ReaderThreads = strtol(opt, NULL, 0);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readercache") == 0) {
	    if (opt) {
		DOpts.ReaderCacheMode = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readercachehashsize") == 0) {
	    if (opt) {
		int n = bsizetol(opt);

		if (n<=0) {
		    fprintf(stderr, "Hash size should be greater than 0 (%d)\n", n);
		    logit(LOG_CRIT, "Hash size should be greater than 0 (%d)", n);
		} else {
		    DOpts.ReaderCacheHashSize = n;
		    optErr = 0;
		}
	    }
	} else if (strcasecmp(cmd, "readerxover") == 0) {
	    if (opt) {
		if (strcasecmp(opt, "trackonly") == 0) {
		    DOpts.ReaderXOverMode = 2;
		    optErr = 0;
		} else {
		    /* OTHER OPTIONS NOT YET SUPPORTED */
		    DOpts.ReaderXOverMode = 1;
		    optErr = 0;
		}
	    }
	} else if (strcasecmp(cmd, "feederrtstats") == 0) {
	    if (opt) {
		if (strcasecmp(opt, "none") == 0)
		    DOpts.FeederRTStats = RTSTATS_NONE;
		else if (strcasecmp(opt, "label") == 0)
		    DOpts.FeederRTStats = RTSTATS_LABEL;
		else if (strcasecmp(opt, "hostname") == 0)
		    DOpts.FeederRTStats = RTSTATS_HOST;
		else {
		    fprintf(fo, "Unknown feedertstats option: '%s'\n", opt);
		    logit(LOG_CRIT, "Unknown feederrtstats option: '%s'\n",
								opt);
		}
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "internalfilter") == 0) {
	    if (opt) {
		strdupfree(&DOpts.SpamFilterOpt, opt, NULL);
		SetSpamFilterOpt();
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "rejectartswithnul") == 0) {
	    if (opt) {
		DOpts.RejectArtsWithNul = strtol(opt, NULL, 0);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "rejectartswithbarecr") == 0) {
	    if (opt) {
		DOpts.RejectArtsWithBareCR = strtol(opt, NULL, 0);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "newsmaster") == 0) {
	    if (opt) {
		strdupfree(&DOpts.NewsMaster, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readercrash") == 0) {
	    if (opt) {
		strdupfree(&DOpts.ReaderCrashHandler, opt, "none");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerban") == 0) {
	    if (opt) {
		strdupfree(&DOpts.ReaderBan, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "xrefhost") == 0) {
	    if (opt) {
		strdupfree(&DOpts.FeederXRefHost, opt, NULL);
		strdupfree(&DOpts.ReaderXRefHost, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederxrefhost") == 0) {
	    if (opt) {
		strdupfree(&DOpts.FeederXRefHost, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerxrefhost") == 0) {
	    if (opt) {
		strdupfree(&DOpts.ReaderXRefHost, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerxrefslavehost") == 0) {
	    if (opt) {
		strdupfree(&DOpts.ReaderXRefSlaveHost, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederfilter") == 0) {
	    if (opt) {
		strdupfree(&DOpts.FeederFilter, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readercachedirs") == 0) {
	    if (opt) {
		optErr = SetCacheDirs(opt, &DOpts.ReaderCacheDirs);
		if (optErr != 0) {
		    fprintf(fo, "Illegal reader cache dir format\n");
		    logit(LOG_CRIT, "Illegal reader cache dir format");
		}
	    }
	} else if (strcasecmp(cmd, "newsadmin") == 0) {
	    if (opt) {
		strdupfree(&DOpts.NewsAdmin, opt, "");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "newsmaster") == 0) {
	    if (opt) {
		strdupfree(&DOpts.NewsMaster, opt, NULL);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "hostname") == 0) {
	    if (opt) {
		strdupfree(&DOpts.FeederHostName, opt, "");
		strdupfree(&DOpts.ReaderHostName, opt, "");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederhostname") == 0) {
	    if (opt) {
		strdupfree(&DOpts.FeederHostName, opt, "");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readerhostname") == 0) {
	    if (opt) {
		strdupfree(&DOpts.ReaderHostName, opt, "");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederbuffersize") == 0) {
	    if (opt) {
		int n = bsizetol(opt);

		optErr = 0;
		if ((n ^ (n - 1)) != (n << 1) - 1) {
		    fprintf(stderr, "Buffer size not a power of 2: %d\n", n);
		    logit(LOG_CRIT, "Buffer size not a power of 2: %d", n);
		    optErr = 1;
		}
		if (optErr == 0)
		    DOpts.FeederBufferSize = n;
	    }
	} else if (strcasecmp(cmd, "feedermaxartsize") == 0) {
	    if (opt) {
		DOpts.FeederMaxArtSize = bsizetol(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "readermaxartsize") == 0) {
	    if (opt) {
		DOpts.ReaderMaxArtSize = bsizetol(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederarttypes") == 0) {
	    if (opt) {
		DOpts.FeederArtTypes = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "feederpreloadart") == 0) {
	    if (opt) {
		DOpts.FeederPreloadArt = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "spoolpreloadart") == 0) {
	    if (opt) {
		DOpts.SpoolPreloadArt = enabled(opt);
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "postxfilter") == 0) {
	    if (opt) {
		strdupfree(&DOpts.PostXFilter, opt, "");
		optErr = 0;
	    }
	} else if (strcasecmp(cmd, "pgpverifyargs") == 0) {
	    if (opt) {
		char *p = opt;
		int i = 0;
		char path[PATH_MAX];

		snprintf(path, sizeof(path), PGP_VERIFY_PATH, NewsHome);
		strdupfree(&DOpts.PgpVerifyArgs[i++], path, NULL);
		strdupfree(&DOpts.PgpVerifyArgs[i++], PGP_VERIFY_ARG0, NULL);
		for (p = strtok(p, " \t"); p != NULL; p = strtok(NULL, " \t")) {
		    if (i < 6)
			strdupfree(&DOpts.PgpVerifyArgs[i++], p, NULL);
		}
		DOpts.PgpVerifyArgs[i] = NULL;
		optErr = 0;
	    }
    } else {
	optErr = 0;
	if (cmdErr != NULL)
	    *cmdErr = 1;
    }
    return(optErr);
}

void
dumpConfigFeeder(FILE *fo, char *cmd)
{
    if (cmd == NULL || strcasecmp(cmd, "hash") == 0) {
	fprintf(fo, "*hash: ");
	switch (DOpts.HashMethod) {
	    case HASH_PRIME: fprintf(fo, "prime");
			     break;
	    case HASH_CRC: fprintf(fo, "crc");
			     break;
	    case HASH_OCRC: fprintf(fo, "ocrc");
			     break;
	    default: fprintf(fo, "UNKNOWN");
			break;
	}
	fprintf(fo, "\n");
    }
    if (cmd == NULL || strcasecmp(cmd, "hsize") == 0)
	fprintf(fo, "*hsize: %d\n", DOpts.HashSize);
    if (cmd == NULL || strcasecmp(cmd, "feederactive") == 0)
	fprintf(fo, "*feederactive: %d\n", DOpts.FeederActiveEnabled);
    if (cmd == NULL || strcasecmp(cmd, "hiscachesize") == 0)
	fprintf(fo, "*hiscachesize: %d\n", GetPCommit(3));
    if (cmd == NULL || strcasecmp(cmd, "feederbindhost") == 0)
	fprintf(fo, "*feederbindhost: %s\n",
					safestr(DOpts.FeederBindHost, "ANY"));
    if (cmd == NULL || strcasecmp(cmd, "feederbindport") == 0)
	fprintf(fo, "*feederbindport: %s\n", safestr(DOpts.FeederPort, NULL));
    if (cmd == NULL || strcasecmp(cmd, "feederpath") == 0)
	fprintf(fo, "*feederpath: %s\n", safestr(DOpts.FeederPathHost, NULL));
    if (cmd == NULL || strcasecmp(cmd, "feederpathlist") == 0) {
	PathListType *pl = NULL;
	fprintf(fo, "*pathlist: ");
	for (pl = DOpts.PathList; pl != NULL; pl = pl->next)
	    fprintf(fo, "%s%s", pl->pathent, (pl->next != NULL) ? "!" : "");
	fprintf(fo, "\n");
    }
    if (cmd == NULL || strcasecmp(cmd, "feederhostname") == 0)
	fprintf(fo, "feederhostname: %s\n",
					safestr(DOpts.FeederHostName, NULL));
    if (cmd == NULL || strcasecmp(cmd, "feederxrefhost") == 0) 
	fprintf(fo, "feederxrefhost: %s\n",
					safestr(DOpts.FeederXRefHost, NULL));
    if (cmd == NULL || strcasecmp(cmd, "internalfilter") == 0)
	fprintf(fo, "*internalfilter: %s\n",
					safestr(DOpts.SpamFilterOpt, NULL));
    if (cmd == NULL || strcasecmp(cmd, "feederfilter") == 0)
	fprintf(fo, "feederfilter: %s\n", safestr(DOpts.FeederFilter, NULL));
    if (cmd == NULL || strcasecmp(cmd, "rejectartswithnul") == 0)
	fprintf(fo, "rejectartswithnul: %d\n", DOpts.RejectArtsWithNul);
    if (cmd == NULL || strcasecmp(cmd, "rejectartswithbarecr") == 0)
	fprintf(fo, "rejectartswithnul: %d\n", DOpts.RejectArtsWithBareCR);
    if (cmd == NULL || strcasecmp(cmd, "remember") == 0)
	fprintf(fo, "remember: %d\n", DOpts.RememberSecs / 24 / 60 / 60);
    if (cmd == NULL || strcasecmp(cmd, "feedermaxacceptage") == 0)
	fprintf(fo, "feedermaxacceptage: %d\n", DOpts.FeederMaxAcceptAge);
    if (cmd == NULL || strcasecmp(cmd, "maxconnect") == 0)
	fprintf(fo, "maxconnect: %d\n", DOpts.MaxPerRemote);
    if (cmd == NULL || strcasecmp(cmd, "hostcacherebuildtime") == 0)
	fprintf(fo, "hostcacherebuildtime: %d\n", DOpts.HostCacheRebuildTime);
    if (cmd == NULL || strcasecmp(cmd, "activedrop") == 0)
	fprintf(fo, "activedrop: %d\n", DOpts.FeederActiveDrop);
    if (cmd == NULL || strcasecmp(cmd, "precommittime") == 0)
	fprintf(fo, "precommittime: %d\n", GetPCommit(1));
    if (cmd == NULL || strcasecmp(cmd, "postcommittime") == 0)
	fprintf(fo, "postcommittime: %d\n", GetPCommit(2));
    if (cmd == NULL || strcasecmp(cmd, "pcommithsize") == 0)
	fprintf(fo, "pcommithsize: %d\n", GetPCommit(3));
    if (cmd == NULL || strcasecmp(cmd, "feederxrefslave") == 0)
	fprintf(fo, "feederxrefslave: %d\n", DOpts.FeederXRefSlave);
    if (cmd == NULL || strcasecmp(cmd, "feederxrefsync") == 0)
	fprintf(fo, "feederxrefsync: %d\n", DOpts.FeederXRefSync);
    if (cmd == NULL || strcasecmp(cmd, "feederautoaddtoactive") == 0)
	fprintf(fo, "feederautoaddtoactive: %d\n", DOpts.FeederAutoAddToActive);
    if (cmd == NULL || strcasecmp(cmd, "feederrtstats") == 0) {
	switch (DOpts.FeederRTStats) {
	    case RTSTATS_NONE: fprintf(fo, "feederrtstats: none\n");
				break;
	    case RTSTATS_LABEL: fprintf(fo, "feederrtstats: label\n");
				break;
	    case RTSTATS_HOST: fprintf(fo, "feederrtstats: host\n");
				break;
	}
    }
    if (cmd == NULL || strcasecmp(cmd, "feederbuffersize") == 0)
	fprintf(fo, "feederbuffersize: %d\n", DOpts.FeederBufferSize);
    if (cmd == NULL || strcasecmp(cmd, "feedermaxartsize") == 0)
	fprintf(fo, "feedermaxartsize: %d\n", DOpts.FeederMaxArtSize);
    if (cmd == NULL || strcasecmp(cmd, "feederarttypes") == 0)
	fprintf(fo, "feederarttypes: %d\n", DOpts.FeederArtTypes);
}

void
dumpConfigReader(FILE *fo, char *cmd)
{
    char st[PATH_MAX];
    if (cmd == NULL || strcasecmp(cmd, "readergrouphash") == 0)
	fprintf(fo, "*readergrouphash: %s\n",
				GetHash(&DOpts.ReaderGroupHashMethod, st));
    if (cmd == NULL || strcasecmp(cmd, "readerdns") == 0)
	fprintf(fo, "*readerdns: %d\n", DOpts.ReaderDns);
    if (cmd == NULL || strcasecmp(cmd, "readerforks") == 0)
	fprintf(fo, "*readerforks: %d\n", DOpts.ReaderForks);
    if (cmd == NULL || strcasecmp(cmd, "readerfeedforks") == 0)
	fprintf(fo, "*readerfeedforks: %d\n", DOpts.ReaderFeedForks);
    if (cmd == NULL || strcasecmp(cmd, "readerbindhost") == 0)
	fprintf(fo, "*readerbindhost: %s\n",
					safestr(DOpts.ReaderBindHost, "ANY"));
    if (cmd == NULL || strcasecmp(cmd, "readerbindport") == 0)
	fprintf(fo, "*readerbindport: %s\n", safestr(DOpts.ReaderPort, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readerpath") == 0)
	fprintf(fo, "*readerpath: %s\n", safestr(DOpts.ReaderPathHost, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readerhostname") == 0)
	fprintf(fo, "readerhostname: %s\n",
					safestr(DOpts.ReaderHostName, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readerxrefhost") == 0)
	fprintf(fo, "readerxrefhost: %s\n",
					safestr(DOpts.ReaderXRefHost, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readerxrefslavehost") == 0)
	fprintf(fo, "readerxrefslavehost: %s\n",
				safestr(DOpts.ReaderXRefSlaveHost, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readercachedirs") == 0) {
	int i;
	fprintf(fo, "readercachedirs: ");
	for (i = 1; i <= DOpts.ReaderCacheDirs.dt_dirlvl; i++)
	    fprintf(fo, "%d%s", DOpts.ReaderCacheDirs.dt_dirinfo[i - 1],
			(i == DOpts.ReaderCacheDirs.dt_dirlvl) ? "" : "/");
	fprintf(fo, "\n");
    }
    if (cmd == NULL || strcasecmp(cmd, "readerautoaddtoactive") == 0)
	fprintf(fo, "readerautoaddtoactive: %d\n", DOpts.ReaderAutoAddToActive);
    if (cmd == NULL || strcasecmp(cmd, "readerdetaillog") == 0)
	fprintf(fo, "readerdetaillog: %d\n", DOpts.ReaderDetailLog);
    if (cmd == NULL || strcasecmp(cmd, "readerthreads") == 0)
	fprintf(fo, "readerthreads: %d\n", DOpts.ReaderThreads);
    if (cmd == NULL || strcasecmp(cmd, "readercache") == 0)
	fprintf(fo, "readercache: %d\n", DOpts.ReaderCacheMode);
    if (cmd == NULL || strcasecmp(cmd, "readercachehashsize") == 0) {
	fprintf(fo, "readercachehashsize: %d\n", DOpts.ReaderCacheHashSize);
    }
    if (cmd == NULL || strcasecmp(cmd, "readerxover") == 0) {
	switch (DOpts.ReaderXOverMode) {
	    case 0: fprintf(fo, "readerxover: off\n");
		    break;
	    case 1: fprintf(fo, "readerxover: on\n");
		    break;
	    case 2: fprintf(fo, "readerxover: trackonly\n");
		    break;
	}
    }
    if (cmd == NULL || strcasecmp(cmd, "readercrash") == 0)
	fprintf(fo, "readercrash: %s\n",
				safestr(DOpts.ReaderCrashHandler, NULL));
    if (cmd == NULL || strcasecmp(cmd, "readermaxartsize") == 0)
	fprintf(fo, "readermaxartsize: %d\n", DOpts.ReaderMaxArtSize);
    if (cmd == NULL || strcasecmp(cmd, "feedermaxheadersize") == 0)
	fprintf(fo, "maxheadersize: %d\n", DOpts.FeederMaxHeaderSize);
#ifdef READER_BAN_LISTS
    if (cmd == NULL || strcasecmp(cmd, "readerban") == 0)
	fprintf(fo, "readerban: %s\n",
				safestr(DOpts.ReaderBan, NULL));
#endif
#ifdef POST_XFILTERHOOK
    if (cmd == NULL || strcasecmp(cmd, "postxfilter") == 0)
	fprintf(fo, "postxfilter: %d\n", DOpts.PostXFilter);
#endif
}

/*
 *
 * Dump options that can also be set via dicmd/drcmd (runtime)
 *
 */
void
DumpConfigOptions(FILE *fo, char *cmd, int which)
{
    fprintf(fo, "Options with '*' cannot be changed while server is running\n");
    if (cmd == NULL || strcasecmp(cmd, "displayadminversion") == 0)
	fprintf(fo, "displayadminversion: %d\n", DOpts.DisplayAdminVersion);
    if (cmd == NULL || strcasecmp(cmd, "newsadmin") == 0)
	fprintf(fo, "newsadmin: %s\n", safestr(DOpts.NewsAdmin, NULL));
    if (cmd == NULL || strcasecmp(cmd, "newsmaster") == 0)
	fprintf(fo, "newsmaster: %s\n", safestr(DOpts.NewsMaster, NULL));
    if (which & CONF_FEEDER)
	dumpConfigFeeder(fo, cmd);
    if (which & CONF_READER)
	dumpConfigReader(fo, cmd);
}

/*
 * Check some configuration options that are required and set some
 * default values if they are wrong.
 */
int
CheckConfigOptions(int which)
{
    switch (which) {
	case CONF_FEEDER:
	    if (DOpts.FeederHostName == NULL)
		SetMyHostName(&DOpts.FeederHostName);
	    break;
	case CONF_READER:
	    if (DOpts.ReaderHostName == NULL)
		SetMyHostName(&DOpts.ReaderHostName);
	    break;
    }
    if (DOpts.PgpVerifyArgs[0] == NULL) {
	char path[PATH_MAX];

	snprintf(path, sizeof(path), PGP_VERIFY_PATH, NewsHome);
	strdupfree(&DOpts.PgpVerifyArgs[0], path, NULL);
	strdupfree(&DOpts.PgpVerifyArgs[1], PGP_VERIFY_ARG0, NULL);
	DOpts.PgpVerifyArgs[2] = NULL;
    }
    return(0);
}

int
SetHashMethod(const char *opt, int *pmethod)
{
    int optErr = 0;

    if (strcasecmp(opt, "prime") == 0) {
	*pmethod = HASH_PRIME;
    } else if (strcasecmp(opt, "crc") == 0) {
	*pmethod = HASH_CRC;
    } else if (strcasecmp(opt, "ocrc") == 0) {
	*pmethod = HASH_OCRC;
    } else {
	optErr = 1;
    }
    return(optErr);
}

/*
 * SetGroupHashMethod - Set the newsgroup hashing method used to create
 *   header db files.
 *
 * Returns: 0 = success
 *	    1 = error
 */
int
SetGroupHashMethod(const char *opt, GroupHashType *pmethod)
{
    bzero(pmethod, sizeof(*pmethod));
    if (strcasecmp(opt, "crc") == 0) {
	pmethod->gh_type = HASHGRP_CRC;
	pmethod->gh_sigbytes = 20;
	pmethod->gh_dirtype = HASHGRP_DIR_BIT;
	pmethod->gh_dirlvl = 1;
	pmethod->gh_dirinfo[0] = 2;
    } else if (strcasecmp(opt, "hierarchy") == 0) {
	pmethod->gh_type = HASHGRP_HIER;
	pmethod->gh_sigbytes = 1024;
	pmethod->gh_dirtype = HASHGRP_DIR_BIT;
	pmethod->gh_dirlvl = 1;
	pmethod->gh_dirinfo[0] = 0;
    } else if (strncasecmp(opt, "md5", 3) == 0) {
	char *p;
	int v;
	pmethod->gh_type = HASHGRP_32MD5;
	pmethod->gh_sigbytes = 22;
	pmethod->gh_dirtype = HASHGRP_DIR_BIT;
	pmethod->gh_dirlvl = 1;
	pmethod->gh_dirinfo[0] = 2;
	opt += 3;
	if (*opt == '-') {
	    opt++;
	    switch (atoi(opt)) {
		case 32:
		    pmethod->gh_type = HASHGRP_32MD5;
		    break;
		case 64:
		    pmethod->gh_type = HASHGRP_64MD5;
		    break;
		default:
		    return(1);
	    }
	}
	if ((p = strchr(opt, ':')) != NULL) {
	    p++;
	    opt = p;
	    v = atoi(p);
	    if (v < 1 || v > 200)
		return(1);
	    pmethod->gh_sigbytes = v;
	}
	if ((p = strchr(opt, '\\')) != NULL) {
	    pmethod->gh_dirtype = HASHGRP_DIR_BIT;
	    pmethod->gh_dirlvl = 0;
	    pmethod->gh_dirinfo[0] = 0;
	    while ((p = strchr(opt, '\\')) != NULL) {
		p++;
		opt = p;
		v = atoi(p);
		if (v < 1 || v > 9)
		    return(1);
		pmethod->gh_dirinfo[pmethod->gh_dirlvl] = v;
		pmethod->gh_dirlvl++;
		if (pmethod->gh_dirlvl > 9)
		    break;
	    }
	} else if ((p = strchr(opt, '/')) != NULL) {
	    pmethod->gh_dirtype = HASHGRP_DIR_NUM;
	    pmethod->gh_dirlvl = 0;
	    pmethod->gh_dirinfo[0] = 0;
	    while ((p = strchr(opt, '/')) != NULL) {
		p++;
		opt = p;
		v = atoi(p);
		if (v < 1)
		    return(1);
		pmethod->gh_dirinfo[pmethod->gh_dirlvl] = v;
		pmethod->gh_dirlvl++;
		if (pmethod->gh_dirlvl > 9)
		    break;
	    }
	} else 
	    return(1);
	return(0);
    } else {
	return(1);
    }
    return(0);
}

int
SetCacheDirs(const char *opt, CacheDirType *cachedir)
{
    int v;
    char *p;

    cachedir->dt_dirlvl = 0;
    v = atoi(opt);
    if (v < 1)
	return(1);
    cachedir->dt_dirinfo[cachedir->dt_dirlvl] = v;
    cachedir->dt_dirlvl++;
    while ((p = strchr(opt, '/')) != NULL) {
	p++;
	opt = p;
	v = atoi(p);
	if (v < 1)
	    return(1);
	cachedir->dt_dirinfo[cachedir->dt_dirlvl] = v;
	cachedir->dt_dirlvl++;
	if (cachedir->dt_dirlvl > 9)
	    break;
    }
    return(0);
}

