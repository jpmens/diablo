# DEXPIRE.CTL
#
# This file is used to determine expire values used by dreaderd/dexpireover
# to store and expire article headers. This file is not used by the
# diablo feeding/spool code - see dspool.ctl.
#
# The line format is:
#
# wildmat:directive:directive...
#
# Each directive is separated by a ':'.
#
# Directives:
#
#	xFLOAT		overview expiration in number of days to keep
#			articles in this group.
#			If no expiration is specified, articles are
#			not removed unless one of the spool based overview
#			expire methods are used. See dexpireover(8).
#
#	aSTARTARTS	Specify the initial size of the overview index file.
#			The default is 512 articles.  This only applies to
#			the initial creation of over.* files.  Ongoing
#			management of the size of the index files is handled
#			dynamically by dexpireover (w/ -a, -s, or -R options).
#
#	iMINARTS	Specify the minimum number of articles that
#			an overview file will possibly contain. The
#			default is 32 and the value will be ignored
#			if it is less than 32.
#
#	jMAXARTS	Specify the maximum number of articles that
#			an overview file will possibly contain. The
#			default is unlimited.
#
#	eDATAENTRIES	Specify how many article headers are stored in each
#			overview data file (it must be a power of 2). The
#			default, if this is not specified, is 256. For groups
#			with a large number of articles, increasing this will
#			improve performance and reduce the number of files at
#			the expense of a little extra disk space (expired
#			headers are kept around a little longer before being
#			removed). Changes to this value come into effect after
#			the next 'dexpireover -R' run for each group.
#
#	lFLOAT		Limit listing articles for commands such as
#			NEWNEWS and XOVER to articles not older than
#			this many days. If you set this to 'x' (lx)
#			it has the same value as the overview expiration.
#			Setting it to zero turns it off (unlimited),
#			which is the default. Tune the l and x values
#			to the retention time of the spool servers,
#			and clients will not see overview info for
#			articles that have expired from the spool already.
#
# The last match is used, with values that match being cumulative.
#
# Set l values to the x value.
*:lx
#
# Store all articles for 30 days
*:x30
#
# Expire binaries a lot quicker, but start with a larger number of slots
*.binar*:x3:a2048

# Keep a large number of header slots available for binaries that
# encounter floods of articles. Keep the binaries x and a values.
alt.binaries.cd.*:i2048

# Don't bother keeping more than 6000 articles in jobs groups
*.jobs.*:j6000

