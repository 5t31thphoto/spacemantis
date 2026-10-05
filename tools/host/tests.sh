#!/bin/sh
# Focused regression tests (desktop compiler only). See run.sh for the long soak.
set -e
cd "$(dirname "$0")"
S=../../src
SRCS="$S/sheet.cpp $S/universe.cpp $S/resolve.cpp $S/contracts.cpp $S/sim.cpp $S/content.cpp $S/lore.cpp $S/haptics.cpp $S/trip.cpp $S/atlas.cpp $S/journal.cpp $S/savefile.cpp"
for t in jobtest heretest atlastest hubtest alientest docksweep savetest; do g++ -std=gnu++17 -O2 -Istubs -I$S $t.cpp $SRCS -o $t 2>&1 | grep -E "error" || true; done
rm -rf sdcard snaps; mkdir -p snaps
./jobtest | tail -1; ./heretest; ./atlastest | tail -1; ./hubtest | tail -1; rm -rf sdcard; ./alientest | tail -1; ./docksweep
rm -rf sdcard; ./savetest play | tail -1; echo "power cycle:"; ./savetest boot
rm -rf sdcard snaps jobtest heretest atlastest hubtest alientest docksweep savetest
