# WEASEL-Flasher

Web tools for **WEASEL**, the .axs instruments digital west coast synthesiser
(Daisy Seed, STM32H750): a firmware flasher, MIDI settings and presets.
Nothing to install. They run in **Chrome or Edge on a computer** and need a
micro-USB data cable.

Built on the MC8P MkII flasher, restyled after the WEASEL front panel.

| Page | Opens | Talks to WEASEL over | WEASEL needs to be |
| --- | --- | --- | --- |
| **Update firmware** | `index.html#flash` | WebUSB, to the STM32 ROM DFU bootloader | In DFU mode |
| **MIDI settings** | `editor.html` | Web Serial (USB CDC) + SysEx frames | Running normally, firmware 0.2 or later |
| **Presets** | `presets.html` | Web Serial (USB CDC) + SysEx frames | Running normally, firmware 0.2 or later |

Connect once and the other pages reconnect by themselves (Chrome remembers
the port this site was allowed to use). **try demo** runs any page against a
simulated WEASEL; add `?demo` to a page's address to start in the demo.

## MIDI settings (`editor.html`)

- **MIDI in channel**: a drop-down, channel 1-16 or omni (factory: omni).
  Applies live; **save to weasel** keeps it after power-off. Clock and
  Start/Stop are received whatever the channel.
- **MIDI in monitor**: last note, channel, velocity, held note, incoming clock
  tempo and whether WEASEL is synced to it.

## Presets (`presets.html`)

A preset holds every mode the panel buttons step through:

| Section | Modes |
| --- | --- |
| Sequential voltage source | trigger select (clock*, pulsar, midi), stages (5*, 4, 3) |
| Envelope generator | trigger select (sequencer*, pulsar, midi), mode (transient*, sustain, oscillating) |
| Pulsar | trigger select (sequencer*, midi, free) |
| Modulation oscillator | waveform (sine*, triangle, sawtooth, square), modulation (fm*, am) |
| Complex oscillator | waveform (saw*, square, triangle), polarity (+*, -), midi (off*, on) |
| Dual lo pass gate | level 1 and level 2 mode (voltage ctrld. amp, lo pass filter, combination*) |
| Random voltage | trigger select (sequencer*, pulsar, midi) |
| Clock | mode (free*, sync) |

Not included: the virtual patch bay, the knobs and faders, and the sequencer
step on/off switches (latching switches, so the panel always shows their
real position).

- Clicking a mode changes WEASEL straight away, like pressing its button.
  Pressing a button on WEASEL shows up on the page.
- **store** saves the panel as a named preset; click a name to recall it. A
  lit dot marks the preset that matches the panel.
- Presets live in this browser (`localStorage`). **export** writes them to a
  JSON file (modes by name, so it's readable); **import** merges a file back.
- Without WEASEL connected the panel is a draft: build or recall presets
  offline, then connect and recall to send one.

### Protocol

`F0 7D 57 53 <cmd> <data...> F7` (7D = non-commercial ID, `57 53` = "WS"), over
the Seed's USB port as a serial port. Bytes outside a frame are ignored, so
debug prints don't get in the way. Replies are `cmd | 0x40`. The full table
and field order are in `protocol.js`.

| Cmd | Request | Reply |
| --- | --- | --- |
| `01` | INFO | `41`: protocol, MIDI byte count, panel byte count, version string |
| `02` / `03` | GET_MIDI / SET_MIDI `<channel>` | `42`: live channel (0-15, 16 = omni) |
| `04` / `05` | GET_PANEL / SET_PANEL `<14 bytes>` | `44`: panel modes |
| `06` | STATUS | `46`: unsaved flag, checksums, last note/channel/velocity, held, clock, bpm, activity, storage state, panel checksum |
| `07` / `08` / `09` | SAVE `"SAV"` / REVERT / RESET `"DEF"` | `46` |
| `0A` | BOOTLOADER `"DFU"` | `4A`, then the Seed restarts in DFU mode |
| | anything refused | `7F <cmd> <reason>` |

## Firmware side (`firmware-src/`)

`firmware-src/WEASEL_FW/` is `src/WEASEL_FW` from the WEASEL repo with the
changes for 0.2 (`firmware-src/WEASEL_FW-0.2.patch` shows exactly what
changed):

- `WeaselEditor.h` (new): the USB link. Keeps the receive channel at the top
  of the Seed's 8 MB QSPI flash (`0x907F0000`, DaisyDuino's
  `PersistentStorage`), so firmware updates keep it.
- `WEASEL_FW.ino`:
  - note on/off ignore other channels when one is set
  - `readPanelModes()` / `writePanelModes()` hand the panel modes to the
    web page; writing one has the same side effects as pressing its button
  - `setModWaveform()` / `setComplexWaveform()` are shared by the buttons and
    presets
  - `editor.poll()` in `loop()`, and **restart into dfu mode** from the web page
