#!/bin/sh

#
# Change to suit your site
#
PATHNAME=newsfeed.ecrc.net
BINDIR=/usr/local/bin
LOGFILE=/var/log/news/path.log
SPOOLDIR=/news/spool/news

#
# Anything below here does NOT need changing!
#
date
echo "Running diablo-inpaths.pl ..."
$BINDIR/diablo-inpaths.pl -v -d $SPOOLDIR -l $LOGFILE -p $PATHNAME
date
echo ""

exit 1
