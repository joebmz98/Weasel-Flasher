#!/bin/sh
# Compiles firmware-src/WEASEL_FW/WeaselEditor.h for the host, with QSPI and
# USB serial faked, then runs it against protocol.js.
# Needs g++ and a DaisyDuino checkout:  DAISYDUINO=~/Arduino/libraries/DaisyDuino sh test/firmware/build.sh
set -e
cd "$(dirname "$0")"
: "${DAISYDUINO:?set DAISYDUINO to your DaisyDuino library folder}"
cp ../../firmware-src/WEASEL_FW/WeaselEditor.h .
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Istub -I"$DAISYDUINO/src" -I"$DAISYDUINO/src/utility" harness.cpp -o harness
node run-firmware-tests.mjs
