##  dcontrol.ctl - access control for control messages (DIABLO)
##
##  Roughly:	this is the same as INN's control.ctl except you must 
##  		specify what to do with cancel's and a few new actions
##		have been added.
##
##  Format:
##	<message>:<from>:<newsgroups>:<action>[=log][,<action>[=log]]*
##  The last match found is used.
##	<message>	Control message or "all" if it applies
##			to all control messages.
##	<from>		Pattern that must match the From line.
##	<newsgroups>	Pattern that must match the newsgroup being
##			newgroup'd or rmgroup'd (ignored for other messages).
##	<action>	What to do:
##	    doit			Perform action (usually sends mail too)
##	    doit=xxx			Do action; log to xxx (see below)
##	    drop			Ignore message
##	    log				One line to error log
##	    log=xxx			Log to xxx (see below)
##	    mail			Send mail to admin
##	    verify-pgp_userid		Do PGP verification on user.
##	    verify-pgp_userid=logfile	PGP verify and log.
##	    break			terminate dcontrol.ctl scan
##
##			xxx=mail to mail; xxx= (empty) to toss; xxx=/full/path
##			to log to /full/path; xxx=foo to log to ${LOG}/foo.log
##
## --------------------------------------------------------------------------
##	DEFAULTS
##
## Get cancel up there in the front so we can terminate dcontrol.ctl 
## processing. 

cancel:*:*:doit,break
all:*:*:log=control.log
checkgroups:*:*:log=checkgroups.other
ihave:*:*:drop
sendme:*:*:drop
sendsys:*:*:drop
senduuname:*:*:drop
version:*:*:drop
newgroup:*:*:log=newgroup.other
rmgroup:*:*:log=rmgroup.other

##	LOCAL HIERARCHIES

##	BEST (Best Internet Communications [duh])
#all:news@bofh.noc.best.net:best.*:mail
all:*@*:best.*:log=control.log

## BIG 8  comp, humanities, misc, news, rec, sci, soc, talk
# *PGP*   See comment at top of file.
checkgroups:group-admin@isc.org:*:verify-news.announce.newgroups=miscctl
newgroup:group-admin@isc.org:comp.*|rec.*|news.*:verify-news.announce.newgroups
newgroup:group-admin@isc.org:misc.*|sci.*|soc.*:verify-news.announce.newgroups
newgroup:group-admin@isc.org:talk.*|humanities.*:verify-news.announce.newgroups
rmgroup:group-admin@isc.org:comp.*|misc.*|news.*:verify-news.announce.newgroups
rmgroup:group-admin@isc.org:rec.*|sci.*|soc.*:verify-news.announce.newgroups
rmgroup:group-admin@isc.org:talk.*|humanities.*:verify-news.announce.newgroups

##	SLUDGE	(alt)
## Accept all newgroup's as well as rmgroup's from trusted sources and
## process them silently.  Only the rmgroup messages from unknown sources
## will be e-mailed to the administrator.
## Other options and comments on alt.* groups can be found on Bill
## Hazelrig's WWW pages at http://www.tezcat.com/~haz1/alt/faqindex.html
newgroup:*:alt.*:doit=newgroup
rmgroup:*:alt.*:log=rmgroup.alt
rmgroup:barr@*.psu.edu:alt.*:doit=rmgroup
rmgroup:david@home.net.nz:alt.*:doit=rmgroup
rmgroup:grobe@*netins.net:alt.*:doit=rmgroup
rmgroup:haz1@*nwu.edu:alt.*:doit=rmgroup
rmgroup:jkramer1@swarthmore.edu:alt.*:doit=rmgroup
rmgroup:news@gymnet.com:alt.*:doit=rmgroup
rmgroup:sjkiii@crl.com:alt.*:doit=rmgroup
rmgroup:smj@*.oro.net:alt.*:doit=rmgroup
rmgroup:zot@ampersand.com:alt.*:doit=rmgroup

##	GNU ( Free Software Foundation )
newgroup:gnu@prep.ai.mit.edu:gnu.*:doit=newgroup
newgroup:news@ai.mit.edu:gnu.*:doit=newgroup
newgroup:news@prep.ai.mit.edu:gnu.*:doit=newgroup
rmgroup:gnu@prep.ai.mit.edu:gnu.*:doit=rmgroup
rmgroup:news@ai.mit.edu:gnu.*:doit=rmgroup
rmgroup:news@prep.ai.mit.edu:gnu.*:doit=rmgroup
checkgroups:gnu@prep.ai.mit.edu:gnu.*:drop
checkgroups:news@ai.mit.edu:gnu.*:drop
checkgroups:news@prep.ai.mit.edu:gnu.*:drop

## CLARINET ( Features and News, Available on a commercial basis)
# *PGP*   See comment at top of file.
newgroup:cl*@clarinet.com:clari.*:verify-ClariNet.Group
rmgroup:cl*@clarinet.com:clari.*:verify-ClariNet.Group
checkgroups:cl*@clarinet.com:clari.*:verify-ClariNet.Group

## BIONET (Biology Network)
checkgroups:kristoff@*.bio.net:bionet.*:drop
checkgroups:news@*.bio.net:bionet.*:drop
checkgroups:shibumi@*.bio.net:bionet.*:drop
newgroup:dmack@*.bio.net:bionet.*:doit=newgroup
newgroup:kristoff@*.bio.net:bionet.*:doit=newgroup
newgroup:shibumi@*.bio.net:bionet.*:drop
rmgroup:dmack@*.bio.net:bionet.*:doit=rmgroup
rmgroup:kristoff@*.bio.net:bionet.*:doit=rmgroup
rmgroup:shibumi@*.bio.net:bionet.*:drop

## BIT (Gatewayed Mailing lists)
newgroup:jim@*american.edu:bit.*:verify-bit.admin
rmgroup:jim@*american.edu:bit.*:verify-bit.admin
checkgroups:jim@*american.edu:bit.*:verify-bit.admin

## BIZ (Business Groups)
newgroup:edhew@xenitec.on.ca:biz.*:doit=newgroup
rmgroup:edhew@xenitec.on.ca:biz.*:doit=rmgroup
checkgroups:edhew@xenitec.on.ca:biz.*:drop


## REGIONALS and other misc.

##  ACS hierarchy (Ohio State)
newgroup:kitw@magnus.acs.ohio-state.edu:acs.*:doit=newgroup
rmgroup:kitw@magnus.acs.ohio-state.edu:acs.*:doit=rmgroup
checkgroups:kitw@magnus.acs.ohio-state.edu:acs.*:drop

## AKR ( Akron, Ohio, USA) 
newgroup:red@redpoll.mrfs.oh.us:akr.*:doit=newgroup
rmgroup:red@redpoll.mrfs.oh.us:akr.*:doit=rmgroup
checkgroups:red@redpoll.mrfs.oh.us:akr.*:drop

## ALABAMA, HSV (USA)
newgroup:news@news.msfc.nasa.gov:alabama.*|hsv.*:verify-alabama-group-admin
rmgroup:news@news.msfc.nasa.gov:alabama.*|hsv.*:verify-alabama-group-admin
checkgroups:news@news.msfc.nasa.gov:alabama.*|hsv.*:verify-alabama-group-admin

## ALIVE (Sander van Minnen)
newgroup:alive@twix.xs4all.nl:alive.*:doit=newgroup
rmgroup:alive@twix.xs4all.nl:alive.*:doit=rmgroup
checkgroups:alive@twix.xs4all.nl:alive.*:drop

