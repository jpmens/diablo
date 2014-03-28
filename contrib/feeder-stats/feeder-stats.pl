#!/usr/bin/perl
#
# ABSOLUTELY NO WARRANTY WITH THIS PACKAGE. USE IT AT YOUR OWN RISK.
#
# Parse DIABLO/CYCLONE logfile and generate incoming & outgoing feed stats.
# URL: http://info.news.surf.net/src/feeder-stats-v4.tgz
# (NOTE: The above URL is for the time being)
#
# $Id: feeder-stats.pl,v 1.23 2007/08/09 19:43:18 jgreco Exp $
# feeder-stats.pl v4 Xander Jansen <x+diablo@surfnet.nl>
#  a modified and slightly fixed version of the feeder-stats.pl
#  as distributed with diablo 4.1-REL which was based on:
# feeder-stats.pl v3.103  2000/06/22  Brad Knowles <brad@shub-internet.org>
#  URL: ftp://ftp.shub-internet.org/pub/shub/brad/news/feeder-stats.pl
#
# Usage: feeder-stats.pl [options]
#     -h       help
#     -a name  newsadmin name (specify -A as well)
#     -A mail  newsadmin mail address (specify -a as well)
#     -c       Cyclone log file format (default: Diablo)
#     -C dir   directory containing configuration files (default: /news)
#     -d date  date to gather statistics in YYMMDD (default: 970703)
#     -D days  number of days of statistics to display (default: all)
#     -B date  begin time to start gathering statistics in YYMMDD
#     -E date  end time to stop gathering statistics in YYMMDD
#     -e       report articles/sec and kbytes/sec using elapsed wall time
#     -g       generate various graphs of traffic statistics
#     -H hour  create graphs ending at this hour
#     -l file  log file (default: /var/log/news/news.notice)
#     -m       use date/time in .png files rather than .png.meta
#     -o file  output file (default: stdout)
#     -s host  server hostname to use in report (default: newsfeed.foo.net)
#     -q       report queue by hostname instead of newsfeed name
#     -Q       report queue status for all feeds even those with no backlogs
#     -r       report (estimated) reject and total volumes in dir/index.html
#     -S       read logfile in pipe mode from STDIN
#     -t       output format as text (default: HTML)
#     -b       generate 'bare' HTML (no HEAD/BODY/HTML tags)
#     -v       verbose
#     -V       debug
#     -w dir   create dir/index.html page to list daily statistics
#     -W dir   create dir/index.html page to list daily statistics and exit
#
# Former maintainers:
# -------------------
#   Iain Lea                iain@bricbrac.de
#   Brad Knowles            brad@shub-internet.org 
#
# Acknowledgements:
# -----------------
#   Jeff Garzik             jeff.garzik@spinne.com
#   Terry Kennedy           terry@spcvxa.spc.edu
#   Steve Rawlinson         steve@clara.net
#   Pierre Belanger         belanger@risq.qc.ca
#   Miquel van Smoorenburg  miquels@cistron.nl
#   David Bonner            dbonner@bu.edu
#   Georg v.Zezschwitz      gvz@hamburg.pop.de
#   Christophe Wolfhugel    wolf@pasteur.fr
#   Andrew O. Smith         aos@insync.net
#   David Riley             driley@direct.ca
#   Bill Davidsen           davidsen@prodigy.com
#   Nate Shockey            underdog@flash.net
#   Jeroen Ruigrok          asmodai@wxs.nl
#   Nickolai Zeldovich      kolya@MIT.EDU
#   Russell Vincent         russellv@uk.uu.net
#   Josef "Bolo" Burger     bolo@cs.wisc.edu
#   Ronald Esveld           ronald@equant.nl
#   Francois Petillon       fantec@proxad.net
#
# TODO:
# -----
# - Add txt seperated output in easy parsable form for support server
#
# - change create for html & png's to be file.new and then 'mv file.new file'
# - add table of last connect messages for all outgoing feeds (ala algo)
#   Last connection to ecrc resulted in: StreamOK. (streaming)
#
# - add InArts/Sec & OutArts/Sec (ala algo)
# - add ftell(logfile) + store 1st line of logfile (save in .file) so that we
#   can jump to the current date in file and not have to parse all the old days
# - add header & footer boilerplate placeholders that are read in from file
# - FIX %3.2f formatting in ascii mode
#
# ChangeLog:
# ----------
# v?????  Pekka Kytölaakso
# - added diablo reader statistics
# - added variable $Subnet to control reader stats collection by
#	full addresses or by ipv4/24 and ipv6/48 subnets
# - changed code order so there is a toplevel if to select 
#   between testing diablo, newslink & readerd log-lines
# v4.006 Version as distributed with diablo 5.0-REL
# v4.006 2003/03/06 XJ
#  - changed: references to SPAM changed to '[Ss]pam' where appropriate.
#             See also URL: http://www.spam.com/ci/ci_in.htm for one
#             potential reason, another being that shouting is not polite
#             ;-)
# v4.006 2002/11/09 XJ
#  - added: (IPv6) reporting of host down error
# v4.006 2002/10/23 XJ
#  - added: reporting of average pending statistics (added to the Queue
#           Stats table)
# v4.006 2002/10/22 XJ
#  - added: reporting of additional errors that might be relevant for
#           trouble spotting
# v4.006 2002/10/13 XJ
#  - changed: make 'HostLink' comparison lower-case/case-independent
# v4.006 2002/08/02 XJ
#  - changed: out/in ratio now '...' when 'in' equals 0.
# v4.006 2002/07/27 XJ/JG/Jimmy
#  - added: -b option to generate 'bare' HTML code: no <HTML>/<BODY>/<HEAD>
#           tags. For postprocessing the stats.
#  - fixed: various </th>, </tr> and </td> tags added all over the place to
#           keep certain browsers happy if the HTML-stats are presented
#           within other webpages with tables and stuff. In the process
#           the HTML-layout (under the hood) has been significantly changed.
#           This might have impact for people who 'process' the resulting
#           HTML-files in some way.
# v4.005  Experimental version with two features that are better served with
#         header/footer files, not backported to 4.006.
# v4.004  Version as distributed with diablo 4.2-REL
# v4.004  2002/06/13 Francois Petillon
#  - added: alias: keyword to map hostnames to common aliases
#           Usage (in feeder-stats.conf):
#           alias: hostname alias-to-use
#           The alias-to-use will then be used in all tables and computations
#           for the specific hostname. Useful if you want to combine the
#           statistics for various hosts in a feeder-cluster or for feeds
#           with different in- and outgoing hostnames.
# v4.004-exp 2002/06/10 XJ
#  - fixed: of-by-100 error in Kbps-totals ($Total3 in the Volume tables)
#           Kbps total included percentage of article totals, noted
#           on a small (low volume) feeder. Fixed by removing
#           the 'my ()' function in the various Total3-initializations.
#  - changed: by fixing the Total3-bug the Kbps-total in the Rejected
#           Volume tables now is just for the Rejected Volume, not the sum
#           of actual and rejected Kbps. This seems to have been the
#           original intent (based on the code for handling the Kbps
#           totals when using WallTime (-e)).
#  - removed: spurious calls to non-existing &DumpTimeList (if $Debug)
#  - changed: Alt Freenix appears to be official now. URL back to
#           www.top1000.org and visual references to (Alt) Freenix
#           changed to Top1000. 
#  - added: Meta-header for character set (iso-8859-1 by default) to get a
#           clean pass at the W3C HTML-validator. Change $MetaCharset if
#           you use some other charset. ([C|Sh]ould this be UTF-8 or
#           US-ASCII ????). Note that this isn't needed if your server
#           adds a charset metaheader but hey, not all of them are so nice.
#  - added: reporting of DNS forward/reverse mismatches
#  - cosmetic: minor stuff
# v4.003  2002/04/18 kolya
#  - changed: include messages about spool directory creation failure
#  - changed: support Solaris 8
# v4.002  2002/04/08 XJ
#  - bugfix: removed two spurious TooOld handlers. The TooOld counters
#            in previous versions were adding left-overs from other feeds
#            and should be regarded as (at least) highly unreliable.
#            Noted when a feed had many TooOld's where only two
#            articles had been offered.
#  - changed: suppress spam table if spamcount is 0.
#  - added: check for BGCOLOR/TABLECOLOR/WWWURL/IMGTEXT environment
#           variables for a few local 'style' customisations.
#           See for usage in diablo-stats.sh.
#  - bugfix: &GetQueueHost choked on missing dnntpspool.ctl file
#            Use $QueueCmd -h in that case.
#  - cosmetic: changed 'unit' of Kb/art to KB/art (bits versus Bytes)
#  - changed: modified BatchFull/BatchMax==1 clause, now show red
#     lights when the number of batches is larger than 1 which is a sign
#     of a nobatch/realtime feed in trouble.
#  - cosmetic: removed spurious <br> in &PrintOutHeader/$Title2,
#    the HTML-parser in the client should do the wrapping there.
# v4.001  2002/03/23 XJ
#  - minor changes and bugfixes relative to 3.103 (Diablo version)
#    - localize LegendLabels to prevent re-use of wrong entries when
#      less than 7 labels.
#    - concatenation of ImgDir and logo.gif more flexible (relative URL).
#    - removed heigth/width specs for logo (thanks to Ronald Esveld).
#    - suppression of errors like 'pathheader contains TAB' etc.
#    - show variants of timeout/connection refused errors.
#    - combine alias mismatch errors (drop the 'in msgid' part before counting).
#    - df -h instead of df -k on platforms supporting 'human readable' df.
#    - GetVolume now also for TeraBytes.
#    - Top1000 now pointing to the Alternative Freenix.
#    - Set $BatchFull to 0 if $BatchMax==1 (to prevent red alerts for realtime
#      feeds when QueueShowAll == 1).
#    - changed ftp-URL for Chart to an existing site (thanks to Ronald Esveld
#      for noting this).
#    - added -C CfgDir option, usage '-C /path/to/dir/with/configfiles').
#    - updated diablo-stats.sh with -C and -r flags.
#    - updated diablo-stats.sh with a few double quotes so that variables
#      containing whitespace are properly shown.
#  - added rejected volume and total outgoing volume to main index, use
#      the -r option to activate.
#  - Version bumped up to 4.x, Copyright changed to The Diablo Project.
#    Updated 00README, ChangeLog and CREDITS.
#
# v3.103  2000/06/22
#  - Previous attempt to handle Diablo 2.x changes was not correct.
#    Now using correct code from Josef "Bolo" Burger.
# v3.102  2000/04/04
#  - Added code to handle Diablo 2.0 changes to the way data is logged
#    for precommit/postcommit cache hits, etc....
# v3.101  2000/03/06
#  - Display all feeds, even those that didn't accept any articles or
#    transmit any articles that were accepted.  This helps you keep an
#    eye on those feeds that are active and working, even if they're
#    not currently exchanging articles.
#  - If a host isn't in the Top1000, then try using just the hostname
#    to compare ratios of incoming versus outgoing articles & volume.
# v3.100  2000/03/01
#  - Don't show batch as full if $BatchMax = 1.  There's no sense seeing
#    lots of red if there's nothing you can do about it.
#  - Convert the script to use the PNG creation code instead of GIF.  This
#    requires the PNG enabled gd library, GD perl module and at least Chart
#    0.99.  The following have worked for me:
#      http://www.boutell.com/gd/http/gd-1.7.3.tar.gz
#      http://www.cpan.org/authors/id/LDS/GD-1.25.tar.gz
#      http://www.cpan.org/authors/id/N/NI/NINJAZ/Chart-0.99c-pre3.tar.gz
#    NOTE:  WE NO LONGER SUPPORT GIF FORMAT!!!
# v3.99 19990818
#  - Because of a problem at one of our peers, I was obliged to add a
#    "TooOld" category to the "Incoming Feeds" tables.  I'm not yet finished
#    with pulling and storing all those stats, so this won't yet be released to
#    the public.  However, what I've got so far does seem to work so far as
#    it goes.
# v3.98 19990816
#  - We're now much more aggressive about promoting a peer to the highest
#    rated path alias for the site (as determined from diablo.hosts,
#    dnewsfeeds, dnntpspool.ctl, and the Top1000 file).
#    This makes calculations of ratios a lot easier and more complete.
#    So, if you get fed by news-spur1.maxwell.syr.edu but you feed
#    news.maxwell.syr.edu, they'll both show up as #1 (or whatever)
#    and your ratios will be calculated appropriately.
# v3.97 19990809
#  - Added code to calculate input vs. output ratios of articles & volume
# v3.96 19990511
#  - Removed code to sleep.  It's a lot easier to just start at 59 minutes
#    after the hour, and let everything else take it's natural course.
# v3.95 19990308
#  - Added code to sleep 61 seconds after grabbing the current time.  Start
#    59 minutes after the hour instead of on the hour.  Prevent loss of stats
#    for 11:00 PM to 12:00 AM.
# v3.94 19990301
#  - Added option "-H <hour>" so that you could go back and fill in old charts
#    that might have somehow accidentally gotten missed
#  - Renamed previous %Acc field to be %Tot and created new %Acc field (by host)
# v3.93 19990226
#  - Added graphs for outgoing feeds corresponding to existing incoming graphs
#  - Fixed URL to http://validator.w3.org/images/vh40.gif
# v3.92
#  - fixed HTML code to be valid HTML 4.0
# v3.91
#  - added more www pages to feeder-stats.conf
#  - changed CmdPs to check for Linux and if so use 'ps ax' (was ps -ax)
# v3.90
#  - added row coloring for all tables
#  - added color coding for GB, MB and KB values
#  - added -Q flag to allow all feeds queue info to be shown (3.80 behaviour)
#    otherwise only feeds whose queues >= $QuePercent1 will be displayed
# v3.81
#  - fixed typo in titles
# v3.80
#  - added 'Newsfeed Contact: ' info. Use -a & -A flags to set to your site
#  - added -m flag to use date/time in images instead of .meta for servers
#    that don't obey CERN .meta files.
#  - changed title to put hostname in italics in and look up Freenix rating
#    of our host and include it if its in the top 1000
#  - fixed Y2K problems in date fields
# v3.71
#  - changed diablo-stats.sh & cyclone-stats.sh to use NUMDAYS=14 (was 30)
#  - fixed tarball which contained a old patch file
# v3.70
#   - added better parsing of top1000 file to find aliases for host
#   - changed GIF images to be 780x480
#   - fixed $Total3 variable in &PrintOutgoingFeeds() to print correctly
#   - fixed diablo-stats.sh script to only delete files that match '[0-9]*'
# v3.69
#   - changed $DATE variable in diablo-stats.sh to use ${1:-`date +%y%m%d`}
#   - fixed table headers to use &nbsp; for better formatting
# v3.68
#   - added basic support to parse Cyclone logfiles (thanx to David Riley)
#   - added filename.gif.meta files for correctly expiring cached GIF images
#   - added check for syslog error msg 'Config line...'
#   - added check for syslog error msg 'DoSession...'
#   - changed $MetaRefresh variable to 3600 (secs)
#   - fixed 1 off error in all GIF images (thanx to Terry Kennedy)
# v3.65
#   - added 'DIABLO uptime=66:38 ...' to system info at top of page
#   - added link to Freenix stats page
#   - changed index.html to not show incoming 'Errs' field
#   - fixed cosmetic problem with SPAM Top25 table headers
# v3.63
#   - changed $MetaRefresh variable to 4200 (secs)
#   - fixed $Month variable in GetMetaExpires() function
#   - fixed key length in MakeVolKey() function to handle large volumes
# v3.62
#   - changed table headers to 'bgcolor=lightblue' background
#   - changed all '<strong></strong>' tags to '<b></b>' tags to save space
# v3.60
#   - added <meta http-equiv="expires"...> tag to expire pages every 60 mins
#   - added <meta http-equiv="refresh"...> tag to refresh pages every 60 mins
#   - added -e cmdline option to use wall-time when calculating arts/sec etc.
#   - added system uptime, df | egrep news, Num. Diablo & Num. Dnewslink procs
#   - added URL: pointer to -h usage output
#   - added new logo icon for BricBrac Consulting
#   - changed IconGif to be $ImgDir/logo.gif (was ecrclogo.gif)
#   - changed border=0 on logo icon
#   - fixed possible divide by 0 error in GetAvgArtSize()
#   - fixed alt= tags to use comment
# v3.53
#   - error message fixups for diablo-1.14
# v3.51
#   - cosmetic nitpickings
# v3.49
#   - added Kb/art field to incoming & outgoing feeds tables
#   - fixed 1 off error in all GIF images (ouch!)
# v3.48
#   - added <table> around the GIF images
# v3.47
#   - fixed graphs to show last hour and not last but 1
#   - fixed top1000 code to case insensitive compares
# v3.46
#   - added check for syslog error msg concerning incorrect Path headers
#   - fixed spam table to use &nbsp; for better formatting of empty cells
# v3.44
#   - fixed Arts/sec & kbps by using feed->{InSecs}->{00} and test if for
#      each hour >3600 and if so reset to 3600
#   - fixed embedded values in incoming table if no diablo lines were parsed
# v3.42
#   - fixed Art/sec & kbps to use wall seconds instead of feed seconds
# v3.41
#   - fixed possible divide by zero error for $InSecs & $OutSecs
# v3.4
#   - added Art/sec & Kbps for incoming & outgoing feed & deleted hh:mm & Cons
#   - added GIF graphs of incoming feeds for articles, volume & time
#   - added checks for more syslog error msgs from diablo & dnewslink
#   - added feed position in Freenix Top1000 list for in & out feeds.
#   - changed curious table to list only time part of 1st & last dates
#   - fixed &GetVolume() to handle large numbers correctly
#   - fixed graphs to not frop to 0 after current hour (used undef values)
#   - fixed tables to use &nbsp; for better formatting of empty cells
# v3.2
#   - added by-hour table showing total arts & vol per hour
#   - added http links to feedname stats page for reverse feed checks:
#     link:  news.foo.com  http://news.foo.com/stats/
#     link:  feed.bar.com  http://feef.bar.com/diablo/
#     etc. etc. (default file is /news/diablo-stats.conf)
#   - added SPAM totals for incoming feeds to top level index.html file
#   - added logo to page headers (default is logo.gif) + link to www site
#   - added $BgColor to <body> tags for defining background color
#   - added rank field to incoming & outgoing tables
#   - added check for 'lost backchannel to master server' in logfile
#   - added 1stDate-LastDate to curious table instead of just 1stDate
#   - changed fields in incoming & outgoing tables for readability
#   - changed index.html to place Total+Volume as first fields
#   - changed the 'Total' field text to be centered
#   - fixed another BigFloat problem when no initialized value is used
#   - fixed BigFloat panic line 31 error by initializing $InBytes to 0
#   - fixed sorted order of >4GB feeds by using &MakeVolKey
# v3.0 970801
#   - added SPAM table
#   - added more checks for '400/500/502' type error messages in logfile

use Math::BigInt;
use Math::BigFloat;
use File::Basename;
require 'getopts.pl';
require "timelocal.pl";

BEGIN {
	eval "use Chart::Lines;";
	$::GraphMode = ($@ eq "");
}

