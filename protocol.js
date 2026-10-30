// WEASEL editor protocol, version 1.
//
// SysEx frames: F0 7D 57 53 <cmd> <data...> F7
//   7D     = non-commercial manufacturer ID
//   57 53  = "WS" (WEASEL), so the MC8P ("M8") and DCLK editors never answer
// Every data byte is 7-bit. Replies use cmd | 0x40.
//
// The frames travel over the Daisy Seed's USB port running as a CDC serial
// port (DaisyDuino gives you that with no extra code), or over USB MIDI if a
// later firmware adds it. Over serial, anything outside an F0..F7 frame (debug
// prints) is ignored.
//
// | Cmd | Request                        | Reply                                   |
// |-----|--------------------------------|-----------------------------------------|
// | 01  | INFO                           | 41 proto, MIDI byte count, panel byte count, version ASCII |
// | 02  | GET_MIDI                       | 42 the live MIDI settings (1 byte)      |
// | 03  | SET_MIDI <1 byte>              | 42 the live settings after clamping     |
// | 04  | GET_PANEL                      | 44 the panel's button modes (14 bytes)  |
// | 05  | SET_PANEL <14 bytes>           | 44 the panel modes after clamping       |
// | 06  | STATUS                         | 46 see decodeStatus()                   |
// | 07  | SAVE  53 41 56 ("SAV")         | 46 (MIDI settings only)                 |
// | 08  | REVERT                         | 46                                      |
// | 09  | RESET 44 45 46 ("DEF")         | 46                                      |
// | 0A  | BOOTLOADER 44 46 55 ("DFU")    | 4A, then the Seed restarts in DFU mode  |
// |     | anything refused               | 7F <cmd> <reason>                       |
//
// MIDI settings: SET_MIDI applies at once; SAVE keeps it in the Seed's QSPI
// flash (survives firmware updates).
// Panel modes: SET_PANEL applies at once, like pressing the panel buttons.
// They are not stored on WEASEL; presets live in the browser.

export const PROTOCOL = 1;
export const HDR = [0xf0, 0x7d, 0x57, 0x53];
export const CMD = {
  INFO: 0x01, GET_MIDI: 0x02, SET_MIDI: 0x03, GET_PANEL: 0x04, SET_PANEL: 0x05,
  STATUS: 0x06, SAVE: 0x07, REVERT: 0x08, RESET: 0x09, BOOTLOADER: 0x0a,
};
export const RPL = { INFO: 0x41, MIDI: 0x42, PANEL: 0x44, STATUS: 0x46, BOOTLOADER: 0x4a, NAK: 0x7f };
export const MAGIC = { SAVE: [0x53, 0x41, 0x56], RESET: [0x44, 0x45, 0x46], BOOTLOADER: [0x44, 0x46, 0x55] };
export const NAK_REASON = { 1: 'unknown command', 2: 'wrong length', 3: 'missing confirmation bytes', 4: 'value out of range', 5: 'flash storage error' };

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// ---------------------------------------------------------------------------
// MIDI settings: just the receive channel.
// ---------------------------------------------------------------------------

export const OMNI = 16;
export const MIDI_FIELDS = [
  { key: 'channel', min: 0, max: 16, def: OMNI },   // 0-15 = channel 1-16, 16 = omni
];
export const MIDI_DEFAULTS = Object.freeze(Object.fromEntries(MIDI_FIELDS.map((f) => [f.key, f.def])));

// ---------------------------------------------------------------------------
// Panel modes: everything the mode buttons cycle through. Not the virtual
// patch bay, and not the sequencer step on/off switches (those are latching
// switches, so the panel always shows their real position).
// `options` are in the order a button press steps through them; the value on
// the wire is the index. `def` is the power-on mode (marked * on the panel).
// ---------------------------------------------------------------------------

export const PANEL_SECTIONS = [
  { title: 'Sequential voltage source', fields: [
    { key: 'seqTrigger', label: 'trigger select', options: ['clock', 'pulsar', 'midi'], def: 0 },
    { key: 'seqStages', label: 'stages', options: ['5', '4', '3'], def: 0 },
  ] },
  { title: 'Envelope generator', fields: [
    { key: 'envTrigger', label: 'trigger select', options: ['sequencer', 'pulsar', 'midi'], def: 0 },
    { key: 'envMode', label: 'mode', options: ['transient', 'sustain', 'oscillating'], def: 0 },
  ] },
  { title: 'Pulsar', fields: [
    { key: 'pulsarTrigger', label: 'trigger select', options: ['sequencer', 'midi', 'free'], def: 0 },
  ] },
  { title: 'Modulation oscillator', fields: [
    { key: 'modWave', label: 'waveform', options: ['sine', 'triangle', 'sawtooth', 'square'], def: 0 },
    { key: 'modType', label: 'modulation', options: ['fm', 'am'], def: 0 },
  ] },
  { title: 'Complex oscillator', fields: [
    { key: 'complexWave', label: 'waveform', options: ['saw', 'square', 'triangle'], def: 0 },
    { key: 'polarity', label: 'polarity', options: ['+', '-'], def: 0 },
    { key: 'midiFollow', label: 'midi', options: ['off', 'on'], def: 0 },
  ] },
  { title: 'Dual lo pass gate', fields: [
    { key: 'lpg1Mode', label: 'level 1 (complex)', options: ['voltage ctrld. amp', 'lo pass filter', 'combination'], def: 2 },
    { key: 'lpg2Mode', label: 'level 2 (modulation)', options: ['voltage ctrld. amp', 'lo pass filter', 'combination'], def: 2 },
  ] },
  { title: 'Random voltage', fields: [
    { key: 'randomTrigger', label: 'trigger select', options: ['sequencer', 'pulsar', 'midi'], def: 0 },
  ] },
  { title: 'Clock', fields: [
    { key: 'clockMode', label: 'mode', options: ['free', 'sync'], def: 0 },
  ] },
];

