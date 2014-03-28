#!/usr/local/bin/python

import cgi
import rrdtool
import os

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# Configuration
#
DATADIRECTORY = '/home/kpettijohn/stats/data'
IMAGEDIRECTORY = '/news/www/stats/images'
IMAGEREF = '/stats/images'
SITEREF = '/stats/'
GRAPHLENGTH = '24 hours'
INDEXTITLE = 'VISI.com Transit Server Graphs'
BACK_URL = 'http://noc.visi.com/'
BACK_TEXT = 'Back to VISI.com NOC'
#
# End of Configuration
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

def doGraph(feedtype, basename, s):
	labelname = basename[0:-4]
	rrdfilename = DATADIRECTORY + '/' + feedtype + '/' + basename

	if feedtype == 'OUTFEED':
		# Articles  . . . . . . . . . . . . . . . . . . . . . . . . . . . .
		rrdtool.graph('%s/%s-%s-a.png' % (IMAGEDIRECTORY, feedtype, labelname),
				'--title=OUTFEED - ' + labelname + ' (articles)',
				'--imgformat=PNG',
				# '--logarithmic',
				# '--lazy', 
				'--vertical-label=articles/second',
				'--start=now-%s' % (s),
				'DEF:off=%s:off:AVERAGE' % (rrdfilename),
				'DEF:acc=%s:acc:AVERAGE' % (rrdfilename),
				'DEF:ref=%s:ref:AVERAGE' % (rrdfilename),
				'DEF:rej=%s:rej:AVERAGE' % (rrdfilename),
				'AREA:off#ff33cc:Articles Offered ',
				'GPRINT:off:LAST:  Cur\: %7.2lf',
				'GPRINT:off:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:off:MAX:  Max\: %7.2lf art/s\l',
				'AREA:acc#00ff00:Articles Accepted',
				'GPRINT:acc:LAST:  Cur\: %7.2lf',
				'GPRINT:acc:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:acc:MAX:  Max\: %7.2lf art/s\l',
				'AREA:rej#ff0000:Articles Rejected',
				'GPRINT:rej:LAST:  Cur\: %7.2lf',
				'GPRINT:rej:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:rej:MAX:  Max\: %7.2lf art/s\l',
				'LINE1:ref#660000:Articles Refused ',
				'GPRINT:ref:LAST:  Cur\: %7.2lf',
				'GPRINT:ref:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:ref:MAX:  Max\: %7.2lf art/s\l',
		)

		# Bytes . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
		rrdtool.graph('%s/%s-%s-b.png' % (IMAGEDIRECTORY, feedtype, labelname),
				'--title=OUTFEED - ' + labelname + ' (bits/sec)',
				'--imgformat=PNG',
				'--height=126', '--width=400',
				# '--logarithmic',
				# '--lazy', 
				'--vertical-label=bits/second',
				'--start=now-%s' % (s),
				'DEF:accbytes=%s:accbytes:AVERAGE' % (rrdfilename),
				'DEF:rejbytes=%s:rejbytes:AVERAGE' % (rrdfilename),
				'CDEF:accbits=accbytes,8,*',
				'CDEF:rejbits=rejbytes,8,*',
				'AREA:accbits#00bb00:Bits Accepted',
				'GPRINT:accbits:LAST:  Cur\: %6.2lf%Sb/s',
				'GPRINT:accbits:AVERAGE:  Avg\: %6.2lf%Sb/s',
				'GPRINT:accbits:MAX:  Max\: %6.2lf%Sb/s\l',
				'AREA:rejbits#bb0000:Bits Rejected',
				'GPRINT:rejbits:LAST:  Cur\: %6.2lf%Sb/s',
				'GPRINT:rejbits:AVERAGE:  Avg\: %6.2lf%Sb/s',
				'GPRINT:rejbits:MAX:  Max\: %6.2lf%Sb/s\l',
		)

	if feedtype == 'INFEED':
		# Articles  . . . . . . . . . . . . . . . . . . . . . . . . . . . .
		rrdtool.graph('%s/%s-%s-a.png' % (IMAGEDIRECTORY, feedtype, labelname),
				'--title=INFEED - ' + labelname + ' (articles)',
				'--imgformat=PNG',
				# '--logarithmic',
				# '--lazy', 
				'--vertical-label=articles/second',
				'--start=now-%s' % (s),
				'DEF:off=%s:off:AVERAGE' % (rrdfilename),
				'DEF:acc=%s:acc:AVERAGE' % (rrdfilename),
				'DEF:rec=%s:rec:AVERAGE' % (rrdfilename),
				'DEF:rej=%s:rej:AVERAGE' % (rrdfilename),
				'DEF:ref=%s:ref:AVERAGE' % (rrdfilename),
				'AREA:off#ff33cc:Articles Offered ',
				'GPRINT:off:LAST:  Cur\: %7.2lf',
				'GPRINT:off:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:off:MAX:  Max\: %7.2lf art/s\l',
				'AREA:rec#006600:Articles Received',
				'GPRINT:rec:LAST:  Cur\: %7.2lf',
				'GPRINT:rec:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:rec:MAX:  Max\: %7.2lf art/s\l',
				'AREA:acc#00ff00:Articles Accepted',
				'GPRINT:acc:LAST:  Cur\: %7.2lf',
				'GPRINT:acc:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:acc:MAX:  Max\: %7.2lf art/s\l',
				'AREA:rej#ff0000:Articles Rejected',
				'GPRINT:rej:LAST:  Cur\: %7.2lf',
				'GPRINT:rej:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:rej:MAX:  Max\: %7.2lf art/s\l',
				'LINE1:ref#660000:Articles Refused ',
				'GPRINT:ref:LAST:  Cur\: %7.2lf',
				'GPRINT:ref:AVERAGE:  Avg\: %7.2lf',
				'GPRINT:ref:MAX:  Max\: %7.2lf art/s\l',
		)

		# Bytes . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
		rrdtool.graph('%s/%s-%s-b.png' % (IMAGEDIRECTORY, feedtype, labelname),
				'--title=INFEED - ' + labelname + ' (bits/sec)',
				'--imgformat=PNG',
				'--height=126', '--width=400',
				# '--logarithmic',
				# '--lazy', 
				'--vertical-label=bits/second',
				'--start=now-%s' % (s),
				'DEF:recbytes=%s:recbytes:AVERAGE' % (rrdfilename),
				'DEF:accbytes=%s:accbytes:AVERAGE' % (rrdfilename),
				'DEF:rejbytes=%s:rejbytes:AVERAGE' % (rrdfilename),
				'CDEF:recbits=recbytes,8,*',
				'CDEF:accbits=accbytes,8,*',
				'CDEF:rejbits=rejbytes,8,*',
				'AREA:recbits#000000:Bits Received',
				'GPRINT:recbits:LAST:  Cur\: %6.2lf%Sb/s',
				'GPRINT:recbits:AVERAGE:  Avg\: %6.2lf%Sb/s',
				'GPRINT:recbits:MAX:  Max\: %6.2lf%Sb/s\l',
				'AREA:accbits#00bb00:Bits Accepted',
				'GPRINT:accbits:LAST:  Cur\: %6.2lf%Sb/s',
				'GPRINT:accbits:AVERAGE:  Avg\: %6.2lf%Sb/s',
				'GPRINT:accbits:MAX:  Max\: %6.2lf%Sb/s\l',
				'AREA:rejbits#bb0000:Bits Rejected',
				'GPRINT:rejbits:LAST:  Cur\: %6.2lf%Sb/s',
				'GPRINT:rejbits:AVERAGE:  Avg\: %6.2lf%Sb/s',
				'GPRINT:rejbits:MAX:  Max\: %6.2lf%Sb/s\l',
		)


	print '<img src="%s/%s-%s">' % (IMAGEREF,feedtype,labelname + '-a.png')
	print '<img src="%s/%s-%s">' % (IMAGEREF,feedtype,labelname + '-b.png')
	print '<br>'

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
print 'Content-type: text/html'
print

