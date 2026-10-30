// Ways to reach a WEASEL from the page. Each transport sends and receives the
// SysEx frames from protocol.js; Link adds request/reply on top.
//
//  SerialTransport  USB serial (Web Serial). DaisyDuino's USB port runs as a
//                   CDC serial port when the sketch is built with USB support
//                   set to CDC; this is what firmware 0.2 uses.
//  MidiTransport    USB MIDI (Web MIDI + SysEx), for a future firmware that
//                   shows up as a MIDI device.
//  DemoTransport    the simulated WEASEL in protocol.js.

import { FrameParser, isOurs, RPL, NAK_REASON, DemoWeasel } from './protocol.js';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class SerialTransport {
  static get supported() { return 'serial' in navigator; }

  // Ports this site already has permission for that look like a Daisy Seed.
  static async knownPorts() {
    if (!SerialTransport.supported) return [];
    try {
      const ports = await navigator.serial.getPorts();
      return ports.filter((p) => (p.getInfo().usbVendorId || 0) === 0x0483);
    } catch (e) {
      return [];
    }
  }

  constructor() {
    this.kind = 'usb serial';
    this.onFrame = () => {};
    this.onClose = () => {};
    this.port = null;
    this.reader = null;
    this.closing = false;
  }

  // Ask the person to pick the port. 0483 is STMicroelectronics, which the
  // Seed's USB serial uses; "show all" drops the filter.
  // `port`: one this site was already allowed to use (navigator.serial.getPorts()),
  // which opens without a popup, so moving between pages reconnects by itself.
  async open({ anyDevice = false, port = null } = {}) {
    const filters = anyDevice ? [] : [{ usbVendorId: 0x0483 }];
    this.port = port || await navigator.serial.requestPort({ filters });
    await this.port.open({ baudRate: 115200 });
    // Some CDC stacks only send while DTR is set.
    try { await this.port.setSignals({ dataTerminalReady: true, requestToSend: true }); } catch (e) { /* not supported */ }
    const info = this.port.getInfo();
    this.name = info.usbVendorId
      ? `USB serial ${info.usbVendorId.toString(16).padStart(4, '0')}:${(info.usbProductId || 0).toString(16).padStart(4, '0')}`
      : 'USB serial';
    this.parser = new FrameParser((f) => { if (isOurs(f)) this.onFrame(f); });
    this.readLoop();
  }

  async readLoop() {
    while (this.port && this.port.readable && !this.closing) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value) this.parser.push(value);
        }
      } catch (e) {
        // Unplugged, or a framing glitch; fall through and see if the port is still there.
      } finally {
        try { this.reader.releaseLock(); } catch (e) { /* ignore */ }
      }
      if (!this.closing) await sleep(50);
      if (!this.port || !this.port.readable) break;
    }
    if (!this.closing) this.onClose();
  }

  async send(bytes) {
    const writer = this.port.writable.getWriter();
    try { await writer.write(Uint8Array.from(bytes)); } finally { writer.releaseLock(); }
  }

  async close() {
    this.closing = true;
    try { if (this.reader) await this.reader.cancel(); } catch (e) { /* ignore */ }
    try { await this.port.close(); } catch (e) { /* ignore */ }
    this.port = null;
  }
}

export class MidiTransport {
  static get supported() { return 'requestMIDIAccess' in navigator; }

  constructor() {
    this.kind = 'usb midi';
    this.onFrame = () => {};
    this.onClose = () => {};
  }

  // Finds the port pair whose name mentions WEASEL or Daisy; if there is none,
  // `probe` sends INFO to every output and keeps the first that answers.
  async open({ probe }) {
    this.access = await navigator.requestMIDIAccess({ sysex: true });
    const outs = [...this.access.outputs.values()];
    const ins = [...this.access.inputs.values()];
    const named = (p) => /weasel|daisy/i.test(p.name || '');
    const listen = (inp) => { inp.onmidimessage = (e) => { if (isOurs(e.data)) this.onFrame(Array.from(e.data)); }; };
    ins.forEach(listen);
    const tryOrder = [...outs.filter(named), ...outs.filter((o) => !named(o))];
    for (const out of tryOrder) {
      this.out = out;
      if (await probe(this)) {
        this.name = out.name;
        this.access.onstatechange = (e) => { if (e.port.id === out.id && e.port.state === 'disconnected') this.onClose(); };
        return;
      }
    }
    ins.forEach((i) => { i.onmidimessage = null; });
    throw new Error('No MIDI port answered. Is WEASEL running firmware with USB MIDI?');
  }

  async send(bytes) { this.out.send(bytes); }
  async close() { if (this.access) for (const i of this.access.inputs.values()) i.onmidimessage = null; }
}

export class DemoTransport {
  constructor() {
    this.kind = 'demo';
    this.name = 'Simulated WEASEL';
    this.onFrame = () => {};
    this.onClose = () => {};
    this.device = new DemoWeasel();
    this.device.onReply = (f) => setTimeout(() => this.onFrame(f), 8);
  }
  async open() {}
  async send(bytes) { setTimeout(() => this.device.receive(Array.from(bytes)), 4); }
  async close() {}
}

// One request at a time: send a frame, wait for the matching reply (or a NAK).
export class Link {
  constructor(transport) {
    this.t = transport;
    this.queue = Promise.resolve();
    this.waiting = null;
    this.t.onFrame = (f) => this.frame(f);
  }

  frame(f) {
    const w = this.waiting;
    if (!w) return;
    const cmd = f[4];
    if (cmd === RPL.NAK && f[5] === w.cmd) {
      this.waiting = null;
      w.reject(new Error(`WEASEL refused the request: ${NAK_REASON[f[6]] || 'reason ' + f[6]}`));
    } else if (cmd === w.expect) {
      this.waiting = null;
      w.resolve(Array.from(f.slice(5, -1)));
    }
  }

  request(frame, expect, timeoutMs = 700) {
    const run = () => new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.waiting && this.waiting.timer === timer) this.waiting = null;
        const e = new Error('WEASEL did not answer');
        e.timeout = true;
        reject(e);
      }, timeoutMs);
      this.waiting = {
        cmd: frame[4], expect, timer,
        resolve: (v) => { clearTimeout(timer); resolve(v); },
        reject: (e) => { clearTimeout(timer); reject(e); },
      };
      this.t.send(frame).catch((e) => { clearTimeout(timer); this.waiting = null; reject(e); });
    });
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }
}
