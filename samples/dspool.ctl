#
# DSPOOL.CTL - control the allocation of newsgroups to spool objects and
#             define the size of spool objects for expire.
#
# ---------------------------------------------------------------
# Define a spool object. A spool object is a region of disk (usually
# a separate partition) that can be used to store news articles.
# Articles are stored in a directory structure under path_spool
# with a top-level directory name of P.nn, where 'nn' is the 2 digit
# spool number specified. e.g:
#
#	/news/spool/news/P.01
#	/news/spool/news/P.02
#
# The spool name is a 2 digit number that is unique for this spool object.
#
# A number of '00' is always defined and refers to the default of a
# single spool under spool/news. This number should be overwritten
# with a locally defined spool if the default spool is not used.
#
# IMPORTANT NOTE: Once a spool object has been defined and used, the
# path and spooldirs options cannot be modified without causing problems
# with the retrieval of articles.
#
# spool nn
#   path		P.01
#   spooldirs		5
#   minfree		1g
#   minfreefiles	1000
#   maxsize		1g
#   keeptime		10d
#   compresslvl		0
#   weight		20
# end
#
#   path     : The path to the spool object. The path is relative to
#	       path_spool, although an absolute path can also be specified.
#	       If not specified, this defaults to path_spool/P.nn, where
#	       'nn' is the spool number specified.
#
#   spooldirs: Split this spool into another level of directories to
#              reduce the number of directory entries per directory.
#	       Directories are named N.xx, where xx is a hex number
#	       starting from 00 until the (spooldirs value - 1). The
#	       directories will be created if they don't exist or
#	       can be pre-created as separate partitions if that is
#	       required.
#              This is really only necessary on very large spool objects
#	       where there are a large number of spool directories,
#	       particularly with large text spools and even then it
#              would be better to store more articles per file (see
#	       "reallocint" in metaspools).
#	       Use of this option is discouraged as it cannot be changed
#	       once articles have been stored. Rather use multiple spool
#	       objects.
#
#   minfree  : Specify how much space must be free before dexpire is happy
#	       Size is specified in KB.
#   minfreefiles  : Specify how many files (inodes) need to be free on
#	       this spool object. WARNING: If some other software is
#	       using the spool, be careful it doesn't suddenly create
#	       lots of files, or you may lose all your articles. Also
#	       ensure that this option is synced between spool objects
#	       on the same physical partition/filesystem.
#   maxsize  : Specify how much space may be used by this spool object.
#	       NOTE: dexpire will free space until this target is reached,
#	       more space than this will be used for a period of time
#	       between dexpire runs. Specified in KB.
#   keeptime : The amount of time (in seconds) to keep articles on
#	       this spool. You can use a suffix of 'd', 'h', 'm' to
#	       specify days, hours or minutes.
#   compresslvl: Set a compression level (if zlib use is compiled in) for
#		 this spool object. This option cannot be changed without
#		 losing the contents of the spool object, although the
#		 level can be adjusted once enabled (except to set it to 0)
#		 NOTE: Not all the recovery tools support compressed spools.
#		 Default: 0 (disabled)
#
#   expiremethod: This option defines the type of expire used on
#		  this spool. The current available methods are:
#		sync - check the available disk space after each
#			directory removal. This is the historical
#			behaviour and the default.
#		dirsize - this option only checks the disk free space
#			once, but keeps track of the directory sizes
#			to know how much to expire. This option is
#			probably faster on smaller spools.
#
#   weight: Used when allocstrat in the metaspool is set to 'weighted'.
#           The default is the size in GB of the partition this
#           spoolobject is located in.
#
# ---------------------------------------------------------------
# metaspool: Define a group of spool objects and define the types
#   of articles stored in the group.
# 
# Any changes to a metaspool come into effect the next time a new
# connection is made to this server. Spool objects can be moved
# between metaspools and spool objects can be allocated to multiple
# metaspools
#
# IMPORTANT: Each spool object in a metaspool should be the same
#	     size, otherwise dexpire may not work as expected with
#	     articles on the smaller partitions(s) being expired
#	     before articles on the larger partitions.
#
# metaspool name
#   maxsize	1m
#   maxcross	10
#   spool	01
#   spool	02
#   allocstrat	sequential
#   dontstore	no
#   rejectarts	no
#   hashfeed	1/1
#   addgroup	wildmat
# end
#
# maxsize    : Maximum size article allowed in this metaspool
#
# maxcross   : Maximum number of newsgroups per article allowed in this
#              metaspool
#
# label	     : Allocate all articles that are received from this label
#	       in dnewsfeeds to this metaspool. Multiple label commands
#	       can be used per metaspool object.
#
# spool      : The spool definition(s) allocated to this metaspool
#
# allocstrat : Specify the spool allocation strategy. Valid values are:
#		sequential: default spool allocation, allocating a
#			different spool to each new incoming connection
#			in a sequential fashion. Default.
#		space: allocate the spool with the highest free space
#			value to each new incoming connection.
#		single: write all feeds to a single spool and only switch
#			to the next spool after reallocint time.
#               weighted: divide the connections over the spools
#			randomly, but weighted by the weights of
#			the spools. If there are 3 spools with weights
#			2,2,4 then the result is 25% / 25% / 50%
#
# reallocint : Specify the maximum amount of time we are allowed
#	       to write to a spool directory before moving on to
#	       the next. This spreads the articles across multiple
#	       spools for long running incoming connections.
#		Default: 600 seconds (10 minutes)
# arttypes   : Specify which article types are allowed/disallowed
#	       for this spool. Valid article types are:
#		none	: no articles
#		default : anything that doesn't match
#		control : a Control: message
#		cancel	: a cancel message
#		mime	: an article specify a MIME type
#		binary	: binary articles (any of the next 5)
#		uuencode: uuencoded binaries 
#		base64  : base64 encoded binaries
#		binhex	: BINHEX encoded binaries
#		yenc    : yenc encoded binaries
#		bommanews : bommanews encoded binaries
#		multipart: multipart MIME messages
#		html	: HTML MIME
#		ps	: Postscript MIME
#		partial	: MIME partial
#		pgp	: A message containing some sort of PGP
#		all	: Any type of article (DEFAULT)
#	Specifying a '!' before a type negates that type
#
# dontstore  : Accept, but don't store articles matching this metaspool
#		 Options: yes|no (Default: no)
#
# rejectarts : Reject and don't store articles matching this metaspool
#		 Options: yes|no (Default: no)
#
# hashfeed: Specify which articles are stored in this metaspool based on
#		a hash of the MessageID. The value is specified as n/n or
#		n-n/n as described in dnewsfeeds.
#
# addgroup: Specify a wildmat pattern for groups stored into this metaspool.
#	    Note that this option works in addition to the 'expire' lines,
#	    by artifically creating an expire line, so the ordering will
#	    be as they appear in the config file.
#
# ---------------------------------------------------------------
# expire: Allocate a news hierarchy to a metaspool
#
# expire	wildmat		metaspool
#
# The first match of newsgroup, arttype, maxsize and maxcross is used
# ---------------------------------------------------------------
# Examples
#
# ---------------------------------------------------
# This example shows how 2 spool objects are combined to create
# a meta spool object for binaries.
#
# Non-binaries will be kept for 30 days, but cannot use more than 200GB
#spool 01
#    maxsize	200g
#    keeptime	30d
#    path	nonbin
#end
#
# Binaries are stored on 2 partitions, with dexpire maintaining
# 4GB free on each partition
#spool 02
#    minfree	4g
#    path	bin01
#end
#
#spool 03
#    minfree	4g
#    path	bin02
#end
#
#metaspool nonbin
#    maxsize	100k
#    maxcross	10
#    spool	1
#    arttypes	!binaries
#end
#
#metaspool binaries
#    spool	3
#    spool	2
#end
#
#expire	*.bina*		binaries
#expire	*		nonbin
# ---------------------------------------------------
# This example duplicates the traditional Diablo spool and is
# probably suitable for a backbone feeder
spool 00
    minfree	6g
end
#
metaspool trad
    spool	00
end
#
expire	*	trad
# ---------------------------------------------------

