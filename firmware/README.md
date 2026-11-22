# WEASEL firmware

The firmware source is in the WEASEL repo:
**[joebmz98/Weasel-West-Coast-Synthesiser › src/WEASEL_FW](https://github.com/joebmz98/Weasel-West-Coast-Synthesiser/tree/main/src/WEASEL_FW)**

This folder holds the released binaries the flasher offers. `manifest.json`
points at the current one, e.g. `firmware/WEASEL_FW.ino.bin`. See
"Releasing new firmware" in the main README for how to build and add one.

## Current release

| Version | File | Built from |
|---|---|---|
| v1.1.4 | `WEASEL_FW.ino.hex` | WEASEL repo `main` (`f61846e`): MIDI clock sync counts clock ticks, so the sequencer stays locked to the master with no drift; the tempo on the MIDI settings page is averaged over a beat (steady instead of jumping 90-160); Start plays step 1 on the next tick, Stop holds until Start / Continue |

Earlier: v1.1.3 from `71103fe` (buttons act on release; hold midi half a second, then level 1 / level 2, to choose which oscillators follow MIDI pitch; LPG mode LEDs). v1.1.2 from `2c50c4d` (Buchla 292 low pass gate model; sequencer steps that are off send no trigger). v1.1 from `bff9d65` (panel memory: powers up with the last button modes; hold the manual pulsar button at power-up to clear). v1.0 from `727fb33` (hardware 1.0, MIDI input fixes, patch-bay guard, USB link) is in this folder's git history.
