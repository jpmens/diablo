
/*
 * X.C
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Array[256][64+1];

int
main(int ac, char **av)
{
    int i;

    for (i = 0; i <= 64; ++i) {
	int j;

	for (j = 0; j < 256; ++j)
	    Array[j][i] = -1;
    }

    for (i = 16; i <= 64; ++i) {
	char path[256];
	FILE *fi;

	sprintf(path, "output.%d", i);
	if ((fi = fopen(path, "r")) != NULL) {
	    char buf[256];
	    int j = 0;

	    while (fgets(buf, sizeof(buf), fi) != NULL) {
		if (strncmp(buf, "Count ", 6) == 0)
		    Array[j][i] = strtol(buf + 6, NULL, 0);
		++j;
	    }
	    fclose(fi);
	}
    }

    {
	int k;

	for (k = 16; k <= 64; k += 7) {
	    printf("SAMPLES ");
	    for (i = k; i < k + 7 && i <= 64; ++i)
		printf("    CRC%2d", i);
	    printf("\n");

	    for (i = 0; i < 182; ++i) {
		int j;

		printf("%5d.%dM ", (i + 1) / 10, (i + 1) % 10);

		for (j = k; j < k + 7 && j <= 64; ++j) {
		    printf(" %8d", Array[i][j]);
		}
		printf("\n");
	    }
	    printf("\n");
	}
    }
    return(0);
}

