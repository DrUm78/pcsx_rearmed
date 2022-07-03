#!/bin/sh
# Copy PCSX binary in /usr/games if md5 is different and add CHD support for RetroFE
if [ `md5sum /usr/games/pcsx | cut -d' ' -f1` != `md5sum pcsx | cut -d' ' -f1` ]; then
	rw
	cp -f pcsx /usr/games
	sed -i 's|list.extensions.*|list.extensions = cue,CUE,pbp,PBP,chd,CHD|' /usr/games/collections/PS1/settings.conf
	ro
fi
exec /usr/games/launchers/psone_launch_pcsx.sh "$1"
