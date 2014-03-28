/*-
 * Copyright (c) 1997 Joe Greco, sol.net Network Services
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include	<stdio.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<string.h>
#include	<sys/types.h>
#include	<limits.h>
#include	<db.h>
#include	<math.h>
#include	<syslog.h>
#include	<varargs.h>
#include	<time.h>





int main(int argc, char *argv[])
{
	FILE *fp, *fout, *ftmp;
	char ibuf[8192], *ptr;
	char buf[1024], *bptr;
	char out[2048];
	int rval;
	char abuf[1100000];
	char *messageid;
	int nbytes = 0;
	int nbatch = 0;

	openlog("batcher", LOG_PID, LOG_NEWS);

	if (argc != 3) {
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	if (! (fp = fopen(argv[1], "r"))) {
		if (errno != ENOENT) {
			syslog(LOG_ERR, "%s: %m", argv[1]);
			exit(1);
		}
		exit(0);
	}
	fout = NULL;

	while (! feof(fp)) {
		fgets(ibuf, sizeof(ibuf), fp);
		ptr = ibuf;
		bptr = buf;
		while (*ptr != ' ' && *ptr) {
			*bptr++ = *ptr++;
		}
		*bptr++ = ':';
		if (*ptr == ' ') {
			ptr++;
		}
		while (*ptr != ' ' && *ptr) {
			ptr++;
		}
		if (*ptr == ' ') {
			ptr++;
		}
		while (*ptr != ' ' && *ptr != '\n' && *ptr) {
			*bptr++ = *ptr++;
		}
		*bptr = '\0';

		if (! feof(fp)) {
			if ((rval = diab_read(buf, abuf, sizeof(abuf))) < 0) {
				syslog(LOG_ERR, "diab_read failed: %s", buf);
				continue;
			}
			if (*(abuf + rval - 1) == '\n') {
				if (! fout) {
					if (! (fout = popen(argv[2], "w"))) {
						syslog(LOG_ERR, "popen: %s: %m", argv[2]);
						break;
					}
				}
				fprintf(fout, "#! rnews %d\n", rval);
				fflush(fout);
				fwrite(abuf, sizeof(char), rval, fout);
				fflush(fout);
				nbytes += rval;
			} else {
				syslog(LOG_ERR, "article didn't end with newline: %s", buf);
				continue;
			}
			if (nbytes > 2000000) {
				pclose(fout);
				fout = NULL;
				nbytes = 0;
				nbatch++;
				if (nbatch > 10) {
					break;
				}
			}
		}
	}
	if (fout) {
		pclose(fout);
		fout = NULL;
	}

	if (! feof(fp)) {
		sprintf(out, "%s.temp", argv[1]);
		if (! (ftmp = fopen(out, "w"))) {
			syslog(LOG_ERR, "%s: %m", out);
			exit(1);
		}
		while (! feof(fp)) {
			fgets(buf, sizeof(buf), fp);
			if (! feof(fp)) {
				fputs(buf, ftmp);
			}
		}
		fclose(ftmp);
		if (rename(out, argv[1]) < 0) {
			syslog(LOG_ERR, "rename(%s, %s): %m", out, argv[1]);
		}
	} else {
		if (unlink(argv[1]) < 0) {
			syslog(LOG_ERR, "unlink(%s): %m", argv[1]);
		}
	}
	fclose(fp);

	exit(0);
}