$Version = 'v4.006';
#
$NewsContactName = "";
$NewsContactMail = "";
#Report reader counts by full address (0) or subnet (1)
$Subnet=1;
#
$LogFile = "/var/log/news/news.notice";
# $LogFile = "/news/lognotice";
#
$CmdUptime = "uptime";
$Uname = `uname -a`;
if ( system("df -h >/dev/null 2>&1") == 0 ) {
	$CmdDf = "df -h | egrep -i 'avail|news'";
} else {
	$CmdDf = "df -k | egrep -i 'avail|news'";
}
if ($Uname =~ /IRIX|SunOS.*5\./i) {
	$CmdPs = "ps -defa";
} elsif ($Uname =~ /Linux/i) {
	$CmdPs = "ps ax";
} else {
	$CmdPs = "ps -ax";
}
#
$CfgDir = '/news';
$CfgFile = "$CfgDir/feeder-stats.conf";
$ScriptName = 'feeder-stats';
$DiabloFormat = 1;
$MetaMode = 0;
$TextUrl = "";
$NameUrl = "";
#
# DIABLO
$DiabloUrl = "http://www.openusenet.org/";
$CtlFile = "$CfgDir/dnntpspool.ctl";
$QueueCmd = "/news/dbin/doutq";
$DiabloUptime = "";
$CmdDiablo = "$CmdPs | egrep -v grep | egrep diablo | wc -l";
$CmdDnewslink = "$CmdPs | egrep -v grep | egrep dnewslink | wc -l";
#
# CYCLONE
$CycloneUrl = "http://www.highwind.com/";
#
$Top1000Dir = "/news/top1000";
$ImgDir = "";
$ImgLogo = "$ImgDir" . "logo.gif";
$ImgInfo = "border=0";
#
$Top1000Url = "http://www.top1000.org/";
$FtpUrl = "http://info.news.surf.net/src/feeder-stats-v4.tgz" ;
if (defined $ENV{WWWURL}) {
	$WwwUrl = "$ENV{WWWURL}";
} else {
	$WwwUrl = "http://www.openusenet.org/";
}
if (defined $ENV{IMGTEXT}) {
	$ImgText = "$ENV{IMGTEXT}";
} else {
	$ImgText = "www.openusenet.org";
}
#
if (defined $ENV{BGCOLOR}) {
	$BgColor = "bgcolor=\"$ENV{BGCOLOR}\"";
} else {
	$BgColor = "bgcolor=\"#FFFFFF\"";
}
if (defined $ENV{TABLECOLOR}) {
	$TableColor = "bgcolor=\"$ENV{TABLECOLOR}\"";
} else {
	$TableColor = "bgcolor=\"#8580D0\"";
}
#
$OutFile = "";
$HtmlDir = "";
$Verbose = 0;
$Debug = 0;
$MaxSpamList = 25;
$MaxLinesPNG = 7;
$WidthPNG = 780;
$HeightPNG = 480;
#
$MetaRefresh = 3600;
$MetaExpires = &GetMetaExpires ($MetaRefresh / 60);
# print "EXPIRE=[$MetaExpires]\n";
$MetaCharset = "iso-8859-1";
#
$EmbeddedHTML = 0;
#
$WallTime = 0;
$QueueHost = 0;
$QueueShowAll = 0;
$RejTotReport = 0;
$TextFormat = 0;
$TotalSpamList = 0;
$TopSpamList = 0;
chop ($NewsHost = `hostname`);
# chop ($GenYear = `date +%y`);
chop ($GenYear = `date +%Y`);
chop ($GenMonth = `date +%m`);
chop ($GenDay = `date +%d`);
chop ($CurrHour = `date +%H`);
chop ($CurrMin = `date +%M`);
chop ($CurrSec = `date +%S`);
$GenDate = "$GenYear$GenMonth$GenDay";
$GenTime = "$CurrHour$CurrMin";
$ElapsedSecs = $CurrHour * 3600 + $CurrMin * 60 + $CurrSec;
$MaxDays = 0;
$CreateIndexAndExit = 0;
$BegDate = $EndDate = $GenDate;
$BegTime = $EndTime = $FileDate = "";
$GenTime = "$CurrHour$CurrMin";

$TDL = "<td align=\"left\">";
$TDR = "<td align=\"right\">";
$TDC = "<td align=\"center\">";
@DayName = ("Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday");
@DayNameShort = ("Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat");
%MonthNumByName = (
	'Jan', 1, 'Feb', 2, 'Mar', 3, 'Apr', 4,  'May', 5,  'Jun', 6,
	'Jul', 7, 'Aug', 8, 'Sep', 9, 'Oct', 10, 'Nov', 11, 'Dec', 12);
%MonthNameByNum = (
	'01', 'Jan', '02', 'Feb', '03', 'Mar', '04', 'Apr', '05', 'May', '06', 'Jun',
	'07', 'Jul', '08', 'Aug', '09', 'Sep', '10', 'Oct', '11', 'Nov', '12', 'Dec');
@XLabelHours = (
	'00', '01', '02', '03', '04', '05', '06', '07',
	'08', '09', '10', '11', '12', '13', '14', '15',
	'16', '17', '18', '19', '20', '21', '22', '23' );
# Colorize outgoing queue. Defaults are 20/25/50/65/80/95%
$QueColor1 = 'lightgreen';
$QueColor2 = 'green';
$QueColor3 = 'lightblue';
$QueColor4 = 'lightyellow';
$QueColor5 = 'orange';
$QueColor6 = 'red';
$QuePercent1 = 20;
$QuePercent2 = 35;
$QuePercent3 = 50;
$QuePercent4 = 65;
$QuePercent5 = 80;
$QuePercent6 = 95;

##############################################################################
#
#

if ($GraphMode == 0 && $TextFormat) {
	print <<EOT

Warning: Chart module not installed on your system. Graphs will not be
generated.

You can download the Perl5 Chart module from any good CPAN site.  Example:

ftp://ftp.nluug.nl/pub/languages/perl/CPAN/modules/by-module/Chart/

EOT
;
}

&ParseCmdLine ($0);
&ParseTop1000;
&ReadDiabloConf;