// Wire order. Changing it changes the protocol: keep in step with WeaselEditor.h.
export const PANEL_FIELDS = [
  'seqTrigger', 'seqStages', 'envTrigger', 'envMode', 'pulsarTrigger', 'randomTrigger',
  'modWave', 'modType', 'lpg2Mode', 'complexWave', 'polarity', 'lpg1Mode', 'midiFollow', 'clockMode',
].map((key) => PANEL_SECTIONS.flatMap((s) => s.fields).find((f) => f.key === key));
export const PANEL_DEFAULTS = Object.freeze(Object.fromEntries(PANEL_FIELDS.map((f) => [f.key, f.def])));

function encodeWith(fields, s) {
  return fields.map((f) => {
    const max = f.options ? f.options.length - 1 : f.max;
    const min = f.options ? 0 : f.min;
    return clamp(Math.round(Number(s[f.key] ?? f.def)), min, max);
  });
}
function decodeWith(fields, bytes) {
  const out = {};
  fields.forEach((f, i) => {
    const max = f.options ? f.options.length - 1 : f.max;
    const min = f.options ? 0 : f.min;
    out[f.key] = clamp(bytes[i] === undefined ? f.def : bytes[i], min, max);
  });
  return out;
}

export const encodeMidi = (s) => encodeWith(MIDI_FIELDS, s);
export const decodeMidi = (b) => decodeWith(MIDI_FIELDS, b);
export const encodePanel = (s) => encodeWith(PANEL_FIELDS, s);
export const decodePanel = (b) => decodeWith(PANEL_FIELDS, b);

export function checksum(bytes) {
  return bytes.reduce((a, b) => (a * 3 + b + 1) & 0x7f, 0);
}

export const msg = (cmd, ...data) => [...HDR, cmd, ...data, 0xf7];
export const isOurs = (d) => d.length >= 6 && HDR.every((b, i) => d[i] === b) && d[d.length - 1] === 0xf7;
export const payload = (d) => Array.from(d.slice(5, -1));

// STATUS reply payload.
export function decodeStatus(p) {
  const bpm10 = ((p[8] & 0x7f) << 7) | (p[9] & 0x7f);
  return {
    dirty: !!p[0],
    liveSum: p[1],
    savedSum: p[2],
    lastNote: p[3],
    noteHeld: !!(p[4] & 1),
    anyNote: !!(p[4] & 2),
    lastChannel: p[5],
    lastVelocity: p[6],
    clockReceiving: !!(p[7] & 1),
    clockSync: !!(p[7] & 2),
    bpm: bpm10 / 10,
    activity: p[10],
    storage: ['unknown', 'factory', 'saved'][p[11]] || 'unknown',
    panelSum: p[12],
  };
}

export function encodeStatus(s) {
  const bpm10 = clamp(Math.round((s.bpm || 0) * 10), 0, 16383);
  return [
    s.dirty ? 1 : 0, s.liveSum & 0x7f, s.savedSum & 0x7f, s.lastNote & 0x7f,
    (s.noteHeld ? 1 : 0) | (s.anyNote ? 2 : 0), s.lastChannel & 0x0f, s.lastVelocity & 0x7f,
    (s.clockReceiving ? 1 : 0) | (s.clockSync ? 2 : 0), bpm10 >> 7, bpm10 & 0x7f, s.activity & 0x7f,
    { unknown: 0, factory: 1, saved: 2 }[s.storage] ?? 0, s.panelSum & 0x7f,
  ];
}

export function decodeInfo(p) {
  return { protocol: p[0], midiCount: p[1], panelCount: p[2], version: String.fromCharCode(...p.slice(3)) };
}

// Pulls F0..F7 frames out of a byte stream (USB serial delivers arbitrary
// pieces and may carry debug text between frames).
export class FrameParser {
  constructor(onFrame, maxLength = 128) {
    this.onFrame = onFrame;
    this.max = maxLength;
    this.buf = null;
  }