## AR (Argentina)
newgroup:jorge_f@nodens.fisica.unlp.edu.ar:ar.*:doit=newgroup
rmgroup:jorge_f@nodens.fisica.unlp.edu.ar:ar.*:doit=rmgroup
checkgroups:jorge_f@nodens.fisica.unlp.edu.ar:ar.*:drop

## ARKANE (Arkane Systems, UK )
# Contact: newsbastard@arkane.demon.co.uk
# URL: http://www.arkane.demon.co.uk/Newsgroups.html
checkgroups:newsbastard@arkane.demon.co.uk:arkane.*:drop
newgroup:newsbastard@arkane.demon.co.uk:arkane.*:doit=newgroup
rmgroup:newsbastard@arkane.demon.co.uk:arkane.*:doit=rmgroup

## AT (Austrian)
checkgroups:control@usenet.backbone.at:at.*:drop
newgroup:control@usenet.backbone.at:at.*:doit=newgroup
rmgroup:control@usenet.backbone.at:at.*:doit=rmgroup
newgroup:georg@blackbox.at:at.blackbox.*:doit=newgroup
rmgroup:georg@blackbox.at:at.blackbox.*:doit=rmgroup
checkgroups:georg@blackbox.at:at.blackbox.*:drop

## AUS (Australia)
newgroup:kre@*mu*au:aus.*:doit=newgroup
newgroup:revdoc@*uow.edu.au:aus.*:doit=newgroup
rmgroup:kre@*mu*au:aus.*:doit=rmgroup
rmgroup:revdoc@*uow.edu.au:aus.*:doit=rmgroup
checkgroups:kre@*mu*au:aus.*:drop
checkgroups:revdoc@*uow.edu.au:aus.*:drop

## AUSTIN (Texas)
newgroup:pug@arlut.utexas.edu:austin.*:doit=newgroup
rmgroup:pug@arlut.utexas.edu:austin.*:doit=rmgroup
checkgroups:pug@arlut.utexas.edu:austin.*:drop

## AZ (Arizona)
newgroup:system@asuvax.eas.asu.edu:az.*:doit=newgroup
rmgroup:system@asuvax.eas.asu.edu:az.*:doit=rmgroup
checkgroups:system@asuvax.eas.asu.edu:az.*:drop

## BA (SF Bay Area)
newgroup:grep@*.sbay.org:ba.jobs*:doit=newgroup
rmgroup:grep@*.sbay.org:ba.jobs*:doit=rmgroup
checkgroups:grep@*.sbay.org:ba.jobs*:drop

## BE (Belgium)
# Contact: usenet@innet.be
# URL: ftp://ftp.innet.be/pub/staff/stef/
newgroup:news@*innet.be:be.*:verify-be.announce.newgroups
rmgroup:news@i*nnet.be:be.*:verify-be.announce.newgroups
checkgroups:news@*innet.be:be.*:verify-be.announce.newgroups

## BERMUDA
newgroup:news@*ibl.bm:bermuda.*:doit=newgroup
rmgroup:news@*ibl.bm:bermuda.*:doit=rmgroup

## BLGTN ( Bloomington, In, USA)
newgroup:control@news.bloomington.in.us:blgtn.*:doit=newgroup
rmgroup:control@news.bloomington.in.us:blgtn.*:doit=rmgroup
checkgroups:control@news.bloomington.in.us:blgtn.*:drop

## BLN (Berlin, Germany)
checkgroups:news@*fu-berlin.de:bln.*:drop
newgroup:news@*fu-berlin.de:bln.*:doit=newgroup
rmgroup:news@*fu-berlin.de:bln.*:doit=rmgroup

## BOFH ( Bastard Operator From Hell )
newgroup:juphoff@*nrao.edu:bofh.*:drop
newgroup:peter@*taronga.com:bofh.*:drop
rmgroup:juphoff@*nrao.edu:bofh.*:drop
rmgroup:peter@*taronga.com:bofh.*:drop
checkgroups:juphoff@*nrao.edu:bofh.*:drop
checkgroups:peter@*taronga.com:bofh.*:drop

## CAPDIST (Albany, The Capital District, New York, USA)
newgroup:danorton@albany.net:capdist.*:doit=newgroup
rmgroup:danorton@albany.net:capdist.*:doit=rmgroup
checkgroups:danorton@albany.net:capdist.*:drop

## CARLETON (Canadian -- Carleton University)
newgroup:news@cunews.carleton.ca:carleton.*:doit=newgroup
newgroup:news@cunews.carleton.ca:carleton*class.*:log
rmgroup:news@cunews.carleton.ca:carleton.*:doit=rmgroup
checkgroups:news@cunews.carleton.ca:carleton.*:drop

## CHRISTNET newsgroups
checkgroups:news@fdma.com:christnet.*:drop
newgroup:news@fdma.com:christnet.*:doit=newgroup
rmgroup:news@fdma.com:christnet.*:doit=rmgroup

## CHI (Chicago, USA)
newgroup:lisbon@*interaccess.com:chi.*:doit=newgroup
newgroup:lisbon@*chi.il.us:chi.*:doit=newgroup
rmgroup:lisbon@*interaccess.com:chi.*:doit=rmgroup
rmgroup:lisbon@*chi.il.us:chi.*:doit=rmgroup
checkgroups:lisbon@*interaccess.com:chi.*:drop
checkgroups:lisbon@*chi.il.us:chi.*:drop

## CHILE (Chile)
newgroup:mod-cga@*webhost.cl:chile.*:doit=newgroup
rmgroup:mod-cga@*webhost.cl:chile.*:doit=rmgroup
checkgroups:mod-cga@*webhost.cl:chile.*:drop

## CHINESE (China and Chinese language groups)
newgroup:pinghua@stat.berkeley.edu:chinese.*:doit=newgroup
rmgroup:pinghua@stat.berkeley.edu:chinese.*:doit=rmgroup
checkgroups:pinghua@stat.berkeley.edu:chinese.*:drop

## CL (CL-Netz, German)
# Contact: CL-KOORDINATION@LINK-GOE.de (CL-Koordination, Link-Goe)
# URL: http://www.zerberus.de/org/cl/
# Syncable server: net2.dinoex.sub.de
# Key fingerprint: 21 ED D6 CB 05 56 6E E8  F6 F1 11 E9 2F 6C D5 BB
checkgroups:cl-koordination@dinoex.sub.org:cl.*:verify-cl.koordination.einstellungen
newgroup:cl-koordination@dinoex.sub.org:cl.*:verify-cl.koordination.einstellungen
rmgroup:cl-koordination@dinoex.sub.org:cl.*:verify-cl.koordination.einstellungen
newgroup:root@cl.sub.de:cl.*:drop
rmgroup:root@cl.sub.de:cl.*:drop
checkgroups:root@cl.sub.de:cl.*:drop
newgroup:dinoex@ud.dinoex.sub.org:cl.*:drop
rmgroup:dinoex@ud.dinoex.sub.org:cl.*:drop
checkgroups:dinoex@ud.dinoex.sub.org:cl.*:drop

## COMPUTER42 (German)
newgroup:news@*computer42.org:computer42.*:doit=newgroup
rmgroup:news@*computer42.org:computer42.*:doit=rmgroup
checkgroups:news@*computer42.org:computer42.*:drop