if ($CreateIndexAndExit) {
	&CreateHtmlIndex;
} else {
	&ReadCfgFile;

	if ($DiabloFormat) {
		&ParseDiabloLogFile;
		&ParseOutQueue;
	} else {
		&ParseCycloneLogFile;
	}

	&CalcHourlyStats;

	&GenerateOutputFile;

	&CreateHtmlIndex;

	if ($GraphMode) {
		$Title = "Top $MaxLinesPNG incoming feeds by articles per hour ($GenDate $GenTime)";
		&MakeIncomingByArticlePNG ("$HtmlDir/$FileDate-in-art.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Top $MaxLinesPNG incoming feeds by volume (KB) per hour ($GenDate $GenTime)";
		&MakeIncomingByVolumePNG ("$HtmlDir/$FileDate-in-vol.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Top $MaxLinesPNG incoming feeds by reject volume (KB) per hour ($GenDate $GenTime)";
		&MakeIncomingByRejectVolumePNG ("$HtmlDir/$FileDate-in-rejvol.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Incoming feeds by articles per hour ($GenDate $GenTime)";
		&MakeIncomingByTimePNG ("$HtmlDir/$FileDate-in-time.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Top $MaxLinesPNG outgoing feeds by articles per hour ($GenDate $GenTime)";
		&MakeOutgoingByArticlePNG ("$HtmlDir/$FileDate-out-art.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Top $MaxLinesPNG outgoing feeds by volume (KB) per hour ($GenDate $GenTime)";
		&MakeOutgoingByVolumePNG ("$HtmlDir/$FileDate-out-vol.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Top $MaxLinesPNG outgoing feeds by reject volume (KB) per hour ($GenDate $GenTime)";
		&MakeOutgoingByRejectVolumePNG ("$HtmlDir/$FileDate-out-rejvol.png", $Title, $WidthPNG, $HeightPNG);
		$Title = "Outgoing feeds by articles per hour ($GenDate $GenTime)";
		&MakeOutgoingByTimePNG ("$HtmlDir/$FileDate-out-time.png", $Title, $WidthPNG, $HeightPNG);

	}
}

exit 0;

##############################################################################
#
#

sub ParseCmdLine
{
	my ($ProgName) = @_;
	
	&Getopts('a:A:bB:cC:E:d:D:eghH:l:mo:s:qQrStvVw:W:');

	if ($opt_h) {
		print <<EOT
$ScriptName $Version  URL: $FtpUrl

Create statistics for a DIABLO/CYCLONE news server in Ascii/HTML format.
Copyright 2002 The Diablo Project (http://www.openusenet.org/diablo/)
  NOTE: Use at your own risk.

Usage: $ProgName [options]
       -h       help
       -a name  newsadmin name (specify -A as well)
       -A mail  newsadmin mail address (specify -a as well)
       -c       Cyclone log file format
       -C dir   directory containing configuration files (default: /news)
       -d date  date to gather statistics in YYMMDD (default: $GenDate)
       -D days  number of days of statistics to display (default: all)
       -B date  begin time to start gathering statistics in YYMMDD
       -E date  end time to stop gathering statistics in YYMMDD
       -e       report articles/sec and kbytes/sec using elapsed wall time
       -g       generate various graphs of traffic statistics
       -H hour  create graphs ending at this hour
       -l file  diablo log file (default: $LogFile)
       -m       use date/time in .png files rather than .png.meta
       -o file  output file (default: stdout)
       -s host  server hostname to use in report (default: $NewsHost)
       -q       report queue by hostname instead of newsfeed name
       -Q       report queue status for all feeds even those with no backlogs
       -r       report (estimated) reject and total volumes in dir/index.html
       -S       read logfile in pipe mode from STDIN
       -t       output format as text (default: HTML)
       -b       generate 'bare' HTML (no HEAD/BODY/HTML tags)
       -v       verbose
       -V       debug
       -w dir   create dir/index.html page to list daily statistics
       -W dir   create dir/index.html page to list daily statistics and exit

EOT
;
		exit 1;
	}
	$NewsContactName = $opt_a if (defined($opt_a));
	$NewsContactMail = $opt_A if (defined($opt_A));
	$BegDate = $opt_B if (defined($opt_B));
	$EndDate = $opt_E if (defined($opt_E));
	$BegDate = $EndDate = $GenDate = $opt_d if (defined($opt_d));
	$MaxDays = $opt_D if (defined($opt_D));
	$DiabloFormat = 0 if (defined($opt_c));
	$WallTime++ if (defined($opt_e));
	$GraphMode++ if (defined($opt_g));
	$GenHour = $opt_H if (defined($opt_H));
	$LogFile = $opt_l if (defined($opt_l));
	$MetaMode++ if (defined($opt_m));
	$OutFile = $opt_o if (defined($opt_o));
	$NewsHost = $opt_s if (defined($opt_s));
	$QueueHost++ if (defined($opt_q));
	$QueueShowAll++ if (defined($opt_Q));
	$TextFormat++ if (defined($opt_t));
	$EmbeddedHTML++ if (defined($opt_b));
	$RejTotReport++ if (defined($opt_r));
	$Verbose++ if (defined($opt_v));
	$Debug++ if (defined($opt_V));
	$HtmlDir = $opt_w if (defined($opt_w));
	if (defined($opt_C)) {
		$CfgDir = $opt_C ;
		$CfgFile = "$CfgDir/feeder-stats.conf";
		$CtlFile = "$CfgDir/dnntpspool.ctl";
	}

	if (defined($opt_c)) {
		$TextUrl = "CYCLONE";
		$NameUrl = $CycloneUrl;
	} else {
		$TextUrl = "DIABLO";
		$NameUrl = $DiabloUrl;
	}

	if (defined($opt_S)) {
		$LogFile = "-";
	}

	if (defined($opt_W)) {
		$HtmlDir = $opt_W;
		$CreateIndexAndExit++;
	} else {
		die "Error: $LogFile - $!\n" if ($LogFile eq '');
	}

	$GraphMode = 0 if $TextFormat;

	if ($MetaMode == 0) {
		$FileDate = $BegDate;
	} else {
		$FileDate = "$BegDate$GenTime";
	}
}


sub ParseCycloneLogFile
{
	my ($Error, $Line, $FullDate, $LineDate);

	printf STDERR "Parsing %s for $BegDate - $EndDate ...\n",
		 ($LogFile eq "-" ? "STDIN" : $LogFile) if $Verbose;

	open (FILE, $LogFile) || die "Error: $LogFile - $!\n";

	while ($Line = <FILE>)
	{
		next unless ($Line =~ /cycloned/o);

		if ($Line =~ /^(... .. ..:..:..)/o) {
			
			$FullDate = $1;
			$LineDate = &GetYyMmDd ($FullDate);
			if ($LineDate >= $BegDate && $LineDate <= $EndDate) {
				chop $Line;

				# ... cycloned[1]: in foo.org 879408495 0 0 0 0 0 0 0 Unknown 18.92 21.01
				if ($Line =~
				  /
				  (\S+)		# host
				  \s+
				  cycloned\[
				  (\d+)		# pid
				  \]:
				  \s+
				  in
				  \s+
				  (\S+)		# hostname
				  \s+
				  (\d+)		# time
				  \s+
				  (\d+)		# offered
				  \s+
				  (\d+)		# refused
				  \s+
				  (\d+)		# rejected
				  \s+
				  (\d+)		# accepted
				  \s+
				  (\d+)		# bytes
				  \s+
				  (\d+)		# connatt
				  \s+
				  (\d+)		# connsuc
				  \s+
				  .*		# other
				  /xo) {

#					$CycloneHost = $1;
#					$CyclonePid = $2;
					$InHost = $Aliases{lc $3} ? $Aliases{lc $3} : $3 ;
					$InTime = $4;
					$InOffered = $5;
					$InRefused = $6;
					$InRejected = $7;
					$InAccepted = $8;
					$InBytes = $9;
					$InConnAtt = $10;
					$InConnSuc = $11;

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					$Feeds{$InHost}->{InOffered} += $InOffered;
					$Feeds{$InHost}->{InRefused} += $InRefused;
					$Feeds{$InHost}->{InRejected} += $InRejected;
					$Feeds{$InHost}->{InAccepted} += $InAccepted;
					$Feeds{$InHost}->{InBytes} += $InBytes;
					$Feeds{$InHost}->{InConnAtt} += $InConnAtt;
					$Feeds{$InHost}->{InConnSuc} += $InConnSuc;

					&AddToInTimeList (
						$FullDate, $InHost, 1, $InAccepted, $InBytes, $InOffered,
						-1, -1, $InRejected, $InRefused, $InConnAtt, $InConnSuc, -1);

print "IN=[$FullDate, $InHost, $InOffered, $InRefused, $InRejected, $InAccepted, $InBytes, $InConnAtt, $InConnSuc]\n" if $Debug;

				} elsif ($Line =~
				  /
				  cycloned\[
				  (\d+)		# pid
				  \]:
				  \s+
				  out
				  \s+
                  (\S+)		# hostname
                  \s+
                  (\d+)		# time
                  \s+
                  (\d+)		# attempted
                  \s+
                  (\d+)		# offered
                  \s+
                  (\d+)		# refused
                  \s+
                  (\d+)		# rejected
                  \s+
                  (\d+)		# dropped
                  \s+
                  (\d+)		# expired
                  \s+
                  (\d+)		# accepted
                  \s+
                  (\d+)		# bytes
                  \s+
                  (\d+)		# backlog
                  \s+
                  (\d+)		# connatt
                  \s+
                  (\d+)		# connsuc
                  \s+
                  .*		# other
				  /xo) {


#					$CyclonedPid = $1;
					$OutHost = $Aliases{lc $2} ? $Aliases{lc $2} : $2 ;
					$OutTime = $3;
					$OutAttempted = $4;
					$OutOffered = $5;
					$OutRefused = $6;
					$OutRejected = $7;
					$OutDropped = $8;
					$OutExpired = $9;
					$OutAccepted = $10;
					$OutBytes = $11;
					$OutBacklog = $12;
					$OutConnAtt = $13;
					$OutConnSuc = $14;

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					$Feeds{$OutHost}->{OutCons} += 1;
					$Feeds{$OutHost}->{OutAttempted} += $OutAttempted;
					$Feeds{$OutHost}->{OutOffered} += $OutOffered;
					$Feeds{$OutHost}->{OutRefused} += $OutRefused;
					$Feeds{$OutHost}->{OutRejected} += $OutRejected;
					$Feeds{$OutHost}->{OutDropped} += $OutDropped;
					$Feeds{$OutHost}->{OutExpired} += $OutExpired;
					$Feeds{$OutHost}->{OutAccepted} += $OutAccepted;
					$Feeds{$OutHost}->{OutBytes} += $OutBytes;
					$Feeds{$OutHost}->{OutBacklog} += $OutBacklog;
					$Feeds{$OutHost}->{OutConnAtt} += $OutConnAtt;
					$Feeds{$OutHost}->{OutConnSuc} += $OutConnSuc;

					&AddToOutTimeList (
						$FullDate, $OutHost, 1, $OutAccepted, $OutBytes,
						$OutRefused, $OutRejected, $OutAttempted, $OutOffered,
						$OutDropped, $OutExpired, $OutBacklog, $OutConnAtt,
						$OutConnSuc, -1);

print "OUT=[$FullDate, $OutHost, $OutAttempted, $OutOffered, $OutRefused, $OutRejected, $OutDropped, $OutExpired, $OutAccepted, $OutBytes, $OutBacklog, $OutConnAtt, $OutConnSuc]\n" if $Debug;
				}
			}
		}
	}
	close (FILE);

	$Num = 0;
	foreach $Host (sort keys %Feeds) {
		# InAccepted
		$Key = sprintf "%012lu.%03d", $Feeds{$Host}->{InAccepted}, $Num;
		$OrderByInAccepted{$Key} = $Host;
#print "INacc  FEED=[$Host] KEY=[$Key]\n";
		# InBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{InBytes}, $Num);
		$OrderByInBytes{$Key} = $Host;
#print "INvol  FEED=[$Host] KEY=[$Key]\n";

		# OutAccepted
		$Key = sprintf "%012lu.%03d", $Feeds{$Host}->{OutAccepted}, $Num;
		$OrderByOutAccepted{$Key} = $Host;
#print "OUTacc FEED=[$Host] KEY=[$Key]\n";
		# OutBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{OutBytes}, $Num);
		$OrderByOutBytes{$Key} = $Host;
#print "OUTvol FEED=[$Host] KEY=[$Key]\n";

		$Num++;
	}

}

sub IPv6Cidr {
# get (1..8)  ':' separated parts of v6 address
#  return in full (no ::) format
	my $addr = shift; 
	my $parts = shift; # how many 16-bit hex parts to return
	my @addr;

	if ($addr =~ /^(.*)::(.*)$/) {
		my ($front, $back) = ($1, $2);
		my @front = split(/:/, $front);
		my @back  = split(/:/, $back);
		
		my $fill = 8 - scalar(@front) - scalar(@back);
		my @middle = (0) x $fill;
		@addr = (@front, @middle, @back);
	} else {
		@addr= split(/:/, $addr);
	}
	$parts--;
	return join(':',@addr[0..$parts]);
}

sub ParseDiabloLogFile
{
	my ($Error, $Line, $FullDate, $LineDate);

	printf STDERR "Parsing %s for $BegDate - $EndDate ...\n",
		 ($LogFile eq "-" ? "STDIN" : $LogFile) if $Verbose;

	open (FILE, $LogFile) || die "Error: $LogFile - $!\n";

	while ($Line = <FILE>)
	{

		next unless ($Line =~ /diablo|DIABLO|newslink|reader/o);

		if ($Line =~ /^(... .. ..:..:..)/o) {

			$FullDate = $1;
			$LineDate = &GetYyMmDd ($FullDate);

			next unless $LineDate >= $BegDate && $LineDate <= $EndDate;

			# Clean up after Solaris 8
			$Line =~ s/: \[ID \d+ \w+\.\w+\]/:/;

			chop $Line;


			if ($Line =~ /diablo|DIABLO/o) {


				# ... diablo[230]: news.foo.org stats acc=0 ctl=0 failsafe=0 misshdrs=0 tooold=0 grpfilt=0 spamfilt=0 earlyexp=0 instantexp=0 notinactv=0 ioerr=0
				#if ($Line =~ /(.*)\s+diablo\[(\d+)\]:\s+(.*)\s+stats acc=(\d+)\s+ctl=(\d+)\s+failsafe=(\d+)\s+misshdrs=(\d+)\s+tooold=(\d+)\s+.*\s+grpfilt=(\d+)\s+spamfilt=(\d+)\s+earlyexp=(\d+)\s+instantexp=(\d+)\s+notinactv=(\d+)\s+ioerr=(\d+)/o)
				if ($Line =~ /(.*)\s+diablo\[(\d+)\]:\s+(\S+).* tooold=(\d+)/o)
				{
#					$DiabloHost = $1;
#					$DiabloPid = $2;
					$InHost = $Aliases{lc $3} ? $Aliases{lc $3} : $3 ;
					$TooOld = $4;
					# $Acc = $4;
					# $Ctl = $5;
					# $FailSafe = $6;
					# $MissHdr = $7;
					# $TooOld = $8;
					# $GrpFilt = $9;
					# $SpamFilt = $10;
					# $EarlyExp = $11;
					# $InstantExp = $12;
					# $NotInActiv = $13;
					# $IOErr = $14;

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					$Feeds{$InHost}->{TooOld} += $TooOld;

					# $Feeds{$InHost}->{InAccepted} += $Acc;
					# $Feeds{$InHost}->{Control} += $Ctl;
					# $Feeds{$InHost}->{FailSafe} += $FailSafe;
					# $Feeds{$InHost}->{MissHdr} += $MissHdr;
					# $Feeds{$InHost}->{GrpFilt} += $GrpFilt;
					# $Feeds{$InHost}->{SpamFilt} += $SpamFilt;
					# $Feeds{$InHost}->{EarlyExp} += $EarlyExp;
					# $Feeds{$InHost}->{InstantExp} += $InstantExp;
					# $Feeds{$InHost}->{NotInActiv} += $NotInActiv;
					# $Feeds{$InHost}->{IOErr} += $IOErr;

					# printf STDERR "IN  [%s] %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
						# $Acc, $Ctl, $FailSafe, $MissHdr, $TooOld, $GrpFilt,
						# $SpamFilt, $EarlyExp, $InstantExp, $NotInActiv, $IOErr; # if $Debug

					printf STDERR "IN  [%s] %4d\n",
						$InHost, $TooOld if $Debug;

				}
				# ... diablo[230]: news.foo.org secs=15 ihave=0 chk=19 rec=3 rej=0 predup=0 posdup=0 pcoll=101 spam=0 err=0 added=3 bytes=2137 (1/sec)
				elsif ($Line =~ /(.*)\s+diablo\[(\d+)\]:\s+(\S+)\s+secs=(\d+)\s+ihave=(\d+)\s+chk=(\d+)\s+rec=(\d+)\s+rej=(\d+)\s+.*\s+err=(\d+)\s+added=(\d+)/o) {
#					$DiabloHost = $1;
#					$DiabloPid = $2;
					$InHost = $Aliases{lc $3} ? $Aliases{lc $3} : $3 ;
					$InSecs = $4;
					$InIhave = $5;
					$InChk = $6;
					$InRec = $7;
					$InRej = $8;
					$InErr = $9;
					$InAdded = $10;

					# Diablo >= 1.10  ... predup=25 posdup=0 pcoll=10 spam=0 ... bytes=18908
					if ($Line =~ /predup=(\d+)\s+posdup=(\d+)\s+pcoll=(\d+)\s+spam=(\d+).*\s+bytes=(\d+)/o) {
						$InPredup = $1;
						$InPosdup = $2;
						$InPcoll = $3;
						$InSpam = $4;
						$InBytes = $5;

					}
					# Diablo >= 1.99 ... precom=0 postcom=0
					if ($Line =~ /predup=(\d+)\s+posdup=(\d+)\s+precom=(\d+)\s+postcom=(\d+)\s+his=(\d+)\s+spam=(\d+).*\s+bytes=(\d+)/o) {
						$InPredup = $1;
						$InPosdup = $2;
						$InPcoll = $3 + $4;
						$InSpam = $6;
						$InBytes = $7;
					}

					$InRejBytes=0;
					if ($InAdded > 0) {
						$InRejBytes = $InRej * $InBytes / $InAdded;
						$InRejBytes = int ($InRejBytes + 0.5);
					}

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					$Feeds{$InHost}->{InIhave} += $InIhave;
					$Feeds{$InHost}->{InChk} += $InChk;
					$Feeds{$InHost}->{InRec} += $InRec;
					$Feeds{$InHost}->{InRej} += $InRej;
					$Feeds{$InHost}->{InPredup} += $InPredup;
					$Feeds{$InHost}->{InPosdup} += $InPosdup;
					$Feeds{$InHost}->{InPcoll} += $InPcoll;
					$Feeds{$InHost}->{InSpam} += $InSpam;
					$Feeds{$InHost}->{InErr} += $InErr;
					$Feeds{$InHost}->{InAdded} += $InAdded;
					$Feeds{$InHost}->{InBytes} += $InBytes;
					$Feeds{$InHost}->{InRejBytes} += $InRejBytes;

					&AddToInTimeList (
						$FullDate, $InHost, $InSecs, $InAdded, $InBytes,
						$InChk, $InIhave, $InSpam, $InRej, $InErr, -1, -1, $InRejBytes);

					printf STDERR "IN  [%s] Se=%4d H=%4d C=%4d Rc=%4d Rj=%4d Sp=%4d E=%4d A=%4d B=%4d RB=%4d\n",
						$InHost, $InSecs, $InIhave, $InChk, $InRec, $InRej,
						$InSpam, $InErr, $InAdded, $InBytes, $InRejBytes if $Debug;

				}
				# diablo[57520]: news.foo.org  secs=62 ihave=0 chk=503 takethis=496 rec=496 acc=355 ref=0 precom=0 postcom=0 his=0 badmsgid=0 rej=141 ctl=8 spam=0 err=0 recbytes=789201 accbytes=537433 rejbytes=251768 (8/sec)
                                elsif ($Line =~ /(.*)\s+diablo\[(\d+)\]:\s+(\S+)\s+secs=(\d+)\s+ihave=(\d+)\s+chk=(\d+)\s+.*\s+rec=(\d+)\s+acc=(\d+)\s+.*\s+rej=(\d+)\s+.*\s+spam=(\d+)\s+err=(\d+)\s+.*\s+accbytes=(\d+)\s+rejbytes=(\d+)/o) {
#					$DiabloHost = $1;
#					$DiabloPid = $2;
					$InHost = $Aliases{lc $3} ? $Aliases{lc $3} : $3 ;
					$InSecs = $4;
					$InIhave = $5;
					$InChk = $6;
					$InRec = $7;
					$InAdded = $8;
					$InRej = $9;
					$InSpam = $10;
					$InErr = $11;
					$InBytes = $12;
					$InRejBytes=$13;

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					$Feeds{$InHost}->{InIhave} += $InIhave;
					$Feeds{$InHost}->{InChk} += $InChk;
					$Feeds{$InHost}->{InRec} += $InRec;
					$Feeds{$InHost}->{InRej} += $InRej;
					$Feeds{$InHost}->{InSpam} += $InSpam;
					$Feeds{$InHost}->{InErr} += $InErr;
					$Feeds{$InHost}->{InAdded} += $InAdded;
					$Feeds{$InHost}->{InBytes} += $InBytes;
					$Feeds{$InHost}->{InRejBytes} += $InRejBytes;

					&AddToInTimeList (
						$FullDate, $InHost, $InSecs, $InAdded, $InBytes,
						$InChk, $InIhave, $InSpam, $InRej, $InErr, -1, -1, $InRejBytes);

					printf STDERR "IN  [%s] Se=%4d H=%4d C=%4d Rc=%4d Rj=%4d Sp=%4d E=%4d A=%4d B=%4d RB=%4d\n",
						$InHost, $InSecs, $InIhave, $InChk, $InRec, $InRej,
						$InSpam, $InErr, $InAdded, $InBytes, $InRejBytes if $Debug;

				} elsif ($Line =~ /diablo.*SpamFilter\/(.*)/o) {

					&AddToSpamList ($1, $FullDate, $LineDate);

				# ... diablo[423]: Connection 8 from 194.59.191.4 (no permission)
				} elsif ($Line =~ /diablo.*Connection.*from\s+(.*)\s+\(no permission\)/o) {

					$Error = "$1 (no permission)";
					&AddToErrorList ($Error, $FullDate, $LineDate);

				# ... diablo[423]: Connect Limit exceeded for 194.108.168.3
				# ... diablo[423]: Connect Limit exceeded (from -M/diablo.config) for 205.252.116.205 (8)
				} elsif ($Line =~ /diablo.*Connect Limit exceeded .* for\s+(.*)/o) {

					$Error = "Connect Limit exceeded for $1";
					&AddToErrorList ($Error, $FullDate, $LineDate);

				# ... diablo[423]: diablo.hosts entry for localhost missing label
				} elsif ($Line =~ /diablo.*diablo.hosts entry for (.*) missing label/o) {

					$Error = "diablo.hosts entry for $1 missing label";
					&AddToErrorList ($Error, $FullDate, $LineDate);

				# ... diablo[423]: dhistory file corrupted on lookup
				# ... diablo[423]: dhistory file realigned by 4 @20439040
				} elsif ($Line =~ /diablo.*(dhistory file.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: lost backchannel to master server
				} elsif ($Line =~ /diablo.*(lost backchannel to master server)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: NumForks [exceeded|ok, reaccepting]
				} elsif ($Line =~ /diablo.*(NumForks.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: Maximum file descriptors exceeded
				} elsif ($Line =~ /diablo.*(Maximum file descriptors exceeded.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: fork failed:
				} elsif ($Line =~ /diablo.*(fork failed:.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: pipe() failed:
				} elsif ($Line =~ /diablo.*(pipe\(\) failed:.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: failure writing to feed
				} elsif ($Line =~ /diablo.*(failure writing to feed.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: .*diablo.hosts file not found
				} elsif ($Line =~ /diablo.*(\/.*diablo.hosts file not found.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

#				# ... diablo[423]: message-id mismatch, command:
#				} elsif ($Line =~ /diablo.*(message-id mismatch, command:.*)/o) {
#
#					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[423]: fdopen() of socket failed
				} elsif ($Line =~ /diablo.*(fdopen\(\) of socket failed.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[13002]: DNS Fwd/Rev mismatch: lookup of h66-244-204-72.bigpipeinc.com failed
				} elsif ($Line =~ /diablo.*\]: (.*DNS Fwd\/Rev mismatch:.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);
				# ... diablo[134]: news.nettuno.it Path element fails to match aliases: news.nettuno.it
				} elsif ($Line =~ /diablo.*\]: (.*Path element fails to match aliases:\s+\S+)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[1234]: nntp1.njy.teleglobe.net Path header contains tab: 
				# ... diablo[1234]: nntp1.njy.teleglobe.net Newsgroup header contains tab: } elsif ($Process eq "diablo" && $Line =~ /header contains tab:/o) {
# ignore

				# ... diablo[134]: Diablo misconfiguration, label news.maxwell.syr.edu not found in dnewsfeeds
				} elsif ($Line =~ /diablo.*\]:  Diablo misconfiguration, (.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[134]: ... Config line ...
				} elsif ($Line =~ /diablo.*\]:\s+(Config line.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[134]: ... DoSession ...
				} elsif ($Line =~ /diablo.*\]:\s+(DoSession.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... diablo[2313]: /news/dspool.ctl: Unable to create article dir ...
				} elsif ($Line =~ /diablo.*\]:\s+\S+:\s+(Unable to create.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# 
				} elsif ($Line =~ /diablo.*\]:\s+\S+\s+spoolstats\s.*/o) {
					# ignore
				} elsif ($Line =~ /diablo.*\]:\s+\S+\s+rejstats\s.*/o) {
					# ignore

				# ... diablo[134]: DIABLO uptime=3:44 arts=83.000K tested=0 bytes=895.835M fed=4.074M
				} elsif ($Line =~ /(DIABLO uptime=.*)/o) {

					$DiabloUptime = $1;
				}
			} elsif ($Line =~ /newslink/o) {
				# ... newslink[7390]: article batch corrupted:

				if ($Line =~ /newslink\[(\d+)\]:\s+(\S+):(.*)\s+final\s+secs=(\d+)\s+acc=(\d+)\s+dup=(\d+)\s+rej=(\d+)\s+tot=(\d+)/o) {

#					$NewslinkPid = $1;
					$OutHost = $Aliases{lc $2} ? $Aliases{lc $2} : $2 ;
					$OutBatch = $3;
					($OutLabel, $OutBatchPath, $OutBatchNum) = fileparse($OutBatch,'\..*?') ;
					$OutSecs = $4;
					$OutAccepted = $5;
					$OutDup = $6;
					$OutRej = $7;

					$BegTime = $LineDate if !$BegTime;
					$EndTime = $LineDate;

					if ($Line =~ /\s+bytes=(\d+)(.*)/o) {
						$OutBytes = $1;
					}
					$OutRejBytes = 0;
					if ($OutAccepted > 0) {
						$OutRejBytes = $OutRej * $OutBytes / $OutAccepted;
						$OutRejBytes = int ($OutRejBytes + 0.5);
					}

					$Feeds{$OutHost}->{OutCons} += 1;
					$Feeds{$OutLabel}->{OutCons} += 1;
					$Feeds{$OutHost}->{OutAccepted} += $OutAccepted;
					$Feeds{$OutHost}->{OutDup} += $OutDup;
					$Feeds{$OutHost}->{OutRej} += $OutRej;
					$Feeds{$OutHost}->{OutBytes} += $OutBytes;
					$Feeds{$OutHost}->{OutRejBytes} += $OutRejBytes;

					if ($Line =~ /\s+avpend=(\d+\.\d+)\s+.*/o) {
						$OutAvPend = $1;
						$Feeds{$OutLabel}->{AvPend} = ($OutAvPend + (($Feeds{$OutLabel}->{OutCons} - 1 ) * $Feeds{$OutLabel}->{AvPend})) / $Feeds{$OutLabel}->{OutCons};
					}

					&AddToOutTimeList (
						$FullDate, $OutHost, $OutSecs, $OutAccepted, $OutBytes,
						$OutDup, $OutRej, -1, -1, -1, -1, -1, -1, -1, $OutRejBytes);

					printf STDERR "OUT [%s] [%s] Se=%4d A=%4d D=%4d Rj=%4d B=%4d RB=%4d AV=%s\n",
						$OutHost, $OutLabel, $OutSecs, $OutAccepted, $OutDup, $OutRej, $OutBytes, $OutRejBytes, $Feeds{$OutLabel}->{AvPend} if $Debug;

				# ... newslink[7390]: news.crg.net:/news/dqueue/news.crg.net.S03454 connect: 500 Syntax error or bad command (nostreaming) ...
				} elsif ($Line =~ /newslink\[(\d+)\]:\s+(\S+):(.*)\s+connect:\s+(.*)/o) {

#					$NewslinkPid = $1;
					$OutHost = $2 ;
					$OutBatch = $3;
					$OutMsg = $4;

					if ($OutMsg =~ /((400|500|502)\s+.*)/ ||
						$OutMsg =~ /(Host is down.*)/ ||
						$OutMsg =~ /(Operation timed out.*)/ ||
						$OutMsg =~ /(Connection timed out.*)/ ||
						$OutMsg =~ /(Connection refused.*)/ ||
						$OutMsg =~ /(.*Unexpected EOF.*)/ ||
						$OutMsg =~ /(\(unknown error\).*)/) {
						$OutHost =~ s/\s//g;
						$Error = "$OutHost: $1";
						&AddToErrorList ($Error, $FullDate, $LineDate);
					}
				# ... newslink[84105]: news-in.maxwell.syr.edu:/news/dqueue/maxwell-bin-1 Remote EOF, attempting to reconnect
				} elsif ($Line =~ /newslink\[(\d+)\]:\s+(\S+):(.*)\s+(Remote EOF.*)/o) {
#					$NewslinkPid = $1;
					$OutHost = $2 ;
					$OutBatch = $3;
					$OutMsg = $4;
					$Error = "$OutHost: $OutMsg";
					&AddToErrorList ($Error, $FullDate, $LineDate);

				# ... diablo[423]: SpamFilter/by-post-rate copy #33: <5rplh4$12p@ron.ipa.com>  206.1.57.29
				# ... diablo[423]: SpamFilter/by-dup-body copy #1: <5rpvin$8ro$1@demdwu11.telemedia.de> essn-m103-83.pool.mediaways.net
				# ... diablo[423]: SpamFilter/dnewsfeeds copy #-1: <5r4dpg$le8$1@news.utu.fi> apus.astro.utu.fi

				} elsif ($Line =~ /newslink.*(article batch corrupted:.*)/o) {

					&AddToErrorList ($1, $FullDate, $LineDate);

				# ... newslink[26871]: news.example.com:example.com.S94013 hostname lookup failure: Error 0
				} elsif ($Line =~ /newslink\[\d+\]:\s+(.*):.*\s+(hostname lookup failure:.*)/o) {
					$OutMsg = "$1: $2";
					&AddToErrorList ($OutMsg, $FullDate, $LineDate);


				}
			} elsif ($Line =~ /reader/o) {
#Jun 29 10:18:27 mortti dreaderd[4928]: closed from tellus.csc.fi (193.166.1.7:38165) groups=102 arts=1 bytes=40256 posts=0 postbytes=0 postfail=0 secs=64209
# .. dreaderd[27393]: closed from 2001:708:10:8:230:48ff:fe28:a9ee ([2001:708:10:8:230:48ff:fe28:a9ee]:59923) groups=...
				next if $Line =~ /: (counters .* tcount=|connect to )/o;
				if  ($Line =~ /: closed from \S+ \(([][a-zA-Z0-9.:]*)\) groups=(\d+) arts=(\d+) bytes=(\d+) posts=(\d+) postbytes=(\d+) postfail=(\d+) secs=(\d+)/o) {
					$this=$1;
					$groups=$2;$arts=$3; $bytes=$4;$posts=$5;

					if ($this =~ /\[([a-zA-Z0-9.:]+)\]:\d+/) { #IPv6
						$this=&IPv6Cidr($1,$Subnet ? 3 : 8);
					} elsif  ($this =~ /(\d+\.\d+\.\d+\.\d+):\d+/) { #IPv4
						if ($Subnet) { 
							$this=join(".",(split(/\./,$1))[0..2]);
						} else {	
							$this=$1;
						}
					} 
					$readerconnect{$this}++; $readerconnect++;
					$readergroups{$this}+=$groups;
					$readerarts{$this}+=$arts;
					$readerbytes{$this}+=$bytes;
					$readerposts{$this}+=$posts;

#dreaderd[27393]: connection to DEFAULT from 130.233.204.66 (130.233.204.66:51198) rejected: Unauthorized
				} elsif ($Line =~ /: (connection to \S+ from \S+ \([][a-zA-Z0-9.:]*\).*)/) {
					$OutMsg=$1;
					$OutMsg =~s/( \(.*):\d+\) /$1) /;
					&AddToErrorList ("reader " . $OutMsg, $FullDate, $LineDate);

				} else { #pending descriptor from 128.214.133.2:1902 on dead DNS pid
					next if $Line =~/info .*|pending descriptor from/;
					next if $Line =~/ Installing access cache| post ok /;
					next if $Line =~/connect\([^\)]*\) /;
					if ($Line =~/ corrupt overview entry for/) {
						$Line="corrupt overview entry";	
					} elsif ($Line =~/Unable to find any spools \(or spool threads too busy\) for request/) {
						$Line="Unable to find any spools (or spool thr
eads too busy)";	
					} else {
						$Line =~ s/^.*reader[^:]+://;
					}
					
					&AddToErrorList ("reader " . $Line, $FullDate, $LineDate);
				}
			}
		}
	}
	close (FILE);

	$Num = 0;
	foreach $Host (sort keys %Feeds) {
		# InAdded
		$Key = sprintf "%012lu.%03d", $Feeds{$Host}->{InAdded}, $Num;
		$OrderByInAdded{$Key} = $Host;

		# InBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{InBytes}, $Num);
		$OrderByInBytes{$Key} = $Host;

		# InRejBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{InRejBytes}, $Num);
		$OrderByInRejBytes{$Key} = $Host;

		# OutAccepted
		$Key = sprintf "%012lu.%03d", $Feeds{$Host}->{OutAccepted}, $Num;
		$OrderByOutAccepted{$Key} = $Host;

		# OutBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{OutBytes}, $Num);
		$OrderByOutBytes{$Key} = $Host;

		# OutRejBytes
		$Key = &MakeVolKey ($Feeds{$Host}->{OutRejBytes}, $Num);
		$OrderByOutRejBytes{$Key} = $Host;

		$Num++;
	}

	$Num = 0;
	foreach $Msg (sort keys %ErrorList) {
		$Key = sprintf "%08lu.%03d%s", $ErrorList{$Msg}->{Tot}, $Num, $Msg;
		$OrderByErrorTotal{$Key} = $Msg;

		$Num++;
	}

	$Num = 0;
	foreach $Host (sort keys %SpamList) {
		# SpamTotal
		$Key = sprintf "%08lu.%03d", $SpamList{$Host}->{Total}, $Num;
		$OrderBySpamTotal{$Key} = $Host;

		$Num++;
	}
	$TotalSpamList = $Num;
	$TopSpamList = $MaxSpamList;
	if ($TopSpamList > $TotalSpamList) {
		$TopSpamList = $TotalSpamList
	}

}


sub ParseOutQueue
{
	my ($Key, $Host, $Num);

	print STDERR "Parsing output of $QueueCmd ...\n" if $Verbose;

	open (CMD, "$QueueCmd |") || die "Error: $QueueCmd - $!\n";

	while (<CMD>)
	{
#		print if $Debug;

		chop;

		# news.foo.de  2699-2700  (  1/200 files   0% full)	02699
		if (/^(.*)\s+(\d+)-(\d+)\s+\((.*)\/(.*)\s+files\s+(\d+)%\s+full\)/o) {

			$Host = $1;
			$BatchBeg = $2;
			$BatchEnd = $3;
			$BatchNum = $4;
			$BatchMax = $5;
			$BatchFull = $6;
			$BatchFull = 0 if (($BatchMax <= 1) && ($BatchNum <= 1));
			$Host =~ s/\s//g;

			if ($QueueShowAll || (($BatchMax > 1) && ($BatchFull > $QuePercent1))) {
				$Feeds{$Host}->{BatchBeg} += $BatchBeg;
				$Feeds{$Host}->{BatchEnd} += $BatchEnd;
				$Feeds{$Host}->{BatchNum} += $BatchNum;
				$Feeds{$Host}->{BatchMax} += $BatchMax;
				$Feeds{$Host}->{BatchFull} += $BatchFull;
			}
			printf STDERR "BATCH  %-30s %4d %4d %4d %4d %4d\n", $Host,
				$Feeds{$Host}->{BatchBeg}, $Feeds{$Host}->{BatchEnd},
				$Feeds{$Host}->{BatchNum}, $Feeds{$Host}->{BatchMax},
				$Feeds{$Host}->{BatchFull} if $Debug;
		}
	}
	close CMD;
	$Num = 0;
	foreach $Host (sort keys %Feeds)
	{
		$Key = sprintf "%10d.%3d", $Feeds{$Host}->{BatchFull}, $Num;
		$OrderByBatchFull{$Key} = $Host;
		$Num++;
	}
}


sub GenerateOutputFile
{
	my $ArtAdded = $VolAdded = 0;

#	print STDERR "OUT=[$OutFile]\n";

	if ($OutFile) {
		open (FILE, "> $OutFile") || warn "Warning: $OutFile - $!\n";
	} else {
		open (FILE, "> STDOUT") || warn "Warning: STDOUT - $!\n";
	}

	&PrintOutHeader;

	print FILE "<p>\n<a name=\"01\"></a>\n<hr>\n<p>\n" if !$TextFormat;
	$ArtAdded = &PrintIncomingFeeds ("1. Summary of Incoming Feeds by Article", 0);

	&PrintSeparator ("02") if !$TextFormat;
	$VolAdded = PrintIncomingFeeds ("2. Summary of Incoming Feeds by Volume", 1);

	&PrintSeparator ("03") if !$TextFormat;
	$RejVolAdded = PrintIncomingFeeds ("3. Summary of Incoming Feeds by Estimated Rejected Volume", 2);

	&PrintSeparator ("04") if !$TextFormat;
	&PrintIncomingTimes ("4. Summary of Incoming Feeds by Time");

	&PrintSeparator ("05") if !$TextFormat;
	&PrintOutgoingFeeds ("5. Summary of Outgoing Feeds by Article", 0, $ArtAdded);

	&PrintSeparator ("06") if !$TextFormat;
	&PrintOutgoingFeeds ("6. Summary of Outgoing Feeds by Volume", 1, $VolAdded);

	&PrintSeparator ("07") if !$TextFormat;
	&PrintOutgoingFeeds ("7. Summary of Outgoing Feeds by Estimated Rejected Volume", 2, $VolAdded);

	&PrintSeparator ("08") if !$TextFormat;
	&PrintOutgoingTimes ("8. Summary of Outgoing Feeds by Time");

	if ($DiabloFormat) {

		&PrintSeparator ("09") if !$TextFormat;
		&PrintQueueStats ("9. Summary of Outgoing Queue");

		&PrintSeparator ("10") if !$TextFormat;
		&PrintCuriousActivity ("10. Summary of Curious Activity");

		unless ( $readerconnect == 0 ) {
			&PrintSeparator ("11") if !$TextFormat;
			&PrintReaderStats ("11. Summary of Reader Activity");
		}
		unless ( $TotalSpamList == 0 ) {
			&PrintSeparator ("12") if !$TextFormat;
			&PrintSpamActivity ("12. Summary of Spam Activity (Top $TopSpamList of $TotalSpamList)") ;
		}
	}

	&PrintSeparator ("end") if !$TextFormat;

	&PrintOutFooter;

	close FILE;
}


sub CreateHtmlIndex
{
	my ($IndexFile) = "$HtmlDir/index.html";
	my ($InVolume, $InRejVolume, $InTotVolume, $OutVolume, $OutRejVolume, $OutTotVolume);
	my (@DailyStats) = "";
	my ($NumDays) = 0;
	my ($Title1, $Title2);
	my ($Rank) = &GetTop1000 ($NewsHost);
	my ($RowColor);
	my ($InColSpan, $InColLabelAdd, $OutColHead, $OutColLabelAdd) ;

	if ($Rank eq "") {
		$Title1 = "$TextUrl statistics for $NewsHost";
		$Title2 = "<a href=\"$NameUrl\">$TextUrl</a>  statistics for <i>$NewsHost</i>";
	} else {
		$Title1 = "$TextUrl statistics for $NewsHost (Top1000 $Rank)";
		$Title2 = "<a href=\"$NameUrl\">$TextUrl</a>  statistics for <i>$NewsHost</i> (Top1000 $Rank)";
	}

	print STDERR "INDEX=[$IndexFile]\n" if $Debug;
	print STDERR "TITLE=[$Title]\n" if $Debug;

	open (DIR, "cd $HtmlDir; find . -name \"[0-9]*.html\" -print | sort -r |") ||
		die "Error: $HtmlDir - $!\n";

	open (FILE, ">$IndexFile") || die "Error: $IndexFile - $!\n";


	unless ( $EmbeddedHTML ) {
		print FILE <<EOT
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN" "http://www.w3.org/TR/REC-html40/loose.dtd">
<html>
<head>
<title>$Title1</title>
<meta http-equiv="Refresh" content="$MetaRefresh">
<meta http-equiv="Expires" content="$MetaExpires">
<meta http-equiv="Content-Type" content="text/html; charset=$MetaCharset"> 
</head>
<body $BgColor>
EOT
	};
	print FILE <<EOT
<table border=0>
<tr>
<td><a href="$WwwUrl"> <img $ImgInfo alt="$ImgText" src="$ImgLogo"></a></td>
<td><h3>$Title2</h3></td>
</tr>
</table>
EOT
;
	if ($NewsContactName) {
		print FILE "Newsfeed Contact: <a href=\"mailto:$NewsContactMail\">$NewsContactName</a>\n<p>\n";
	}
	&PrintOutCopyright;
	if ($RejTotReport) {
		$InColSpan = 6 ;
		# Start with an </th> tag, don't end with one !
		$InColLabelAdd = "</th>\n<th $TableColor>Rej. Vol.</th>\n<th $TableColor>Tot. Vol." ;
		$OutColHead = "</th><th $TableColor colspan=2>Outgoing Estimates" ;
		$OutColLabelAdd = "</th>\n<th $TableColor>Rej. Vol.</th>\n<th $TableColor>Tot. Vol." ;
	} else {
		$InColSpan = 4 ;
		$InColLabelAdd = "" ;
		$OutColHead = "" ;
		$OutColLabelAdd = "" ;
	}
	print FILE <<EOT
<hr>
<p>
<table border=2>
<tr>
<th $TableColor rowspan=2 colspan=2>Daily Statistics</th>
<th $TableColor colspan=$InColSpan>Incoming Feeds</th>
<th $TableColor colspan=4>Outgoing Feeds$OutColHead</th>
<tr>
<th $TableColor>Accepted</th>
<th $TableColor>Volume</th>
<th $TableColor>Spam</th>
<th $TableColor>Rejs$InColLabelAdd</th>
<th $TableColor>Accepted</th>
<th $TableColor>Volume</th>
<th $TableColor>Dups</th>
<th $TableColor>Rejs$OutColLabelAdd</th>
</tr>
EOT
;

	while (<DIR>) {
		chop;
		s/^\.\///;
		if (/^(........)\.html/o && ($MaxDays == 0 || ($NumDays < $MaxDays))) {
			$NumDays++;
			$Date = $1;
			$FancyDate = &GetFancyNameDate ($Date, 1, 0);
			@DailyStats = &GetDailyStats ($Date);
			$InVolume = &GetVolume ($DailyStats[3]);
			$InRejVolume = $DailyStats[10];
			if ($InRejVolume == -1) {
			   $InRejVolume = "-";
			   $InTotVolume = "-";
			} else {
			   $InTotVolume = &GetVolume ($InRejVolume + $DailyStats[3]);
			   $InRejVolume = &GetVolume ($InRejVolume);
			}
			$OutVolume = &GetVolume ($DailyStats[7]);
			$OutRejVolume = &GetVolume ($DailyStats[9]);
			$OutTotVolume = &GetVolume ($DailyStats[7] + $DailyStats[9]);
			$RowColor = &GetRowColor ($RowColor);

			$FancyDate =~ s/\ /&nbsp;/g;

			print FILE <<EOT
<tr bgcolor="$RowColor">
<td align="right"><b>$NumDays</b></td>
<td>&nbsp;<a href="$Date.html"><tt>$FancyDate</tt></a></td>
<td align="right"><b>$DailyStats[2]</b></td>
<td align="right"><b>$InVolume</b></td>
<td align="right">$DailyStats[8]</td>
<td align="right">$DailyStats[1]</td>
EOT
;
			if ($RejTotReport) {
				print FILE <<EOT
<td align="right"><i>$InRejVolume</i></td>
<td align="right"><b><i>$InTotVolume</i></b></td>
EOT
};
			print FILE <<EOT
<td align="right"><b>$DailyStats[6]</b></td>
<td align="right"><b>$OutVolume</b></td>
<td align="right">$DailyStats[4]</td>
<td align="right">$DailyStats[5]</td>
EOT
;
			if ($RejTotReport) {
				print FILE <<EOT
<td align="right"><i>$OutRejVolume</i></td>
<td align="right"><b><i>$OutTotVolume</i></b></td>
EOT
};
		print FILE "</tr>\n";
		}
	}
	print FILE <<EOT
</table>
<p>
<hr>
EOT
;
	&PrintOutFooter;

	close (FILE);
	close (DIR);
}

	
sub PrintSeparator
{
	my ($Num) = @_;

	if ($Num =~ /beg/i) {
		print FILE "<p>\n<hr>\n<p>\n";
	} elsif ($Num =~ /end/i) {
		print FILE "<a href=\"#00\">Goto top of page</a>\n";
		print FILE "<p>\n<hr>\n";
	} else {
		print FILE "<a name=\"$Num\"></a>\n";
#		print FILE "<a href=\"#00\">Goto top of page</a>, Goto top of table\n";
		print FILE "<a href=\"#00\">Goto top of page</a>\n";
		print FILE "<p>\n<hr>\n<p>\n";
	}
}


sub PrintIncomingFeeds
{
	my ($Title, $ListByVolume) = @_;
	my ($Num, $InSecs, $InIhave, $InChk, $InErr, $InSpam, $InRej, $InRejBytes, $InTot);
	my ($InRec, $InAdded, $InBytes, $InPercent, $InSubPercent, $Key, $Comment);
	my ($InPercentAcc, $InSubPercentAcc, $InPercentVol, $InPercentRejVol, $InSubPercentVol, $InSubPercentRejVol, $FilePNG);
	my ($InPercentTot, $InSubPercentTot, $SubTotal7, $Field7, $Total7);
	my ($SubTotal1, $SubTotal2, $SubTotal3, $SubTotal4, $SubTotal5, $SubTotal6);
	my ($Field1, $Field2, $Field3, $Field4, $Field5, $Field6);
	my ($Total1, $Total2, $Total4, $Total5, $Total6, $TooOld);
	my ($Format, $SubFormat, $AscFormat, $RowColor, $SubColor, $Freenix, $retval);

	&PrintTableHeader ($Title);

	if ($ListByVolume == 1) {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByInBytes;
		$Field1 = "Volume";
		$Field2 = "%Vol";
		$Field3 = "Kbps";
		$Field4 = "Accepted";
		$Field5 = "%Acc";
		$Field6 = "%Tot";
		$Field7 = "KB/art";
		$AscFormat = "%8s %3.2f %3.2f %8d %3.2f %3.2f %3.2f";
#		$SubFormat = "%s%s %s%3.2f %s%3.2f %s%d %s%3.2f %s%3.2f %s%3.2f";
		$SubFormat = "%s%s%s %s%3.2f%s %s%3.2f%s %s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%s%s %s%s%3.2f%s %s%s%3.2f%s %s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-in-vol.png";
	} elsif ($ListByVolume == 0) {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByInAdded;
		$Field1 = "Accepted";
		$Field2 = "%Acc";
		$Field3 = "%Tot";
		$Field4 = "Art/sec";
		$Field5 = "Volume";
		$Field6 = "%Vol";
		$Field7 = "KB/art";
		$AscFormat = "%8d %3.2f %3.2f %3.2f %8s %3.2f %3.2f";
#		$SubFormat = "%s%d %s%3.2f %s%3.2f %s%3.2f %s%s %s%3.2f %s%3.2f";
		$SubFormat = "%s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s %s%s%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s %s%s%s%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-in-art.png";
	} else {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByInRejBytes;
		$Field1 = "Rej. Vol.";
		$Field2 = "%Vol";
		$Field3 = "Kbps";
		$Field4 = "Accepted";
		$Field5 = "%Acc";
		$Field6 = "%Tot";
		$Field7 = "KB/art";
		$AscFormat = "%8s %3.2f %3.2f %8d %3.2f %3.2f %3.2f";
#		$SubFormat = "%s%s %s%3.2f %s%3.2f %s%d %s%3.2f %s%3.2f %s%3.2f";
		$SubFormat = "%s%s%s %s%3.2f%s %s%3.2f%s %s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%s%s %s%s%3.2f%s %s%s%3.2f%s %s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-in-rejvol.png";
	}

	if ($TextFormat) {
		printf FILE "\n%-30s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n\n",
			"Incoming Feed", $Field1, $Field2, $Field3, $Field4, $Field5, $Field6, $Field7, "Check", "Ihave", "Spam", "TooOld", "Rejs", "Errs";
	} else {
		print FILE "<tr><th $TableColor colspan=3>Incoming&nbsp;Feed&nbsp;(+&nbsp;<a href=\"$Top1000Url\">Top1000&nbsp;#</a>)</th><th $TableColor>$Field1</th><th $TableColor>$Field2</th><th $TableColor>$Field3</th><th $TableColor>$Field4</th><th $TableColor>$Field5</th><th $TableColor>$Field6</th><th $TableColor>$Field7</th><th $TableColor>Check</th><th $TableColor>Ihave</th><th $TableColor>Spam</th><th $TableColor>TooOld</th><th $TableColor>Rejs</th><th $TableColor>Errs</th></tr>\n";
	}

	$InAdded = $InBytes = $InErr = $InSpam = $InRej = $InRejBytes = $TooOld = 0;
	$InSecs = 1;

	foreach $Key (reverse sort keys %OrderedList) {
		$Host = $OrderedList{$Key};

		if ($Feeds{$Host}->{InAdded} ||
		    $Feeds{$Host}->{InIhave} ||
		    $Feeds{$Host}->{InChk}   ||
		    $Feeds{$Host}->{InSpam}  ||
		    $Feeds{$Host}->{InSecs}  ||
		    $Feeds{$Host}->{InErr}   ||
		    $Feeds{$Host}->{InRej}) {
			$InSecs += $Feeds{$Host}->{InSecs};
			$InIhave += $Feeds{$Host}->{InIhave};
			$InChk += $Feeds{$Host}->{InChk};
			$InSpam += $Feeds{$Host}->{InSpam};
			$InErr += $Feeds{$Host}->{InErr};
			$InRej += $Feeds{$Host}->{InRej};
			$InAdded += $Feeds{$Host}->{InAdded};
			$InBytes += $Feeds{$Host}->{InBytes};
			$InRejBytes += $Feeds{$Host}->{InRejBytes};
			$TooOld += $Feeds{$Host}->{TooOld};
			$Feeds{$Host}->{Freenix} = &GetTop1000($Host);
		}
	}

	$InTot = $InChk + $InIhave + $InSpam + $InRej + $InErr;

	foreach $Key (reverse sort keys %OrderedList) {
		$Host = $OrderedList{$Key};

printf STDERR "HOST=[$Host]  SECS=[$Feeds{$Host}->{InSecs}]  ADDED=[$Feeds{$Host}->{InAdded}] BYTES=[$Feeds{$Host}->{InBytes}] REJECTED=[$Feeds{$Host}->{InRej}] REJECTED BYTES=[$Feeds{$Host}->{InRejBytes}]\n" if $Debug;

		if ($Feeds{$Host}->{InAdded} ||
		    $Feeds{$Host}->{InIhave} ||
		    $Feeds{$Host}->{InChk}   ||
		    $Feeds{$Host}->{InSpam}  ||
		    $Feeds{$Host}->{InSecs}  ||
		    $Feeds{$Host}->{InErr}   ||
		    $Feeds{$Host}->{InRej}) {

			$InSubPercentAcc = &GetPercent ($Feeds{$Host}->{InChk} + $Feeds{$Host}->{InIhave} + $Feeds{$Host}->{InSpam} + $Feeds{$Host}->{InErr} + $Feeds{$Host}->{InRej} + $Feeds{$Host}->{TooOld}, $Feeds{$Host}->{InAdded});

			$InSubPercentTot = &GetPercent ($InAdded, $Feeds{$Host}->{InAdded});
			$InPercentTot += $InSubPercentTot;

			$InSubPercentVol = &GetPercent ($InBytes, $Feeds{$Host}->{InBytes});
			$InSubPercentRejVol = &GetPercent ($InRejBytes, $Feeds{$Host}->{InRejBytes});
			$InPercentVol += $InSubPercentVol;
			$InPercentRejVol += $InSubPercentRejVol;

			if ($ListByVolume == 1) {
				$SubTotal1 = &GetVolume ($Feeds{$Host}->{InBytes});
				$SubColor = &GetVolumeColor ($SubTotal1);
				if (! $TextFormat) {
					$SubTotal1 = "<font color=\"$SubColor\">$SubTotal1</font>";
				}

				$SubTotal2 = $InSubPercentVol;
				if ($WallTime) {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{InBytes}, $ElapsedSecs);
				} else {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{InBytes}, $Feeds{$Host}->{InSecs});
					$Total3 += $SubTotal3;
				}
				$SubTotal4 = $Feeds{$Host}->{InAdded};
				$SubTotal5 = $InSubPercentAcc;
				$SubTotal6 = $InSubPercentTot;
			} elsif ($ListByVolume == 0) {
				$SubTotal1 = $Feeds{$Host}->{InAdded};
				$SubTotal2 = $InSubPercentAcc;
				$SubTotal3 = $InSubPercentTot;
				if ($WallTime) {
					if ($ElapsedSecs > 0) {
						$SubTotal4 = $Feeds{$Host}->{InAdded} / $ElapsedSecs;
					} else {
						$SubTotal4 = $Feeds{$Host}->{InAdded};
					}
				} else {
					if ($Feeds{$Host}->{InSecs} > 0) {
						$SubTotal4 = $Feeds{$Host}->{InAdded} / $Feeds{$Host}->{InSecs};
					} else {
						$SubTotal4 = $Feeds{$Host}->{InAdded};
					}
					$Total4 += $SubTotal4;
				}
				$SubTotal5 = &GetVolume ($Feeds{$Host}->{InBytes});
				$SubColor = &GetVolumeColor ($SubTotal5);
				if (! $TextFormat) {
					$SubTotal5 = "<font color=\"$SubColor\">$SubTotal5</font>";
				}
				$SubTotal6 = $InSubPercentVol;
			} else {
				$SubTotal1 = &GetVolume ($Feeds{$Host}->{InRejBytes});
				$SubColor = &GetVolumeColor ($SubTotal1);
				if (! $TextFormat) {
					$SubTotal1 = "<font color=\"$SubColor\">$SubTotal1</font>";
				}
				$SubTotal2 = $InSubPercentRejVol;
				if ($WallTime) {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{InRejBytes}, $ElapsedSecs);
				} else {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{InRejBytes}, $Feeds{$Host}->{InSecs});
					$Total3 += $SubTotal3;
				}
				$SubTotal4 = $Feeds{$Host}->{InAdded};
				$SubTotal5 = $InSubPercentAcc;
				$SubTotal6 = $InSubPercentTot;
			}
			$SubTotal7 = &GetAvgArtSize ($Feeds{$Host}->{InBytes}, $Feeds{$Host}->{InAdded});

			if ($TextFormat) {
				printf FILE "%-30s $AscFormat %8d %8d %8d %8d %8d %8d %8d\n",
					&GetHostLink ($Host),
					$SubTotal1,
					$SubTotal2,
					$SubTotal3,
					$SubTotal4,
					$SubTotal5,
					$SubTotal6,
					$SubTotal7,
					$Feeds{$Host}->{InChk},
					$Feeds{$Host}->{InIhave},
					$Feeds{$Host}->{InSpam},
					$Feeds{$Host}->{TooOld},
					$Feeds{$Host}->{InRej},
					$Feeds{$Host}->{InErr};
			} else {
				$RowColor = &GetRowColor ($RowColor);
				printf FILE "<tr bgcolor=\"$RowColor\">%s%s%d%s%s %s%s%s %s&nbsp;%s%s $SubFormat %s%d%s %s%d%s %s%d%s %s%d%s %s%d%s %s%d%s</tr>\n",
					$TDR, "<b>", ++$Num, "</b>", "</td>",
					$TDL, &GetHostLink ($Host), "</td>",
					$TDR, &GetTop1000 ($Host), "</td>",
					$TDR, $SubTotal1, "</td>",
					$TDR, $SubTotal2, "</td>",
					$TDR, $SubTotal3, "</td>",
					$TDR, $SubTotal4, "</td>",
					$TDR, $SubTotal5, "</td>",
					$TDR, $SubTotal6, "</td>",
					$TDR, $SubTotal7, "</td>",
					$TDR, $Feeds{$Host}->{InChk}, "</td>",
					$TDR, $Feeds{$Host}->{InIhave}, "</td>",
					$TDR, $Feeds{$Host}->{InSpam}, "</td>",
					$TDR, $Feeds{$Host}->{TooOld}, "</td>",
					$TDR, $Feeds{$Host}->{InRej}, "</td>",
					$TDR, $Feeds{$Host}->{InErr}, "</td>";
			}
		}
	}

	if ($InTot > 0)
	{
		$InPercentAcc = $inAdded / $InTot;
	}
	else
	{
		$InPercentAcc = 1.0;
	}

	if ($ListByVolume == 1) {
		$Total1 = &GetVolume ($InBytes);
		$Total2 = $InPercentVol;
		if ($WallTime) {
			$Total3 = &GetKbps ($InBytes, $ElapsedSecs);
		}
		$Total4 = $InAdded;
		$Total5 = $InPercentAcc;
		$Total6 = $InPercentTot;
	} elsif ($ListByVolume == 0) {
		$Total1 = $InAdded;
		$Total2 = $InPercentAcc;
		$Total3 = $InPercentTot;
		if ($WallTime) {
			if ($ElapsedSecs > 0) {
				$Total4 = $InAdded / $ElapsedSecs;
			} else {
				$Total4 = $InAdded;
			}
		}
		$Total5 = &GetVolume ($InBytes);
		$Total6 = $InPercentVol;
	} else {
		$Total1 = &GetVolume ($InRejBytes);
		$Total2 = $InPercentRejVol;
		if ($WallTime) {
			$Total3 = &GetKbps ($InRejBytes, $ElapsedSecs);
		}
		$Total4 = $InAdded;
		$Total5 = $InPercentAcc;
		$Total6 = $InPercentTot;
	}
	$Total7 = &GetAvgArtSize ($InBytes, $InAdded);

	if ($TextFormat) {
		printf FILE "\n%s $AscFormat %8d %8d %8d %8d %8d %8d\n\n",
			"                              ",
			$Total1, $Total2, $Total3, $Total4, $Total5, $Total6,
			$Total7, $InChk, $InIhave, $InSpam, $TooOld, $InRej, $InErr;
	} else {
		$RowColor = &GetRowColor ($RowColor);
		printf FILE "<tr bgcolor=\"$RowColor\">%s%s $Format %s%s%d%s %s%s%d%s %s%s%d%s %s%s%d%s %s%s%d%s %s%s%d%s</tr>\n",
			"<td align=\"center\" colspan=3>", "<b>Total</b></td>",
			$TDR, "<b>", $Total1, "</b></td>",
			$TDR, "<b>", $Total2, "</b></td>",
			$TDR, "<b>", $Total3, "</b></td>",
			$TDR, "<b>", $Total4, "</b></td>",
			$TDR, "<b>", $Total5, "</b></td>",
			$TDR, "<b>", $Total6, "</b></td>",
			$TDR, "<b>", $Total7, "</b></td>",
			$TDR, "<b>", $InChk, "</b></td>",
			$TDR, "<b>", $InIhave, "</b></td>",
			$TDR, "<b>", $InSpam, "</b></td>",
			$TDR, "<b>", $TooOld, "</b></td>",
			$TDR, "<b>", $InRej, "</b></td>",
			$TDR, "<b>", $InErr, "</b></td>";
	}

	$Comment = "INCOMING ERR=$InErr REJ=$InRej SPAM=$InSpam ADD=$InAdded VOL=$InBytes TooOld=$TooOld REJVOL=$InRejBytes";

	&PrintTableFooter ($Comment);

	&PrintUrlPNG ($FilePNG);

	if ($ListByVolume == 1)
	{
		$retval = $InBytes;
	}
	elsif ($ListByVolume == 0)
	{
		$retval = $InAdded;
	}
	else
	{
		$retval = $InRejBytes;
	}

	return $retval;
}


sub PrintIncomingTimes
{
	my ($Title) = @_;
	my ($Num, $Hour, $InVolume, $InSubVolume, $FilePNG, $RowColor, $SubColor);
	my ($InPercentAcc, $InSubPercentAcc, $InPercentVol, $InSubPercentVol);

	$FilePNG = "$FileDate-in-time.png";

	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "\n%5s %8s %6s %8s %6s %8s %8s %8s %8s %8s\n\n",
			"Hour", "Accepted", "%Acc", "Volume", "%Vol", "Check", "Ihave", "Spam", "Rejs", "Errs";
	} else {
		print FILE "<tr><th $TableColor>Hour</th><th $TableColor>Accepted</th><th $TableColor>%Acc</th><th $TableColor>Volume</th><th $TableColor>%Vol</th><th $TableColor>Check</th><th $TableColor>Ihave</th><th $TableColor>Spam</th><th $TableColor>Rejs</th><th $TableColor>Errs</th></tr>\n";
	}

	for ($Num = 0; $Num < 24; $Num++) {
		$Hour = sprintf ("%02d", $Num);

		$InSubPercentAcc = &GetPercent ($HourlyStats{Total}->{InAdded}, $HourlyStats{$Hour}->{InAdded});
		$InPercentAcc += $InSubPercentAcc;

		$InSubPercentVol = &GetPercent ($HourlyStats{Total}->{InBytes}, $HourlyStats{$Hour}->{InBytes});
		$InPercentVol += $InSubPercentVol;

		$InSubVolume = &GetVolume ($HourlyStats{$Hour}->{InBytes});
		$SubColor = &GetVolumeColor ($InSubVolume);
		if (! $TextFormat) {
			$InSubVolume = "<font color=\"$SubColor\">$InSubVolume</font>";
		}
		if ($TextFormat) {
			printf FILE "%5s %8d %3.2f %8s %3.2f %8d %8d %8d %8d %8d\n",
				$Hour,
				$HourlyStats{$Hour}->{InAdded},
				$InSubPercentAcc,
				$InSubVolume,
				$InSubPercentVol,
				$HourlyStats{$Hour}->{InChk},
				$HourlyStats{$Hour}->{InIhave},
				$HourlyStats{$Hour}->{InSpam},
				$HourlyStats{$Hour}->{InRej},
				$HourlyStats{$Hour}->{InErr};
		} else {
			$RowColor = &GetRowColor ($RowColor);
			printf FILE "<tr bgcolor=\"$RowColor\">%s%s%s%s %s%d%s %s%3.2f%s %s%s%s %s%3.2f%s %s%d%s %s%d%s %s%d%s %s%d%s %s%d%s</tr>\n",
				$TDR, "<b>", $Hour, "</b></td>",
				$TDR, $HourlyStats{$Hour}->{InAdded}, "</td>",
				$TDR, $InSubPercentAcc, "</td>",
				$TDR, $InSubVolume, "</td>",
				$TDR, $InSubPercentVol, "</td>",
				$TDR, $HourlyStats{$Hour}->{InChk}, "</td>",
				$TDR, $HourlyStats{$Hour}->{InIhave}, "</td>",
				$TDR, $HourlyStats{$Hour}->{InSpam}, "</td>",
				$TDR, $HourlyStats{$Hour}->{InRej}, "</td>",
				$TDR, $HourlyStats{$Hour}->{InErr}, "</td>";
		}
	}

	$InVolume = &GetVolume ($HourlyStats{Total}->{InBytes});

	if ($TextFormat) {
		printf FILE "\n%5s %8d %3.2f %8s %3.2f %8d %8d %8d %8d %8d\n\n",
			"Total",
			$HourlyStats{Total}->{InAdded},
			$InPercentAcc,
			$InVolume,
			$InPercentVol,
			$HourlyStats{Total}->{InChk},
			$HourlyStats{Total}->{InIhave},
			$HourlyStats{Total}->{InSpam},
			$HourlyStats{Total}->{InRej},
			$HourlyStats{Total}->{InErr};
	} else {
		$RowColor = &GetRowColor ($RowColor);
		printf FILE "<tr bgcolor=\"$RowColor\">%s%s%s%s %s%s%d%s %s%s%3.2f%s %s%s%s%s %s%s%3.2f%s %s%s%d%s %s%s%d%s %s%s%d%s %s%s%d%s %s%s%d%s</tr>\n",
			$TDC, "<b>", "Total", "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InAdded}, "</b></td>",
			$TDR, "<b>", $InPercentAcc, "</b></td>",
			$TDR, "<b>", $InVolume, "</b></td>",
			$TDR, "<b>", $InPercentVol, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InChk}, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InIhave}, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InSpam}, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InRej}, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{InErr}, "</b></td>";
	}

	&PrintTableFooter ("");

	&PrintUrlPNG ($FilePNG);
}