  push(bytes) {
    for (const b of bytes) {
      if (b === 0xf0) { this.buf = [b]; continue; }
      if (!this.buf) continue;
      if (b === 0xf7) {
        this.buf.push(b);
        const frame = this.buf;
        this.buf = null;
        this.onFrame(frame);
        continue;
      }
      if (b & 0x80 || this.buf.length >= this.max) { this.buf = null; continue; }
      this.buf.push(b);
    }
  }
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
// Middle C (60) = C4, as on most keyboards and DAWs.
export const noteName = (n) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
export const channelName = (c) => (c === OMNI ? 'omni' : `channel ${c + 1}`);

// A WEASEL that lives in the page, for "try demo" and the tests. It answers
// exactly like the firmware and makes up some MIDI traffic to show.
export class DemoWeasel {
  constructor({ version = '0.2-demo' } = {}) {
    this.version = version;
    this.saved = { ...MIDI_DEFAULTS };
    this.live = { ...MIDI_DEFAULTS };
    this.panel = { ...PANEL_DEFAULTS };
    this.storage = 'factory';
    this.onReply = () => {};
    this.t0 = Date.now();
    this.activity = 0;
  }

  reply(cmd, data = []) { this.onReply(msg(cmd, ...data)); }
  nak(cmd, reason) { this.reply(RPL.NAK, [cmd & 0x7f, reason]); }

  // Someone pressed a panel button (used by the demo to show panel changes arriving).
  press(key) {
    const f = PANEL_FIELDS.find((x) => x.key === key);
    this.panel[key] = (this.panel[key] + 1) % f.options.length;
  }

  status() {
    const t = (Date.now() - this.t0) / 1000;
    const step = Math.floor(t * 2);
    const notes = [48, 55, 60, 63, 67, 70, 72, 58];
    const ch = this.live.channel === OMNI ? 0 : this.live.channel;
    this.activity = (this.activity + 1) & 0x7f;
    const live = encodeMidi(this.live);
    const saved = encodeMidi(this.saved);
    return {
      dirty: live.some((b, i) => b !== saved[i]),
      liveSum: checksum(live),
      savedSum: checksum(saved),
      lastNote: notes[step % notes.length],
      noteHeld: (t * 2) % 1 < 0.6,
      anyNote: true,
      lastChannel: ch,
      lastVelocity: 70 + ((step * 37) % 57),
      clockReceiving: true,
      clockSync: !!this.panel.clockMode,
      bpm: 120 + Math.round(Math.sin(t / 7) * 2 * 10) / 10,
      activity: this.activity,
      storage: this.storage,
      panelSum: checksum(encodePanel(this.panel)),
    };
  }

  receive(frame) {
    if (!isOurs(frame)) return;
    const cmd = frame[4];
    const p = payload(frame);
    const magic = (m) => m.every((b, i) => p[i] === b);
    switch (cmd) {
      case CMD.INFO:
        return this.reply(RPL.INFO, [PROTOCOL, MIDI_FIELDS.length, PANEL_FIELDS.length, ...[...this.version].map((c) => c.charCodeAt(0) & 0x7f)]);
      case CMD.GET_MIDI:
        return this.reply(RPL.MIDI, encodeMidi(this.live));
      case CMD.SET_MIDI:
        if (p.length !== MIDI_FIELDS.length) return this.nak(cmd, 2);
        this.live = decodeMidi(p);
        return this.reply(RPL.MIDI, encodeMidi(this.live));
      case CMD.GET_PANEL:
        return this.reply(RPL.PANEL, encodePanel(this.panel));
      case CMD.SET_PANEL:
        if (p.length !== PANEL_FIELDS.length) return this.nak(cmd, 2);
        this.panel = decodePanel(p);
        return this.reply(RPL.PANEL, encodePanel(this.panel));
      case CMD.STATUS:
        return this.reply(RPL.STATUS, encodeStatus(this.status()));
      case CMD.SAVE:
        if (!magic(MAGIC.SAVE)) return this.nak(cmd, 3);
        this.saved = { ...this.live };
        this.storage = 'saved';
        return this.reply(RPL.STATUS, encodeStatus(this.status()));
      case CMD.REVERT:
        this.live = { ...this.saved };
        return this.reply(RPL.STATUS, encodeStatus(this.status()));
      case CMD.RESET:
        if (!magic(MAGIC.RESET)) return this.nak(cmd, 3);
        this.live = { ...MIDI_DEFAULTS };
        this.saved = { ...MIDI_DEFAULTS };
        this.storage = 'factory';
        return this.reply(RPL.STATUS, encodeStatus(this.status()));
      case CMD.BOOTLOADER:
        if (!magic(MAGIC.BOOTLOADER)) return this.nak(cmd, 3);
        return this.reply(RPL.BOOTLOADER);
      default:
        return this.nak(cmd, 1);
    }
  }
}
