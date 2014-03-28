#!/bin/sh
#$Id: diablo-stats.sh,v 1.4 2002/04/10 21:54:26 rv Exp $
#
# Change to suit your site
#
HOST=newsfeed.yoursite.com
LOGDIR=/var/log/news
CFGDIR=/news
BINDIR=/usr/local/bin
WWWDIR=/home/httpd/html
NAME="Mr. News Admin"
MAIL="news@yoursite.com"
NUMDAYS=14
FIND=find
CD=cd
RM=rm
LN=ln
#
# Change and uncomment to add a 'personal look' to the generated HTML
# (and please do change the colors, the example is ugly by design ;-)
#
#export WWWURL=http://www.yoursite.com/
#export IMGTEXT="My Very Own Organi[sz]ation"
#export BGCOLOR='#AAAAAA'
#export TABLECOLOR='#C01015'

#
# Anything below here does NOT need changing!
#
DATE=${1:-`date +%Y%m%d`}
LOGFILE=$LOGDIR/news.notice
WWWFILE=$WWWDIR/$DATE.html
WWWCURR=current.html

if [ ! -d $WWWDIR ]; then
	mkdir -p $WWWDIR
fi

date
echo "Running feeder-stats.pl ..."
$CD $WWWDIR
$BINDIR/feeder-stats.pl -v -r -d $DATE -D $NUMDAYS -s "$HOST" \
   -C $CFGDIR -l $LOGFILE -o $WWWFILE -w $WWWDIR -a "$NAME" -A "$MAIL"
$FIND $WWWDIR -type f -name "[0-9]*" -mtime +90 | xargs $RM -f 
$RM -f $WWWCURR
$LN -s $DATE.html $WWWCURR

date
echo ""

exit 1
