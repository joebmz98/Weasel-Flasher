#!/bin/sh
# Compiles WeaselEditor.h (the firmware's USB link, from the WEASEL repo's src/WEASEL_FW)
# for the host, with QSPI and USB serial faked, then runs it against protocol.js.
# Needs g++ and a DaisyDuino checkout:  DAISYDUINO=~/Arduino/libraries/DaisyDuino sh test/firmware/build.sh
#
# Where WeaselEditor.h comes from, first match wins:
#   WEASEL_FW_DIR=<path to src/WEASEL_FW>    a local checkout of the WEASEL repo
#   ../Weasel-West-Coast-Synthesiser/...     the WEASEL repo checked out next to this one
#   GitHub, branch WEASEL_REF (default main)
set -e
cd "$(dirname "$0")"
: "${DAISYDUINO:?set DAISYDUINO to your DaisyDuino library folder}"

HERE=$(pwd)
FOUND=""
for d in "${WEASEL_FW_DIR:-}" \
         "$HERE/../../../Weasel-West-Coast-Synthesiser/src/WEASEL_FW" \
         "$HERE/../../../weasel-west-coast-synthesiser/src/WEASEL_FW"; do
  if [ -n "$d" ] && [ -f "$d/WeaselEditor.h" ]; then FOUND="$d"; break; fi
done

if [ -n "$FOUND" ]; then
  echo "WeaselEditor.h from $FOUND"
  cp "$FOUND/WeaselEditor.h" .
else
  REF="${WEASEL_REF:-main}"
  URL="https://raw.githubusercontent.com/joebmz98/Weasel-West-Coast-Synthesiser/$REF/src/WEASEL_FW/WeaselEditor.h"
  echo "WeaselEditor.h from $URL"
  curl -fsSL "$URL" -o WeaselEditor.h || {
    echo "Could not download WeaselEditor.h (branch '$REF'). Set WEASEL_FW_DIR to a local src/WEASEL_FW." >&2
    exit 1
  }
fi

g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Istub -I"$DAISYDUINO/src" -I"$DAISYDUINO/src/utility" harness.cpp -o harness
node run-firmware-tests.mjs