sub PrintOutgoingTimes
{
	my ($Title) = @_;
	my ($Num, $Hour, $OutVolume, $OutSubVolume, $FilePNG, $RowColor, $SubColor);
	my ($OutPercentAcc, $OutSubPercentAcc, $OutPercentVol, $OutSubPercentVol);

	$FilePNG = "$FileDate-out-time.png";

	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "\n%5s %8s %6s %8s %6s %8s %8s\n\n",
			"Hour", "Accepted", "%Acc", "Volume", "%Vol", "Dups", "Rejs";
	} else {
		print FILE "<tr><th $TableColor>Hour</th><th $TableColor>Accepted</th><th $TableColor>%Acc</th><th $TableColor>Volume</th><th $TableColor>%Vol</th><th $TableColor>dups</th><th $TableColor>Rejs</th></tr>\n";
	}

	for ($Num = 0; $Num < 24; $Num++) {
		$Hour = sprintf ("%02d", $Num);

		$OutSubPercentAcc = &GetPercent ($HourlyStats{Total}->{OutAccepted}, $HourlyStats{$Hour}->{OutAccepted});
		$OutPercentAcc += $OutSubPercentAcc;

		$OutSubPercentVol = &GetPercent ($HourlyStats{Total}->{OutBytes}, $HourlyStats{$Hour}->{OutBytes});
		$OutPercentVol += $OutSubPercentVol;

		$OutSubVolume = &GetVolume ($HourlyStats{$Hour}->{OutBytes});
		$SubColor = &GetVolumeColor ($OutSubVolume);
		if (! $TextFormat) {
			$OutSubVolume = "<font color=\"$SubColor\">$OutSubVolume</font>";
		}

		if ($TextFormat) {
			printf FILE "%5s %8d %3.2f %8s %3.2f %8d %8d\n",
				$Hour,
				$HourlyStats{$Hour}->{OutAccepted},
				$OutSubPercentAcc,
				$OutSubVolume,
				$OutSubPercentVol,
				$HourlyStats{$Hour}->{OutDup},
				$HourlyStats{$Hour}->{OutRej},
		} else {
			$RowColor = &GetRowColor ($RowColor);
			printf FILE "<tr bgcolor=\"$RowColor\">%s%s%s%s %s%d%s %s%3.2f%s %s%s%s %s%3.2f%s %s%d%s %s%d%s</tr>\n",
				$TDR, "<b>", $Hour, "</b></td>",
				$TDR, $HourlyStats{$Hour}->{OutAccepted}, "</td>",
				$TDR, $OutSubPercentAcc, "</td>",
				$TDR, $OutSubVolume, "</td>",
				$TDR, $OutSubPercentVol, "</td>",
				$TDR, $HourlyStats{$Hour}->{OutDup}, "</td>",
				$TDR, $HourlyStats{$Hour}->{OutRej}, "</td>";
		}
	}

	$OutVolume = &GetVolume ($HourlyStats{Total}->{OutBytes});

	if ($TextFormat) {
		printf FILE "\n%5s %8d %3.2f %8s %3.2f %8d %8d\n\n",
			"Total",
			$HourlyStats{Total}->{OutAccepted},
			$OutPercentAcc,
			$OutVolume,
			$OutPercentVol,
			$HourlyStats{Total}->{OutDup},
			$HourlyStats{Total}->{OutRej},
	} else {
		$RowColor = &GetRowColor ($RowColor);
		printf FILE "<tr bgcolor=\"$RowColor\">%s%s%s%s %s%s%d%s %s%s%3.2f%s %s%s%s%s %s%s%3.2f%s %s%s%d%s %s%s%d%s</tr>\n",
			$TDC, "<b>", "Total", "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{OutAccepted}, "</b></td>",
			$TDR, "<b>", $OutPercentAcc, "</b></td>",
			$TDR, "<b>", $OutVolume, "</b></td>",
			$TDR, "<b>", $OutPercentVol, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{OutDup}, "</b></td>",
			$TDR, "<b>", $HourlyStats{Total}->{OutRej}, "</b></td>";
	}

	&PrintTableFooter ("");

	&PrintUrlPNG ($FilePNG);
}

