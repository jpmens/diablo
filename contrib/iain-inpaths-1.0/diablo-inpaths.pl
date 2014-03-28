#!/usr/local/bin/perl5
#
# 1. with 'egrep dir/each-art >> log'  41491940
# 1326.470u 863.656s 1:33:06.41 39.2% 0+0k 242384+103io 10pf+0w
#
# 2. with 'egrep dir/* >> log'         44084303
# 1241.241u 402.263s 1:20:10.11 34.1% 0+0k 232356+39io 54pf+0w
#
#
# ABSOLUTELY NO WARRANTY WITH THIS PACKAGE. USE IT AT YOUR OWN RISK.
#
# Parse DIABLO news spool and generate raw Path: info for inpaths.
# URL: ftp://ftp.ecrc.de/pub/news/servers/diablo-utils
#
# diablo-inpaths.pl v1.0  970824  Iain Lea  iain@ecrc.de
#
# Usage: diablo-inpaths.pl [options]
#     -h       help
#     -d dir   diablo news spool dir (default: /news/spool/news)
#     -l file  log to file (default: /var/log/news/path.log)
#     -p host  hostname in Path: header (default: newsfeed.ecrc.net)
#     -v       verbose
#     -V       debug
#
# Logic:
# ------
#    +---------+--------------+--------------+
#    |Oldest   | LowWater     | HighWater    | Current
#    v         v              v              v
#    /    /    /    /    /    /    /    /    / Created new dir
#   ...  -70  -60  -50  -40  -30  -20  -10   0 Time in minutes
#
# This program uses a couple of tricks to gain speed over a normal
# 'find /spool -exec grep ...' script (note that such a find script
# would log some Path: headers twice due to diablo still appending
# articles to its latest spool files).
# 1. diablo creates a new dir in /news/spool/news for its large 
#    multi-article files every 10 minutes to stop big dirs.
# 2. the multi-article files are only changing size in the new dir.
# 3. older dirs have multi-article files that are only being read
#    from by dnewslink processes and are therefore stable & inert.
# This script uses the above mentioned points todo the following:
# - 1st time its run it logs all Path: headers from Oldest -> HighWater.
# - logs HighWater time to $SpoolDir/.diablo-inpaths.info for next run.
# - when run again it reads the $SpoolDir/.diablo-inpaths.info file and
#   sets the LowWater mark.
# - sets HighWater mark to current time - 30 minutes.
# - logs all Path: headers between LowWater -> HighWater marks.
# - logs HighWater time to $SpoolDir/.diablo-inpaths.info for next run.
# - etc. etc.
#
# TODO:
# -----
#
# Acknowledgements:
# -----------------
#   Jeff Garzik             jeff.garzik@spinne.com
#
# ChangeLog:
# ----------
# v1.0BETA
#   - 

require 'getopts.pl';
require "timelocal.pl";

$PathLogFile = "/var/log/news/path.log";
$SpoolDir = "/news/spool/news";
$FtpUrl = "ftp://ftp.ecrc.de/pub/news/servers/diablo-utils/";
chop ($PathName = `hostname`);
$Verbose = 0;
$Debug = 0;
$Version = 'v1.0';
$ScriptName = 'diablo-inpaths';
$SpoolInfoOk = 0;
$CurrTime = time;
$HighWater = $CurrTime - (60 * 30);	# 30 mins ago
$LowWater = 1;	# a long time ago :)

##############################################################################
# 
#

print "Set LowWater=$LowWater HighWater=$HighWater\n" if $Debug;

&ParseCmdLine ($0);

&ReadSpoolInfo;

&ParseSpoolDir;

&WriteSpoolInfo;

exit 0;

##############################################################################
# 
#

sub ParseCmdLine
{
	my ($ProgName) = @_;
	
	&Getopts('d:hl:p:vV');

	if ($opt_h) { 
		print <<EOT
$ScriptName $Version  $FtpUrl

Create inpaths Path: propagation data for DIABLO news relay server.
Copyright 1997 Iain Lea (iain\@ecrc.de). NOTE: Use at your own risk.

Usage: $ProgName [options]
       -h       help
       -d dir   diablo news spool dir (default: $SpoolDir)
       -l file  log to file (default: $PathLogFile)
       -p host  hostname in Path: header (default: $PathName)
       -v       verbose
       -V       debug

EOT
;
		exit 1;
	}
	$SpoolDir = $opt_d if (defined($opt_d));
	$PathLogFile = $opt_l if (defined($opt_l));
	$PathName = $opt_p if (defined($opt_p));
	$Verbose++ if (defined($opt_v));
	$Debug++ if (defined($opt_V));

	$SpoolInfoFile = "$SpoolDir/.diablo-inpaths.info";
}


sub ReadSpoolInfo
{
	print "Reading $SpoolInfoFile ...\n" if $Verbose;

	if (open (FILE, $SpoolInfoFile)) {
		while (<FILE>) {
			if (/^(\d+)$/) {
				$LowWater = $1;
				print "Set LowWater=$LowWater HighWater=$HighWater\n" if $Debug;
			}
		}
		close (FILE);
	}
}


sub WriteSpoolInfo
{
	print "Writing $SpoolInfoFile HighWater=$HighWater...\n" if $Verbose;

	if (open (FILE, "> $SpoolInfoFile")) {
		print FILE "$HighWater\n";
		close (FILE);
	} else {
		print "Error: $SpoolInfoFile - $!\n";
	}
}


sub ParseSpoolDir
{
	my ($Dir);

	print "Parsing $SpoolDir $LowWater (low) <--> $HighWater (high) ...\n" if $Verbose;

	chdir ($SpoolDir) || die "Error: $SpoolDir - $!\n";

	open (PIPE, "find . -type d -name '[A-z0-9]*' -print |") || die "Error: $SpoolDir - $!\n";

	while ($Dir = <PIPE>) 
	{
		chop $Dir;

		@Stat = stat ($Dir);
		$Ctime = $Stat[10];

		if ($Ctime > $LowWater && $Ctime < $HighWater) {
			print "DIR:  $Dir  $Ctime > $LowWater && $Ctime < $HighWater\n" if $Debug;
			&ParseDir ($Dir);
		} elsif ($Ctime <= $LowWater) {
			print "OLD:  $Dir  $Ctime =< $LowWater\n" if $Debug;
		} elsif ($Ctime >= $HighWater) {
			print "NEW:  $Dir  $Ctime >= $HighWater\n" if $Debug;
		}
	}
	close (PIPE);
}


sub ParseDir
{
	my ($Dir, $Secs) = @_;
	my (@FileList, $File);

	print "GREP $Dir/*\n";
	chdir ($Dir);
	`egrep -h "^Path: $PathName" * >> $PathLogFile`;
	chdir ($SpoolDir);

	return;

#	opendir (DIR, $Dir) || die "Error: $Dir - $!\n";
#	@FileList = egrep (!/^\./, readdir (DIR));
#	foreach $File (@FileList) {
#			print "GREP $Dir/$File\n";
#			`date`;
#			`egrep "^Path: $PathName" $Dir/$File >> $PathLogFile`;
#			`date`;
#		}
#	}
#	closedir (DIR);
}
