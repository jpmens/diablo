
/*
 * LIB/EXPIRE.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype int GetOverExpire(const char *groupName, OverExpire *oe);
Prototype void LoadExpireCtl(int force);

OverExpire *OvExBase = NULL;
MemPool *EXMemPool = NULL;

uint32 ExGmtMin = (uint32)-1;
time_t ExMTime = 0;

void dumpOvExpire(OverExpire *ov);

/*
 * Return the overview expire in seconds and the overview expire
 * entry for the specified group
 */
int
GetOverExpire(const char *groupName, OverExpire *oe)
{
    OverExpire *toe;

    memset(oe, 0, sizeof(OverExpire));
    oe->oe_ExpireDays = -1.0;
    oe->oe_LimitDays = 0;
    oe->oe_InitArts = 512;
    oe->oe_MinArts = 32;
    oe->oe_MaxArts = 0;
    oe->oe_DataSize = OD_HARTS;
    oe->oe_Next = NULL;
    oe->oe_StoreGZDays = -1.0;
    for (toe = OvExBase; toe; toe = toe->oe_Next) {
	if (WildCmp(toe->oe_Wild, groupName) == 0) {
	    strcpy(oe->oe_Wild, toe->oe_Wild);
	    if (toe->oe_ExpireDays != -2)
		oe->oe_ExpireDays = toe->oe_ExpireDays;
	    if (toe->oe_LimitDays != -2)
		oe->oe_LimitDays = toe->oe_LimitDays;
	    if (toe->oe_InitArts != -2)
		oe->oe_InitArts = toe->oe_InitArts;
	    if (toe->oe_MinArts != -2)
		oe->oe_MinArts = toe->oe_MinArts;
	    if (toe->oe_MaxArts != -2)
		oe->oe_MaxArts = toe->oe_MaxArts;
	    if (toe->oe_DataSize != -2)
		oe->oe_DataSize = toe->oe_DataSize;
	    if (toe->oe_StoreGZDays != -2)
		oe->oe_StoreGZDays = toe->oe_StoreGZDays;
	}
    }
    if (oe->oe_LimitDays == -1)
	oe->oe_LimitDays = oe->oe_ExpireDays;
    if (DebugOpt > 5)
	dumpOvExpire(oe);
    return(oe->oe_ExpireDays * 24.0 * 60.0 * 60.0);
}

void
loadExpireCtl(FILE *fi)
{
    char buf[MAXGNAME+256];
    int line = 0;
    OverExpire **pov = &OvExBase;

    if (DebugOpt)
	printf("Loading dexpire.ctl\n");

    freePool(&EXMemPool);
    while (fi && fgets(buf, sizeof(buf), fi) != NULL) {
	char *p = buf;
	char *q = buf;

	line++;
	while (isspace((int)*p))
	    p++;
	if (!*p || *p == '/' || *p == '#')
	    continue;

	if ((q = strchr(p, ':')) != NULL) {
	    int n;
	    OverExpire *ov = zalloc(&EXMemPool,
					sizeof(OverExpire) + (q - p) + 1);
	    ov->oe_ExpireDays = -2.0;
	    ov->oe_LimitDays = -2.0;
	    ov->oe_InitArts = -2;
	    ov->oe_MinArts = -2;
	    ov->oe_MaxArts = -2;
	    ov->oe_DataSize = -2;
	    ov->oe_StoreGZDays = -2.0;

	    memmove(&ov->oe_Wild, buf, q - p);
	    ov->oe_Wild[q-p] = 0;
	    *pov = ov;
	    pov = &ov->oe_Next;

	    q = p;
	    while ((q = strchr(q + 1, ':')) != NULL) {
		switch(q[1]) {
		    case 'a':
			ov->oe_InitArts = strtol(q + 2, NULL, 0);
			break;
		    case 'i':
			ov->oe_MinArts = strtol(q + 2, NULL, 0);
			break;
		    case 'j':
			ov->oe_MaxArts = strtol(q + 2, NULL, 0);
			break;
		    case 'e':
			n = strtol(q + 2, NULL, 0);
			if ((n ^ (n - 1)) != (n << 1) - 1) {
			    logit(LOG_ERR, "dexpire.ctl 'e' option not a power of 2 in line %d - ignoring", line);
			
			} else {
			    ov->oe_DataSize = n;
			}
			break;
		    case 'x':
			ov->oe_ExpireDays = strtod(q + 2, NULL);
			break;
		    case 'l':
			if (q[2] == 'x')
				ov->oe_LimitDays = -1;
			else
				ov->oe_LimitDays = strtod(q + 2, NULL);
			break;
		    case 'Z':
			ov->oe_StoreGZDays = strtod(q + 2, NULL);
			break;
		}
	    }
	}
    }
    *pov = NULL;
}

/*
 * Load dexpire.ctl - overview expiry info
 */
void
LoadExpireCtl(int force)
{
    time_t gmt = time(NULL) / 60;

    /*
     * check for dspool.ctl file modified once a minute
     */

    if (force || gmt != ExGmtMin) {
	struct stat st = { 0 };

	ExGmtMin = gmt;

	/*
	 * dexpire.ctl
	 */

	{
	    FILE *fi;

	    fi = fopen(PatLibExpand(DExpireCtlPat), "r");

	    if (fi == NULL) {
		logit(LOG_CRIT, "%s file not found",
					PatLibExpand(DExpireCtlPat));
		exit(1);
	    }

	    if (force || fi == NULL || 
		(fstat(fileno(fi), &st) == 0 && st.st_mtime != ExMTime)
	    ) {
		if (force)
		    fstat(fileno(fi), &st);
		ExMTime = st.st_mtime;	/* may be 0 if file failed to open */
		loadExpireCtl(fi);
	    }
	    if (fi)
		fclose(fi);
	}
    }
}

void
dumpOvExpire(OverExpire *ov)
{
    printf("%s:%.2f:%.2f:%d:%d:%d:%d:%d\n", ov->oe_Wild,
				ov->oe_ExpireDays,
				ov->oe_LimitDays,
				ov->oe_InitArts,
				ov->oe_MinArts,
				ov->oe_DataSize,
				ov->oe_MaxArts,
				(int)ov->oe_StoreGZDays);
}
