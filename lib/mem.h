
/*
 * LIB/MEM.H	- Memory pool support
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution
 *    for specific rights granted.
 */

typedef struct MemNode {
    struct MemNode	*mr_Next;
    int32		mr_Bytes;
} MemNode;

typedef struct MemPool {
    struct MemPool	*mp_Next;
    void		*mp_Base;
    MemNode		*mp_First;
    int			mp_Size;
    int			mp_Used;
} MemPool;

