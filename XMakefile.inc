
/*
 * XMakefile.inc
 */

.set VERS 	5
.set SUBREV	CUR-20090530-00
/* .set SUBREV	REL */
.set DLIB	"$(BD)obj/libdiablo.a"
.set FLIB	"$(BD)obj/libfilter.a"
.set RLIB	"$(BD)obj/libdreader.a"
.set CDEFINES	'-DVERS="$(VERS)"' '-DSUBREV="$(SUBREV)"'
#if __GNUC__
.set CFLAGS	-g -O2 -Wall -Wstrict-prototypes "-I$(BD)" $(CDEFINES)
.set LSTATIC
.set CC		gcc
#else
.set CFLAGS	-g -O "-I$(BD)." $(CDEFINES)
.set LSTATIC
.set CC		cc
#endif

.set LFLAGS	-L$(BD)obj -lfilter -ldiablo -lutil -lm
.set IDIR	/news
.set MDIR	/news/man
.set INSTALL	install

/*
 * Add -lcrypto or -ldes if you enable any of the des encryption options in lib/vendor.h
 */
#ifdef __FreeBSD__
.set LFLAGS	-L$(BD)obj -lfilter -ldiablo -lutil -lm -lmd -lradius -lz
#endif

#ifdef __APPLE_CC__
.set CFLAGS	-g -O2 -Wall -Wstrict-prototypes "-I$(BD)" "-I/usr/include/openssl" $(CDEFINES)
.set LFLAGS	-L$(BD)obj -lfilter -ldiablo -lutil -lm -lcrypto -lssl -lz
.set LSTATIC	
#endif

#ifdef __hpux
.set LFLAGS	"-L$(BD)obj" -lfilter -ldiablo -lm
#endif

#ifdef __linux__
.set CFLAGS	-g -O2 -Wall -Wstrict-prototypes "-I$(BD)" $(CDEFINES) "-I/usr/include/openssl" -D_FILE_OFFSET_BITS=64
.set LFLAGS "-L$(BD)obj" -lfilter -ldiablo -lm -lz -lrt -lldap -lcrypto -lssl
#endif

#ifdef __osf__
.set CC cc
.set CFLAGS   -g3 -O2 "-I$(BD)" $(CDEFINES)
.set LFLAGS "-L$(BD)obj" -lfilter -ldiablo -lm -lrt
.set LSTATIC  -non_shared
.set INSTALL	installbsd
#endif

#ifdef sun
/*
 * some sun systems use -lsocket, some don't.  complain to sun
 *
 * Add -lldap to end of LFLAGS if you have LDAP_ENABLED
 */
.set LFLAGS	"-L$(BD)obj" -lfilter -ldiablo -lm -lnsl -lsocket -lposix4
.set CFLAGS	-g -O2 -Wall -Wstrict-prototypes "-I$(BD)" $(CDEFINES) -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
/* Sun cc compiler for 64-bit code */
/* .set CFLAGS	-g -xs -xtarget=ultra -xarch=v9 -O "-I$(BD)." $(CDEFINES) */
.set LSTATIC
.set INSTALL	/usr/ucb/install
#endif

#ifdef _AIX
.set INSTALL	installbsd
#endif

.set DIST /news

