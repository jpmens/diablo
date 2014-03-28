/*-
 * Copyright (c) 1999, 2002 Joe Greco and sol.net Network Services
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
 *	$Id: decrypt.c,v 1.1 2002/05/03 20:36:42 jgreco Exp $
 */

#include	<stdio.h>
#include	<des.h>
#include	<string.h>

char *XTRAUnmangle(char *str, char *cryptpw)
{
	static char res[256];
	char *rptr, *sptr;
	int count, i;
	unsigned char plain[8], enc[8];
	des_cblock key;
	des_key_schedule sched;

	des_string_to_key(cryptpw, &key);
	des_set_key(&key, sched);

	if (! strncmp(str, "DXC=", 4)) {
		str += 4;
	}

	*res = '\0';
	if (strlen(str) % 11) {
		snprintf(res, sizeof(res), "(X-Trace damaged; may not decode OK) ");
	}

	rptr = res + strlen(res);
	
	sptr = str;
	count = 0;
	bzero(enc, sizeof(enc));
	while (*sptr) {
	    switch (count % 11) {
		case 0:
		    enc[0] = (*sptr - '0');
		    break;
		case 1:
		    enc[0] += ((*sptr - '0') & 0x03) << 6;
		    enc[1] = (*sptr - '0') >> 2;
		    break;
		case 2:
		    enc[1] += ((*sptr - '0') & 0x0f) << 4;
		    enc[2] = (*sptr - '0') >> 4;
		    break;
		case 3:
		    enc[2] += (*sptr - '0') << 2;
		    break;
		case 4:
		    enc[3] = (*sptr - '0');
		    break;
		case 5:
		    enc[3] += ((*sptr - '0') & 0x03) << 6;
		    enc[4] = (*sptr - '0') >> 2;
		    break;
		case 6:
		    enc[4] += ((*sptr - '0') & 0x0f) << 4;
		    enc[5] = (*sptr - '0') >> 4;
		    break;
		case 7:
		    enc[5] += (*sptr - '0') << 2;
		    break;
		case 8:
		    enc[6] = (*sptr - '0');
		    break;
		case 9:
		    enc[6] += ((*sptr - '0') & 0x03) << 6;
		    enc[7] = (*sptr - '0') >> 2;
		    break;
		case 10:
		    enc[7] += ((*sptr - '0') & 0x0f) << 4;
		    break;
	    }
	    count++;
	    sptr++;
	    if (! *sptr || (count % 11) == 0) {
		/* Decrypt block of (up to) 8 and pump out plain ASCII */
		des_ecb_encrypt((des_cblock *)enc,(des_cblock *)plain, sched, 0);
		bzero(enc, sizeof(enc));
		for (i = 0; i < 8; i++) {
			*rptr++ = plain[i];
		}
	    }
	}
	*rptr = '\0';
	return(res);
}

int main(int argc, char *argv[])
{
	char linebuf[2048], *ptr;

	if (argc != 2) {
		fprintf(stderr, "usage: decrypt <password>\n\nTo decrypt a Diablo encrypted X-Trace header, run this program, specifying\nthe password used for encryption.  Feed the entire X-Trace: line, including\nthe X-Trace: header token, to this program, and it will return the\noriginal data.\n");
		exit(1);
	}

	while (! feof(stdin)) {
		fgets(linebuf, sizeof(linebuf), stdin);
		if ((ptr = strrchr(linebuf, '\n'))) {
			*ptr = '\0';
		}
		if (! feof(stdin) && ! strncmp(linebuf, "X-Trace: ", 9)) {
			printf("\n**>> %s <<**\n==>> %s <<==\n", linebuf, XTRAUnmangle(linebuf + 9, argv[1]));
		}
	}
}
