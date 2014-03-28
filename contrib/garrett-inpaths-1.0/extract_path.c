#include <sys/types.h>
#include <sys/mman.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PGSZ	       	4096
#define	PGMASK		~(PGSZ - 1)

char *
mmapodd(int fd, off_t off, size_t len)
{
	off_t xoff = (off & PGMASK);
	size_t xlen = ((off + len + PGSZ - 1) & PGMASK);
	char *cp;

	cp = mmap(0, xlen, PROT_READ, MAP_PRIVATE, fd, xoff);
	if (cp == MAP_FAILED)
		return 0;
	return cp + (off - xoff);
}

int
munmapodd(char *cp, off_t offset, size_t len)
{
	return munmap(cp - (offset & ~PGMASK),
		      (offset + len + PGSZ - 1) & PGMASK);
}

int
findpath(int fd, off_t offset, size_t len)
{
	char *art;
	char *pathp, *ep;

	pathp = art = mmapodd(fd, offset, len);
	if (!art)
		return -1;
	while (pathp < art + len) {
		if (strncasecmp("Path:", pathp, 5) == 0) {
			ep = pathp;
			while (*ep && *ep != '\n')
				ep++;
			printf("%.*s\n", ep - pathp, pathp);
			break;
		}
		pathp = strchr(pathp, '\n');
		if (pathp == 0)
			break;
		pathp++;
		if (*pathp == '\n')
			break;
	}
	munmapodd(art, offset, len);
	return 0;
}

void
crunch(FILE *fp)
{
	size_t linelen, artlen;
	off_t artoff;
	char *line, *msgid, *offlen, *len;
	int fd;

	while ((line = fgetln(fp, &linelen)) != 0) {
		line[linelen - 1] = '\0';
		msgid = strchr(line, ' ');
		if (!msgid)
			continue;
		*msgid++ = '\0';
		offlen = strrchr(msgid, ' ');
		if (!offlen)
			continue;
		offlen++;
		artoff = strtoq(offlen, &len, 10);
		if (*len != ',')
			continue;
		artlen = strtoul(++len, 0, 10);

		fd = open(line, O_RDONLY, 0);
		if (fd < 0) {
			warn("%s", line);
			continue;
		}
		findpath(fd, artoff, artlen);
		close(fd);
	}
}

int
main(int argc, char **argv)
{
	FILE *fp;
	if (argc < 3)
		exit(1);

	fp = fopen(argv[1], "r");
	if (fp == 0)
		err(1, "fopen: %s", argv[1]);
	if (unlink(argv[1]) < 0)
		err(1, "unlink: %s", argv[1]);
	if (chdir(argv[2]) < 0)
		err(1, "chdir: %s", argv[2]);
	crunch(fp);
	return 0;
}
