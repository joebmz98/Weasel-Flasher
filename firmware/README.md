# WEASEL firmware

The firmware source is in the WEASEL repo:
**[joebmz98/Weasel-West-Coast-Synthesiser › src/WEASEL_FW](https://github.com/joebmz98/Weasel-West-Coast-Synthesiser/tree/main/src/WEASEL_FW)**

This folder holds the released binaries the flasher offers. `manifest.json`
points at the current one, e.g. `firmware/WEASEL_FW.ino.bin`. See
"Releasing new firmware" in the main README for how to build and add one.

## Current release

| Version | File | Built from |
|---|---|---|
| v1.1.2 | `WEASEL_FW.ino.hex` | WEASEL repo `main` (`2c50c4d`): low pass gates are now a model of the Buchla 292 (vactrol + audio circuit, VCA / lo pass / combination switch the modelled circuit); a sequencer step that is off is a rest and sends no trigger |

Earlier: v1.1 from `bff9d65` (panel memory: powers up with the last button modes; hold the manual pulsar button at power-up to clear). v1.0 from `727fb33` (hardware 1.0, MIDI input fixes, patch-bay guard, USB link) is in this folder's git history.
