
/*
 * LIB/ACTIVE.C	- dactive.kp active file support
 *
 * (c)Copyright 1998, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void InitDActive(const char *fileNamePat);
Prototype int InActive(const char *nglist);
Prototype int GenerateXRef(Buffer *b, const char *nglist, const char *npName, const char *ctl);
Prototype int UpdateActiveNX(char *xrefdata);
Prototype int CheckForModeratedGroups(const char *nglist);

KPDB *DActiveDB;

void 
InitDActive(const char *fileNamePat)
{
    DActiveDB = KPDBOpen(PatDbExpand(fileNamePat), O_RDWR);
    if (DActiveDB == NULL) {
	logit(LOG_CRIT, "InitDActive, open failed: %s", PatDbExpand(fileNamePat));
	exit(1);
    }
}

/*
 * Check to see if any of a list of comma-separated newsgroups exist in
 * the active file
 *
 *	Returns: 0 = no groups in active
 *		 1 = one or more groups in active
 */
int 
InActive(const char *nglist)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int found = 0;

    while (*p) {
	int i;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0)
	    break;
	if (i < sizeof(group)) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, 0, &recLen);
	    if (rec != NULL) {
		found = 1;
		break;
	    }
	}
	p += i;
	if (*p != ',')
	    break;
	++p;
    }
    return(found);
}

/*
 * GenerateXRef() - generate XRef: header for feeder.  Return the number
 *		    of groups that were included in the header.
 *
 *		    Note: if this is a control message, no XRef is generated
 *		    but we still return the group-match count.
 */

int 
GenerateXRef(Buffer *b, const char *nglist, const char *npName, const char *ctl)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int count = 0;
    int doneHeader = 0;

    while (*p) {
	int i;
	while (*p == ' ')
	    ++p;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0)
	    break;
	if (i < sizeof(group)) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);

	    /*
	     * Create ALL seen groups, if this define is enabled.  You will
	     * have to manually add a GD, M, and fix S as appropriate
	     * through some external process, if you use this.
	     */

	    if (! rec && DOpts.FeederAutoAddToActive) {
		if (ValidGroupName(group) < 0) {
		    /* logit(LOG_ERR, "group %s illegal", group); */
		} else {
		    char tsBuf[64];

		    KPDBWrite(DActiveDB, group, "NB", "1", KP_LOCK);
		    KPDBWrite(DActiveDB, group, "NE", "0", KP_LOCK_CONTINUE);
		    sprintf(tsBuf, "%08x", (int)time(NULL));
		    KPDBWrite(DActiveDB, group, "CTS", tsBuf, KP_LOCK_CONTINUE);
		    KPDBWrite(DActiveDB, group, "LMTS", tsBuf, KP_LOCK_CONTINUE);
		    KPDBWrite(DActiveDB, group, "S", "y", KP_UNLOCK);
	    	    rec = KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);
		}
	    }

	    /*
	     * Control messages are special, Xref header is still required,
	     * but must only contain control.* groups. Therefore, for non
	     * control messages, process as normal. But, only the control
	     * group is put in the Xref for control messages.
	     */

	    if ((rec != NULL && !ctl[0]) || (rec != NULL && ctl[0] &&
					(strncmp("control",group,7) == 0)))
		{
		int f1len;
		int f2len;
		const char *f1 = KPDBGetField(rec, recLen, "NE", &f1len, NULL);
		const char *f2 = KPDBGetField(rec, recLen, "NX", &f2len, NULL);

		if (f1 || f2) {
		    artno_t n;
		    char buf[20];

		    if (f2)
			n = (strtoll(f2, NULL, 10) + 1) & 0x7FFFFFFFFFFFFFFFLL;
		    else
			n = (strtoll(f1, NULL, 10) + 1) & 0x7FFFFFFFFFFFFFFFLL;

		    sprintf(buf, "%010lld", n);
		    KPDBWrite(DActiveDB, group, "NX", buf, KP_UNLOCK);
		    if (!doneHeader) {
			bwrite(b, "Xref: ", 6);
			bwrite(b, npName, strlen(npName));
			doneHeader = 1;
		    }
		    bwrite(b, " ", 1);
		    bwrite(b, group, i);
		    bwrite(b, ":", 1);
		    sprintf(buf, "%lld", n);
		    bwrite(b, buf, strlen(buf));
		    ++count;
		} else {
		    KPDBUnlock(DActiveDB, rec);
		}

	    /*
	     * Now catch the non control groups that were listed for a
	     * control message and unlock the active file.
	     */
	    } else if (rec != NULL && ctl[0]) {
                        ++count;
                        KPDBUnlock(DActiveDB, rec);
                    }
	}
	p += i;
	while (*p == ' ')
	    ++p;
	if (*p != ',')
	    break;
	++p;
    }
    if (doneHeader)
	bwrite(b, "\n", 1);
    return(count);
}

/*
 * CheckForModeratedGroups() - return 1 if any of the newsgroups listed are
 *			       marked as moderated in our active file.
 */
int
CheckForModeratedGroups(const char *nglist)
{
    const char *p = nglist;
    char group[MAXGNAME];
    int foundModerated = 0;

    while (*p) {
	int i;
	for (i = 0; p[i] && p[i] != ',' && p[i] != ' ' && p[i] != '\t'; ++i)
	    ;
	if (i == 0)
	    break;

	if (i < sizeof(group)) {
	    const char *rec;
	    int recLen;

	    bcopy(p, group, i);
	    group[i] = 0;
	    rec = KPDBReadRecord(DActiveDB, group, 0, &recLen);

	    if (rec != NULL) {
		int sLen;
		const char *s = KPDBGetField(rec, recLen, "S", &sLen, NULL);

		if (s && ((s[0] == 'm') || (s[0] == 'M'))) {
		    foundModerated = 1;
		}
	    }
	}

	p += i;
	if (*p != ',')
	    break;
	++p;
    }

    return foundModerated;
}

/*
 * UpdateActiveNX - update the active file NX field based on Xref: data
 *  This is useful for slaving Xref: data, but also maintaining
 *  the NX field in case this machine needs to become the Xref: master.
 */
int
UpdateActiveNX(char *xrefdata)
{
    char *p, *artnum, group[MAXGNAME];

    /* Throw away the first part of the line */
    p=strtok(xrefdata, " \t\n");
    if (p == NULL)
	return(-1);

    /* Now get the group:artnum pairs one at a time */
    p=strtok(NULL, " \t\n");
    while (p != NULL) {
	strncpy(group, p, sizeof(group) - 1);
	group[sizeof(group) - 1] = '\0';
	artnum=strchr(group, ':');
	if (artnum != NULL) {
	    const char *rec;
	    int recLen;

	    group[artnum-group]='\0';
	    artnum++;	
		
	    if (DebugOpt)
		ddprintf("XREF UPDATE %s %s\n",group, artnum);

	    rec=KPDBReadRecord(DActiveDB, group, KP_LOCK, &recLen);
	    if (rec != NULL) {
		artno_t n;
		char buf[20];

		n = (strtoll(artnum, NULL, 10)) & 0x7FFFFFFFFFFFFFFFLL;
		sprintf(buf, "%010lld", n);
		KPDBWrite(DActiveDB, group, "NX", buf, KP_UNLOCK);
	    } else {
		return(0);
	    }
	}
	p=strtok(NULL, " \t\n");
    }
    return(1);
}

