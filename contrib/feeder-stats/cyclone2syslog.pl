#!/usr/local/bin/perl -w
#
# ABSOLUTELY NO WARRANTY WITH THIS PACKAGE. USE IT AT YOUR OWN RISK.
#
# Parse CYCLONE stats.in & stats.out files into syslog format.
# URL: ftp://ftp.bricbrac.de/pub/news/feeder-utils
#
# cyclone2syslog.pl v1.00  980125  David Riley  @bricbrac.de
#
# Usage: cat stats.[in|out] | cyclone2syslog.pl > /var/log/news/cyclone.log
#
# Acknowledgements:
# -----------------
#   Iain Lea             iain@bricbrac.de
#
# TODO:
# -----
# - 
#
# ChangeLog:
# ----------
# v1.00
#   - first public release

use strict;

use POSIX;

my $outfieldcount = 15;
my $IN = 1;
my $OUT = 2;

my $readline;
my @line;

my $mode;

my $hostname = shift || "news";

my $program = "cycloned";
my $pid = "1";

my $time;
my $timestring;

my @months = ("",
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
);

LINE: 
while ($readline = <>) 
{
	chop ($readline);

	@line = split (/\t/, $readline);

	if ($line[1] eq "Time") {
		next LINE;
	}

	if ($#line + 1 == $outfieldcount) {
		$mode = "out";
	} else {
		$mode = "in";
	}

	$time = $line[1];
	$timestring = strftime ("%b %d %H:%M:%S", localtime ($time));

	printf("%s %s %s[%s]: %s %s\n", 
		$timestring, $hostname, $program, 
		$pid, $mode, join(" ", @line));
}

exit 0;