## CONCORDIA newsgroups (Concordia University, Montreal, Canada)
# URL: General University info at http://www.concordia.ca/
# Contact: newsmaster@concordia.ca
newgroup:news@newsflash.concordia.ca:concordia.*:doit=newgroup
rmgroup:news@newsflash.concordia.ca:concordia.*:doit=rmgroup
checkgroups:news@newsflash.concordia.ca:concordia.*:drop

## CPCU/IIA (American Institute for Chartered Property Casulty
## Underwriter/Insurance Institute of America, USA )
# Contact: miller@cpcuiia.org
# URL: www.aicpcu.org
checkgroups:miller@cpcuiia.org:cpcuiia.*:drop
newgroup:miller@cpcuiia.org:cpcuiia.*:doit=newgroup
rmgroup:miller@cpcuiia.org:cpcuiia.*:doit=rmgroup

## CZ newsgroups (Czech Republic)
checkgroups:petr.kolar@vslib.cz:cz.*:drop
newgroup:petr.kolar@vslib.cz:cz.*:doit=newgroup
rmgroup:petr.kolar@vslib.cz:cz.*:doit=rmgroup

## DC (Washington, D.C. USA )
newgroup:news@mattress.atww.org:dc.*:doit=newgroup
rmgroup:news@mattress.atww.org:dc.*:doit=rmgroup
checkgroups:news@mattress.atww.org:dc.*:drop

## DE (German language)
newgroup:moderator@dana.de:de.*:verify-de.admin.news.announce
newgroup:*@*:de.alt.*:doit=newgroup
checkgroups:moderator@dana.de:de.*:verify-de.admin.news.announce
rmgroup:moderator@dana.de:de.*:verify-de.admin.news.announce

## DFW (Dallas/Fort Worth, Texas, USA)
newgroup:eric@*cirr.com:dfw.*:doit=newgroup
rmgroup:eric@*cirr.com:dfw.*:doit=rmgroup
checkgroups:eric@*cirr.com:dfw.*:drop

## DK (Denmark)
newgroup:news@news.dknet.dk:dk.*:doit=newgroup
rmgroup:news@news.dknet.dk:dk.*:doit=rmgroup
checkgroups:news@news.dknet.dk:dk.*:drop

## EFN, EUG (Oregon)
newgroup:newsadmin@efn.org:efn.*|eug.*:verify-eug.config
rmgroup:newsadmin@efn.org:efn.*|eug.*:verify-eug.config
checkgroups:newsadmin@efn.org:efn.*|eug.*:verify-eug.config

## EHIME-U (? University, Japan ?)
newgroup:news@cc.nias.ac.jp:ehime-u.*:doit=newgroup
newgroup:news@doc.dpc.ehime-u.ac.jp:ehime-u.*:doit=newgroup
rmgroup:news@cc.nias.ac.jp:ehime-u.*:doit=rmgroup
rmgroup:news@doc.dpc.ehime-u.ac.jp:ehime-u.*:doit=rmgroup
checkgroups:news@cc.nias.ac.jp:ehime-u.*:drop
checkgroups:news@doc.dpc.ehime-u.ac.jp:ehime-u.*:drop

## ES (Spain)
# Contact: Juan.Garcia@rediris.es
# See: http://www.rediris.es/netnews/infonews/config.es.html
# See: http://news.rediris.es/infonews/docs/news_config/newsgroups.es
# Key fingerprint = 3B 63 18 6F 83 EA 89 82 95 1B 7F 8D B6 ED DD 87
newgroup:news@news.rediris.es:es.*:verify-es.news
rmgroup:news@news.rediris.es:es.*:verify-es.news
checkgroups:news@news.rediris.es:es.*:verify-es.news

## EUNET ( Europe )
newgroup:news@noc.eu.net:eunet.*:doit=newgroup
rmgroup:news@noc.eu.net:eunet.*:doit=rmgroup
checkgroups:news@noc.eu.net:eunet.*:drop

## FIDO newsgroups (FidoNet)
newgroup:root@mbh.org:fido.*:doit=newgroup
rmgroup:root@mbh.org:fido.*:doit=rmgroup
checkgroups:root@mbh.org:fido.*:drop

## FIDO.BELG.* newsgroups (FidoNet)
# URL: http://www.z2.fidonet.org/news/fido.belg.news/
# *PGP*   See comment at top of file.
checkgroups:fidobelg@mail.z2.fidonet.org:fido.belg.*:verify-fido.belg.news
newgroup:fidobelg@mail.z2.fidonet.org:fido.belg.*:verify-fido.belg.news
rmgroup:fidobelg@mail.z2.fidonet.org:fido.belg.*:verify-fido.belg.news

## FIDO7
newgroup:newgroups-request@fido7.ru:fido7.*:verify-fido7.announce.newgroups
rmgroup:newgroups-request@fido7.ru:fido7.*:verify-fido7.announce.newgroups
checkgroups:newgroups-request@fido7.ru:fido7.*:verify-fido7.announce.newgroups

## FJ (Japan and Japanese language)
newgroup:fj-committee@*o.jp:fj.*:doit=newgroup
rmgroup:fj-committee@*o.jp:fj.*:doit=rmgroup
checkgroups:fj-committee@*o.jp:fj.*:drop

## FL (Florida)
newgroup:hgoldste@news1.mpcs.com:fl.*:doit=newgroup
rmgroup:hgoldste@news1.mpcs.com:fl.*:doit=rmgroup
checkgroups:hgoldste@news1.mpcs.com:fl.*:drop
newgroup:scheidell@fdma.fdma.com:fl.*:doit=newgroup
rmgroup:scheidell@fdma.fdma.com:fl.*:doit=rmgroup
checkgroups:scheidell@fdma.fdma.com:fl.*:drop

## FR (French Language)

# *PGP*   See comment at top of file.
newgroup:control@usenet.fr.net:fr.*:verify-fr.announce.newgroups
rmgroup:control@usenet.fr.net:fr.*:verify-fr.announce.newgroups
checkgroups:control@usenet.fr.net:fr.*:verify-fr.announce.newgroups

#newgroup:control@usenet.fr.net:fr.*:doit=newgroup
#rmgroup:control@usenet.fr.net:fr.*:doit=rmgroup

## FREE
newgroup:*:free.*:drop
rmgroup:*:free.*:drop

## FUDAI (Japanese ?)
newgroup:news@picard.cs.osakafu-u.ac.jp:fudai.*:doit=newgroup
rmgroup:news@picard.cs.osakafu-u.ac.jp:fudai.*:doit=rmgroup
checkgroups:news@picard.cs.osakafu-u.ac.jp:fudai.*:drop

## GER (Hannover, Germany)
newgroup:fifi@hiss.han.de:ger.*:doit=newgroup
rmgroup:fifi@hiss.han.de:ger.*:doit=rmgroup
checkgroups:fifi@hiss.han.de:ger.*:drop

## GIT (Georgia Institute of Technology, USA )
newgroup:news@news.gatech.edu:git.*:doit=newgroup
newgroup:news@news.gatech.edu:git*class.*:log
rmgroup:news@news.gatech.edu:git.*:doit=rmgroup
checkgroups:news@news.gatech.edu:git.*:drop

## GOV (GOVNEWS Project)
# URL: http://www.govnews.org/govnews/
# PGPKEY URL: http://www.govnews.org/govnews/site-setup/gov.pgpkeys
newgroup:gov-usenet-announce-moderator@govnews.org:gov.*:verify-gov.usenet.announce
rmgroup:gov-usenet-announce-moderator@govnews.org:gov.*:verify-gov.usenet.announce
checkgroups:gov-usenet-announce-moderator@govnews.org:gov.*:verify-gov.usenet.announce=miscctl

