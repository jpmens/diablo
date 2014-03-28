
/*
 * LIB/IPLIST.C
 *
 * (c)Copyright 2001, Russell Vincent, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 *
 * This file maintains an IP address hash list that can be used
 * as a ban/spam list.
 */

#include "defs.h"

#define DEFAULT_HSIZE	64
#define	DEFAULT_HLINK	8

Prototype void IPListAdd(IPList **iplist, char *ip, int count, time_t expire, int hashsize, int hashlinksize);
Prototype int IPListCheck(IPList *iplist, char *ip, int decrement, time_t expire, time_t t, int hashsize);
Prototype void IPListDump(FILE *fo, IPList *iplist, int hashsize);

void
IPListAdd(IPList **iplist, char *ip, int count, time_t expire, int hashsize, int hashlinksize)
{
    md5hash_t iphash;
    IPList *ipl;
    IPList *iparray;

    if (hashsize == 0)
	hashsize = DEFAULT_HSIZE;
    if (hashlinksize == 0)
	hashlinksize = DEFAULT_HLINK;
    if (*iplist == NULL) {
	*iplist = (struct IPList *)malloc(hashsize * sizeof(struct IPList));
	if (*iplist == NULL)
	    return;
	bzero(*iplist, hashsize * sizeof(struct IPList));
    }
    iparray = *iplist;
    memcpy(&iphash, md5hash(ip), sizeof(iphash));
    ipl = &iparray[(iphash.h1 + iphash.h2) & (hashsize - 1)];
    if (ipl->il_ip != NULL && strcmp(ip, ipl->il_ip) == 0)
	return;
    while (ipl->il_expire != 0 && ipl->il_next != NULL) {
	ipl = ipl->il_next;
	hashlinksize--;
	if (ipl != NULL && ipl->il_ip != NULL && strcmp(ip, ipl->il_ip) == 0)
	    return;
    }
    if (hashlinksize < 0)
	return;
    if (ipl->il_expire != 0) {
	ipl->il_next = (struct IPList *) malloc(sizeof(struct IPList));
	ipl = ipl->il_next;
	bzero(ipl, sizeof(*ipl));
    }
    if (ipl->il_ip != NULL)
	free(ipl->il_ip);
    ipl->il_ip = strdup(ip);
    ipl->il_count = count;
    ipl->il_expire = expire;
    ipl->il_next = NULL;
}

/*
 * Check to see if an IP has reached a count of zero
 *
 * Returns:
 *	-1 = not found or expired
 *	 0 = count is zero
 *	>0 = count
 */
int
IPListCheck(IPList *iplist, char *ip, int decrement, time_t expire, time_t t, int hashsize)
{
    md5hash_t iphash;
    IPList *ipl;

    if (iplist == NULL)
	return(-1);
    if (hashsize == 0)
	hashsize = DEFAULT_HSIZE;
    memcpy(&iphash, md5hash(ip), sizeof(iphash));
    ipl = &iplist[(iphash.h1 + iphash.h2) & (hashsize - 1)];
    while (ipl != NULL && ipl->il_ip != NULL && strcmp(ip, ipl->il_ip) != 0)
	ipl = ipl->il_next;
    if (ipl == NULL)
	return(-1);
    if (ipl->il_expire < t) {
	if (ipl->il_ip != NULL)
	    free(ipl->il_ip);
	bzero(ipl, sizeof(*ipl));
	return(-1);
    }
    if (decrement && ipl->il_count > 0) {
	ipl->il_count--;
	ipl->il_expire = expire;
    }
    return(ipl->il_count);
}

void
IPListDelete(IPList *iplist, char *ip)
{
    
}

void
IPListDump(FILE *fo, IPList *iplist, int hashsize)
{
    time_t t = time(NULL);
    int i;
    IPList *ipl;

    if (iplist == NULL)
	return;
    if (hashsize == 0)
	hashsize = DEFAULT_HSIZE;
    for (i = 0; i < hashsize; i++) {
	ipl = &iplist[i];
	while (ipl != NULL) {
	    if (ipl->il_ip != NULL)
		fprintf(fo, "%c %-50s %10d %12u\n",
			(ipl->il_count == 0 && ipl->il_expire > t) ? 'B' : ' ',
			ipl->il_ip,
			ipl->il_count,
			(int)(ipl->il_expire - t)
		);
	    ipl = ipl->il_next;
	}
    }
}

