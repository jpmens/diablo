#!/usr/local/bin/perl -w
#
# an extern authentication module for dreaderd written in perl
#
# This is a very simple example for check usernames and passwords
# on a external Oracle-DB. You must have installed the oracle-client,
# DBI and DBD::Oracle or you can use MySQL with DBI.
# The module returns with '100 Success' normaly or you use an extented 
# authentication  '110 adultuser'. In this case you must defined a 
# 'readerdef adultuser' in your dreader.access. The second key in the
# return-code modifieds the readerdef.
# ckpasswd.pl connects to startup to the database and hold a permanent 
# connection. You must restart dreaderd for reconnect the DBI.
#
# For more details see dreader/dns.c and lib/vendor.h
#
#    Winfried Koenig <win@arcor-online.net>
#    Frank Kloeker <eumel@arcor-online.net>
#
####################################################################

use strict;
use DBI;

$ENV{NLS_LANG}="AMERICAN_AMERICA.WE8ISO8859P1";
$ENV{ORACLE_HOME}="/home/oracle";
$ENV{LANG}="de";

my $dbname = "oracle";	# Database name
my $dbuser = "oracleuser";	# Database user
my $dbpass = "oracleuser";	# Database password

my $adultnews_group = 3002;

my $dbh;			# Database handle object
my $name_sth;			# Statement handle object for name login
my $num_sth;			# Statement handle object for number login

my $success_freeuser = "110 freeuser";
my $success_adultuser = "110 adultuser";

########################################################################
# code for script test, dreaderd executes the script without arguments

while (@ARGV >= 2) {
    my $user_arg = shift;
    my $pass_arg = shift;
    my $res = checkuser($user_arg, $pass_arg);
    if ($res) {
	print "$user_arg: $res\n";
    } else {
	print "$user_arg: FAILED\n";
    }
}
########################################################################
# subroutine called from dreaderd (and from script test)

# checkuser called from 'dns.c'
sub checkuser {
    my($user, $pass) = @_;
    my($sth, $rv, $result);
    dbconnect() if !$dbh;
    if ($dbh && $name_sth && $num_sth) {
	$user =~ tr/A-Z/a-z/;
	$sth = $name_sth;
	$rv = $sth->execute($user);
    }
    if (!$rv) {
	# database is down or give an error by connect
	undev $dbh;			# DB error, force reconnect
	$result = $success_freeuser;	# allow minimal access
    } else {
	my(@row, $password, $ugroup);
	my($adultuser);
	while (@row = $sth->fetchrow_array) {
	    ($password, $ugroup) = @row;
	    $adultuser = 1 if $ugroup == $adultnews_group;
	}
	if ($password && crypt($pass, $password) eq $password) {
	    if ($adultuser) {
		$result = $success_adultuser;
	    } else {
		$result = $success_freeuser;
	    }
	}
    }
    return $result;
}

sub dbconnect {
    $dbh = DBI->connect("dbi:Oracle:$dbname",
	$dbuser,  $dbpass, { PrintError => 0});
    return unless $dbh;		# connect failed
    $name_sth = $dbh->prepare(
	"SELECT password, groupnumber \
	 FROM person \
	WHERE username = ? "
    );
}
