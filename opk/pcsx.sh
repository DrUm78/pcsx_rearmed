#!/bin/sh
# Copy PCSX binary in /usr/games if md5 is different
if [ `md5sum /usr/games/pcsx | cut -d' ' -f1` != `md5sum pcsx | cut -d' ' -f1` ]; then
	rw
	cp -f pcsx /usr/games
	ro
fi
exec /usr/games/launchers/psone_launch_pcsx.sh "$1"