form = cgi.FieldStorage()

print '<html><head><title>Newsfeeder Statistics</title></head><body>'
if form.getvalue('v') is None:
	print '<p style="font-size: 125%; font-weight: bold;',
	print 'font-dectoration: underline;">'
	print '%s</p>' % INDEXTITLE

	print '<div style="margin-left: 60px; font-size: 110%;">'
	print 'Please choose what to view:</p>'
	print '</div>'

	print '<form method="POST">'
	print '<div style="margin-left: 100px;">'
	print '<table>'
	print '<tr><td align="right">Search for Server:&nbsp;&nbsp;</td>'
	print '<td><input type="text" name="t"> <i>(blank for all)</i></tr>'
	print '<tr><td align="right">Timespan:&nbsp;&nbsp;</td>'
	print '<td><select name="s">'
	print '<option value="6h">Last 6-hours</option>'
	print '<option value="12h">Last 12-hours</option>'
	print '<option value="1d" selected>Last 24-hours</option>'
	print '<option value="2d">Last 48-hours</option>'
	print '<option value="1w">Last week</option>'
	print '<option value="2w">Last two weeks</option>'
	print '<option value="1m">Last month</option>'
	print '<option value="3m">Last 3 months</option>'
	print '<option value="6m">Last 6 months</option>'
	print '<option value="1y">Last year</option>'
	print '</select></td></tr>'
	print '<tr><td colspan="2" align="center">'
	print '<input type="hidden" name="v" value="1">'
	print '<input type="submit" name="Graph!">'
	print '</td></tr>'
	print '<tr><td colspan="2" align="center">'
	print '<i style="font-size: 10px;">'
	print '(may take a minute, be patient ...)</i>'
	print '</td></tr>'
	print '</table>'
	print '</div>'
	print '</form>'

else:
	print '<p><a href="%s">Go back ...</a></p>' % SITEREF
	tag = form.getvalue('t')
	
	s = GRAPHLENGTH
	if form.getvalue('s') is not None:
		s = form.getvalue('s')
	
	for feed in ['INFEED', 'OUTFEED']:
		print '<h1>' + feed + '</h1>'
		filenames = os.listdir(DATADIRECTORY + '/' + feed)
	
		# Sort, and ensure TOTAL comes before anything
		filenames.remove('TOTAL.rrd')
		filenames.sort()
		filenames.insert(0, 'TOTAL.rrd')
	
		for n in filenames:
			if tag is None or \
			  n.find(tag) > -1 or n.find('TOTAL') > -1:
				doGraph(feed, n, s)
		print '<hr>'

print '<p><a href="%s">%s</a></p>' % (BACK_URL, BACK_TEXT)
print '</body></html>'

