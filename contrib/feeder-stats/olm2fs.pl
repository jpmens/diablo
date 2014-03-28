#!/usr/bin/perl

# Crude and ugly but working script to convert the OLM list to
# feeder-stats.conf format. Output is to standard output, redirect
# to the file of your choice
#
# The OLM (Online Monitoring List) can be found at:
#
#   ftp://ftp.switch.ch/info_service/netnews/wg/config/olm-lookup
#
#   format: searchstr[TAB]official_name[TAB]isp_code[TAB]moni_url

$Debug = 0 ;
$OlmFile = "olm-lookup" ;
&ReadOlmFile ;
&PrintFeederStats ;

sub ReadOlmFile
{
if (open (FILE, $OlmFile)) {
	while (<FILE>) {
		next if (/^#/) ;
		if (/^(\S*)\t(\S*)\t(\S*)\t(\S*)\n/) {
			$Host = $1;
			$PathHost = $2;
			$ISP = $3;
			$Link = $4;

			$OLM{$ISP . $Host}->{isp} = $ISP ;
			$OLM{$ISP . $Host}->{host} = $Host ;
			$OLM{$ISP . $Host}->{link} = $Link ;
			$OLM{$ISP . $Host}->{path} = $PathHost ;

			print "[$Host] [$PathHost] [$ISP] [$Link]\n" if $Debug ;

		}
	}
	close FILE;
} # end if open
} # end sub ReadOlmFile

sub PrintFeederStats
{
	my($ISPHost,$PrevISP);
	print <<EOM
# 
#   Links and aliases in this file are based on The OLM
#   (Online Monitoring List):
#
#     ftp://ftp.switch.ch/info_service/netnews/wg/config/olm-lookup
#
EOM
;
	foreach $ISPHost (sort keys %OLM) { 
		print "# $OLM{$ISPHost}->{isp}\n" unless ($OLM{$ISPHost}->{isp} eq $PrevISP) ;
		$PrevISP = $OLM{$ISPHost}->{isp} ;
		print "link: $OLM{$ISPHost}->{host}	$OLM{$ISPHost}->{link}\n";
		print "top1000: $OLM{$ISPHost}->{host}	$OLM{$ISPHost}->{path}\n"
                    unless (lc $OLM{$ISPHost}->{host} eq lc $OLM{$ISPHost}->{path});
	}
} # end sub PrintFeederStats
