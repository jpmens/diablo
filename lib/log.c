
/*
 * LOG.C
 *
 */

#include "defs.h"

Prototype void OpenLog(const char *ident, int option);
Prototype void CloseLog(LogInfo *LI, int killit);
Prototype void Log(LogInfo *LI, int priority, const char *ctl, ...);
Prototype void VLog(LogInfo *LI, int priority, const char *ctl, va_list va);
Prototype void logit(int priority, const char *ctl, ...);
Prototype void vlogit(int priority, const char *ctl, va_list va);
Prototype void LogIncoming(const char *format, char *label, const char *msgid, char *err);
Prototype void CloseIncomingLog(void);
Prototype void WritePath(char *path);
Prototype void ClosePathLog(int killit);
Prototype void WriteArtLog(char *path, int size, char *arttype, char *nglist);
Prototype void CloseArtLog(int killit);

static LogInfo GeneralLog = { &GeneralLogPat, NULL, NULL, 0, 0, 0, 0, 0, {0} };
static LogInfo IncomingLog = { &IncomingLogPat, NULL, NULL, 0, 0, 0, 0, 0, {0} };
static LogInfo PathLog = { &FPathLogPat, NULL, NULL, 0, 0, 0, 0, 0, {0} };
static LogInfo ArtLog = { &FArtLogPat, NULL, NULL, 0, 0, 0, 0, 0, {0} };

#if USE_INTERNAL_VSYSLOG
void
vsyslog(int priority, const char *ctl, va_list va) 
{
    char buf[1024];

    vsnprintf(buf, sizeof(buf), ctl, va);
    syslog(priority, "%s", buf);
}
#endif

void
openProg(LogInfo *LI, const char *prog)
{
    int fds[2];

    if (pipe(fds) < 0) {
	logit(LOG_ERR, "log unable to pipe(): %s", strerror(errno));
	return;
    }
    if ((LI->Pid = fork()) < 0) {
	logit(LOG_ERR, "log unable to fork(): %s", strerror(errno));
	close(fds[0]);
	close(fds[1]);
	return;
    }
    if (!LI->Pid) {
	if (dup2(fds[0], fileno(stdin)) < 0) {
	    logit(LOG_ERR, "log unable to dup2(): %s", strerror(errno));
	    close(fds[0]);
	    close(fds[1]);
	    return;
	}
	close(fds[0]);
	close(fds[1]);
	execl(prog, prog, NULL);
	logit(LOG_ERR, "log unable to execl(%s): %s", prog, strerror(errno));
	close(fileno(stdin));
	_exit(1);
    }
    /*
     * Only parent should reach here
     */
    close(fds[0]);
    if (fcntl(fds[1], F_SETFD, 1) < 0)
	logit(LOG_ERR, "log unable to fcntl(): %s", strerror(errno));
    LI->Fd = fdopen(fds[1], "a");
}

void
openLog(LogInfo *LI)
{
    struct stat st;
    const char *Fname;

    if ((!LI->Fd && !LI->UseSyslog && !LI->Disabled) ||
    		(time(NULL) > LI->NextCheck)) {
	LI->NextCheck = time(NULL) + 10;
	if (strcmp(*LI->Pat, "SYSLOG") == 0) {
	    /*
	     * I'm sure this seemed like a good idea at the time, but
	     * this is really and truly broken.  dreaderd ends up calling
	     * closelog() all the time because of the way this was hacked
	     * in to things.  Combined with some changes made to FreeBSD
	     * in the late rev. 4's, some weird stuff starts happening
	     * and so I've commented this out.  JG20031020
	     */
	    /*CloseLog(LI, 1);*/
	    LI->UseSyslog = 1;
	} else if (strcmp(*LI->Pat, "NONE") == 0) {
	    CloseLog(LI, 1);
	    LI->Disabled = 1;
	} else if (**LI->Pat == '|') {
	    Fname = PatLogExpand(*LI->Pat);
	    if (LI->Fd == NULL || strcmp(Fname, LI->Fname) != 0) {
		CloseLog(LI, 1);
		strncpy(LI->Fname, Fname, sizeof(LI->Fname) - 1);
		LI->Fname[sizeof(LI->Fname) - 1] = '\0';
		openProg(LI, Fname + 1);
	    }
	} else {
	    Fname = PatLogExpand(*LI->Pat);
	    if (stat(Fname, &st) != 0)
		LI->LastInode = 0;
	    if (!LI->Fd || !LI->LastInode || (LI->LastInode != st.st_ino)) {
		/* Need to reopen the log file */
		CloseLog(LI, 1);
		LI->Fd = fopen(Fname, "a");
		if (LI->Fd == NULL) {
		    fprintf(stderr, "Unable to open log file: %s\n", Fname);
		    logit(LOG_ERR, "Unable to open log file: %s\n", Fname);
	            LI->UseSyslog = 1;
		} else {
		    if (stat(Fname, &st) == 0)
			LI->LastInode = st.st_ino;
		    strncpy(LI->Fname, Fname, sizeof(LI->Fname) - 1);
		    LI->Fname[sizeof(LI->Fname) - 1] = '\0';
		}
	    }
	}
    }

}