## HAMILTON (Canadian)
newgroup:news@*dcss.mcmaster.ca:hamilton.*:doit=newgroup
rmgroup:news@*dcss.mcmaster.ca:hamilton.*:doit=rmgroup
checkgroups:news@*dcss.mcmaster.ca:hamilton.*:drop

## HAN (Korean Hangul)
newgroup:news@usenet.hana.nm.kr:han.*:verify-han.news.admin
rmgroup:news@usenet.hana.nm.kr:han.*:verify-han.news.admin
checkgroups:news@usenet.hana.nm.kr:han.*:verify-han.news.admin=miscctl

## HANNET & HANNOVER (Hannover, Germany)
newgroup:fifi@hiss.han.de:hannover.*|hannet.*:doit=newgroup
rmgroup:fifi@hiss.han.de:hannover.*|hannet.*:doit=rmgroup
checkgroups:fifi@hiss.han.de:hannover.*|hannet.*:drop

## HAWAII
newgroup:news@lava.net:hawaii.*:doit=newgroup
rmgroup:news@lava.net:hawaii.*:doit=rmgroup
checkgroups:news@lava.net:hawaii.*:drop

## HK (Hong Kong)
newgroup:hknews@comp.hkbu.edu.hk:hk.*:doit=newgroup
rmgroup:hknews@comp.hkbu.edu.hk:hk.*:doit=rmgroup
checkgroups:hknews@comp.hkbu.edu.hk:hk.*:drop

## HOUSTON (Houston, Texas, USA)
# *PGP*   See comment at top of file.
checkgroups:news@academ.com:houston.*:verify-houston.usenet.config=miscctl
newgroup:news@academ.com:houston.*:verify-houston.usenet.config
rmgroup:news@academ.com:houston.*:verify-houston.usenet.config

## HUN (Hungary)
checkgroups:kissg@*sztaki.hu:hun.*:drop
checkgroups:hg@*.elte.hu:hun.org.elte.*:drop
newgroup:kissg@*sztaki.hu:hun.*:doit=newgroup
newgroup:hg@*.elte.hu:hun.org.elte.*:doit=newgroup
rmgroup:kissg@*sztaki.hu:hun.*:doit=rmgroup
rmgroup:hg@*.elte.hu:hun.org.elte.*:doit=rmgroup

## IA (Iowa, USA)
newgroup:skunz@iastate.edu:ia.*:doit=newgroup
rmgroup:skunz@iastate.edu:ia.*:doit=rmgroup
checkgroups:skunz@iastate.edu:ia.*:drop

## IE (Ireland)
newgroup:usenet@ireland.eu.net:ie.*:doit=newgroup
rmgroup:usenet@ireland.eu.net:ie.*:doit=rmgroup
checkgroups:usenet@ireland.eu.net:ie.*:drop

## IEEE
newgroup:burt@ieee.org:ieee.*:doit=newgroup
rmgroup:burt@ieee.org:ieee.*:doit=rmgroup
checkgroups:burt@ieee.org:ieee.*:drop

## INFO newsgroups
newgroup:rjoyner@uiuc.edu:info.*:doit=newgroup
rmgroup:rjoyner@uiuc.edu:info.*:doit=rmgroup
checkgroups:rjoyner@uiuc.edu:info.*:drop

## ISC ( Japanese ?)
newgroup:news@sally.isc.chubu.ac.jp:isc.*:doit=newgroup
rmgroup:news@sally.isc.chubu.ac.jp:isc.*:doit=rmgroup
checkgroups:news@sally.isc.chubu.ac.jp:isc.*:drop

## ISRAEL and IL newsgroups (Israel)
newgroup:news@news.biu.ac.il:israel.*:doit=newgroup
rmgroup:news@news.biu.ac.ul:israel.*|il.*:doit=rmgroup
checkgroups:news@news.biu.ac.ul:israel.*|il.*:drop

## IT (Italian)
newgroup:stefano@*unipi.it:it.*:verify-it.announce.newgroups
rmgroup:stefano@*unipi.it:it.*:verify-it.announce.newgroups
checkgroups:stefano@*unipi.it:it.*:verify-it.announce.newgroups

## IU (Indiana University)
newgroup:news@usenet.ucs.indiana.edu:iu.*:doit=newgroup
newgroup:root@usenet.ucs.indiana.edu:iu.*:doit=newgroup
newgroup:*@usenet.ucs.indiana.edu:iu*class.*:log
rmgroup:news@usenet.ucs.indiana.edu:iu.*:doit=rmgroup
rmgroup:root@usenet.ucs.indiana.edu:iu.*:doit=rmgroup

## JAPAN (Japan)
checkgroups:usenet@*.iijnet.or.jp:japan.*:drop
checkgroups:news@*.iijnet.or.jp:japan.*:drop

## K12 ( US Educational Network )
newgroup:braultr@csmanoirs.qc.ca:k12.*:doit=newgroup
rmgroup:braultr@csmanoirs.qc.ca:k12.*:doit=rmgroup
checkgroups:braultr@csmanoirs.qc.ca:k12.*:drop

## KA (Germany)
newgroup:news@*karlsruhe.de:ka.*:doit=newgroup
rmgroup:news@*karlsruhe.de:ka.*:doit=rmgroup
checkgroups:news@*karlsruhe.de:ka.*:drop

## KANTO (Japan)
newgroup:*@*:kanto.*:doit=newgroup
rmgroup:ty@kamoi.imasy.or.jp:kanto.*:verify-kanto.news.network
checkgroups:ty@kamoi.imasy.or.jp:kanto.*:verify-kanto.news.network

## KIEL newsgroups
checkgroups:kris@white.schulung.netuse.de:kiel.*:drop
newgroup:kris@white.schulung.netuse.de:kiel.*:doit=newgroup
rmgroup:kris@white.schulung.netuse.de:kiel.*:doit=rmgroup

## LIU newsgroups (Sweden?)
newgroup:linus@tiny.lysator.liu.se:liu.*:doit=newgroup
rmgroup:linus@tiny.lysator.liu.se:liu.*:doit=rmgroup
checkgroups:linus@tiny.lysator.liu.se:liu.*:drop

## LINUX (gatewayed mailing lists for the Linux OS)
newgroup:hpa@yggdrasil.com:linux.*:doit=newgroup
rmgroup:hpa@yggdrasil.com:linux.*:doit=rmgroup
checkgroups:hpa@yggdrasil.com:linux.*:drop

## MALTA (Republic of Malta)
newgroup:cmeli@cis.um.edu.mt:malta.*:verify-malta.config
rmgroup:cmeli@cis.um.edu.mt:malta.*:verify-malta.config
checkgroups:cmeli@cis.um.edu.mt:malta.*:verify-malta.config

## MAUS ( MausNet, German )
# Key fingerprint: 82 52 C7 70 26 B9 72 A1  37 98 55 98 3F 26 62 3E
newgroup:guenter@gst0hb.*.de:maus.*:verify-maus-info
rmgroup:guenter@gst0hb.*.de:maus.*:verify-maus-info
checkgroups:guenter@gst0hb.*.de:maus.*:verify-maus-info=miscctl

## MCOM ( Netscape Inc, USA) 
newgroup:*@*.mcom.com:mcom.*:doit=newgroup
rmgroup:*@*.mcom.com:mcom.*:doit=rmgroup
checkgroups:*@*.mcom.com:mcom.*:drop

