</$objtype/mkfile

BIN=/$objtype/bin/osm
TARG=fs map osrmfs
RC=get
OFILES=gettile.$O readfile.$O

</sys/src/cmd/mkmany


install:V:
	mkdir -p $BIN
	for (i in $TARG)
		mk $MKFLAGS $i.install
	for (i in $RC)
		mk $MKFLAGS $i.rcinstall

%.rcinstall:V:
	cp $stem $BIN/$stem
	chmod +x $BIN/$stem