void
OpenLog(const char *ident, int option)
{
    GeneralLog.Ident = ident;
    openlog(ident, option, LOG_NEWS);
}

void
Log(LogInfo *LI, int priority, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    VLog(LI, priority, ctl, va);
    va_end(va);
}

void
VLog(LogInfo *LI, int priority, const char *ctl, va_list va)
{
    /* check & (re)open the log if neccessary */
    openLog(LI);

    /* print message itself */
    if (LI->UseSyslog) {
	vsyslog(priority, ctl, va);
    } else if (LI->Disabled || (priority == LOG_DEBUG && !DebugOpt)) {
	/* Do nothing. */
    } else {
	static char logLine[8192];
	char *p = logLine;

	/* Add date and time. Syslog has its own time stamps. */
	p += sprintf(logLine, "%s %s%s ", LogTime(),
				LI->Ident ? LI->Ident : "",
				LI->Ident ? ":" : "");
	vsnprintf(p, sizeof(logLine) - (p - logLine) - 1, ctl, va);
	fprintf(LI->Fd, "%s\n", logLine);
	fflush(LI->Fd);
    }
}

void
CloseLog(LogInfo *LI, int killit)
{
    if (LI == NULL)
	LI = &GeneralLog;
    if (LI->UseSyslog)
	closelog();
    if (LI->Fd != NULL) {
	if (LI->Pid > 0 && killit) {
	    kill(LI->Pid, SIGINT);
	    LI->Pid = -1;
	}
	fclose(LI->Fd);
	LI->Fd = NULL;
	LI->LastInode = 0;
    }
}

void
logit(int priority, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vlogit(priority, ctl, va);
    va_end(va);
}

void
vlogit(int priority, const char *ctl, va_list va)
{
    VLog(&GeneralLog, priority, ctl, va);
}

void
LogIncoming(const char *format, char *label, const char *msgid, char *err)
{
    Log(&IncomingLog, LOG_INFO, format, label, msgid, err);
}

void
CloseIncomingLog(void)
{
    CloseLog(&IncomingLog, 1);
}

void
WritePath(char *path)
{
    openLog(&PathLog);

    /* print message itself */
    if (PathLog.Fd != NULL) {
	if (fprintf(PathLog.Fd, "Path: %s\n", path) == 0)
	    ClosePathLog(1);
	else
	    fflush(PathLog.Fd);
    }
}

void
ClosePathLog(int killit)
{
    CloseLog(&PathLog, killit);
}

void
WriteArtLog(char *path, int size, char *arttype, char *nglist)
{
    char host[255];
    char *p;

    openLog(&ArtLog);

    if ((p = strchr(path, '!')) != NULL && p - path < sizeof(host)) {
	int n = p - path;
	if (n >= sizeof(host))
	    n = sizeof(host) - 1;
	strncpy(host, path, n);
	host[n] = '\0';
    } else {
	strncpy(host, path, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
    }
    /* print message itself */
    if (ArtLog.Fd != NULL) {
	if (fprintf(ArtLog.Fd, "%s %d %s %s\n", host, size, arttype, nglist) == 0)
	    CloseArtLog(1);
	else
	    fflush(ArtLog.Fd);
    }
}

void
CloseArtLog(int killit)
{
    CloseLog(&ArtLog, killit);
}