## ME (Maine, USA)
newgroup:kerry@maine.maine.edu:me.*:doit=newgroup
rmgroup:kerry@maine.maine.edu:me.*:doit=rmgroup
checkgroups:kerry@maine.maine.edu:me.*:drop

## MEDLUX (Russia)
# URL: ftp://ftp.medlux.ru/pub/news/medlux.grp
newgroup:neil@new*.medlux.ru:medlux.*:doit=newgroup
rmgroup:neil@new*.medlux.ru:medlux.*:doit=rmgroup
checkgroups:neil@new*.medlux.ru:medlux.*:drop

## MELB ( Melbourne, Australia)
newgroup:kre@*mu*au:melb.*:doit=newgroup
newgroup:revdoc@*uow.edu.au:melb.*:doit=newgroup
rmgroup:kre@*mu*au:melb.*:doit=rmgroup
rmgroup:revdoc@*uow.edu.au:melb.*:doit=rmgroup
checkgroups:kre@*mu*au:melb.*:drop
checkgroups:revdoc@*uow.edu.au:melb.*:drop

## METOCEAN newsgroups (ISP in Japan)
newgroup:fwataru@*.metocean.co.jp:metocean.*:doit=newgroup
rmgroup:fwataru@*.metocean.co.jp:metocean.*:doit=rmgroup
checkgroups:fwataru@*.metocean.co.jp:metocean.*:drop

## MI (Michigan)
newgroup:news@lokkur:mi.*:doit=newgroup
rmgroup:news@lokkur:mi.*:doit=rmgroup
checkgroups:news@lokkur:mi.*:drop

## MUC (Munchen, Germany. Gatewayed mailing lists??)
newgroup:muc-cmsg@muenchen.pro-bahn.org:muc.*:verify-muc.admin
rmgroup:muc-cmsg@muenchen.pro-bahn.org:muc.*:verify-muc.admin
checkgroups:muc-cmsg@muenchen.pro-bahn.org:muc.*:verify-muc.admin

## NAGASAKI-U ( Nagasaki University, Japan ?)
newgroup:root@*nagasaki-u.ac.jp:nagasaki-u.*:doit=newgroup
rmgroup:root@*nagasaki-u.ac.jp:nagasaki-u.*:doit=rmgroup
checkgroups:root@*nagasaki-u.ac.jp:nagasaki-u.*:drop

## NBG (German, ?)
newgroup:muftix@*.nbg.sub.org|muftix@*.de:nbg.*:doit=newgroup
rmgroup:muftix@*.nbg.sub.org|muftix@*.de:nbg.*:doit=rmgroup
checkgroups:muftix@*.nbg.sub.org|muftix@*.de:nbg.*:drop

## NCTU newsgroups (Taiwan)
newgroup:chen@cc.nctu.edu.tw:nctu.*:doit=newgroup
rmgroup:chen@cc.nctu.edu.tw:nctu.*:doit=rmgroup
checkgroups:chen@cc.nctu.edu.tw:nctu.*:drop

## NET ( Usenet 2 )
newgroup:peter@taronga.com|control@usenet2.org:net.*:verify-control@usenet2.org
rmgroup:peter@taronga.com|control@usenet2.org:net.*:verify-control@usenet2.org
checkgroups:peter@taronga.com|control@usenet2.org:net.*:verify-control@usenet2.org

## NIAGARA (Niagara Peninsula, US/CAN)
newgroup:news@niagara.com:niagara.*:doit=newgroup
rmgroup:news@niagara.com:niagara.*:doit=rmgroup
checkgroups:news@niagara.com:niagara.*:drop

## NIAS (Japanese ?)
newgroup:news@cc.nias.ac.jp:nias.*:doit=newgroup
rmgroup:news@cc.nias.ac.jp:nias.*:doit=rmgroup
checkgroups:news@cc.nias.ac.jp:nias.*:drop

## NL (Netherlands)
# Contact: nl-admin@nic.surfnet.nl
# URL: http://www.xs4all.nl/~egavic/NL/ (Dutch)
# URL: http://www.kinkhorst.com/usenet/nladmin.en.html (English)
# Key fingerprint: 45 20 0B D5 A1 21 EA 7C  EF B2 95 6C 25 75 4D 27
newgroup:nl-admin@nic.surfnet.nl:nl.*:verify-nl.newsgroups
rmgroup:nl-admin@nic.surfnet.nl:nl.*:verify-nl.newsgroups
checkgroups:nl-admin@nic.surfnet.nl:nl.*:verify-nl.newsgroups

## NLNET newsgroups (Netherlands ISP)
newgroup:beheer@nl.net:nlnet.*:doit=newgroup
rmgroup:beheer@nl.net:nlnet.*:doit=rmgroup
checkgroups:beheer@nl.net:nlnet.*:drop

## NM (New Mexico, USA)
newgroup:news@tesuque.cs.sandia.gov:nm.*:doit=newgroup
rmgroup:news@tesuque.cs.sandia.gov:nm.*:doit=rmgroup
checkgroups:news@tesuque.cs.sandia.gov:nm.*:drop

## NO (Norway)
## See also http://www.usenet.no/
checkgroups:control@usenet.no:no.*:verify-no-hir-control
newgroup:control@usenet.no:no.*:verify-no-hir-control
newgroup:*@*.no:no.alt.*:doit=newgroup
rmgroup:control@usenet.no:no.*:verify-no-hir-control

## NORTH (Germany)
newgroup:olav@*north.de:north.*:doit=newgroup
checkgroups:olav@*north.de:north.*:drop
rmgroup:olav@*north.de:north.*:doit=rmgroup

## NV (Nevada)
newgroup:doctor@netcom.com:nv.*:doit=newgroup
newgroup:cshapiro@netcom.com:nv.*:doit=newgroup
rmgroup:doctor@netcom.com:nv.*:doit=rmgroup
rmgroup:cshapiro@netcom.com:nv.*:doit=rmgroup
checkgroups:doctor@netcom.com:nv.*:drop
checkgroups:cshapiro@netcom.com:nv.*:drop

## NY (New York State, USA)
newgroup:root@ny.psca.com:ny.*:doit=newgroup
rmgroup:root@ny.psca.com:ny.*:doit=rmgroup
checkgroups:root@ny.psca.com:ny.*:drop

## NZ (New Zealand)
# Contact root@usenet.net.nz
# URL: http://usenet.net.nz
# URL: finger://root@usenet.net.nz
# PGP fingerprint: 07 DF 48 AA D0 ED AA 88  16 70 C5 91 65 3D 1A 28
newgroup:root@usenet.net.nz:nz.*:verify-nz-hir-control
rmgroup:root@usenet.net.nz:nz.*:verify-nz-hir-control
checkgroups:root@usenet.net.nz:nz.*:verify-nz-hir-control

## OC newsgroups (?)
newgroup:bob@tsunami.sugarland.unocal.com:oc.*:doit=newgroup
rmgroup:bob@tsunami.sugarland.unocal.com:oc.*:doit=rmgroup
checkgroups:bob@tsunami.sugarland.unocal.com:oc.*:drop

## OH (Ohio, USA)
newgroup:trier@ins.cwru.edu:oh.*:doit=newgroup
rmgroup:trier@ins.cwru.edu:oh.*:doit=rmgroup
checkgroups:trier@ins.cwru.edu:oh.*:drop

## OK (Oklahoma, USA)
newgroup:quentin@*qns.com:ok.*:doit=newgroup
rmgroup:quentin@*qns.com:ok.*:doit=rmgroup
checkgroups:quentin@*qns.com:ok.*:drop

