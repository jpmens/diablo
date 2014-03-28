
/*
 * LIB/DGP.C - Diablo's Great Privacy module (or Dillon's Great Privacy
 *		module, depending on how stuck up you think I am :-))
 *	
 *	Implements the DGP control protocol.  See README.DGP
 */

#include "defs.h"

Prototype const char *DGPVerify(Control *ctl, const char *art, int artLen);

#if DIABLO_DGP_SUPPORT

const char *
DGPVerify(Control *ctl, const char *art, int artLen)
{
    return("dgp protocol not implemented yet\n");
}

#endif

