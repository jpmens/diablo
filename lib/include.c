
/*
 * LIB/INCLUDE.C
 *
 * (c)Copyright 2002, Joe Greco and sol.net Network Services.
 *    All Rights Reserved.  Refer to the COPYRIGHT file in the
 *    base directory of this distribution for specific rights 
 *    granted.
 */

#include "defs.h"

Prototype FILE *iopen(const char *path, const char *mode);
Prototype int iclose(FILE *stream);
Prototype char *igets(char *str, int size, FILE *stream);
Prototype int istat(int fd, struct stat *st);





/*
 * These functions are designed to (very!) loosely replace
 * fopen/fclose/fgets, interpreting the named file as a 
 * include-directive preprocessor.
 *
 * Only one instance at a time is supported.
 */

#define	MAXLEVELS	4
#define	MAXFILES	1024

int inc_initialized = 0;
int inc_isopen = 0;

FILE *inc_fps[MAXLEVELS];
int inc_nfps = 0;
int inc_prog[MAXLEVELS];

char *inc_files[MAXFILES];
int inc_nfiles = 0;





/*
 * The return from this isn't a usable FILE *.  It's just
 * NULL for failure or non-NULL for success.
 */

FILE *
iopen(const char *path, const char *mode)
{
	FILE *rval;
	int i;

	if (! inc_initialized) {
		inc_initialized++;
		bzero((char *)inc_fps, sizeof(inc_fps));
		bzero((char *)inc_prog, sizeof(inc_prog));
		bzero((char *)inc_files, sizeof(inc_files));
	}


	/*
	 * Only allow one instance
	 */

	if (inc_isopen) {
		logit(LOG_ERR, "iopen failed; already open");
		return(NULL);
	}


	/*
	 * Clear the cached filenames being used by istat
	 */

	for (i = 0; i < inc_nfiles; i++) {
		zfreeStr(&SysMemPool, &inc_files[i]);
		inc_files[i] = NULL;
	}
	inc_nfiles = 0;


	/*
	 * Open
	 */

	if (*path == '|') {
		path++;
		if (! ((rval = popen(path, mode)))) {
			return(rval);
		}
		inc_fps[inc_nfps] = rval;
		inc_prog[inc_nfps] = 1;
		inc_nfps++;
	} else {
		if (! ((rval = fopen(path, mode)))) {
			return(rval);
		}
		inc_fps[inc_nfps] = rval;
		inc_nfps++;
		inc_files[inc_nfiles] = zallocStr(&SysMemPool, path);
		inc_nfiles++;
	}

	inc_isopen++;
	return(rval);
}





int
iclose(FILE *stream)
{
	int rval = 0;
	int i;

	for (i = 0; i < inc_nfps; i++) {
		if (inc_prog[i]) {
			rval += pclose(inc_fps[i]);
		} else {
			rval += fclose(inc_fps[i]);
		}
		inc_fps[i] = NULL;
		inc_prog[i] = 0;
	}
	inc_nfps = 0;

	inc_isopen = 0;
	return(rval);
}





/*
 * This might not work right if the user supplied
 * buffer is too small.  Not an issue here.
 */

char *
igets(char *str, int size, FILE *stream)
{
	char *rval, *fname, *ptr;
	FILE *fp;

	rval = fgets(str, size, inc_fps[inc_nfps - 1]);

	if (! rval) {

		/* We got an EOF or error */

		if (inc_nfps == 1) {

			/*
			 * We're on the primary file
			 * so we just return
			 */

			return(rval);
		}

		/* We step back to the previous file */

		inc_nfps--;
		if (inc_prog[inc_nfps]) {
			rval += pclose(inc_fps[inc_nfps]);
		} else {
			rval += fclose(inc_fps[inc_nfps]);
		}
		inc_fps[inc_nfps] = NULL;
		inc_prog[inc_nfps] = 0;

		/* 
		 * And now we recurse into igets to read
		 * "the next line"
		 */
		
		return(igets(str, size, stream));
	}

	/* We got a line */

	if (strncmp(str, "%include", 8)) {

		/* And it's not an include directive */

		return(rval);
	}

	if (inc_nfps >= MAXLEVELS)
	{
		logit(LOG_ERR, "too many %%include levels");
		return(rval);
	}

	/* Fetch out the filename */

	if (! (fname = strchr(str, '"'))) {
		logit(LOG_ERR, "no open quote in %%include");
		return(rval);
	}
	fname++;
	if (! (ptr = strchr(fname, '"'))) {
		logit(LOG_ERR, "no close quote in %%include");
		return(rval);
	}
	*ptr = '\0';

	/* Do the open */

	if (*fname == '|') {
		fname++;
		if (! ((fp = popen(fname, "r")))) {
			logit(LOG_ERR, "error popening %%include: %s", fname);
			*ptr = '"';
			return(rval);
		}
		inc_fps[inc_nfps] = fp;
		inc_prog[inc_nfps] = 1;
		inc_nfps++;
	} else {
		if (! ((fp = fopen(fname, "r")))) {
			logit(LOG_ERR, "error fopening %%include: %s", fname);
			*ptr = '"';
			return(rval);
		}
		inc_fps[inc_nfps] = fp;
		inc_nfps++;
		if (inc_nfiles < MAXFILES) {
			inc_files[inc_nfiles] = zallocStr(&SysMemPool, fname);
			inc_nfiles++;
		}
	}

	/* 
	 * And now we recurse into igets to read
	 * "the next line"
	 */

	return(igets(str, size, stream));
}





int
istat(int fd, struct stat *st)
{
	int rval = 0;
	struct stat sb;
	int i;

	/*
	 * Pick the newest stat time.
	 */

	bzero((char *)st, sizeof(struct stat));

	for (i = 0; i < inc_nfiles; i++) {
		if (stat(inc_files[i], &sb) < 0) {
			logit(LOG_ERR, "error stating %%include: %s", inc_files[i]);
			rval = -1;
		} else {
			if (sb.st_mtime > st->st_mtime) {
				bcopy((char *)&sb, (char *)st, sizeof(sb));
			}
		}
	}
	return(rval);
}
