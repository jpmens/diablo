
/*
 * UTIL/PGPVERIFY.C
 *
 * pgpverify < control-news-article
 *
 * Extract X-PGP-Sig header from news article, construct 
 * PGP text file, and pipe into pgp -f
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

#include "defs.h"

typedef struct Header {
    struct Header *he_Next;	/* next header				*/
    char	  *he_Name;	/* name of header & body, incl of newlines */
    int		  he_Flagged;
} Header;

char *FindHeader(const char *name, char *def);

Header *HBase = NULL;
Header **PHe = &HBase;
char *argv[10] = { PGP_PATH, PGP_ARG0, "-f", NULL };

void
Usage(void)
{
    printf("Usage: pgpverify [-d] [pgp_path] [pgp_args]\n");
    exit(1);
}

int
main(int ac, char **av)
{
    char buf[8192];
    Header *he = NULL;
    int nl = 1;
    int r = 2;
    char *sig;
    char *version = NULL;
    char *sheaders = NULL;
    char *signature = NULL;
    pid_t pid = 0;
    int fds[3] = { -1, -1, -1 };
    int i;
    int argcount = 0;
    int argend = 0;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-' || argend) {
	    argend = 1;
	    if (argcount > 8)
		break;
	    argv[argcount++] = ptr;
	    if (argcount == 0) {
		if (strrchr(ptr, '/') == NULL)
		    argv[argcount++] = ptr;
		else
		    argv[argcount++] = strrchr(ptr, '/') + 1;
	    }
	    argv[argcount] = NULL;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case '-':
	    argend = 1;
	    break;
	case 'd':
	    if (*ptr)
		DebugOpt = strtol(ptr, NULL, 0);
	    else
		DebugOpt = 1;
	    break;
	default:
	    fprintf(stderr, "illegal option: %s\n", ptr - 2);
	    Usage();
	}
    }

    /*
     * Scan headers in message
     */

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
	int l;

	if ((l = strlen(buf)) == 1)	/* blank line, end of headers */
	    break;

	/*
	 * check for end of previous header
	 */
	if (he && nl && buf[0] != ' ' && buf[0] != '\t') {
	    he = NULL;
	}

	/*
	 * create new header if necessary
	 */
	if (he == NULL) {		/* new header		      */
	    he = malloc(sizeof(Header));
	    nl = 1;
	    *PHe = he;
	    PHe = &he->he_Next;
	    he->he_Next = NULL;
	    he->he_Name = malloc(1);
	    he->he_Name[0] = 0;
	}

	/*
	 * append data buffer to header
	 */
	{
	    int r = strlen(he->he_Name);
	    he->he_Name = realloc(he->he_Name, r + l + 1);
	    strcpy(he->he_Name + r, buf);
	}

	/*
	 * handle headers longer then our buffer size
	 */

	if (buf[l-1] != '\n')
	    nl = 0;
	else
	    nl = 1;
    }

    /*
     * Extract X-PGP-Sig: header
     */

    if ((sig = FindHeader("X-PGP-Sig", NULL)) == NULL)
	exit(1);

    /*
     * Extract version and list of signed headers
     */

    version = strtok(sig, " \n");
    sheaders = strtok(NULL, " \n");
    signature = strtok(NULL, "");

    if (version == NULL || sheaders == NULL || signature == NULL)
	exit(2);

    /*
     * Start pgp -f
     */

    {
	char nhtmp[PATH_MAX];
	char *env[2];

	env[0] = nhtmp;
	env[1] = NULL;

	snprintf(nhtmp, sizeof(nhtmp), "HOME=%s", NewsHome);

	pid = RunProgramPipe(fds, RPF_STDOUT|RPF_STDERR, argv, env);
	if (pid <= 0)
	    exit(2);
    }

    /*
     * output PGP text
     */

    {
	FILE *fo = fdopen(fds[1], "w");
	char *name;

	fprintf(fo, "-----BEGIN PGP SIGNED MESSAGE-----\n");
	fprintf(fo, "\n");
	fprintf(fo, "X-Signed-Headers: %s\n", sheaders);

	/*
	 * extract requested headers
	 */
	for (name = strtok(sheaders, ","); name; name = strtok(NULL, ",")) {
	    fprintf(fo, "%s:%s", name, FindHeader(name, "\n"));
	}

	/*
	 * blank line, then article body
	 */

	fprintf(fo, "\n");

	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '-')
		fputs("- ", fo);
	    fputs(buf, fo);
	}

	/*
	 * blank line, then PGP signature
	 */

	fprintf(fo, "\n");
	fprintf(fo, "-----BEGIN PGP SIGNATURE-----\n");
	fprintf(fo, "Version: %s\n", version);
	fprintf(fo, "\n");

	for (name = signature; *name; ++name) {
	    if (*name == ' ' || *name == '\t')
		continue;
	    fputc(*name, fo);
	}

	fprintf(fo, "-----END PGP SIGNATURE-----\n");
	fclose(fo);
    }

    /*
     * input results
     */

    {
	FILE *fi = fdopen(fds[2], "r");
	while (fgets(buf, sizeof(buf), fi) != NULL) {
            char *good;

	    if (DebugOpt)
		printf(">> %s", buf);
	    if (strstr(buf, "Bad signature")) {
		r= 3;
		break;
	    }
	    if ((good=strstr(buf, "Good signature from"))) {
		char *tok;
		good = strchr(good + 19, '"');
		if (good == NULL)
		    break;
		tok = strtok(good + 1, " \"\n");
		if (tok[0]) {
		    r = 0;
		    printf("%s\n", tok);
		}
		break;
	    }
	}
	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    if (DebugOpt)
		printf(">> %s", buf);
	}
	fclose(fi);
    }
    if (pid > 0)
	waitpid(pid, NULL, 0);
    exit(r);
}

char *
FindHeader(const char *name, char *def)
{
    Header *he;
    int l = strlen(name);

    for (he = HBase; he; he = he->he_Next) {
	if (strncmp(he->he_Name, name, l) == 0 && he->he_Name[l] == ':')
	    break;
    }
    if (he)
	return(he->he_Name + l + 1);
    return(def);
}