sub PrintOutgoingFeeds
{
	my ($Title, $ListByVolume, $InAdded) = @_;
	my ($Key, $Comment, $OutCons, $OutSecs, $OutDup, $OutRej, $OutAcc, $OutBytes, $OutRejBytes);
	my ($Num, $OutPercentAcc, $OutSubPercentAcc, $OutPercentVol, $OutPercentRejVol, $OutSubPercentVol, $OutSubPercentRejVol);
	my ($OutPercentTot, $OutSubPercentTot, $SubTotal7, $Field7, $Total7);
	my ($SubTotal1, $SubTotal2, $SubTotal3, $SubTotal4, $SubTotal5, $SubTotal6);
	my ($Field1, $Field2, $Field3, $Field4, $Field5, $Field6);
	my ($Total1, $Total2, $Total4, $Total5, $Total6);
	my ($Format, $SubFormat, $FilePNG, $RowColor, $SubColor, $Freenix, $InHost);
	my ($OutFreenix, $InBytes);

	&PrintTableHeader ($Title);

	if ($ListByVolume == 1) {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByOutBytes;
		$Field1 = "Volume";
		$Field2 = "%Vol";
		$Field3 = "Kbps";
		$Field4 = "Accepted";
		$Field5 = "%Acc";
		$Field6 = "%Tot";
		$Field7 = "KB/art";
		$AscFormat = "%8s %3.2f %3.2f %8d %3.2f %3.2f %3.2f";
#		$SubFormat = "%s%s %s%3.2f %s%3.2f %s%d %s%3.2f %s%3.2f %s%3.2f";
		$SubFormat = "%s%s%s %s%3.2f%s %s%3.2f%s %s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%s%s %s%s%3.2f%s %s%s%3.2f%s %s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-out-vol.png";
		$InBytes = $InAdded;	# What we were passed is really number
					# of bytes, not number of articles
	} elsif ($ListByVolume == 0) {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByOutAccepted;
		$Field1 = "Accepted";
		$Field2 = "%Acc";
		$Field3 = "%Tot";
		$Field4 = "Art/sec";
		$Field5 = "Volume";
		$Field6 = "%Vol";
		$Field7 = "KB/art";
		$AscFormat = "%8d %3.2f %3.2f %3.2f %8s %3.2f %3.2f";
#		$SubFormat = "%s%d %s%3.2f %s%3.2f %s%3.2f %s%s %s%3.2f %s%3.2f";
		$SubFormat = "%s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s %s%s%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s %s%s%s%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-out-art.png";
	} else {
		$Total3 = new Math::BigFloat 0;
		%OrderedList = %OrderByOutRejBytes;
		$Field1 = "Rej. Vol.";
		$Field2 = "%Vol";
		$Field3 = "Kbps";
		$Field4 = "Accepted";
		$Field5 = "%Acc";
		$Field6 = "%Tot";
		$Field7 = "KB/art";
		$AscFormat = "%8s %3.2f %3.2f %8d %3.2f %3.2f %3.2f";
#		$SubFormat = "%s%s %s%3.2f %s%3.2f %s%d %s%3.2f %s%3.2f %s%3.2f";
		$SubFormat = "%s%s%s %s%3.2f%s %s%3.2f%s %s%d%s %s%3.2f%s %s%3.2f%s %s%3.2f%s";
		$Format = "%s%s%s%s %s%s%3.2f%s %s%s%3.2f%s %s%s%d%s %s%s%3.2f%s %s%s%3.2f%s %s%s%3.2f%s";
		$FilePNG = "$FileDate-out-rejvol.png";
		$InBytes = $InAdded;	# What we were passed is really number
					# of bytes, not number of articles
	}

	if ($TextFormat) {
		printf FILE "\n%-30s %8s %8s %8s %8s %8s %8s %8s %8s %8s %14s\n\n",
			"Outgoing Feed", $Field1, $Field2, $Field3, $Field4, $Field5, $Field6, $Field7, "Dups", "Rejs", "% Ratio (Out/In)";
	} else {
		print FILE "<tr><th $TableColor colspan=3>Outgoing&nbsp;Feed&nbsp;(+&nbsp;<a href=\"$Top1000Url\">Top1000&nbsp;#</a>)</th><th $TableColor>$Field1</th><th $TableColor>$Field2</th><th $TableColor>$Field3</th><th $TableColor>$Field4</th><th $TableColor>$Field5</th><th $TableColor>$Field6</th><th $TableColor>$Field7</th><th $TableColor>Dups</th><th $TableColor>Rejs</th><th $TableColor>% Ratio (Out/In)</th></tr>\n";
	}

	$OutAccepted = $OutBytes = $OutRej = $OutRejBytes = $OutDup = 0;
	$OutSecs = 1;

	foreach $Key (reverse sort keys %OrderedList) {
		$Host = $OrderedList{$Key};

		if ($Feeds{$Host}->{OutAccepted} ||
		    $Feeds{$Host}->{OutSecs}     ||
		    $Feeds{$Host}->{OutDup}      ||
		    $Feeds{$Host}->{OutRej}) {

			$OutSecs += $Feeds{$Host}->{OutSecs};
			$OutDup += $Feeds{$Host}->{OutDup};
			$OutRej += $Feeds{$Host}->{OutRej};
			$OutAccepted += $Feeds{$Host}->{OutAccepted};
			$OutBytes += $Feeds{$Host}->{OutBytes};
			$OutRejBytes += $Feeds{$Host}->{OutRejBytes};
		}
		if ($Feeds{$Host}->{Freenix})
		{
			$Freenix{$Feeds{$Host}->{Freenix}} = $Host;
		}
	}

	foreach $Key (reverse sort keys %OrderedList) {
		$Host = $OrderedList{$Key};

		if ($Feeds{$Host}->{OutAccepted} ||
		    $Feeds{$Host}->{OutSecs}     ||
		    $Feeds{$Host}->{OutDup}      ||
		    $Feeds{$Host}->{OutRej}) {

			$OutSubPercentAcc = &GetPercent ($Feeds{$Host}->{OutRej} + $Feeds{$Host}->{OutDup} + $Feeds{$Host}->{OutAccepted}, $Feeds{$Host}->{OutAccepted});
			$OutSubPercentTot = &GetPercent ($OutAccepted, $Feeds{$Host}->{OutAccepted});
			$OutPercentTot += $OutSubPercentTot;

			$OutSubPercentVol = &GetPercent ($OutBytes, $Feeds{$Host}->{OutBytes});
			$OutSubPercentRejVol = &GetPercent ($OutRejBytes, $Feeds{$Host}->{OutRejBytes});
			$OutPercentVol += $OutSubPercentVol;
			$OutPercentRejVol += $OutSubPercentRejVol;

			if ($ListByVolume == 1) {
				$SubTotal1 = &GetVolume ($Feeds{$Host}->{OutBytes});
				$SubColor = &GetVolumeColor ($SubTotal1);
				if (! $TextFormat) {
					$SubTotal1 = "<font color=\"$SubColor\">$SubTotal1</font>";
				}
				$SubTotal2 = $OutSubPercentVol;
				if ($WallTime) {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{OutBytes}, $ElapsedSecs);
				} else {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{OutBytes}, $Feeds{$Host}->{OutSecs});
					$Total3 += $SubTotal3;
				}
				$SubTotal4 = $Feeds{$Host}->{OutAccepted};
				$SubTotal5 = $OutSubPercentAcc;
				$SubTotal6 = $OutSubPercentTot;
			} elsif ($ListByVolume == 0) {
				$SubTotal1 = $Feeds{$Host}->{OutAccepted};
				$SubTotal2 = $OutSubPercentAcc;
				$SubTotal3 = $OutSubPercentTot;
				if ($WallTime) {
					if ($ElapsedSecs > 0) {
						$SubTotal4 = $Feeds{$Host}->{OutAccepted} / $ElapsedSecs;
					} else {
						$SubTotal4 = $Feeds{$Host}->{OutAccepted};
					}
				} else {
					if ($Feeds{$Host}->{OutSecs} > 0) {
						$SubTotal4 = $Feeds{$Host}->{OutAccepted} / $Feeds{$Host}->{OutSecs};
					} else {
						$SubTotal4 = $Feeds{$Host}->{OutAccepted};
					}
					$Total4 += $SubTotal4;
				}
				$SubTotal5 = &GetVolume ($Feeds{$Host}->{OutBytes});
				$SubColor = &GetVolumeColor ($SubTotal5);
				if (! $TextFormat) {
					$SubTotal5 = "<font color=\"$SubColor\">$SubTotal5</font>";
				}
				$SubTotal6 = $OutSubPercentVol;
			} else {
				$SubTotal1 = &GetVolume ($Feeds{$Host}->{OutRejBytes});
				$SubColor = &GetVolumeColor ($SubTotal1);
				if (! $TextFormat) {
					$SubTotal1 = "<font color=\"$SubColor\">$SubTotal1</font>";
				}
				$SubTotal2 = $OutSubPercentRejVol;
				if ($WallTime) {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{OutRejBytes}, $ElapsedSecs);
				} else {
					$SubTotal3 = &GetKbps ($Feeds{$Host}->{OutRejBytes}, $Feeds{$Host}->{OutSecs});
					$Total3 += $SubTotal3;
				}
				$SubTotal4 = $Feeds{$Host}->{OutAccepted};
				$SubTotal5 = $OutSubPercentAcc;
				$SubTotal6 = $OutSubPercentTot;
			}
			$SubTotal7 = &GetAvgArtSize ($Feeds{$Host}->{OutBytes}, $Feeds{$Host}->{OutAccepted});

			$OutFreenix = &GetTop1000($Host);
			if ((length $OutFreenix) > 0) {
				$InHost = $Freenix{$OutFreenix};
			} else {
				$InHost = $Host;
			}
			if ($TextFormat) {
				printf FILE "%-30s $AscFormat %8d %8d",
					&GetHostLink ($Host),
					$SubTotal1,
					$SubTotal2,
					$SubTotal3,
					$SubTotal4,
					$SubTotal5,
					$SubTotal6,
					$SubTotal7,
					$Feeds{$Host}->{OutDup},
					$Feeds{$Host}->{OutRej};
				if ($Feeds{$InHost}->{InAdded} > 0)
				{
					if ($ListByVolume == 1)
					{
						printf FILE " %8.2f\n", 100 * $Feeds{$Host}->{OutBytes} / $Feeds{$InHost}->{InBytes};
					}
					elsif ($ListByVolume == 0)
					{
						printf FILE " %8.2f\n", 100 * $Feeds{$Host}->{OutAccepted} / $Feeds{$InHost}->{InAdded};
					}
					else
					{
						printf FILE " %8.2f\n", 100 * $Feeds{$Host}->{OutRejBytes} / $Feeds{$InHost}->{InBytes};
					}
				}
				else
				{
					printf FILE "\n";
				}
			} else {
				$RowColor = &GetRowColor ($RowColor);
				printf FILE "<tr bgcolor=\"$RowColor\">%s%s%d%s %s%s%s %s&nbsp;%s%s $SubFormat %s%d%s %s%d%s",
					$TDR, "<b>", ++$Num, "</b></td>",
					$TDL, &GetHostLink ($Host), "</td>",
					$TDR, &GetTop1000 ($Host), "</td>",
					$TDR, $SubTotal1, "</td>",
					$TDR, $SubTotal2, "</td>",
					$TDR, $SubTotal3, "</td>",
					$TDR, $SubTotal4, "</td>",
					$TDR, $SubTotal5, "</td>",
					$TDR, $SubTotal6, "</td>",
					$TDR, $SubTotal7, "</td>",
					$TDR, $Feeds{$Host}->{OutDup}, "</td>",
					$TDR, $Feeds{$Host}->{OutRej}, "</td>";
				if ($Feeds{$InHost}->{InAdded} != 0)
				{
					if ($ListByVolume == 1)
					{
						printf FILE "%s%8.2f%s", $TDR, 100 * $Feeds{$Host}->{OutBytes} / $Feeds{$InHost}->{InBytes},  "</td>";
					}
					elsif ($ListByVolume == 0)
					{
						printf FILE "%s%8.2f%s", $TDR, 100 * $Feeds{$Host}->{OutAccepted} / $Feeds{$InHost}->{InAdded},  "</td>";
					}
					else
					{
						printf FILE "%s%8.2f%s", $TDR, 100 * $Feeds{$Host}->{OutRejBytes} / $Feeds{$InHost}->{InBytes},  "</td>";
					}
				}
				else
				{
					printf FILE "%s&#133;", $TDR;
				}
				printf FILE "%s\n", "</tr>";
			}
		}
	}

	if ($OutAccepted != 0)
	{
		$OutPercentAcc = $OutAccepted / ($OutDup + $OutRej + $OutAccepted);
	}
	else
	{
		$OutPercentAcc = 0;
	}

	if ($ListByVolume == 1) {
		$Total1 = &GetVolume ($OutBytes);
		$Total2 = $OutPercentVol;
		if ($WallTime) {
			$Total3 = &GetKbps ($OutBytes, $ElapsedSecs);
		}
		$Total4 = $OutAccepted;
		$Total5 = $OutPercentAcc;
		$Total6 = $OutPercentTot;
	} elsif ($ListByVolume == 0) {
		$Total1 = $OutAccepted;
		$Total2 = $OutPercentAcc;
		$Total3 = $OutPercentTot;
		if ($WallTime) {
			if ($ElapsedSecs > 0) {
				$Total4 = ($OutAccepted ? $OutAccepted / $ElapsedSecs : 0);
			} else {
				$Total4 = ($OutAccepted ? $OutAccepted : 0);
			}
		}
		$Total4 = 0 if ($OutAccepted == 0);
		$Total5 = &GetVolume ($OutBytes);
		$Total6 = $OutPercentVol;
	} else {
		$Total1 = &GetVolume ($OutRejBytes);
		$Total2 = $OutPercentRejVol;
		if ($WallTime) {
			$Total3 = &GetKbps ($OutRejBytes, $ElapsedSecs);
		}
		$Total4 = $OutAccepted;
		$Total5 = $OutPercentAcc;
		$Total6 = $OutPercentTot;
	}
	$Total7 = &GetAvgArtSize ($OutBytes, $OutAccepted);

	if ($TextFormat) {
		printf FILE "\n%s $AscFormat %8d %8d",
			"                              ", $Total1, $Total2,
			$Total3, $Total4, $Total5, $Total6, $Total7, $OutDup, $OutRej;
		if (($InAdded > 0) && ($OutAccepted > 0))
		{
			if ($ListByVolume == 1)
			{
				if ($InBytes > 0) {
					printf FILE " %8.2f\n\n", $OutBytes / $InBytes;
				} else {
					printf FILE " %8.2f\n\n", $OutBytes;
				}
			}
			elsif ($ListByVolume == 0)
			{
				if ($InAdded > 0) {
					printf FILE " %8.2f\n\n", $OutAccepted / $InAdded;
				} else {
					printf FILE " %8.2f\n\n", $OutAccepted;
				}
			}
			else
			{
				if ($InBytes > 0) {
					printf FILE " %8.2f\n\n", $OutRejBytes / $InBytes;
				} else {
					printf FILE " %8.2f\n\n", $OutRejBytes;
				}
			}
		}
		else
		{
			printf FILE " \n\n";
		}
	} else {
		$RowColor = &GetRowColor ($RowColor);
		printf FILE "<tr bgcolor=\"$RowColor\">%s%s $Format %s%s%d%s %s%s%d%s",
			"<td align=\"center\" colspan=3>", "<b>Total</b></td>",
			$TDR, "<b>", $Total1, "</b></td>",
			$TDR, "<b>", $Total2, "</b></td>",
			$TDR, "<b>", $Total3, "</b></td>",
			$TDR, "<b>", $Total4, "</b></td>",
			$TDR, "<b>", $Total5, "</b></td>",
			$TDR, "<b>", $Total6, "</b></td>",
			$TDR, "<b>", $Total7, "</b></td>",
			$TDR, "<b>", $OutDup, "</b></td>",
			$TDR, "<b>", $OutRej, "</b></td>";
		if (($InAdded > 0) && ($OutAccepted > 0))
		{
			if ($ListByVolume == 1)
			{
				if ($InBytes > 0) {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", 100 * $OutBytes / $InBytes, "</b></td></tr>";
				} else {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", $OutBytes, "</b></td></tr>";
				}
			}
			elsif ($ListByVolume == 0)
			{
				if ($InAdded > 0) {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", 100 * $OutAccepted / $InAdded, "</b></td></tr>";
				} else {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", $OutAccepted, "</b></td></tr>";
				}
			}
			else
			{
				if ($InBytes > 0) {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", 100 * $OutRejBytes / $InBytes, "</b></td></tr>";
				} else {
					printf FILE "%s%s%8.2f%s\n", $TDR, "<b>", $OutRejBytes, "</b></td></tr>";
				}
			}
		}
		else
		{
			printf FILE "%s&nbsp%s\n", $TDR, "</td></tr>";
		}
	}

	$Comment = "OUTGOING DUP=$OutDup REJ=$OutRej ACC=$OutAccepted VOL=$OutBytes REJ VOL=$OutRejBytes";

	&PrintTableFooter ($Comment);

	&PrintUrlPNG ($FilePNG);
}

