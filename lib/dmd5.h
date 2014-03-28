/* See dmd5.c for explanation and copyright information.  */

#ifndef DMD5_H
#define DMD5_H

/* Unlike previous versions of this code, uint32 need not be exactly
   32 bits, merely 32 bits or more.  Choosing a data type which is 32
   bits instead of 64 is not important; speed is considerably more
   important.  ANSI guarantees that "unsigned long" will be big enough,
   and always using it seems to have few disadvantages.  */
typedef unsigned long diablo_uint32;

struct diablo_MD5Context {
	diablo_uint32 buf[4];
	diablo_uint32 bits[2];
	unsigned char in[64];
};

void diablo_MD5Init (struct diablo_MD5Context *context);
void diablo_MD5Update (struct diablo_MD5Context *context,
			   unsigned char const *buf, unsigned len);
void diablo_MD5Final (unsigned char digest[16],
			  struct diablo_MD5Context *context);
void diablo_MD5Transform (diablo_uint32 buf[4], const unsigned char in[64]);

#endif /* !DMD5_H */
