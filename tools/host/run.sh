#!/bin/sh
# Build and soak the firmware logic on a desktop compiler. No ESP32 needed.
set -e
cd "$(dirname "$0")"
S=../../src
g++ -std=gnu++17 -O2 -Istubs -I$S harness.cpp \
  $S/sheet.cpp $S/universe.cpp $S/resolve.cpp $S/contracts.cpp $S/sim.cpp $S/content.cpp $S/lore.cpp $S/haptics.cpp $S/trip.cpp $S/atlas.cpp $S/journal.cpp $S/savefile.cpp \
  -o harness
for seed in 1 2 3 4 5 6; do ./harness $seed ${1:-2}; done