sub PrintReaderStats
{
	my ($Title) = @_;
	my($RowColor);


	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "%16s %6d %6d %6d %9d %6d\n","Host", "Connections",\
				"Groups", "Articles", "Bytes","Posts";
		foreach $Key (sort keys %readerconnect) {
			printf FILE "%16s %6d %6d %6d %9d %6d\n",$Key, \
				$readerconnect{$Key}, $readergroups{$Key}, \
				$readerarts{$Key}, $readerbytes{$Key}, \
				$readerposts{$Key};
			$readerconnect+=$readerconnect{$Key};
			$readergroups+=$readergroups{$Key};
			$readerarts+=$readerarts{$Key};
			$readerbytes+=$readerbytes{$Key};
			$readerposts+=$readerposts{$Key};
		}
		printf FILE "%16s %6d %6d %6d %9d %6d\n",$Key, $readerconnect,
				$readergroups, $readerarts, $readerbytes, $readerposts,
		print;
	} else {
		print FILE <<EOT

<table border=2>
<tr>
<td align="left"><b>Reader</b></td>
<td align="right"><b>Connections</b></td>
<td align="right"><b>Groups</b></td>
<td align="right"><b>Read articles</b></td>
<td align="right"><b>Read bytes</b></td>
<td align="right"><b>Posts</b></td>
</tr>\n
EOT
;
		foreach $Key (sort {$readerarts{$b}  <=> $readerarts{$a}} keys %readerarts) {
                        $RowColor = &GetRowColor ($RowColor);
			print FILE "<tr bgcolor=\"$RowColor\"><td align=\"left\">",
					$Key,"</td><td align=\"right\">",
					$readerconnect{$Key},"</td>\n<td align=\"right\">",
					$readergroups{$Key},"</td>\n<td align=\"right\">",
					$readerarts{$Key},"</td>\n<td align=\"right\">",
                                        $readerbytes{$Key},"</td>\n<td align=\"right\">",
                                        $readerposts{$Key},"</td></tr>\n";
				$readerconnect+=$readerconnect{$Key};
				$readergroups+=$readergroups{$Key};
				$readerarts+=$readerarts{$Key};
				$readerbytes+=$readerbytes{$Key};
				$readerposts+=$readerposts{$Key};
		}
		print FILE "<tr bgcolor=\"$RowColor\"><td align=\"left\">Total</td><td align=\"right\">",
				$readerconnect,"</td>\n<td align=\"right\">",
				$readergroups,"</td>\n<td align=\"right\">",
				$readerarts,"</td>\n<td align=\"right\">",
				$readerbytes,"</td>\n<td align=\"right\">",
				$readerposts,"</td></tr>\n";
		print FILE "</table></P>";
	}
}
sub PrintQueueStats
{
	
	my ($Title) = @_;
	my ($OutCons, $OutSecs, $OutDup, $OutRej, $OutAccepted);
	my ($Key, $Color, $TDL, $TDR);

	if (! $TextFormat) {
		print FILE <<EOT
<table border=2 width="100%">
<tr>
<td align="center" bgcolor=$QueColor1><b>&gt;= $QuePercent1% Full</b></td>
<td align="center" bgcolor=$QueColor2><b>&gt;= $QuePercent2% Full</b></td>
<td align="center" bgcolor=$QueColor3><b>&gt;= $QuePercent3% Full</b></td>
<td align="center" bgcolor=$QueColor4><b>&gt;= $QuePercent4% Full</b></td>
<td align="center" bgcolor=$QueColor5><b>&gt;= $QuePercent5% Full</b></td>
<td align="center" bgcolor=$QueColor6><b>&gt;= $QuePercent6% Full</b></td>
</tr>
</table>
<p>
EOT
;
	}

	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "\n%-30s %19s %9s %9s %9s\n\n",
			"Outgoing Feed", "Batch Seq",
			"Batch Num", "Batch Max", "%Full";
	} else {
		print FILE "<tr><th $TableColor>Outgoing Feed</th><th $TableColor>Batch Seq</th><th $TableColor>Batch Num</th><th $TableColor>Batch Max</th><th $TableColor>%Full</th><th $TableColor>Av. Pending</th></tr>\n";
	}

	foreach $Key (reverse sort keys %OrderByBatchFull) {
		$Host = $OrderByBatchFull{$Key};

		if ($Feeds{$Host}->{BatchMax}) {

			if ($TextFormat) {
				printf FILE "%-30s %9d-%9d %9d %9d %9d\n",
					&GetQueueHost ($Host),
					$Feeds{$Host}->{BatchBeg},
					$Feeds{$Host}->{BatchEnd},
					$Feeds{$Host}->{BatchNum},
					$Feeds{$Host}->{BatchMax},
					$Feeds{$Host}->{BatchFull};
			} else {
				if ($Feeds{$Host}->{BatchFull} >= $QuePercent6) {
					$Color = "bgcolor=$QueColor6";
				} elsif ($Feeds{$Host}->{BatchFull} >= $QuePercent5) {
					$Color = "bgcolor=$QueColor5";
				} elsif ($Feeds{$Host}->{BatchFull} >= $QuePercent4) {
					$Color = "bgcolor=$QueColor4";
				} elsif ($Feeds{$Host}->{BatchFull} >= $QuePercent3) {
					$Color = "bgcolor=$QueColor3";
				} elsif ($Feeds{$Host}->{BatchFull} >= $QuePercent2) {
					$Color = "bgcolor=$QueColor2";
				} elsif ($Feeds{$Host}->{BatchFull} >= $QuePercent1) {
					$Color = "bgcolor=$QueColor1";
				} else {
					$Color = "";
				}
				$TDL = "<td $Color>";
				$TDR = "<td $Color align=\"right\">";

				$Feeds{$Host}->{AvPend} = "&#133;" unless ( $Feeds{$Host}->{AvPend} > 0) ;
				print "AVPDBG: ", $Host, $Feeds{$Host}->{AvPend}, "\n" if $Debug ;
				printf FILE "<tr>%s%s %s%d-%d%s %s%d%s %s%d%s %s%d%s %s%3.1f%s</tr>\n",
					$TDL, &GetQueueHost ($Host),
					$TDR, $Feeds{$Host}->{BatchBeg}, $Feeds{$Host}->{BatchEnd}, "</td>",
					$TDR, $Feeds{$Host}->{BatchNum}, "</td>",
					$TDR, $Feeds{$Host}->{BatchMax}, "</td>",
					$TDR, $Feeds{$Host}->{BatchFull}, "</td>",
					$TDR, $Feeds{$Host}->{AvPend}, "</td>";
			}
		}
	}

	&PrintTableFooter ("");
}


sub PrintCuriousActivity
{
	my ($Title) = @_;
	my ($Key, $BegTime, $EndTime, $RowColor);

	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "\n%s %s %-8s %50s\n\n",
			"1st Time", "Last Time", "# Msgs", "Message";
	} else {
		print FILE "<tr><th $TableColor>1st Time</th><th $TableColor>Last Time</th><th $TableColor># Msgs</th><th $TableColor>Message</th></tr>\n";
	}

	foreach $Key (reverse sort keys %OrderByErrorTotal) {
		$Msg = $OrderByErrorTotal{$Key};
		$HTMLMsg = $Msg;
		$HTMLMsg =~ s/</\&lt\;/g;
		$HTMLMsg =~ s/>/\&gt\;/g;
		
		if ($ErrorList{$Msg}->{Tot}) {

			$BegTime = $EndTime = "";
			$BegTime = $1 if ($ErrorList{$Msg}->{BegDate} =~ /(..:..:..)/);
			$EndTime = $1 if ($ErrorList{$Msg}->{EndDate} =~ /(..:..:..)/);

			if ($TextFormat) {
				printf FILE "%s %s %8d %-50s\n",
					$BegTime, $EndTime, $ErrorList{$Msg}->{Tot}, $Msg;
			} else {
				$RowColor = &GetRowColor ($RowColor);
				printf FILE "<tr bgcolor=\"$RowColor\">%s%s%s %s&nbsp;%s%s %s%d%s %s%s%s</tr>\n",
					$TDL, $BegTime, "</td>",
					$TDL, $EndTime, "</td>",
					$TDR, $ErrorList{$Msg}->{Tot}, "</td>",
					$TDL, $HTMLMsg, "</td>";
			}
		}
	}

	&PrintTableFooter ("");
}


sub PrintSpamActivity
{
	my ($Title) = @_;
	my ($Key, $Time, $Num, $Host, $ByDups, $ByRate, $Total, $RowColor);

	&PrintTableHeader ($Title);

	if ($TextFormat) {
		printf FILE "\n%-50s %8d %8d %8d\n\n",
			"Host", "By Post Rate", "By Dup Body", "# Articles";
	} else {
		print FILE "<tr><th $TableColor>Host</th><th $TableColor>By Post Rate</th><th $TableColor>By Dup Body</th><th $TableColor># Articles</th></tr>\n";
	}

	$Num = 0;
	foreach $Key (reverse sort keys %OrderBySpamTotal) {
		if ($Num < $MaxSpamList) {

			$Host = $OrderBySpamTotal{$Key};
	
			$Total = $SpamList{$Host}->{Total};
			$ByRate = $SpamList{$Host}->{ByRate};
			$ByDups = $SpamList{$Host}->{ByDups};
#			print "SPAM=[$Key] $Host\n";

			if ($Total) {
				if ($TextFormat) {
					printf FILE "%-50s %8d %8d %8d\n",
						$Host, $ByRate, $ByDups, $Total;
				} else {	
					$RowColor = &GetRowColor ($RowColor);
					printf FILE "<tr bgcolor=\"$RowColor\">%s&nbsp;%s%s %s%d%s %s%d%s %s%d%s</tr>\n",
						$TDL, $Host, "</td>",
						$TDR, $ByRate, "</td>",
						$TDR, $ByDups, "</td>",
						$TDR, $Total, "</td>";
				}
			}
			$Num++;
		} else {
			goto SpamDone;
		}
	}
SpamDone:

	&PrintTableFooter ("");
}


sub PrintTableHeader
{
	my ($Title) = @_;

	if ($TextFormat) {
		print FILE "$Title\n";
	} else {
		print FILE <<EOT
<h3 align=center>$Title</h3>
<table border=2 width="100%">
EOT
;
	}
}


sub PrintTableFooter
{
	my ($Comment) = @_;

	if (! $TextFormat) {
		print FILE "</table>\n";
		print FILE "<!-- $Comment -->\n" if $Comment ne "";
	}
}


