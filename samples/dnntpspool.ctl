# DNNTPSPOOL.CTL
#
# *******************************************************************
# ************ IMPORTANT NOTE ***************************************
#
# NOTE: This file is no longer necessary. All the options are
# available in dnewsfeeds and this file will only be used if it
# exists as dnntpspool.ctl in the path_lib directory.
#
# *******************************************************************
#
# FORMAT:
#
#	label	domainname	maxqueue	options
#
#	label:		specify the label to match the one in dnewsfeeds
#	domainname:	remote host to push the articles to (to connect to)
#	maxqueue:	maximum number of queue files to keep for this feed
#	options:	other options (see below)
#
# OPTIONS:
#
#	xreplic		- uses innxmit instead of dnewslink, and use
#			  the xreplic protocol.  Not supported by DIABLO.
#
#		 ----->>> NOT SUPPORTED FOR DIABLO V1.08 OR GREATER DUE TO
#			  MULTI-ARTICLE SPOOL FILES, WHICH INNXMIT DOES NOT
#			  UNDERSTAND 
#			
#	pNNNN		- connect to specified port on destination
#
#	qNNNN		- specify that dspoolout not run dnewslink on the
#		 	  most recent N queue files, which effectively
#			  introduces a queueing delay of N x 5 or N x 10
#			  minutes depending on how often you run dspoolout
#			  from cron.  This can be used to delay normal articles
#			  when you have a separate feed dealing with control
#			  messages and want to get the cancels out ahead
#			  of the normals.
#
#	dNNNN		- specify the startup delay for dnewslink, in seconds.
#			  This is useful when you are connecting to several
#			  extremely fast and extremely well connected sites.
#			  By specifying different startup delays for the fast
#			  sites, you reduce efficiency losses due to article
#			  collisions and you can favor one site over another.
#
#			  It should be noted that feeds generally synchronize
#			  pretty quickly, and anything under 4 seconds will not
#			  yield any useful results.
#
#			  A "delay" of -1 means that dnewslink will reduce
#			  the tail latecy for the queue files and use poll()
#			  instead if OS permits.
#
#	Tnnnnn		- specify size of TCP transmit buffer for socket.
#			  minimum suggested size: 4096
#
#			  nominal suggested size: 16384 to 65536 depending
#			  on how your kernel is configured.
#
#	Rnnnnn		- specify size of TCP receive buffer for socket.
#			  minimum suggested size: 4096
#			  suggeseted size: 8192
#
#	bind		- specify local IP address to bind to when creating
#			  the NNTP session.  This may also be globally set by
#			  the "-B" command line option to DSpoolout.
#
#	n4		- run up to 4 dnewslink's in parallel (default is 2).
#			  You can specify any number up to 32, but we suggest
#			  that you never specify more then 3 to any external
#			  outbound feed, and no more then 4 to any internal
#			  newsreader machine if it is running INN.
#
#			  You should never have to specify more then 2 (the
#			  default) to any outbound feed that is running diablo
#
#	nostream	- (for dnewslink) - do not try to stream, use ihave
#			  instead.  
#
#			  If not specified, dnewslink will attempt to 
#			  negotiate streaming, and fall back to ihave
#			  if the remote does not support it.
#
#	realtime	- specify that this is a realtime feed.  dspoolout
#			  will startup and maintain a dnewslink that sits
#			  on diablo's run-time outgoing feed file.  This 
#			  operates in parallel with standard queue file
#			  mechanisms.
#
#			  NOTE: having a queue-file delay (qNNNN) precludes
#			  making the feed realtime.
#
#	nobatch		- tell dspoolout NOT to run any batch dnewslink's 
#			  for this feed.  Nominally used for realtime-only
#			  feeds where the loss of an article here and there
#			  does not hurt anything.  One also normally reduces 
#			  the number of queue files to 1 when using this 
#			  option. (do not use this option on critical feeds!,
#			  always make sure there is at least one guarenteed way
#			  out for an article).
#
#	headfeed	- dspoolout passes -H option to dnewslink to dump a
#			  header-only feed, used by caching diablo newsreader
#			  side.  Server must support 'mode headfeed'.
#
#	nocheck		- disable the use of the NNTP "check" command when
#			  a streaming feed is negotiated.  Instead, push 
#			  articles out solely with "takethis".  Useful for
#			  headfeed's.
#
#	logarts=classes	- log all outgoing articles into a file in the log
#			  directory, called feedlog.FEEDNAME. Classes is 
#			  either comma-separated list of any combination 
#			  of "accept", "reject", "defer", "refuse" and "error", 
#			  or "all". Not that specifying "refuse" can generate
#			  huge logfiles. Default is no logging.
#
#			  This corresponds to the -A option for dnewslink.
#
#	hours		- specify the hours during which we are allowed
#			  to send news to this host.
#
# NNTPSPOOL.CTL
#
# Format:
# spoolfile	machine				maxfileq	options

idiom		gidiom.com			50
sgigate		news.fsgi.com			250
aimnet2		newsfeed9.aimnet.com		250
nntp2a		oldnntp.best.com		500	n4 q1
nntp2c		nntp1x.ba.best.com		500	n4 realtime
hp		xsdd.hp.com			250	d15

# If your news machine has multiple interfaces, you can bind to a particular
# one with bind=
#

xal     fubar.xalxxx.com   50   bind=news1-eth0.best.com n2 nostream

# If you want to limit the hours to 20:00-21:00 and 03:00-08:00 then
example		news.example.com		50 hours=20,3-7

# and so on
#
