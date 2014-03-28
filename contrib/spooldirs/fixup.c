/*-
 * Copyright (c) 2000 Joe Greco and sol.net Network Services
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
 *	$Id: fixup.c,v 1.1 2000/08/16 16:23:43 jgreco Exp $
 */

#include <stdio.h>
#include <string.h>

/* CHANGE THIS AS NEEDED */
#error You must set the #define and remove the #error directive.
#define	DIABSPOOLDIRS	9

/* Feed me a list of N.xx/D.xxxxxxxx and I will tell you which are misplaced */

int main()
{
	char buffer[8192], *ptr;
	int n, d, i;

	while (! feof(stdin)) {
		fgets(buffer, sizeof(buffer), stdin);
		if (! feof(stdin)) {
			if ((ptr = strrchr(buffer, '\n'))) {
				*ptr = '\0';
			}
			if (sscanf(buffer, "N.%02x/D.%08x", &n, &d) != 2) {
				printf("Malformed line %s\n", buffer);
			} else {
				i = ((d / 10) % DIABSPOOLDIRS);
				if (n != i) {
					printf("mv N.%02x/D.%08x N.%02x/D.%08x\n", n, d, i, d);
				} else {
					printf("ok N.%02x/D.%08x\n", n, d);
				}
			}
		}
	}
}