sub PrintOutHeader
{
	my ($TimePeriod) = $BegDate;
	my ($Title, $Title2);
	my ($Rank) = &GetTop1000 ($NewsHost);

	if ($BegDate ne $EndDate) {
		$TimePeriod = "$BegDate - $EndDate";
	} else {
		$TimePeriod = &GetFancyNameDate ($BegDate, 1, 0);
	}

	if ($Rank eq "") {
		$Title = "$TextUrl statistics for $NewsHost on $TimePeriod";
		$Title2 = "<a href=\"$NameUrl\">$TextUrl</a>  statistics for <i>$NewsHost</i> on $TimePeriod";
	} else {
		$Title = "$TextUrl statistics for $NewsHost (Top1000 $Rank) on $TimePeriod";
		$Title2 = "<a href=\"$NameUrl\">$TextUrl</a>  statistics for <i>$NewsHost</i> (Top1000 $Rank) on $TimePeriod";
	}

	if ($TextFormat) {
		print FILE "$Title\n\n\n";
	} else {
		unless ( $EmbeddedHTML ) {
			print FILE <<EOT
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN" "http://www.w3.org/TR/REC-html40/loose.dtd">
<html>
<head>
<title>$Title</title>
<meta http-equiv="Refresh" content="$MetaRefresh">
<meta http-equiv="Expires" content="$MetaExpires">
<meta http-equiv="Content-Type" content="text/html; charset=$MetaCharset"> 
</head>
<body $BgColor>
EOT
;
		}
		print FILE <<EOT
<a name="00"></a>
<table>
<tr>
<td><a href="$WwwUrl"> <img $ImgInfo alt="$ImgText" src="$ImgLogo"></a></td>
<td><h3>$Title2</h3></td>
</tr>
</table>
EOT
;
		if ($NewsContactName) {
			print FILE "Newsfeed Contact: <a href=\"mailto:$NewsContactMail\">$NewsContactName</a>\n<p>\n";
		}
		&PrintOutCopyright;
		print FILE <<EOT
<hr>
<p>
<ol>
<li><a href="#01">Summary of Incoming Feeds by Article</a>
<li><a href="#02">Summary of Incoming Feeds by Volume</a>
<li><a href="#03">Summary of Incoming Feeds by Estimated Rejected Volume</a>
<li><a href="#04">Summary of Incoming Feeds by Time</a>
<li><a href="#05">Summary of Outgoing Feeds by Article</a>
<li><a href="#06">Summary of Outgoing Feeds by Volume</a>
<li><a href="#07">Summary of Outgoing Feeds by Estimated Rejected Volume</a>
<li><a href="#08">Summary of Outgoing Feeds by Time</a>
EOT
;
		if ($DiabloFormat) {
			print FILE <<EOT
<li><a href="#09">Summary of Outgoing Queue</a>
<li><a href="#10">Summary of Curious Activity</a>
EOT
;
			unless ( $readerconnect == 0 ) {
				print FILE <<EOT
<li><a href="#11">Summary of Reader Activity</a>
EOT
			}
			unless ( $TotalSpamList == 0 ) {
				print FILE <<EOT
<li><a href="#12">Summary of Spam Activity (Top $TopSpamList of $TotalSpamList)</a>
EOT
;
			}
		}
		print FILE "</ol>\n";

		chop (($OutputUptime = `$CmdUptime`));
		chop (($OutputDf = `$CmdDf`));
		chop (($OutputDiablo = `$CmdDiablo`));
		chop (($OutputDnewslink = `$CmdDnewslink`));
		$OutputDiablo =~ s/ //g;
		$OutputDnewslink =~ s/ //g;

		if ($DiabloFormat) {
			$DiabloProcs = "Diablo running processes:  $OutputDiablo    Dnewslink running processes:  $OutputDnewslink";
			$SysInfo = "$DiabloProcs\n\n$DiabloUptime\n\n$OutputUptime\n\n$OutputDf";
		} else {
			$SysInfo = "$OutputUptime\n\n$OutputDf";
		}

		print FILE <<EOT
<hr>
<p>
<pre>
$SysInfo
</pre>
EOT
;

	}
}


sub PrintOutFooter
{
	&PrintOutCopyright;

	if (! $TextFormat) {
		print FILE <<EOT
<a href="http://validator.w3.org/check/referer"> <img src="http://validator.w3.org/images/vh40.gif"
  height=31 width=88 border=0 alt="Valid HTML 4.0!"></a>
EOT
;
		unless ( $EmbeddedHTML ) {
			print FILE <<EOT
</body>\n</html>
EOT
;
		}
	}
}


sub PrintOutCopyright
{
	if ($TextFormat) {
		print FILE "\nGenerated on $GenDate $GenTime by $ScriptName $Version. Copyright (C) 2002 The Diablo Project (http://www.openusenet.org/diablo/)\n";
	} else {
		print FILE <<EOT
Generated on $GenDate $GenTime by
<a href="$FtpUrl">$ScriptName $Version</a>.
Copyright &copy; 2002
<a href="http://www.openusenet.org/diablo/">The Diablo Project</a>.
EOT
;
	}
}


sub GetYyMmDd
{
	my ($Date) = @_;

	# Apr 10 11:36:54
	if ($Date =~ /^(...) (..)/) {
		return sprintf "%04d%02d%02d", $GenYear, $MonthNumByName{$1}, $2;
	} else {
		return "";
	}
}


sub GetHhMmSs
{
	my ($Secs) = @_;
#	my ($Time) = "00:00:00";
	my ($Time) = "00:00";
	my ($Hh, $Mm, $Ss);

	if ($Secs) {
		$Hh = int($Secs / 3600);
		$Mm = int(($Secs - ($Hh * 3600)) / 60);
		$Ss = $Secs % 60;

#		$Time = sprintf "%02d:%02d:%02d", $Hh, $Mm, $Ss;
		$Time = sprintf "%02d:%02d", $Hh, $Mm;
	}

#	print "Secs=[$Secs] Time=[$Time]\n";

	return $Time;
}


sub GetFancyDate
{
	my ($Date) = @_;

	if (/^(....)(..)(..)/) {
		$Date = sprintf "%s %s %s", $3, $MonthNameByNum{$2}, $1;
	}

	return $Date;
}


# $Date = &GetFancyNameDate ("19960109", [0|1], [0|1]);
# par2: 0 = Long / 1 = Short form of dayname ie. Monday / Mon
# par3: 0 = not split / 1 = split returned line ie. Mon\n14 Oct 95

sub GetFancyNameDate
{
	local ($CurDate, $ShortName, $SplitLine) = @_;
	local ($Break, $DayName);

	if ($SplitLine) {
		$Break = '<br>';
	} else {
		$Break = ' ';
	}
	$CurDate =~ /^(....)(..)(..)/;
	$DayName = &GetDayOfWeek ($1, $2, $3, $ShortName);

	return "$DayName$Break$3 $MonthNameByNum{$2} $1";
}


sub GetDayOfWeek
{
	# Parameters: YYYY, MM, DD, [0|1]
	my ($Yy, $Mm, $Dd, $ShortName) = @_;
	my ($DayNum);

	if ($Yy && $Mm && $Dd) {
		$DayNum = (localtime(timelocal(0,0,0,$Dd,$Mm-1,$Yy,0,0)))[6];

		if ($ShortName) {
			return $DayNameShort[$DayNum];
		} else {
			return $DayName[$DayNum];
		}
	} else {
		return "ERROR";
	}
}


sub GetDailyStats
{
	my ($Date) = @_;
	my ($InErr, $InRej, $InSpam, $InTot, $InVol, $InRejVol, $OutDup, $OutRej, $OutAccepted, $OutVol, $OutRejVol);

	$InErr = $InRej = $InSpam = $InTot = $InVol  = $InRejVol = $OutDup = $OutRej = $OutAccepted = $OutVol = $OutRejVol = 0;

	if (open (STATS, "$Date.html")) {
		while (<STATS>) {
			if (/INCOMING ERR=([\ 0-9]*) REJ=([\ 0-9]*) SPAM=([\ 0-9]*) ADD=([\ 0-9]*) VOL=([\ 0-9]*) TooOld=[\ 0-9]* REJVOL=([\ 0-9]*)/) {
				$InErr = $1;
				$InRej = $2;
				$InSpam = $3;
				$InTot = $4;
				$InVol = $5;
				$InRejVol = $6;
			} elsif (/INCOMING ERR=([\ 0-9]*) REJ=([\ 0-9]*) SPAM=([\ 0-9]*) ADD=([\ 0-9]*) VOL=([\ 0-9]*)/) {
				$InErr = $1;
				$InRej = $2;
				$InSpam = $3;
				$InTot = $4;
				$InVol = $5;
				$InRejVol = -1;
			} elsif (/INCOMING ERR=([\ 0-9]*) REJ=([\ 0-9]*) ADD=([\ 0-9]*) VOL=([\ 0-9]*)/) {
				$InErr = $1;
				$InRej = $2;
				$InTot = $3;
				$InVol = $4;
				$InRejVol = -1;
			} elsif (/INCOMING ERR=([\ 0-9]*) REJ=([\ 0-9]*) ADD=([\ 0-9]*)/) {	# < v1.10
				$InErr = $1;
				$InRej = $2;
				$InTot = $3;
				$InRejVol = -1;
			}
			if (/OUTGOING DUP=([\ 0-9]*) REJ=([\ 0-9]*) ACC=([\ 0-9]*) VOL=([\ 0-9]*) REJ VOL=([\ 0-9]*)/) {
				$OutDup = $1;
				$OutRej = $2;
				$OutAccepted = $3;
				$OutVol = $4;
				$OutRejVol = $5;
			} elsif (/OUTGOING DUP=([\ 0-9]*) REJ=([\ 0-9]*) ACC=([\ 0-9]*)/) {	# < v1.10
				$OutDup = $1;
				$OutRej = $2;
				$OutAccepted = $3;
			}
		}
		close STATS;
	} else {
		warn "Error: $Date.html - $!\n";
	}
	
	return ($InErr, $InRej, $InTot, $InVol, $OutDup, $OutRej, $OutAccepted, $OutVol, $InSpam, $OutRejVol, $InRejVol);
}


sub GetPercent
{
	my ($Total, $SubTotal) = @_;

	return ($SubTotal && $Total) ? ($SubTotal / $Total) * 100 : 0; 	
}


# $Date
# $Host
#
# DIABLO      CYCLONE
# ------      -------
# $InSecs     -1
# $InAdded    $InAccepted
# $InBytes    $InBytes
# $InChk      $InOffered
# $InIhave    -1
# $InSpam     -1
# $InRej      $InRejected
# $InErr      $InRefused
# -1          $InConnAtt
# -1          $InConnSuc
# $InRejBytes -1
#
sub AddToInTimeList
{
	my ($Date, $Host, $InSecs, $InAdded, $InBytes, $InChk, $InIhave,
		$InSpam, $InRej, $InErr, $InConnAtt, $InConnSuc, $InRejBytes) = @_;

# print "IN:  $Host, $InSecs, $InAdded, $InBytes, $InChk, $InIhave, $InSpam, $InRej, $InErr\n";

	# ... 23:59:59
	if ($Date =~ / (..):..:../) {
		$Hour = $1;
		$Feeds{$Host}->{$Hour}->{InAdded} += $InAdded;
		$Feeds{$Host}->{$Hour}->{InBytes} += $InBytes;
		$Feeds{$Host}->{$Hour}->{InChk} += $InChk;
		$Feeds{$Host}->{$Hour}->{InIhave} += $InIhave;
		$Feeds{$Host}->{$Hour}->{InSpam} += $InSpam;
		$Feeds{$Host}->{$Hour}->{InRej} += $InRej;
		$Feeds{$Host}->{$Hour}->{InRejBytes} += $InRejBytes;
		$Feeds{$Host}->{$Hour}->{InErr} += $InErr;
		if ($DiabloFormat) {
			if ($InAdded || $InBytes) {
				$Feeds{$InHost}->{$Hour}->{InSecs} += $InSecs;
			}
		} else {
			$Feeds{$Host}->{$Hour}->{InConnAtt} += $InConnAtt;
			$Feeds{$Host}->{$Hour}->{InConnSuc} += $InConnSuc;
			$Feeds{$InHost}->{$Hour}->{InSecs} = 1;
		}

# print "IN: $Hour  $Host  $Feeds{$Host}->{$Hour}->{InSecs}  $Feeds{$Host}->{$Hour}->{InAdded}  $Feeds{$Host}->{$Hour}->{InBytes}\n";
	}
}


# $Date
# $Host
#
# DIABLO        CYCLONE
# ------        -------
# $OutSecs      -1
# $OutAccepted  $OutAccepted
# $OutBytes     $OutBytes
# $OutDup       ??? $OutRefused ???
# $OutRej       $OutRejected
# -1            $OutAttempted
# -1            $OutOffered
# -1            $OutDropped
# -1            $OutExpired
# -1            $OutBacklog
# -1            $OutConnAtt
# -1            $OutConnSuc
# $OutRejBytes  -1
#
sub AddToOutTimeList
{
	my ($Date, $Host, $OutSecs, $OutAccepted, $OutBytes, $OutDup, $OutRej,
		$OutAttempted, $OutOffered, $OutDropped, $OutExpired, $OutBacklog,
		$OutConnAtt, $OutConnSuc, $OutRejBytes) = @_;

	# ... 23:59:59
	if ($Date =~ / (..):..:../) {
		$Hour = $1;
		$Feeds{$Host}->{$Hour}->{OutAccepted} += $OutAccepted;
		$Feeds{$Host}->{$Hour}->{OutBytes} += $OutBytes;
		$Feeds{$Host}->{$Hour}->{OutDup} += $OutDup;
		$Feeds{$Host}->{$Hour}->{OutRej} += $OutRej;
		$Feeds{$Host}->{$Hour}->{OutSecs} += $OutSecs;
		$Feeds{$Host}->{$Hour}->{OutRejBytes} += $OutRejBytes;
		if (! $DiabloFormat) {
			$Feeds{$Host}->{$Hour}->{OutAttempted} += $OutAttempted;
			$Feeds{$Host}->{$Hour}->{OutOffered} += $OutOffered;
			$Feeds{$Host}->{$Hour}->{OutDropped} += $OutDropped;
			$Feeds{$Host}->{$Hour}->{OutExpired} += $OutExpired;
			$Feeds{$Host}->{$Hour}->{OutBacklog} += $OutBacklog;
			$Feeds{$Host}->{$Hour}->{OutConnAtt} += $OutConnAtt;
			$Feeds{$Host}->{$Hour}->{OutConnSuc} += $OutConnSuc;
		}
	}
}


sub AddToErrorList
{
	my ($Error, $FullDate, $LineDate) = @_;

	$ErrorList{$Error}->{Tot} += 1;
	if ($ErrorList{$Error}->{BegDate} eq "") {
		$ErrorList{$Error}->{BegDate} = $FullDate;
	} else {
		$ErrorList{$Error}->{EndDate} = $FullDate;
	}

	print "$LineDate  $Error\n" if $Debug;
}


sub AddToSpamList
{
	my ($Spam, $FullDate, $LineDate) = @_;
	my ($Num, $Host, $MsgId, $ByDups, $ByRate, $ByFeed);

	$ByDups = $ByRate = $ByFeed = 0;

	# ... by-post-rate copy #33: <5rplh4$12p@ron.ipa.com> 206.1.57.29
	# ... by-dup-body copy #1: <5rpvin$8ro$1@telemedia.de> essn.mediaways.net
	# ... dnewsfeeds copy #-1: <5r4dpg$le8$1@news.utu.fi> apus.astro.utu.fi
	if ($Spam =~ /by-post-rate copy #([\-0-9]*):\s+([A-z0-9\.\-_\<>\$@]*)\s+([A-z0-9\.\-_]*)/) {
		$Num = $1;
		$MsgId = $2;
		$Host = $3;
		$ByRate = 1;
	} elsif ($Spam =~ /by-dup-body copy #([\-0-9]*):\s+([A-z0-9\.\-_\<>\$@]*)\s+([A-z0-9\.\-_]*)/) {
		$Num = $1;
		$MsgId = $2;
		$Host = $3;
		$ByDups = 1;
	} elsif ($Spam =~ /dnewsfeeds copy #([\-0-9]*):\s+([A-z0-9\.\-_\<>\$@]*)\s+([A-z0-9\.\-_]*)/) {
		$Num = $1;
		$MsgId = $2;
		$Host = $3;
		$ByFeed = 1;
	}
	if ($ByRate || $ByDups || $ByFeed) {
		$SpamList{$Host}->{Total} += 1;
		$SpamList{$Host}->{ByRate} += $ByRate;
		$SpamList{$Host}->{ByDups} += $ByDups;
		$SpamList{$Host}->{ByFeed} += $ByFeed;

		print "SPAM:  Type=$ByDups ($SpamList{$Host}->{ByDups})| $ByRate ($SpamList{$Host}->{ByRate})| $ByFeed ($SpamList{$Host}->{ByFeed})  MsgID=$MsgId  Site=$Host\n" if $Debug;
	}
}


sub GetVolume
{
	# This is disgusting, but fcmp does lexical compares of floats expressed in
	# scientific notation, which is completely useless... If I knew more Perl I
	# could probably compute <X>Int from <X> instead of having duplicate defines.

#print "GetVolume(@_)\n";

	$_[0] += 0;		# In case $_[0] is empty

	my ($Bytes) = new Math::BigFloat @_;
	my ($BytesInt) = new Math::BigInt @_;
	my ($Vol) = new Math::BigFloat "0.0";
	my ($Tb) = new Math::BigFloat "1099511627776.0";
	my ($TbInt) = new Math::BigInt "1099511627776";
	my ($Gb) = new Math::BigFloat "1073741824.0";
	my ($GbInt) = new Math::BigInt "1073741824";
	my ($Mb) = new Math::BigFloat "1048576.0";
	my ($MbInt) = new Math::BigInt "1048576";
	my ($Kb) = new Math::BigFloat "1024.0";
	my ($KbInt) = new Math::BigInt "1024";

#print "Bytes=[$Bytes]  BytesInt=[$BytesInt]";

	if ($BytesInt > $TbInt) {
		$Sign = "TB";
		$Vol = $Bytes / $Tb;
	} elsif ($BytesInt > $GbInt) {
CalcGB:
		$Sign = "GB";
		$Vol = $Bytes / $Gb;
	} elsif ($BytesInt > $MbInt) {
CalcMb:
		$Sign = "MB";
		$Vol = $Bytes / $Mb;
	} else {
CalcKb:
		$Sign = "KB";
		$Vol = $Bytes / $Kb;
	}

#printf "Num=$Bytes  Format(%g)=%3.2f%s\n", $Vol, $Vol, $Sign;

	$Vol = sprintf "%3.2f%s", $Vol, $Sign;

	return $Vol;
}


sub GetKbps
{
	$_[0] += 0; # In case @_[0] bytes is empty

	my ($KBytes) = new Math::BigFloat ($_[0] / 1024);
	my ($Secs) = new Math::BigFloat $_[1];
	my ($Zero) = new Math::BigFloat 0.0;
	my ($One) = new Math::BigFloat 1.0;
	my ($Eight) = new Math::BigFloat 8.0;
        my ($Kbps) = new Math::BigFloat 0.0;
	if ($Secs > $Zero) {
		$Kbps = new Math::BigFloat (($KBytes * $Eight) / $Secs);
	} else {
		$Kbps = new Math::BigFloat ($KBytes * $Eight);
	}

#print "KBytes=[$KBytes]  Secs=[$Secs]  Kbps=[$Kbps]\n";

	return $Kbps;
}


sub GetAvgArtSize
{
	$_[0] += 0; # In case @_[0] bytes is empty

	my ($KBytes) = new Math::BigFloat ($_[0] / 1024);
	my ($NumArts) = $_[1];
	my ($Size);

	if ($NumArts) {
		$Size = sprintf "%3.2f", $KBytes / $NumArts;
	} else {
		$Size = sprintf "%3.2f", 0;
	}

# print "NEW:  KBytes=[$KBytes]  NumArts=[$NumArts]  KbSize=[$Size]\n";

	return $Size;
}


sub GetQueueHost
{
	my ($QHost) = @_;
	my ($QLine, $First, $Second, $Rest);

	$QHost =~ s/[ \t]+//;

	if ($QueueHost) {
		if ( open (QFILE, $CtlFile) ) {

			do {
				$QLine = <QFILE>;
				chop ($QLine);
				($First, $Second, $Rest) = split(/[ \t]+/, $QLine, 3);

			} until ($First eq $QHost || eof);

			close (QFILE);
		} else {
			#
			# Perhaps dnewsfeeds is being used instead of dnntpspool.ctl.
			# Try running doutq to substitute for it.
			#
			if (open (FD, "$QueueCmd -h $QHost |")) {
				while (<FD>) {
					($First, $Second) = /^(\S+)\s+(\S+)/;
				}
				close (FD) ;
			}
		}

		if ($Second eq '') {
			$Rest = $QHost;
		}
		$Rest = $Second.'  ('.$QHost.')';

		return $Rest;
	} else {
		return $QHost;
	}
}


sub MakeVolKey
{
	my ($Bytes, $Num) = @_;

printf STDERR "BEFORE: Bytes=$Bytes, Num=$Num\t" if $Debug;

	until (length($Bytes) > 11) {
		$Bytes = "0" . $Bytes;
	}
	until (length($Num) > 2) {
		$Num = "0" . $Num;
	}

printf STDERR " AFTER: Bytes=$Bytes, Num=$Num\n" if $Debug;

	return "$Bytes.$Num";
}


sub GetHostLink
{
	my ($Host) = @_;
	my ($Link, $HostLink, $Pos1000);

	$HostLink = $Host;
	if ($HostLink) {
		$Link = $HostLinkList{lc $Host};

		if ($Link) {
			$HostLink = sprintf ("<a href=\"%s\">%s</a>", $Link, $Host);
		}
		print "HOST:  Host=[$Host]  Link=[$Link]  HostLink=[$HostLink]\n" if $Debug;
	}

	return $HostLink;
}


