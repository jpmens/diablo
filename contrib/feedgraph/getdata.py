#!/usr/local/bin/python

import os
import re
import time
import rrdtool

FEEDINFOCMD = '/news/dbin/dfeedinfo -i -o -r'
DATADIRECTORY = '/home/kpettijohn/stats/data'

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
def getFeedInfo():
	output = os.popen(FEEDINFOCMD).readlines()

	mydict = {}
	reLine = re.compile('^(\S+)\s+(\S+?)(?:-(?:in|bofhnet|out|head|controlfromvisi|small|med|large|bin|binary|text|l|m|s|local)?-?(?:\d)*)?\s+(.*)$')
	reLine2 = re.compile('(?:(\S+)=(\d+)\s*)')

	for l in output:
		r = reLine.match(l)
		if r:
			print 'Getting %s data for %s' % (r.group(1), r.group(2))
			pairs = reLine2.findall(r.group(3))
			for p in pairs:
				(k, v) = p
				while True:
					try:
						mydict[r.group(1)][r.group(2)][k] = \
							mydict[r.group(1)][r.group(2)][k] + int(v)
						break
					except KeyError:
						try:
							try:
								mydict[r.group(1)][r.group(2)][k] = 0
							except KeyError:
								mydict[r.group(1)][r.group(2)] = dict()
						except KeyError:
							mydict[r.group(1)] = dict()
	return mydict

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
def populate(timestamp, mydict):
	for p in mydict.items():
		(feedtype, label) = p
		print 'Checking', feedtype

		# - - - - - - - - - - - - - - - - - - - -
		for (labelname, values) in label.items():
			filename = '%s/%s/%s.rrd' % (DATADIRECTORY, feedtype, labelname)
			# - - - - - - - - - - - - - - - - - -
			if feedtype == 'INFEED':
				if not os.path.exists(filename):
					rrdtool.create(filename,
						'--start=%d' % (timestamp-1),
						'DS:off:COUNTER:900:U:U',
						'DS:rec:COUNTER:900:U:U',
						'DS:acc:COUNTER:900:U:U',
						'DS:ref:COUNTER:900:U:U',
						'DS:rej:COUNTER:900:U:U',
						'DS:recbytes:COUNTER:900:U:U',
						'DS:accbytes:COUNTER:900:U:U',
						'DS:rejbytes:COUNTER:900:U:U',
						'RRA:MIN:0.5:1:2880',
						'RRA:MAX:0.5:1:2880',
						'RRA:AVERAGE:0.5:1:2880',
						'RRA:AVERAGE:0.5:12:720',
						'RRA:AVERAGE:0.5:24:1080',
						'RRA:AVERAGE:0.5:72:730',
					)
				# . . . .
				rrdtool.update(filename,
					'%d:%d:%d:%d:%d:%d:%d:%d:%d' % (\
						timestamp,
						int(values['off']),
						int(values['rec']),
						int(values['acc']),
						int(values['ref']),
						int(values['rej']),
						int(values['recbytes']),
						int(values['accbytes']),
						int(values['rejbytes']),
					) # ::: stuff
				)  #update
			# - - - - - - - - - - - - - - - - - -
			elif feedtype == 'OUTFEED':
				if not os.path.exists(filename):
					rrdtool.create(filename,
						'--start=%d' % (timestamp-1),
						'DS:off:COUNTER:900:U:U',
						'DS:acc:COUNTER:900:U:U',
						'DS:ref:COUNTER:900:U:U',
						'DS:rej:COUNTER:900:U:U',
						'DS:deffail:COUNTER:900:U:U',
						'DS:accbytes:COUNTER:900:U:U',
						'DS:rejbytes:COUNTER:900:U:U',
						'RRA:MIN:0.5:1:2880',
						'RRA:MAX:0.5:1:2880',
						'RRA:AVERAGE:0.5:1:2880',
						'RRA:AVERAGE:0.5:12:720',
						'RRA:AVERAGE:0.5:24:1080',
						'RRA:AVERAGE:0.5:72:730',
					)
				# . . . .
				rrdtool.update(filename,
					'%d:%d:%d:%d:%d:%d:%d:%d' % (\
						timestamp,
						int(values['off']),
						int(values['acc']),
						int(values['ref']),
						int(values['rej']),
						int(values['deffail']),
						int(values['accbytes']),
						int(values['rejbytes']),
					) # ::: stuff
				)  #update

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
timestamp = int(time.time())
dict = getFeedInfo()
populate(timestamp, dict)
