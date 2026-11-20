# WEASEL firmware

The firmware source is in the WEASEL repo:
**[joebmz98/Weasel-West-Coast-Synthesiser › src/WEASEL_FW](https://github.com/joebmz98/Weasel-West-Coast-Synthesiser/tree/main/src/WEASEL_FW)**

This folder holds the released binaries the flasher offers. `manifest.json`
points at the current one, e.g. `firmware/WEASEL_FW.ino.bin`. See
"Releasing new firmware" in the main README for how to build and add one.

## Current release

| Version | File | Built from |
|---|---|---|
| v1.1 | `WEASEL_FW.ino.hex` | WEASEL repo `main` (`bff9d65`): v1.0 plus panel memory (powers up with the last button modes; hold the manual pulsar button at power-up to clear) |

Earlier: v1.0 from `727fb33` (hardware 1.0, MIDI input fixes, patch-bay guard, USB link) is in this folder's git history.
