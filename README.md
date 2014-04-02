
This was a vanilla copy of [Diablo](http://www.openusenet.org/diablo/) version 

```
diablo-5-CUR-20090530-00.tgz	709721	Thu Jul 29 06:33:49 2010 GMT
```

from [the "daily" snapshots](http://www.openusenet.org/diablo/download/snapshots)

The following changes are in this branch:

### Patches from XS4ALL

I've applied the following patches from [http://www.miquels.cistron.nl/diablo/](http://www.miquels.cistron.nl/diablo/):

```
01_diablo-CUR-64bit-fixes.patch
02_diablo-CUR-large-artno-fixes.patch
03_diablo-CUR-ipv46-dns.patch
04_diablo-CUR-dreaderd-msg.patch
05_diablo-CUR-zlib-lfs.patch
06_diablo-CUR-glibc-strptime.patch
07_diablo-CUR-dreaderd-linux-db185.patch
08_diablo-CUR-listen-v4-v6.patch
09_diablo-CUR-zalloc-fixes.patch
10_diablo-CUR-dreaderd-groupmap-memleak.patch
11_diablo-CUR-dreader-sr_cconn-null.patch
12_diablo-CUR-dreaderd-fast-connlost.patch
20_diablo-CUR-large-fdset.patch
21_diablo-CUR-dreaderd-limit-pipelining.patch
30_diablo-CUR-diablo-allocstrat-weighted.patch
31_diablo-CUR-dreaderd-prealloc.patch
32_diablo-CUR-nostatic.patch
33_diablo-CUR-ipv6-enable.patch
```

I have **not** applied these:

```
40_diablo-CUR-dreaderd-friendly-maxconn-msgs.patch
41_diablo-CUR-dreaderd-rad-readerdef.patch
42_diablo-CUR-xs4all-config.patch
50_diablo-CUR-zalloc-debug.patch
```

### Reformat `XMakefile`

A bug in xmake (which was supposedly fixed in 1.06) makes it skip continuation lines and/or lines which end with a macro. I've joined long lines and have separated others with a blank line.

The result compiles cleanly (with some warning which can be ignored, it seems) on a CentOS 6.3 with
gcc version 4.4.7 20120313 (Red Hat 4.4.7-4) (GCC) .

### New LDAP module by JPMens

I've included a new LDAP module (which does **NOT** compare passwords -- sigh) which binds as a user to the directory, searches for `mail=`_username_ and then binds as that user with the password provided as part of the `AUTHINFO`. The source of the module (in `dreaderd/dns.c`) may have to be tweaked to specify the name/value of an LDAP attribute type which can be used to supply distinct _readerdefs` to the client.
