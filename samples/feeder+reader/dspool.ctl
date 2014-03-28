#
# Sample dspool.ctl for use on a feeder running as a spool
#
# ---------------------------------------------------
#
# Store all non-binary articles into a spool /news/spool/news/P.01 and
# keep the articles for 30 days as long as it doesn't use more than 40GB
#
# Store all other articles (binaries) into /news/spool/news/P.02 and
# always ensure that there is 10gb free
#
# Note that with this configuration, both P.01 and P.02 can be
# sub-directories of a mounted partition /news/spool/news or can
# be separate partitions, as long as the text spool exists on
# a partition of at least 40GB and there is enough space to store
# 10GB binaries + the size of binaries received between dexpire runs.
#
#
spool 01
    keeptime	30d
    maxsize	40g
end
spool 02
    minfree	10g
end
#
metaspool text
    spool	01
    arttypes	!binary
end
metaspool binaries
    spool	02
end
#
expire	*	text
expire	*	binaries
# ---------------------------------------------------

