
/*
 * FILTER/DEFS.H
 */

#include <lib/defs.h>
#include "socket.h"

#define         NAMELEN         256
#define         FILENAMELEN     1024

#ifndef PATH_FILTER
#if 1
#define         PATH_FILTER     "/news/cfilter/run"
#else
#define         PATH_FILTER     "remote.host.domain.name.9000"
#endif
#endif

#include <obj/filter-protos.h>

