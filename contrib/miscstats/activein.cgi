#!/usr/bin/perl5
#
# By Jeff Garzik <jeff.garzik@spinne.com>

$PS_Command = "/bin/ps -wwwax";

####################################################################

print "Content-type: text/html\n\n";

if (!open(PIPE, "$PS_Command |")) {
  print "cannot open $PS_Command: $!";
  exit(1);
}


$nproc = 0;
while (<PIPE>) {
  $nproc = $nproc + 1;
  next unless (($ihave, $check, $rec, $ent, $site) =
  	       (/ihav=(\d+)\s+chk=(\d+)\s+rec=(\d+)\s+ent=(\d+)\s+(\S+)/));

  $rej = $rec - $ent;
  push(@sitelist, "$site\t$ihave\t$check\t$rej\t$ent");
}
close(PIPE);

$cxncnt = $#sitelist + 1;
$uptime = `uptime`;
$iostat  = `iostat -c3 1`;
$vmstat = `vmstat -c3 1`;
chomp $uptime;

print <<"EOM";
<html><head>
<title>DIABLO Incoming Connections</title>
</head><body>
<h1>DIABLO Incoming Connections ($cxncnt)</h1>
$uptime
<BR>$nproc total processes, PPro 200 running FreeBSD 2.2.x, 192MB ram, 4G root and 4Gx2 spool
<PRE>
$vmstat
$iostat
</PRE>

<p><hr><p>

<table border=5>
<tr>
<th>Site</th>
<th>IHAVE</th>
<th>CHECK</th>
<th>Reject</th>
<th>Accept</th>
</tr>

EOM

foreach (sort @sitelist) {
  @tmp = split(/\t/);

  print "<tr>\n";
  foreach $elem (@tmp) {
    if ($elem eq $tmp[0]) {
      $align = 'left';
    } else {
      $align = 'right';
    }
    print "<td align=$align>$elem</td>\n";
  }
  print "</tr>\n";
}

print "</table></body></html>\n";
exit(0);