## OTT (Ottawa, Ontario, Canada)
# Contact: onag@pinetree.org
# URL: http://www.pinetree.org/ONAG/
newgroup:news@bnr.ca:ott.*:doit=newgroup
newgroup:news@nortel.ca:ott.*:doit=newgroup
newgroup:clewis@ferret.ocunix.on.ca:ott.*:doit=newgroup
newgroup:news@ferret.ocunix.on.ca:ott.*:doit=newgroup
newgroup:news@*pinetree.org:ott.*:doit=newgroup
newgroup:gordon@*pinetree.org:ott.*:doit=newgroup
newgroup:dave@revcan.ca:ott.*:doit=newgroup
rmgroup:news@bnr.ca:ott.*:doit=rmgroup
rmgroup:news@nortel.ca:ott.*:doit=rmgroup
rmgroup:clewis@ferret.ocunix.on.ca:ott.*:doit=rmgroup
rmgroup:news@ferret.ocunix.on.ca:ott.*:doit=rmgroup
rmgroup:news@*pinetree.org:ott.*:doit=rmgroup
rmgroup:gordon@*pinetree.org:ott.*:doit=rmgroup
rmgroup:dave@revcan.ca:ott.*:doit=rmgroup
checkgroups:news@bnr.ca:ott.*:drop
checkgroups:news@nortel.ca:ott.*:drop
checkgroups:clewis@ferret.ocunix.on.ca:ott.*:drop
checkgroups:news@ferret.ocunix.on.ca:ott.*:drop
checkgroups:news@*pinetree.org:ott.*:drop
checkgroups:gordon@*pinetree.org:ott.*:drop
checkgroups:dave@revcan.ca:ott.*:drop

## PA (Pennsylvania, USA)
# URL: http://www.netcom.com/~rb1000/pa_hierarchy/
newgroup:fxp@epix.net:pa.*:doit=newgroup
rmgroup:fxp@epix.net:pa.*:doit=rmgroup
checkgroups:fxp@epix.net:pa.*:drop

## PGH (Pittsburgh, Pennsylvania, USA)
checkgroups:pgh-config@psc.edu:pgh.*:verify-pgh.config
newgroup:pgh-config@psc.edu:pgh.*:verify-pgh.config
rmgroup:pgh-config@psc.edu:pgh.*:verify-pgh.config

## PHL (Philadelphia, Pennsylvania, USA)
newgroup:news@vfl.paramax.com:phl.*:doit=newgroup
rmgroup:news@vfl.paramax.com:phl.*:doit=rmgroup
checkgroups:news@vfl.paramax.com:phl.*:drop

## PIN (Personal Internauts' NetNews)
newgroup:pin-admin@forus.or.jp:pin.*:doit=newgroup
rmgroup:pin-admin@forus.or.jp:pin.*:doit=rmgroup
checkgroups:pin-admin@forus.or.jp:pin.*:drop

## PL (Poland and Polish language)
## For more info, see http://www.ict.pwr.wroc.pl/doc/news-pl-new-site-faq.html
newgroup:michalj@*fuw.edu.pl:pl.*:verify-pl.announce.newgroups
newgroup:newgroup@usenet.pl:pl.*:verify-pl.announce.newgroups
rmgroup:michalj@*fuw.edu.pl:pl.*:verify-pl.announce.newgroups
rmgroup:newgroup@usenet.pl:pl.*:verify-pl.announce.newgroups
checkgroups:michalj@*fuw.edu.pl:pl.*:verify-pl.announce.newgroups
checkgroups:newgroup@usenet.pl:pl.*:verify-pl.announce.newgroups

## PT (Portugal and Portuguese language)
newgroup:pmelo@*inescc.pt:pt.*:doit=newgroup
rmgroup:pmelo@*inescc.pt:pt.*:doit=rmgroup
checkgroups:pmelo@*inescc.pt:pt.*:drop

## RELCOM ( Commonwealth of Independent States)
## The official list of relcom groups is supposed to be available from
## ftp://ftp.kiae.su/relcom/netinfo/telconfs.txt
checkgroups:dmart@new*.relcom.ru:relcom.*:drop
newgroup:dmart@new*.relcom.ru:relcom.*:doit=newgroup
rmgroup:dmart@new*.relcom.ru:relcom.*:doit=rmgroup

## SACHSNET (German)
newgroup:root@lusatia.de:sachsnet.*:doit=newgroup
rmgroup:root@lusatia.de:sachsnet.*:doit=rmgroup
checkgroups:root@lusatia.de:sachsnet.*:drop

## SAT (San Antonio, Texas, USA)
newgroup:satgroup@endicor.com:sat.*:verify-satgroup@endicor.com
rmgroup:satgroup@endicor.com:sat.*:verify-satgroup@endicor.com
checkgroups:satgroup@endicor.com:sat.*:verify-satgroup@endicor.com

## SBAY (South Bay/Silicon Valley, California)
newgroup:steveh@grafex.sbay.org:sbay.*:doit=newgroup
newgroup:ikluft@thunder.sbay.org:sbay.*:doit=newgroup
rmgroup:steveh@grafex.sbay.org:sbay.*:drop
rmgroup:ikluft@thunder.sbay.org:sbay.*:drop
checkgroups:steveh@grafex.sbay.org:sbay.*:drop
checkgroups:ikluft@thunder.sbay.org:sbay.*:drop

## SCHULE (German schools on the net)
newgroup:newsctrl@schule.de:schule.*:verify-schule.konfig
rmgroup:newsctrl@schule.de:schule.*:verify-schule.konfig
checkgroups:newsctrl@schule.de:schule.*:verify-schule.konfig

## SDNET (Greater San Diego Area, California, USA)
newgroup:wkronert@*.ucsd.edu:sdnet.*:doit=newgroup
rmgroup:wkronert@*.ucsd.edu:sdnet.*:doit=rmgroup
checkgroups:wkronert@*.ucsd.edu:sdnet.*:drop

## SEATTLE (Seattle, Washington, USA)
newgroup:billmcc@akita.com:seattle.*:doit=newgroup
newgroup:graham@ee.washington.edu:seattle.*:doit=newgroup
rmgroup:billmcc@akita.com:seattle.*:doit=rmgroup
rmgroup:graham@ee.washington.edu:seattle.*:doit=rmgroup
checkgroups:billmcc@akita.com:seattle.*:drop
checkgroups:graham@ee.washington.edu:seattle.*:drop

## SFNET newsgroups (Finland)
newgroup:hmj@*cs.tut.fi:sfnet.*:doit=newgroup
rmgroup:hmj@*cs.tut.fi:sfnet.*:doit=rmgroup
checkgroups:hmj@*cs.tut.fi:sfnet.*:drop

## SHAMASH (Jewish)
newgroup:archives@israel.nysernet.org:shamash.*:doit=newgroup
rmgroup:archives@israel.nysernet.org:shamash.*:doit=rmgroup
checkgroups:archives@israel.nysernet.org:shamash.*:drop

## SI (The Republic of Slovenia)
# *PGP*   See comment at top of file.
checkgroups:news-admin@arnes.si:si.*:verify-si.news.announce.newsgroups=miscctl
newgroup:news-admin@arnes.si:si.*:verify-si.news.announce.newsgroups
rmgroup:news-admin@arnes.si:si.*:verify-si.news.announce.newsgroups

