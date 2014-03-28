
/*
 * PGP.C	- run pgpverify
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype const char *PGPVerify(Control *ctl, const char *art, int artLen);

/*
 * PGPVerify()
 *
 * cat article | pgpverify.  If the output of the program is non-null it
 * contains the verified pgp user id which we compare against the
 * control verification string.
 *
 * Return:
 *	NULL	success
 *	string	failure (return error template)
 */

#if DIABLO_PGP_SUPPORT

const char *
PGPVerify(Control *ctl, const char *art, int artLen)
{
    const char *badCmd = "Unable to exec pgp verification program\n";
    int fds[2];
    pid_t pid;

    if (DebugOpt) {
	int i = 0;
	while (i < 10 && DOpts.PgpVerifyArgs[i] != NULL) {
	    printf("PgpverifyArg[%d]=%s\n", i, DOpts.PgpVerifyArgs[i]);
	    i++;
	}
    }

    if ((pid = RunProgramPipe(fds, RPF_STDIN|RPF_STDOUT,
					DOpts.PgpVerifyArgs, NULL)) > 0) {
	/*
	 * write article to pgpverify, result answer
	 */
	char buf[256];
	int n;
	int lcr = 0;
	FILE *fo = fdopen(fds[1], "w");

	for (n = 0; n < artLen; ++n) {
	    if (lcr == 0 && art[n] == '\r') {
		lcr = 1;
		continue;
	    }
	    if (art[n] == '\n')
		lcr = 0;
	    if (lcr) {
		fputc('\r', fo);
	    }
	    fputc((int)(uint8)art[n], fo);
	    lcr = 0;
	}
	fflush(fo);
	if (ferror(fo))
	    logit(LOG_ERR, "write error sending article to pgpverify");
	fclose(fo);

	{
	    int v = 0;
	    for (n = 0; n < sizeof(buf); n += v) {
		v = read(fds[0], buf + n, sizeof(buf) - n);
		if (v <= 0)
		    break;
	    }
	    if (n > 0 && buf[n-1] == '\n')
		--n;
	    if (n > 0 && buf[n-1] == '\r')
		--n;
	    if (n == sizeof(buf))
		--n;
	    buf[n] = 0;

	    /*
	     * if non-null answer, pgp-verify succeeded
	     */
	    if (n) {
		if (strcmp(buf, ctl->ct_Verify) == 0) {
		    badCmd = NULL;
		} else {
		    if (DebugOpt)
			printf("pgp label mismatch:%s:%s:\n", buf, ctl->ct_Verify);
		    badCmd = "pgp parsed ok, but label did not match\n";
		}
	    } else {
		badCmd = "pgp header parsing failed\n";
	    }
	}
	close(fds[0]);
	waitpid(pid, NULL, 0);
    } else {
	logit(LOG_ERR, "Unable to run %s", DOpts.PgpVerifyArgs[0]);
    }
    return(badCmd);
}

#endif

