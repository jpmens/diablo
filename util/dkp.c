
/*
 * UTIL/DKP.C
 *
 * DKP -w filename [key tok=data]
 * DKP filename [key]
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

int
main(int ac, char **av)
{
    char *fileName = NULL;
    char *key = NULL;
    char *tokval = NULL;
    char *val;
    int i;
    int readMode = 0;
    int writeMode = 0;
    int forceSort = 0;
    int deleteMode = 0;
    int trimMode = 0;
    KPDB *db;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    if (fileName == NULL)
		fileName = ptr;
	    else if (key == NULL)
		key = ptr;
	    else if (tokval == NULL)
		tokval = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'd':
	    DebugOpt = 1;
	    break;
	case 'x':
	    deleteMode = 1;
	    break;
	case 'r':
	    readMode = 1;
	    break;
	case 'w':
	    readMode = 1;
	    writeMode = 1;
	    break;
	case 's':
	    forceSort = 1;
	    break;
	case 't':
	    trimMode = 1;
	    break;
	case 'V':
	    PrintVersion();
	    break;
	default:
	    break;
	}
    }

    if (fileName == NULL) {
	printf("DKP [-d] [-s] [-r] [-w] filename [key [tok=data]]\n");
	exit(0);
    }

    if (trimMode == 1) {
	/*
	 * trim file, remove dead entries.  Only works if there are no other
	 * users of the file.
	 */
	int fd = open(fileName, O_RDWR);

	forceSort = 1;

	if (fd >= 0) {
	    struct stat st;
	    char *buf = malloc(strlen(fileName) + 32);
	    FILE *fi;
	    FILE *fo;

	    if (hflock(fd, 0, XLOCK_EX|XLOCK_NB) < 0) {
		printf("Unable to exclusively lock %s, someone else is using it\n", fileName);
		exit(1);
	    }
	    if (fstat(fd, &st) < 0 || st.st_nlink == 0) {
		printf("File was ripped out from under us, exiting\n");
		exit(1);
	    }
	    sprintf(buf, "%s.tmp%d", fileName, (int)getpid());
	    fi = fdopen(fd, "r");
	    fo = fopen(buf, "w");
	    if (fo) {
		char tmp[1024];
		int lnl = 1;

		while (fgets(tmp, sizeof(tmp), fi) != NULL) {
		    int l = strlen(tmp);
		    if (lnl && tmp[0] == '-')
			continue;
		    fputs(tmp, fo);
		    if (l == 0 || tmp[l-1] != '\n')
			lnl = 0;
		    else
			lnl = 1;
		}
		fflush(fo);
		if (ferror(fo)) {
		    printf("Error writing temporary file: %s\n", strerror(errno));
		    exit(1);
		}
		fclose(fo);
		rename(buf, fileName);
		hflock(fd, 0, XLOCK_UN);
		fclose(fi);
	    } else {
		printf("Unable to create %s\n", buf);
	    }
	} else {
	    printf("Unable to open %s\n", fileName);
	}
    }

    db = KPDBOpen(fileName, O_RDWR | ((writeMode) ? O_CREAT : 0));

    if (db == NULL) {
	fprintf(stderr, "Unable to open KPDB %s\n", fileName);
	exit(0);
    }

    if (forceSort) {
	KPDBReSort(db);
    }

    if (key == NULL && (readMode || writeMode)) {
	char buf[1024];

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    char *key = strtok(buf, " \t\n");
	    char *tokval = (key) ? strtok(NULL, " \t\n") : NULL;

	    if (deleteMode && key) {
		KPDBDelete(db, key);
	    } else if (writeMode && key && tokval && (val = strchr(tokval, '='))) {
		*val++ = 0;
		KPDBWrite(db, key, tokval, val, 0);
	    } else if (readMode && key) {
		const char *data;
		int dataLen;

		if ((data = KPDBReadRecord(db, key, 0, &dataLen)) == NULL) {
		    printf("key %s data <not-found>\n", key);
		} else {
		    printf("key %s data (%d) %*.*s", key, dataLen, dataLen, dataLen, data);
		}
	    }
	}
    } else if (deleteMode && key) {
	KPDBDelete(db, key);
    } else if (writeMode && key && tokval && (val = strchr(tokval, '='))) {
	int r;

	*val++ = 0;
	r = KPDBWrite(db, key, tokval, val, 0);
    } else if (readMode && key) {
	const char *data;
	int dataLen;

	if ((data = KPDBReadRecord(db, key, 0, &dataLen)) == NULL) {	/* XXX not locking thru printf */
	    printf("<not found>\n");
	} else {
	    printf("key %s data (%d) %*.*s", key, dataLen, dataLen, dataLen, data);
	}
    }
    KPDBClose(db);
    return(0);
}