## SK (Slovakia)
checkgroups:uhlar@ccnews.ke.sanet.sk:sk.*:drop
newgroup:uhlar@ccnews.ke.sanet.sk:sk.*:doit=newgroup
rmgroup:uhlar@ccnews.ke.sanet.sk:sk.*:doit=rmgroup

## SLAC newsgroups (Stanford Linear Accelerator Center)
newgroup:bebo@slacvm.slac.stanford.edu:slac.*:doit=newgroup
rmgroup:bebo@slacvm.slac.stanford.edu:slac.*:doit=rmgroup
checkgroups:bebo@slacvm.slac.stanford.edu:slac.*:drop

## SOLENT (Solent region, England)
newgroup:news@tcp.co.uk:solent.*:doit=newgroup
rmgroup:news@tcp.co.uk:solent.*:doit=rmgroup
checkgroups:news@tcp.co.uk:solent.*:drop

## STGT (Stuttgart, Germany)
checkgroups:news@news.uni-stuttgart.de:stgt.*:drop
newgroup:news@news.uni-stuttgart.de:stgt.*:doit=newgroup
rmgroup:news@news.uni-stuttgart.de:stgt.*:doit=rmgroup

## STL (Saint Louis, Missouri, USA)
newgroup:news@icon-stl.net:stl.*:doit=newgroup
rmgroup:news@icon-stl.net:stl.*:doit=rmgroup
checkgroups:news@icon-stl.net:stl.*:drop

## SURFNET (? in the Netherlands)
newgroup:news@info.nic.surfnet.nl:surfnet.*:doit=newgroup
rmgroup:news@info.nic.surfnet.nl:surfnet.*:doit=rmgroup
checkgroups:news@info.nic.surfnet.nl:surfnet.*:drop

## SWNET (Sverige, Sweden)
newgroup:ber@sunic.sunet.se:swnet.*:doit=newgroup
rmgroup:ber@sunic.sunet.se:swnet.*:doit=rmgroup
checkgroups:ber@sunic.sunet.se:swnet.*:drop

## TCFN (Toronto Free Community Network, Canada)
newgroup:news@t-fcn.net:tcfn.*:doit=newgroup
rmgroup:news@t-fcn.net:tcfn.*:doit=rmgroup
checkgroups:news@t-fcn.net:tcfn.*:drop

## TNN ( The Network News, Japan )
newgroup:netnews@news.iij.ad.jp:tnn.*:doit=newgroup
newgroup:tnn@iij-mc.co.jp:tnn.*:doit=newgroup
rmgroup:tnn@iij-mc.co.jp:tnn.*:doit=rmgroup
rmgroup:netnews@news.iij.ad.jp:tnn.*:doit=rmgroup
checkgroups:tnn@iij-mc.co.jp:tnn.*:drop
checkgroups:netnews@news.iij.ad.jp:tnn.*:drop

## TRIANGLE (Central North Carolina, USA )
newgroup:jfurr@acpub.duke.edu:triangle.*:doit=newgroup
newgroup:tas@concert.net:triangle.*:doit=newgroup
newgroup:news@news.duke.edu:triangle.*:doit=newgroup
rmgroup:jfurr@acpub.duke.edu:triangle.*:doit=rmgroup
rmgroup:tas@concert.net:triangle.*:doit=rmgroup
rmgroup:news@news.duke.edu:triangle.*:doit=rmgroup
checkgroups:jfurr@acpub.duke.edu:triangle.*:drop
checkgroups:tas@concert.net:triangle.*:drop
checkgroups:news@news.duke.edu:triangle.*:drop

## TW (Taiwan)
newgroup:ltc@news.cc.nctu.edu.tw:tw.*:doit=newgroup
newgroup:k-12@news.nchu.edu.tw:tw.k-12.*:doit=newgroup
rmgroup:ltc@news.cc.nctu.edu.tw:tw.*:doit=rmgroup
rmgroup:k-12@news.nchu.edu.tw:tw.k-12*:doit=rmgroup
checkgroups:ltc@news.cc.nctu.edu.tw:tw.*:drop
checkgroups:k-12@news.nchu.edu.tw:tw.k-12*:drop

## TX (Texas, USA)
newgroup:eric@cirr.com:tx.*:doit=newgroup
newgroup:fletcher@cs.utexas.edu:tx.*:doit=newgroup
newgroup:usenet@academ.com:tx.*:doit=newgroup
rmgroup:eric@cirr.com:tx.*:doit=rmgroup
rmgroup:fletcher@cs.utexas.edu:tx.*:doit=rmgroup
rmgroup:usenet@academ.com:tx.*:doit=rmgroup
checkgroups:eric@cirr.com:tx.*:drop
checkgroups:fletcher@cs.utexas.edu:tx.*:drop
checkgroups:usenet@academ.com:tx.*:drop

## UCB ( University of California Berkeley, USA)
newgroup:usenet@agate.berkeley.edu:ucb.*:doit=newgroup
rmgroup:usenet@agate.berkeley.edu:ucb.*:doit=rmgroup
checkgroups:usenet@agate.berkeley.edu:ucb.*:drop

## UCD ( University of California Davis, USA )
newgroup:usenet@rocky.ucdavis.edu:ucd.*:doit=newgroup
newgroup:usenet@mark.ucdavis.edu:ucd.*:doit=newgroup
rmgroup:usenet@rocky.ucdavis.edu:ucd.*:doit=rmgroup
rmgroup:usenet@mark.ucdavis.edu:ucd.*:doit=rmgroup
checkgroups:usenet@rocky.ucdavis.edu:ucd.*:drop
checkgroups:usenet@mark.ucdavis.edu:ucd.*:drop

## UIUC (University of Illinois, USA )
newgroup:p-pomes@*.cso.uiuc.edu:uiuc.*:doit=newgroup
newgroup:paul@*.cso.uiuc.edu:uiuc.*:doit=newgroup
rmgroup:p-pomes@*.cso.uiuc.edu:uiuc.*:doit=rmgroup
rmgroup:paul@*.cso.uiuc.edu:uiuc.*:doit=rmgroup
checkgroups:p-pomes@*.cso.uiuc.edu:uiuc.*:drop
checkgroups:paul@*.cso.uiuc.edu:uiuc.*:drop

## UK (United Kingdom of Great Britain and Northern Ireland)
checkgroups:control@usenet.org.uk:uk.*:verify-uk.net.news.announce
newgroup:control@usenet.org.uk:uk.*:verify-uk.net.news.announce
rmgroup:control@usenet.org.uk:uk.*:verify-uk.net.news.announce

## UKR ( Ukraine )
newgroup:news-server@sita.kiev.ua:ukr.*:doit=newgroup
rmgroup:news-server@sita.kiev.ua:ukr.*:doit=rmgroup
checkgroups:news-server@sita.kiev.ua:ukr.*:drop

## UMN (University of Minnesota, USA )
newgroup:edh@*.tc.umn.edu:umn.*:doit=newgroup
newgroup:news@*.tc.umn.edu:umn.*:doit=newgroup
newgroup:Michael.E.Hedman-1@umn.edu:umn.*:doit=newgroup
newgroup:edh@*.tc.umn.edu:umn*class.*:log
newgroup:news@*.tc.umn.edu:umn*class.*:log
newgroup:Michael.E.Hedman-1@umn.edu:umn*class.*:log
rmgroup:news@*.tc.umn.edu:umn.*:doit=rmgroup
rmgroup:edh@*.tc.umn.edu:umn.*:doit=rmgroup
rmgroup:Michael.E.Hedman-1@umn.edu:umn.*:doit=rmgroup
checkgroups:news@*.tc.umn.edu:umn.*:drop
checkgroups:edh@*.tc.umn.edu:umn.*:drop
checkgroups:Michael.E.Hedman-1@umn.edu:umn.*:drop

