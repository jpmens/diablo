
/*
 * LIB/CTL.H
 */

/*
 * Control - control.ctl
 */

typedef struct Control {
    char        *ct_Msg;
    char        *ct_From;
    char        *ct_Groups;
    FILE        *ct_LogFo;
    long        ct_LogSeekPos;
    char        *ct_Verify;
    char        *ct_TmpFileName;
    int         ct_Flags;
} Control;

#define CTF_PGPVERIFY   0x0001
#define CTF_EXECUTE     0x0002
#define CTF_DROP        0x0004
#define CTF_LOG         0x0008
#define CTF_MAIL        0x0010
#define CTF_BREAK       0x0020
#define CTF_DGPVERIFY   0x0040

#define CTFF_VERIFY     (CTF_PGPVERIFY|CTF_DGPVERIFY)


