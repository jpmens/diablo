#
# Sample dspool.ctl for use on a feeder
#
# ---------------------------------------------------
# This example duplicates the traditional Diablo spool and is
# probably suitable for a backbone feeder
# Store all articles on a single spool in sub-directories of /news/spool/news
# Try and maintain 10gb free on the spool
#
spool 00
    minfree	10g
end
#
metaspool trad
    spool	00
end
#
expire	*	trad
# ---------------------------------------------------