sub ReadCfgFile
{
	if (open (FILE, $CfgFile)) {
		while (<FILE>) {
			if
			(/^link:\s+([A-z0-9_\.\/\-\~:]+)\s+([A-z0-9_\.\/\-\~\?=:]+)\n/) {
				$Host = lc $1;
				$Link = $2;
				$HostLinkList{$Host} = $Link;
				print "LINK:  [$Host]==[$HostLinkList{$Host}]\n" if $Debug;
			}
			if (/^top1000:\s+([A-z0-9_\.\/\-\~:]+)\s+([A-z0-9_\.\/\-\~:]+)\n/) {
				$Host = lc $1;
				$TopHost = $2;
				$Top1000Host{$Host} = $TopHost;
				print "TOP1000:  [$Host]==[$Top1000Host{$Host}]\n" if $Debug;
			}
			if (/^alias:\s+([A-z0-9_\.\/\-\~:]+)\s+([A-z0-9_\.\/\-\~:]+)\n/) {
				$Host = lc $1;
				$Alias = $2;
				$Aliases{$Host} = $Alias;
				print "ALIAS:  [$Host]==[$Aliases{$Host}]\n" if $Debug;
			}
		}
		close FILE;
	}
}


sub CalcHourlyStats
{
	my ($Num, $Hour);

	print "Calculating hourly statistics ...\n";

	for ($Num = 0; $Num < 24; $Num++) {
		$Hour = sprintf ("%02d", $Num);

		foreach $Host (keys %Feeds) {
			# Incoming feeds
			$HourlyStats{$Hour}->{InAdded} += $Feeds{$Host}->{$Hour}->{InAdded};
			$HourlyStats{$Hour}->{InBytes} +=  $Feeds{$Host}->{$Hour}->{InBytes};
			$HourlyStats{$Hour}->{InChk} += $Feeds{$Host}->{$Hour}->{InChk};
			$HourlyStats{$Hour}->{InIhave} += $Feeds{$Host}->{$Hour}->{InIhave};
			$HourlyStats{$Hour}->{InSpam} += $Feeds{$Host}->{$Hour}->{InSpam};
			$HourlyStats{$Hour}->{InRej} += $Feeds{$Host}->{$Hour}->{InRej};
			$HourlyStats{$Hour}->{InRejBytes} += $Feeds{$Host}->{$Hour}->{InRejBytes};
			$HourlyStats{$Hour}->{InErr} += $Feeds{$Host}->{$Hour}->{InErr};

			# reset if multiple connections have more than 1 hour walltime
			if ($Feeds{$Host}->{$Hour}->{InSecs} > 3600) {
# print "IN  SECS: [$Hour]  [$Host]  Secs=[$Feeds{$Host}->{$Hour}->{InSecs}]\n";
				$Feeds{$Host}->{$Hour}->{InSecs} = 3600;
			}
			$Feeds{$Host}->{InSecs} += $Feeds{$Host}->{$Hour}->{InSecs};

			# Outgoing feeds
			$HourlyStats{$Hour}->{OutAccepted} += $Feeds{$Host}->{$Hour}->{OutAccepted};
			$HourlyStats{$Hour}->{OutBytes} += $Feeds{$Host}->{$Hour}->{OutBytes};
			$HourlyStats{$Hour}->{OutDup} += $Feeds{$Host}->{$Hour}->{OutDup};
			$HourlyStats{$Hour}->{OutRej} += $Feeds{$Host}->{$Hour}->{OutRej};
			$HourlyStats{$Hour}->{OutRejBytes} += $Feeds{$Host}->{$Hour}->{OutRejBytes};

			# reset if multiple connections have more than 1 hour walltime
			if ($Feeds{$Host}->{$Hour}->{OutSecs} > 3600) {
# print "OUT SECS: [$Hour]  [$Host]  Secs=[$Feeds{$Host}->{$Hour}->{OutSecs}]\n";
				$Feeds{$Host}->{$Hour}->{OutSecs} = 3600;
			}
			$Feeds{$Host}->{OutSecs} += $Feeds{$Host}->{$Hour}->{OutSecs};

# print "HOST: [$Hour]  [$Host]  [$HourlyStats{$Hour}->{InAdded}]  [$Feeds{$Host}->{$Hour}->{InAdded}]\n";
		}
		# Incoming feeds
		$HourlyStats{Total}->{InAdded} += $HourlyStats{$Hour}->{InAdded};
		$HourlyStats{Total}->{InBytes} += $HourlyStats{$Hour}->{InBytes};
		$HourlyStats{Total}->{InChk} += $HourlyStats{$Hour}->{InChk};
		$HourlyStats{Total}->{InIhave} += $HourlyStats{$Hour}->{InIhave};
		$HourlyStats{Total}->{InSpam} += $HourlyStats{$Hour}->{InSpam};
		$HourlyStats{Total}->{InRej} += $HourlyStats{$Hour}->{InRej};
		$HourlyStats{Total}->{InRejBytes} += $HourlyStats{$Hour}->{InRejBytes};
		$HourlyStats{Total}->{InErr} += $HourlyStats{$Hour}->{InErr};

		# Outgoing feeds
		$HourlyStats{Total}->{OutAccepted} += $HourlyStats{$Hour}->{OutAccepted};
		$HourlyStats{Total}->{OutBytes} += $HourlyStats{$Hour}->{OutBytes};
		$HourlyStats{Total}->{OutDup} += $HourlyStats{$Hour}->{OutDup};
		$HourlyStats{Total}->{OutRej} += $HourlyStats{$Hour}->{OutRej};
		$HourlyStats{Total}->{OutRejBytes} += $HourlyStats{$Hour}->{OutRejBytes};

#		printf "HOUR: [$Hour]  Acc=%d Vol=%ld Chk=%d Ihave=%d Spam=%d Rejs=%d Errs=%d\n",
#			$HourlyStats{$Hour}->{InAdded}, $HourlyStats{$Hour}->{InBytes},
#			$HourlyStats{$Hour}->{InChk}, $HourlyStats{$Hour}->{InIhave},
#			$HourlyStats{$Hour}->{InSpam}, $HourlyStats{$Hour}->{InRej},
#			$HourlyStats{$Hour}->{InErr};
	}
#	printf "TOTAL:  Acc=%d Vol=%ld Chk=%d Ihave=%d Spam=%d Rejs=%d Errs=%d\n",
#		$HourlyStats{Total}->{InAdded}, $HourlyStats{Total}->{InBytes},
#		$HourlyStats{Total}->{InChk}, $HourlyStats{Total}->{InIhave},
#		$HourlyStats{Total}->{InSpam}, $HourlyStats{Total}->{InRej},
#		$HourlyStats{Total}->{InErr};

}


sub MakeIncomingByTimePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my ($Num, $Hour, $Graph);
	my (@LegendLabels) = ('Accepted', 'Spam', 'Rejected', 'Errors');
	my (@Data1) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my (@Data2) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my (@Data3) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my (@Data4) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);

	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);

		if ($HourlyStats{$Hour}->{InAdded}) {
			$Data1[$Num] = $HourlyStats{$Hour}->{InAdded};
		} else {
			$Data1[$Num] = 0;
		}
		if ($HourlyStats{$Hour}->{InSpam}) {
			$Data2[$Num] = $HourlyStats{$Hour}->{InSpam};
		} else {
			$Data2[$Num] = 0;
		}
		if ($HourlyStats{$Hour}->{InRej}) {
			$Data3[$Num] = $HourlyStats{$Hour}->{InRej};
		} else {
			$Data3[$Num] = 0;
		}
		if ($HourlyStats{$Hour}->{InErr}) {
			$Data4[$Num] = $HourlyStats{$Hour}->{InErr};
		} else {
			$Data4[$Num] = 0;
		}
	}

	$Graph->add_dataset (@Data1);
	$Graph->add_dataset (@Data2);
	$Graph->add_dataset (@Data3);
	$Graph->add_dataset (@Data4);
	$Graph->set ('legend_labels' => \@LegendLabels);

#	print "TIME PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}


sub MakeIncomingByArticlePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByInAdded) {
		$Host = $OrderByInAdded{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{InAdded}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{InAdded};
				} else {
					$Data[$Num] = 0;
				}
#				$Total[$Num] += $Data[$Num];
# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{InAdded}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{InAdded}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto IncomingByArticleDone;
		}
#		$Total[$Num] += $Feeds{$Host}->{$Hour}->{InAdded};
	}
IncomingByArticleDone:

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);
		$Total[$Num] = $HourlyStats{$Hour}->{InAdded};
	}

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "ARTS PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeIncomingByVolumePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByInBytes) {
		$Host = $OrderByInBytes{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{InBytes}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{InBytes} / 1024;  # KB
				} else {
					$Data[$Num] = 0;
				}
				$Total[$Num] += $Data[$Num];

# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{InBytes}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{InAdded}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto IncomingByVolumeDone;
		}
	}
IncomingByVolumeDone:

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "VOL PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeIncomingByRejectVolumePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByInRejBytes) {
		$Host = $OrderByInRejBytes{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{InRejBytes}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{InRejBytes} / 1024;  # KB
				} else {
					$Data[$Num] = 0;
				}
				$Total[$Num] += $Data[$Num];

# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{InRejBytes}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{InAdded}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto IncomingByRejectVolumeDone;
		}
	}
IncomingByRejectVolumeDone:

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "Reject Volume PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeOutgoingByTimePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my ($Num, $Hour, $Graph);
	my (@LegendLabels) = ('Accepted', 'Duplicates', 'Rejected');
	my (@Data1) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my (@Data2) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my (@Data3) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);

	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);

		if ($HourlyStats{$Hour}->{OutAccepted}) {
			$Data1[$Num] = $HourlyStats{$Hour}->{OutAccepted};
		} else {
			$Data1[$Num] = 0;
		}
		if ($HourlyStats{$Hour}->{OutDup}) {
			$Data2[$Num] = $HourlyStats{$Hour}->{OutDup};
		} else {
			$Data2[$Num] = 0;
		}
		if ($HourlyStats{$Hour}->{OutRej}) {
			$Data3[$Num] = $HourlyStats{$Hour}->{OutRej};
		} else {
			$Data3[$Num] = 0;
		}
	}

	$Graph->add_dataset (@Data1);
	$Graph->add_dataset (@Data2);
	$Graph->add_dataset (@Data3);
	$Graph->set ('legend_labels' => \@LegendLabels);

#	print "TIME PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeOutgoingByArticlePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByOutAccepted) {
		$Host = $OrderByOutAccepted{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{OutAccepted}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{OutAccepted};
				} else {
					$Data[$Num] = 0;
				}
#				$Total[$Num] += $Data[$Num];
# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{OutAccepted}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{OutAccepted}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto OutgoingByArticleDone;
		}
#		$Total[$Num] += $Feeds{$Host}->{$Hour}->{OutAccepted};
	}
OutgoingByArticleDone:

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);
		$Total[$Num] = $HourlyStats{$Hour}->{OutAccepted};
	}

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "ARTS PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeOutgoingByVolumePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByOutBytes) {
		$Host = $OrderByOutBytes{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{OutBytes}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{OutBytes} / 1024;  # KB
				} else {
					$Data[$Num] = 0;
				}
				# $Total[$Num] += $Data[$Num];

# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{OutBytes}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{OutAccepted}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto OutgoingByVolumeDone;
		}
	}
OutgoingByVolumeDone:

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);
		$Total[$Num] = $HourlyStats{$Hour}->{OutBytes} / 1024;
	}

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "VOL PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub MakeOutgoingByRejectVolumePNG
{
	my ($File, $Title, $WidthPNG, $HeightPNG) = @_;
	my (@Data, $Num, $Hour, $Graph, $Key, $Host, $NumLinesPNG) = 0;
	my (@LegendLabels);
	my (@Total) = (
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef,
		undef, undef, undef, undef, undef, undef, undef, undef);
	my ($CurrHourForPNG) = &GetCurrHourForPNG ($CurrHour, $CurrMin);

	$Graph = Chart::Lines->new ($WidthPNG, $HeightPNG);
	$Graph->set ('x_label' => $Title);
	$Graph->set ('transparent' => 'true');
	$Graph->set ('grid_lines' => 'true');
	$Graph->set ('legend_placement' => 'bottom');
	$Graph->add_dataset (@XLabelHours);

	foreach $Key (reverse sort keys %OrderByOutRejBytes) {
		$Host = $OrderByOutRejBytes{$Key};

		if ($NumLinesPNG < $MaxLinesPNG) {
			$LegendLabels[$NumLinesPNG] = $Host;
			@Data = (
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef,
				undef, undef, undef, undef, undef, undef, undef, undef);
			for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
				$Hour = sprintf ("%02d", $Num);
	
				if ($Feeds{$Host}->{$Hour}->{OutBytes}) {
					$Data[$Num] = $Feeds{$Host}->{$Hour}->{OutRejBytes} / 1024;  # KB
				} else {
					$Data[$Num] = 0;
				}
				# $Total[$Num] += $Data[$Num];

# printf STDERR "TEST [$Host] [$Hour] [$Feeds{$Host}->{$Hour}->{OutBytes}]\n";
			}
# printf STDERR "HOST: [%s]  [%s]\n", $Host, join (" ", @Data);
			if ($Feeds{$Host}->{OutAccepted}) {
				$Graph->add_dataset (@Data);
				$NumLinesPNG++;
			}
		} else {
			goto OutgoingByRejVolumeDone;
		}
	}
OutgoingByRejVolumeDone:

	for ($Num = 0; $Num < $CurrHourForPNG; $Num++) {
		$Hour = sprintf ("%02d", $Num);
		$Total[$Num] = $HourlyStats{$Hour}->{OutRejBytes} / 1024;
	}

	$LegendLabels[$NumLinesPNG] = "TOTAL (all feeds)";
	$Graph->set ('legend_labels' => \@LegendLabels);
	$Graph->add_dataset (@Total);

#	print "VOL PNG=$File\n" if $Verbose;

	$Graph->png ($File);

	&CreateMetaFile ($File, $MetaExpires);
}

sub PrintUrlPNG
{
	my ($FilePNG) = @_;

	if ($GraphMode) {
		print FILE <<EOT
<p>
<table border=2 align=center>
<tr>
<td><img src="$FilePNG" width=$WidthPNG height=$HeightPNG alt="Graph"></td>
</tr>
</table>
<br>
EOT
;
	}
}


sub CreateMetaFile
{
	if ($MetaMode == 0) {
		my ($File, $Time) = @_;
		my ($MetaFile) = "$File.meta";

		if (open (META, ">$MetaFile")) {
			print META "Expires: $Time\n";
			close META;
		}
	}
}


sub ParseTop1000
{
	my ($File) = "$Top1000Dir/current";
	my ($Index);

	print "FILE: $File\n" if $Debug;

	if (open (FILE, $File)) {
		print "Parsing Top1000 file $File ...\n" if $Verbose;
		while (<FILE>) {
			if (/^\s+(\d+)\s+[0-9\.]*\s+(.*)/o) {
				print "$1  $2\n" if $Debug;
 				$Index = lc $2;
 				$Top1000List{$Index} = $1;
			}
		}
		close (FILE);
	}
}


sub GetTop1000
{
	my ($Host) = @_;
	my ($TopHost, $CurrPos1000, $Pos1000, $i) = "";
	my @Aliases;

	$Host = lc $Host;
	$TopHost = $Top1000Host{$Host} ? $Top1000Host{$Host} : $Host;

	if ($Top1000List{$TopHost})
	{
		$Pos1000 = "$Top1000List{$TopHost}";
		print "POS: Host = $Host  TopHost = $TopHost  Pos = $Pos1000\n"
			if $Debug;
	}
	#
	#	This host may have multiple path aliases.
	#	Try them all.
	#
	@Aliases = split(/:/, $TopAliases{$Host});
	foreach $i (@Aliases)
	{
		$i = lc $i;
		if ($CurrPos1000 = $Top1000List{$i})
		{
			print "Trying i = $i ... " if $Debug;
			if (($Pos1000 eq "") || ($CurrPos1000 < $Pos1000))
			{
				print "CurrPos = $CurrPos1000 < Pos = $Pos1000\n"
					if $Debug;
				$Pos1000 = $CurrPos1000;
			}
		}
	}
	if ($Pos1000 eq "")
	{
		print "Error: Pos1000 is empty for host $Host\n" unless  $Missing1000{$Host};
		$Missing1000{$Host}=1; # report missing only once
	}
	else
	{
		$Pos1000 = "#$Pos1000";
		print "POS: TopHost = $TopHost  i = $i  Pos = $Pos1000\n"
					if $Debug;
	}

	return $Pos1000;
}


sub GetCurrHourForPNG
{
my ($Hour, $Min) = @_;
my ($CurrHour) = $Hour;

if (defined($GenHour))
{
	$CurrHour = $GenHour;
}
else
{
	if ($Min > 28)
	{	# already ~30 minutes into current hour
		$CurrHour++;
			$CurrHour = 0 if ($Hour > 23);
		}
	}

	return $CurrHour;
}


sub GetMetaExpires
{
  my ($Time) = time + $_[0] * 60 + 5;
  my ($Wday) = ('Sun','Mon','Tue','Wed','Thu','Fri','Sat')[(gmtime($Time))[6]];
  my ($Month) = ('Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep',
		 'Oct','Nov','Dec')[(gmtime($Time))[4]];
  my ($Mday, $Year, $Hour, $Min, $Sec) = (gmtime($Time))[3,5,2,1,0];

  if ($Mday < 10) {$Mday = "0$Mday"};

  if ($Hour < 10) {$Hour = "0$Hour"};

  if ($Min < 10) {$Min = "0$Min";}

  if ($Sec < 10) {$Sec = "0$Sec";}

  return "$Wday, $Mday $Month " . ($Year + 1900) . " $Hour:$Min:$Sec GMT";
}


#
# Read the dnewsfeeds, diablo.hosts and dnntpspool.ctl files.
#
sub ReadDiabloConf
{
	my ($state, $label, $alias, $host, $i);
	my (%dnewsfeeds, %dhosts);

	#
	# First read dnewsfeeds, save the aliases.
	#
	open (FD, "$CfgDir/dnewsfeeds") || die ("Error: $CfgDir/dnewsfeeds - $!");
	$state = 0;
	while (<FD>) {
		if (/^label/) {
			$state = 0;
			($label) = /^label\s+(\S+)/;
			next if ($label =~ m/^(GLOBAL|DEFAULT)$/);
			$state = 1;
			next;
		}
		next if ($state == 0);
		if (/^end/) {
			$state = 0;
			next;
		}
		if (/^\s*alias/) {
			($alias) = /^\s*alias\s+(\S+)/;
			#
			# Add '# Stats-Ignore' after an alias entry in
			# /etc/news/dnewsfeeds if you want it ignored.
			#
			$dnewsfeeds{$label} .= "$alias:" unless (/stats-ignore/i);
		}
		if (/^\s*hostname/) {
			($host) = /^\s*hostname\s+(\S+)/;
			#
			# new versions of diablo put hostname here
			$dhosts{$host} = $label;
		}
		if (/^\s*inhost/) {
			($host) = /^\s*inhost\s+(\S+)/;
			#
			# new versions of diablo put hostname here
			$dhosts{$host} = $label;
		}
	}
	close FD;

	#
	# Read diablo.hosts file. Save it in an array _by hostname_
	#
	if (open (FD, "$CfgDir/diablo.hosts")) {
		while (<FD>) {
			next if (/^(#|$)/);
			($host, $label) = /^(\S+)\s+(\S+)/;
			next if ($host eq '' || $label eq '');
			$dhosts{$host} = $label;
		}
		close FD;
	}

	#
	#	Read dnntpspool file. Save it in an array _by hostname_
	#
	#	No file is not an error
	#
	if (open (FD, $CtlFile)) {
		while (<FD>) {
			next if (/^(#|$)/);
			($label, $host) = /^(\S+)\s+(\S+)/;
			next if ($host eq '' || $label eq '');
			$dhosts{$host} = $label;
		}
		close FD;
	} else {
		#
		# Perhaps dnewsfeeds is being used instead of dnntpspool.ctl.
		# Try running doutq to substitute for it.
		#
		if (open (FD, "$QueueCmd -h |")) {
			while (<FD>) {
				($label, $host) = /^(\S+)\s+(\S+)/;
				$dhosts{$host} = $label;
			}
			close FD ;
		}
	} 
	#
	# Now create the TopAliases array.
	#
	foreach $i (keys %dhosts) {
		$label = $dhosts{$i};
		$TopAliases{$i} .= lc $dnewsfeeds{$label};
	}
}


sub GetVolumeColor
{
	my ($Volume) = @_;
	my ($Color);

	if ($Volume =~ /GB/) {
		$Color = "#C01010";		# red
	} elsif ($Volume =~ /MB/) {
		$Color = "#1020A0";		# blue
	} else {
		$Color = "#255510";		# green
	}

	return $Color;
}


sub GetRowColor
{
	my ($Color) = @_;

	return ($Color =~ /#C5C5C5/ ? "#DFDFDF" : "#C5C5C5");
}