- **Fix:** the complex oscillator waveform button now steps saw > square >
  triangle, as printed on the panel. In 0.1 it started on saw, but the first
  press went to triangle and the second back to saw.

Build in Arduino IDE with the Daisy Seed board, and
**Tools > USB support > CDC (generic 'Serial' supersede U(S)ART)** so the USB
port shows up as a serial port. MIDI stays on `Serial1` (TRS in, D14).

`npm run test:firmware` compiles `WeaselEditor.h` on your computer with
the QSPI and USB faked, and checks it against `protocol.js` (needs g++ and
`DAISYDUINO=<path to the DaisyDuino library>`). The full sketch has not
been compiled for the Seed or tried on hardware yet.

## Update firmware (flasher)

1. Connect the Seed's micro-USB straight to the computer.
2. DFU mode: **hold BOOT, tap RESET, release BOOT**. Or, with 0.2 running,
   **restart weasel into dfu mode** on the flasher page does it over USB.
3. Pick the firmware, click **flash firmware**, choose **DFU in FS Mode**.
4. Wait for **done**.

Release firmware comes from `manifest.json`. Until one is published, the
release option is disabled and the page offers a custom file only.

### What's different from the MC8P flasher

`dfuse.js` is the MC8P one with a chip table (`CHIPS.STM32H750`):

- The H750 programs flash in **32 byte flash words** with ECC, and a word
  must only be programmed once per erase. Every write is now whole, aligned
  32 byte words, blocks never shrink below 32 bytes, and blank skipping
  works in whole words.
- The H750 has **128 KB** at `0x08000000`, one sector. Its ROM bootloader
  reports the 2 MB layout of the H743 die, so the usable end comes from the
  chip table. Images over 128 KB are refused before anything is erased.
- Builds linked for the **Daisy bootloader** (`0x90040000` QSPI or SRAM)
  are flagged: the ROM bootloader can't write them.
- The stack pointer check accepts all H750 RAM (DTCM, AXI SRAM, SRAM1-4).

The verify and repair pass is unchanged: if a retried write leaves a bad
word, the sector is erased and rewritten.

### Troubleshooting

- **DFU in FS Mode not in the list:** not in DFU mode yet, or a charge-only cable.
- **Windows, "access denied":** install WinUSB once with
  [Zadig](https://zadig.akeo.ie/) (Options > List All Devices > DFU in FS Mode > WinUSB).
- **Linux:** udev rule
  `SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="df11", MODE="0666"`.
- `?block=256` on the flasher URL forces smaller USB transfers.

## Releasing new firmware

1. Arduino IDE, Daisy Seed board, USB support CDC.
   **Sketch > Export Compiled Binary**, copy the `.bin` (or `.hex`) into `firmware/`.
2. Update `manifest.json`:
   ```json
   {
     "name": "WEASEL Digital West Coast Synthesiser Firmware",
     "version": "v0.2",
     "builds": [{ "chipFamily": "STM32H750", "parts": [{ "path": "firmware/WEASEL_FW.ino.bin", "offset": "0x08000000" }] }]
   }
   ```
3. If the protocol changed, bump `PROTOCOL` in both `protocol.js` and `WeaselEditor.h`.
4. `npm test` (Node 18+, no packages), commit, push.

## Repo layout

| File | What it is |
| --- | --- |
| `index.html` | Start page and the flasher (`#flash`) |
| `editor.html` | MIDI settings: channel drop-down and MIDI in monitor |
| `presets.html` | Panel modes and the preset library |
| `app.js` | Connection panel shared by the two pages above |
| `theme.css` | Front panel look: Caprasimo for the Cooper Black wordmark, Nanum Gothic for Malgun Gothic labels |
| `protocol.js` | Protocol, MIDI and panel fields, frame parser, simulated WEASEL |
| `transport.js` | Web Serial, Web MIDI and demo transports, request/reply |
| `dfuse.js` | STM32 DfuSe over WebUSB, with the H750 chip table |
| `assets/` | Fonts (OFL, licences included), the panel's weasel artwork (from the KiCad panel silkscreen), and the .axs logo without text from [axs-instruments-assets](https://github.com/joebmz98/axs-instruments-assets), made all white, as the tab icon |
| `firmware-src/` | WEASEL_FW 0.2 with the USB link |
| `test/` | Simulated F411 and H750 ROM bootloaders, flasher and protocol tests; host build of the firmware protocol code |

## Hosting

GitHub Pages (Settings > Pages > Deploy from branch > `main` / root). WebUSB,
Web Serial and Web MIDI all need HTTPS, which Pages provides.
