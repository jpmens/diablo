
/*
 * LIB/NODE.C	- Simple string node routines
 *
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

Prototype void FreeNodeList(MemPool **pmp, Node **pnode);
Prototype void FreeNode(MemPool **pmp, Node *node);
Prototype Node *MakeNode(MemPool **pmp, const char *str, int val);
Prototype Node *MakeNode2(MemPool **pmp, const char *str1, const char *str2, int val);
Prototype Node *MakeNodeAppList(Node ***ppnode, MemPool **pmp, const char *str, int val);

#ifdef NOTDEF
const char *GetStrTab(const char *s);
void RelStrTab(const char *s);
int strhash(const char *s);
#endif

void
FreeNodeList(MemPool **pmp, Node **pnode)
{
    Node *no;

    while ((no = *pnode) != NULL) {
	*pnode = no->no_Next;
	FreeNode(pmp, no);
    }
}

void
FreeNode(MemPool **pmp, Node *node)
{
    if (node->no_Name2)
	zfree(pmp, node, sizeof(Node) + strlen(node->no_Name) + strlen(node->no_Name2) + 2);
    else
	zfree(pmp, node, sizeof(Node) + strlen(node->no_Name) + 1);
}

Node *
MakeNode(MemPool **pmp, const char *str, int val)
{
    Node *node = zalloc(pmp, sizeof(Node) + strlen(str) + 1);

    node->no_Next = NULL;
    node->no_Name = (char *)(node + 1);
    node->no_Name2 = NULL;
    node->no_Data = NULL;
    node->no_Value = val;
    strcpy((char *)node->no_Name, str);
    return(node);
}

Node *
MakeNode2(MemPool **pmp, const char *str1, const char *str2, int val)
{
    Node *node = zalloc(pmp, sizeof(Node) + strlen(str1) + strlen(str2) + 2);

    node->no_Next = NULL;
    node->no_Name = (char *)(node + 1);
    node->no_Name2 = node->no_Name + strlen(str1) + 1;
    node->no_Data = NULL;
    node->no_Value = val;
    strcpy((char *)node->no_Name, str1);
    strcpy((char *)node->no_Name2, str2);

    return(node);
}

Node *
MakeNodeAppList(Node ***ppnode, MemPool **pmp, const char *str, int val)
{
    Node *r = NULL;
    Node **pnode;

    if ((pnode = *ppnode) != NULL) {
	*pnode = r = MakeNode(pmp, str, val);
	*ppnode = &r->no_Next;
    }
    return(r);
}

#ifdef NOTDEF

/*
 *
 */

#define STHSIZE         128
#define STHMASK         (STHSIZE-1)

typedef struct STab {
    struct STab *st_Next;
    int         st_Refs;
    int         st_Hv;
} STab;

STab    *STHash[STHSIZE];

const char *
GetStrTab(const char *s)
{
    int hv = strhash(s);
    STab **pst = &STHash[hv & STHMASK];
    STab *st;

    while ((st = *pst) != NULL) {
        if (st->st_Hv == hv && strcmp((char *)(st + 1), s) == 0)
            break;
        pst = &st->st_Next;
    }
    if (st == NULL) {
        st = *pst = malloc(sizeof(STab) + strlen(s) + 1);
        st->st_Next = NULL;
        st->st_Hv = hv;
        st->st_Refs = 0;
        strcpy((char *)(st + 1), s);
    }
    ++st->st_Refs;
    return((char *)(st + 1));
}

void
RelStrTab(const char *s)
{
    int hv = strhash(s);
    STab **pst = &STHash[hv & STHMASK];
    STab *st;

    while ((st = *pst) != NULL) {
        if (st->st_Hv == hv && strcmp((char *)(st + 1), s) == 0)
            break;
        pst = &st->st_Next;
    }
    if (st && --st->st_Refs == 0) {
        *pst = st->st_Next;
        free(st);
    }
}

int
strhash(const char *s)
{
    int hv = 0xA45CD32F;

    while (*s) {
        hv = (hv << 5) ^ *s ^ (hv >> 23);
        ++s;
    }
    return(hv ^ (hv > 16));
}

#endif