## UN (international)
# URL: http://www.itu.int/Conferences/un/
newgroup:news@news.itu.int:un.*:verify-ungroups@news.itu.int
rmgroup:news@newsitu.int:un.*:verify-ungroups@news.itu.int
checkgroups:news@newsitu.int:un.*:verify-ungroups@news.itu.int

## US (United States)
newgroup:usadmin@wwa.com:us.*:doit=newgroup
rmgroup:usadmin@wwa.com:us.*:doit=rmgroup
checkgroups:usadmin@wwa.com:us.*:drop
# Please note that the PGP key has been registered but that the authority
# is not yet set up to sign her control messages. 
# newgroup:usadmin@wwa.com:us.*:verify-us.config=newgroup
# rmgroup:usadmin@wwa.com:us.*:verify-us.config=rmgroup

## UT (U. of Toronto)
# newgroup:news@ecf.toronto.edu:ut.*:doit=newgroup
# newgroup:news@ecf.toronto.edu:ut.class.*:log
# rmgroup:news@ecf.toronto.edu:ut.*:doit=rmgroup
# checkgroups:news@ecf.toronto.edu:ut.*:drop

## UTA (Finnish)
newgroup:news@news.cc.tut.fi:uta.*:doit=newgroup
rmgroup:news@news.cc.tut.fi:uta.*:doit=rmgroup
checkgroups:news@news.cc.tut.fi:uta.*:drop

## UTEXAS (University of Texas, USA )
newgroup:fletcher@cs.utexas.edu:utexas.*:doit=newgroup
newgroup:fletcher@cs.utexas.edu:utexas*class.*:log
newgroup:news@geraldo.cc.utexas.edu:utexas.*:doit=newgroup
newgroup:news@geraldo.cc.utexas.edu:utexas*class.*:log
rmgroup:news@geraldo.cc.utexas.edu:utexas.*:doit=rmgroup
rmgroup:fletcher@cs.utexas.edu:utexas.*:doit=rmgroup
checkgroups:news@geraldo.cc.utexas.edu:utexas.*:drop
checkgroups:fletcher@cs.utexas.edu:utexas.*:drop

## UW (University of Waterloo, Canada)
newgroup:bcameron@math.uwaterloo.ca:uw.*:doit=newgroup
rmgroup:bcameron@math.uwaterloo.ca:uw.*:doit=rmgroup
checkgroups:bcameron@math.uwaterloo.ca:uw.*:drop

## UWO (University of Western Ontario, London, Canada)
newgroup:reggers@julian.uwo.ca:uwo.*:doit=newgroup
rmgroup:reggers@julian.uwo.ca:uwo.*:doit=rmgroup
checkgroups:reggers@julian.uwo.ca:uwo.*:drop

## VEGAS (Las Vegas, Nevada, USA)
newgroup:cshapiro@netcom.com:vegas.*:doit=newgroup
newgroup:doctor@netcom.com:vegas.*:doit=newgroup
rmgroup:cshapiro@netcom.com:vegas.*:doit=rmgroup
rmgroup:doctor@netcom.com:vegas.*:doit=rmgroup
checkgroups:cshapiro@netcom.com:vegas.*:drop
checkgroups:doctor@netcom.com:vegas.*:drop

## VMSNET ( VMS Operating System )
newgroup:cts@dragon.com:vmsnet.*:doit=newgroup
rmgroup:cts@dragon.com:vmsnet.*:doit=rmgroup
checkgroups:cts@dragon.com:vmsnet.*:drop

## WADAI (Japanese ?)
newgroup:kohe-t@*wakayama-u.ac.jp:wadai.*:doit=newgroup
rmgroup:kohe-t@*wakayama-u.ac.jp:wadai.*:doit=rmgroup
checkgroups:kohe-t@*wakayama-u.ac.jp:wadai.*:drop

## WASH (Washington State, USA)
newgroup:graham@ee.washington.edu:wash.*:doit=newgroup
rmgroup:graham@ee.washington.edu:wash.*:doit=rmgroup
checkgroups:graham@ee.washington.edu:wash.*:drop

## WG (Germany ?)
newgroup:sec@*42.org:wg.*:doit=newgroup
rmgroup:sec@*42.org:wg.*:doit=rmgroup
checkgroups:sec@*42.org:wg.*:drop

## WPI (Worcester Polytechnic Institute, Worcester, MA)
newgroup:aej@*.wpi.edu:wpi.*:doit=newgroup
rmgroup:aej@*.wpi.edu:wpi.*:doit=rmgroup
checkgroups:aej@*.wpi.edu:wpi.*:drop

## Z-NETZ (German email network.)
checkgroups:z-netz-gatekoo@as-node.jena.thur.de:z-netz.*:verify-z-netz.koordination.user+sysops
newgroup:z-netz-gatekoo@as-node.jena.thur.de:z-netz.*:verify-z-netz.koordination.user+sysops
newgroup:*@*.de:z-netz.alt.*:doit=newgroup
newgroup:*@*.sub.org:z-netz.alt.*:doit=newgroup
rmgroup:z-netz-gatekoo@as-node.jena.thur.de:z-netz.*:verify-z-netz.koordination.user+sysops

## ZA (South Africa)
newgroup:root@duvi.eskom.co.za:za.*:doit=newgroup
newgroup:ccfj@hippo.ru.ac.za:za.*:doit=newgroup
rmgroup:root@duvi.eskom.co.za:za.*:doit=rmgroup
rmgroup:ccfj@hippo.ru.ac.za:za.*:doit=rmgroup
checkgroups:root@duvi.eskom.co.za:za.*:drop
checkgroups:ccfj@hippo.ru.ac.za:za.*:drop

## People we'd rather just send over the falls in a barrel
## (aka. The Idiot List) Keep this at the end.

## "Real" People
all:*@*michigan.com:*:drop
all:*@*rabbit.net:*:drop
all:*@*anatolia.org:*:drop
all:djk@*:*:drop
all:*@*tasp*:*:drop
all:*@espuma*:*:drop
all:cosar@*:*:drop
all:*@*caprica.com:*:drop
all:gritton@montana.et.byu.edu:*:drop
all:riley@planet*:*:drop
all:biff@bit.net:*:drop
all:jeffery.hunt@*sympatico.ca:*:drop
all:oliver1@ici.net:*:drop
all:bedwarm@*geocities.com:*:drop
all:*@hotmail.com:*:drop

## "Open" sites
all:*@*infi.net:*:drop
all:*@*.cs.du.edu:*:drop
all:*@*netcom.com:alt.*:drop
all:*@*.penet.*:*:drop
all:*@utmb.edu:*:drop
all:*@lbss.fcps*:*:drop
all:*@freenet.carleton.ca:*:drop
all:*@kaiwan.com:*:drop
all:*@ripco.com:*:drop

## The usual aliases
all:*@*heaven*:*:drop
all:*@*hell*:*:drop
all:*@*nowhere*:*:drop
all:noone@*:*:drop
all:nobody@*:*:drop
all:*d00d*@*:*:drop
all:*dude@*:*:drop
all:*warez*@*:*:drop
all:*@*warez*:*:drop
all:hacker@*:*:drop
all:guest@*:*:drop
all:cracker@*:*:drop
all:news@mail:*:drop

