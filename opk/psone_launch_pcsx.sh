#!/bin/sh
# Compare md5 of the PCSX binary in /usr/games with the OPK one and add CHD support for RetroFE
if [ `md5sum /usr/games/pcsx | cut -d' ' -f1` != `md5sum pcsx | cut -d' ' -f1` ]; then
    rw
    cp -f pcsx /usr/games
    sed -i 's|list.extensions = bin,BIN,cue,CUE,pbp,PBP|list.extensions = bin,BIN,cue,CUE,pbp,PBP,chd,CHD|' /usr/games/collections/PS1/settings.conf
    ro
fi
exec /usr/games/launchers/psone_launch_pcsx.sh "$1"
