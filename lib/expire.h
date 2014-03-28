
/*
 * LIB/EXPIRE.H
 */

typedef struct ExpireCtl {
    struct ExpireCtl    *ex_Next;
    char                *ex_Wild;
    int                 ex_Slot;
    int                 ex_MaxSize;
    int                 ex_ExpireLines;
    int                 ex_InitArts;
    int                 ex_MinArts;
    int                 ex_MaxArts;
    double              ex_ExpireDays;
    double              ex_CrossFactor;
} ExpireCtl;

