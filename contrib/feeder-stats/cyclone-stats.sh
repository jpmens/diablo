#!/bin/sh

#
# Change to suit your site
#
HOST=cyclone.bricbrac.de
LOGDIR=/var/log/news
BINDIR=/usr/local/bin
WWWDIR=/home/httpd/html
NUMDAYS=14
FIND=find
CD=cd
RM=rm
LN=ln

#
# Anything below here does NOT need changing!
#
DATE=${1:-`date +%y%m%d`}
LOGFILE=$LOGDIR/cyclone.log
WWWFILE=$WWWDIR/$DATE.html
WWWCURR=current.html

if [ ! -d $WWWDIR ]; then
	mkdir -p $WWWDIR
fi

date
echo "Running cyclone2syslog.pl ..."
cat $LOGDIR/stats.in $LOGDIR/stats.out | $BINDIR/cyclone2syslog.pl > $LOGFILE
echo "Running feeder-stats.pl ..."
$CD $WWWDIR
$BINDIR/cyclone-stats.pl -v -c -d $DATE -D $NUMDAYS -s $HOST -l $LOGFILE -o $WWWFILE -w $WWWDIR
$FIND $WWWDIR -type f -name "[0-9]*" -mtime +90 | xargs $RM -f 
$FIND $WWWDIR -type f -name "*.meta" -mtime +14 | xargs $RM -f 
$RM -f $WWWCURR
$LN -s $DATE.html $WWWCURR

date
echo ""

exit 1
