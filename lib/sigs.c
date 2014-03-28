
/*
 * LIB/SIGS.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

Prototype void rsignal(int sigNo, void (*func)(int sigNo));
Prototype void nrsignal(int sigNo, void (*func)(int sigNo));

void 
internal_signal(int sigNo, void (*func)(int sigNo), int restart)
{
    struct sigaction sa;

    bzero(&sa, sizeof(sa));

#ifndef SUNOS
    if (restart)
	sa.sa_flags = SA_RESTART;	/* do not set SA_RESETHAND */
#endif
    sa.sa_handler = func;
    sigaddset(&sa.sa_mask, sigNo);

    if (sigaction(sigNo, &sa, NULL) < 0) {
	perror("sigaction");
	exit(1);
    }
}

void 
rsignal(int sigNo, void (*func)(int sigNo))
{
    internal_signal(sigNo, func, 1);
}

void 
nrsignal(int sigNo, void (*func)(int sigNo))
{
    internal_signal(sigNo, func, 0);
}

