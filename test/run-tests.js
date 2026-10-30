// Run with: node test/run-tests.js   (Node 18 or newer, no packages needed)
// Flashes firmware images into a simulated STM32H750 (Daisy Seed) ROM
// bootloader and checks every byte that ends up in its flash. Also runs the
// SysEx protocol against the editor's simulated WEASEL.

import assert from 'node:assert/strict';
import { FirmwareImage, StmDfuse, parseIntelHex, parseMemoryLayout, DfuError, CHIPS, FLASH_BASE } from '../dfuse.js';
import { MockStm32Dfu } from './mock-stm32-dfu.js';
import * as P from '../protocol.js';

const H750 = CHIPS.STM32H750;

let passed = 0;
async function test(name, fn) {
  const t0 = Date.now();
  try {
    await fn();
    passed++;
    console.log(`  ok    ${name} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  } catch (e) {
    console.log(`  FAIL  ${name}\n        ${e.stack}`);
    process.exitCode = 1;
  }
}

// A stand-in for a DaisyDuino build: vector table (stack in DTCM, reset
// handler in the image), code-like bytes, runs of 0xFF and zero padding.
function fakeFirmware(size, seed = 3) {
  const out = new Uint8Array(size);
  let x = seed;
  for (let i = 0; i < size; i++) {
    x = (x * 1103515245 + 12345) & 0x7fffffff;
    out[i] = x >> 16;
  }
  out.fill(0xff, 4096, 4096 + 700);   // a blank stretch that is not word aligned
  out.fill(0x00, 9000, 9500);
  out.fill(0xff, 20000, 20000 + 4096);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, 0x20020000, true);
  dv.setUint32(4, FLASH_BASE + 0x1a1, true);
  return out;
}

function toHex(blocks) {
  const lines = [];
  const rec = (type, addr, data) => {
    const bytes = [data.length, (addr >> 8) & 0xff, addr & 0xff, type, ...data];
    const sum = (-bytes.reduce((a, b) => a + b, 0)) & 0xff;
    lines.push(':' + [...bytes, sum].map((b) => b.toString(16).padStart(2, '0')).join('').toUpperCase());
  };
  for (const { address, data } of blocks) {
    let hi = -1;
    for (let i = 0; i < data.length; i += 16) {
      const a = address + i;
      if (a >>> 16 !== hi) { hi = a >>> 16; rec(4, 0, [hi >> 8, hi & 0xff]); }
      rec(0, a & 0xffff, [...data.subarray(i, i + 16)]);
    }
  }
  rec(1, 0, []);
  return lines.join('\n');
}

const app = fakeFirmware(110 * 1024 + 13);  // odd length on purpose

function image(bytes = app, addr = FLASH_BASE) {
  const img = new FirmwareImage();
  img.addBin(bytes, addr, 'weasel.bin');
  return img;
}

function daisy(opts = {}) {
  return new MockStm32Dfu({ chip: 'h750', transferSize: 1024, ...opts });
}

async function flashInto(mock, img, opts = {}, dfuOpts = {}) {
  const dfu = new StmDfuse(mock, { log: () => {}, chip: H750, ...dfuOpts });
  await dfu.open();
  await dfu.flash(img, { verify: true, ...opts });
  return dfu;
}

function dataWrites(mock) { return mock.log.filter((l) => l.req === 1 && l.block >= 2); }

console.log('Images and layout (STM32H750)');

await test('H750 layout as the ROM reports it parses to 16 x 128 KB', () => {
  const l = parseMemoryLayout('@Internal Flash   /0x08000000/16*128Kg');
  assert.equal(l.sectors.length, 16);
  assert.equal(l.sectors[1].start, 0x08020000);
});

await test('segments are padded to whole 32 byte flash words with 0xFF', () => {
  const segs = image().segments(32);
  assert.equal(segs.length, 1);
  assert.equal(segs[0].data.length % 32, 0);
  assert.ok(segs[0].data.subarray(app.length).every((b) => b === 0xff));
});

await test('parts that share a flash word are joined, not written twice', () => {
  const img = new FirmwareImage();
  img.addBin(new Uint8Array(40).fill(1), FLASH_BASE, 'a');
  img.addBin(new Uint8Array(40).fill(2), FLASH_BASE + 120, 'b'); // 0x78: same word as nothing, 80 byte gap
  img.addBin(new Uint8Array(8).fill(3), FLASH_BASE + 200, 'c');   // gap 40 < 64: merged
  const segs = img.segments(32);
  for (let i = 1; i < segs.length; i++) {
    assert.ok(segs[i].address >= segs[i - 1].address + segs[i - 1].data.length + 32 ||
      Math.floor(segs[i].address / 32) > Math.floor((segs[i - 1].address + segs[i - 1].data.length - 1) / 32));
  }
  for (const s of segs) { assert.equal(s.address % 32, 0); assert.equal(s.data.length % 32, 0); }
});

await test('a normal DaisyDuino build gets no warnings', () => {
  assert.deepEqual(image().warnings(H750), []);
});

await test('stack in AXI SRAM is accepted too', () => {
  const b = app.slice();
  new DataView(b.buffer).setUint32(0, 0x24080000, true);
  assert.deepEqual(image(b).warnings(H750), []);
});

await test('a Daisy bootloader build (0x90040000) is flagged', () => {
  assert.match(image(app, 0x90040000).warnings(H750).join(' '), /Daisy bootloader/);
});

await test('a build over 128 KB is flagged', () => {
  const big = fakeFirmware(140 * 1024);
  assert.match(image(big).warnings(H750).join(' '), /will not fit/);
});

await test('Intel HEX with a gap round-trips', () => {
  const hex = toHex([{ address: FLASH_BASE, data: app.subarray(0, 5000) }, { address: FLASH_BASE + 0x4000, data: app.subarray(0, 3000) }]);
  const blocks = parseIntelHex(hex);
  assert.equal(blocks.length, 2);
  assert.deepEqual(blocks[1].data, app.subarray(0, 3000));
});

console.log('Flashing the simulated Daisy Seed bootloader');

await test('every byte lands where it should, over old firmware', async () => {
  const mock = daisy({ fillFlash: 0x00 });
  const dfu = await flashInto(mock, image());
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
  assert.ok(mock.read(FLASH_BASE + app.length, 32 - (app.length % 32)).every((b) => b === 0xff), 'padding is blank');
  assert.equal(mock.doubleProgrammed, 0, 'no flash word programmed twice');
  assert.equal(mock.misaligned || 0, 0, 'every write starts on a flash word');
  assert.ok(dataWrites(mock).every((w) => w.len % 32 === 0), 'every write is whole flash words');
  assert.equal(dfu.transferSize, 1024);
});

await test('only the one 128 KB sector is erased', async () => {
  const mock = daisy({ fillFlash: 0x00 });
  await flashInto(mock, image());
  const erases = mock.log.filter((l) => l.req === 1 && l.block === 0 && l.cmd === 0x41);
  assert.equal(erases.length, 1);
  assert.ok(mock.read(0x08020000, 64).every((b) => b === 0x00), 'past 128 KB untouched');
});

await test('blank stretches are skipped in whole 32 byte words', async () => {
  const mock = daisy({ fillFlash: 0x00 });
  const logs = [];
  await flashInto(mock, image(), {}, { log: (m) => logs.push(m) });
  assert.ok(logs.some((m) => /Skipped 4\d{3} bytes that were 0xFF/.test(m)), logs.join('\n'));
  assert.deepEqual(mock.read(FLASH_BASE + 4096, 700), new Uint8Array(700).fill(0xff));
});

await test('writeSpans works in 32 byte words', () => {
  const dfu = new StmDfuse(daisy(), { chip: H750 });
  const d = new Uint8Array(128).fill(0xff);
  d[5] = 1;        // word 0 not blank
  d[64 + 31] = 2;  // word 2 not blank
  assert.deepEqual(dfu.writeSpans(d), [{ offset: 0, length: 32 }, { offset: 64, length: 32 }]);
});

await test('reads the layout from string descriptors when interface names are missing', async () => {
  const mock = daisy({ reportAltNames: false });
  const dfu = await flashInto(mock, image());
  assert.equal(dfu.layout.sectors.length, 16);
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
});

await test('a build over 128 KB is refused before anything is erased', async () => {
  const mock = daisy({ fillFlash: 0x11 });
  const dfu = new StmDfuse(mock, { chip: H750 });
  await dfu.open();
  await assert.rejects(dfu.flash(image(fakeFirmware(130 * 1024))), /past the 128 KB/);
  assert.ok(mock.read(FLASH_BASE, 16).every((b) => b === 0x11));
});

await test('leave request makes the chip jump to 0x08000000', async () => {
  const mock = daisy();
  const dfu = await flashInto(mock, image());
  await dfu.leave();
  assert.equal(mock.jumpedTo, FLASH_BASE);
});

await test('recovers when an earlier session left the device in dfuERROR', async () => {
  const mock = daisy();
  mock.state = 10; mock.status = 15;
  await flashInto(mock, image());
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
});

const FAST = { opDelayMs: 0, nakWhileBusy: false };

console.log('Unreliable USB');

await test('large transfers fail: blocks shrink, never below one flash word', async () => {
  const mock = daisy({ ...FAST, fillFlash: 0x00, failOutAbove: 256, failInAbove: 256 });
  const dfu = await flashInto(mock, image());
  assert.equal(dfu.blockSize, 256);
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
  assert.equal(mock.misaligned || 0, 0);
});

await test('a link that fails every data transfer stops at 32 byte blocks with a clear error', async () => {
  const mock = daisy({ ...FAST, failOutAbove: 0 });
  const dfu = new StmDfuse(mock, { chip: H750 });
  await dfu.open();
  await assert.rejects(dfu.flash(image()), (e) => e.usbUnreliable === true && /even in 32 byte blocks/.test(e.message));
});

await test('random transfer errors, some after the chip took the data: verify + repair fix double-programmed words', async () => {
  const mock = daisy({ ...FAST, fillFlash: 0x00, flakyRate: 0.08, seed: 11 });
  const logs = [];
  await flashInto(mock, image(), {}, { log: (m) => logs.push(m) });
  assert.ok(mock.injectedFailures > 3);
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
  assert.equal(mock.eccBad.size, 0, 'no flash word left with a bad ECC code');
});

await test('half-received blocks are thrown away, not programmed', async () => {
  const mock = daisy({ ...FAST, fillFlash: 0x00, packetErrorRate: 0.15, seed: 5 });
  await flashInto(mock, image());
  assert.ok(mock.injectedFailures > 0, 'the model should drop some transfers');
  assert.equal(mock.partialBlocksProgrammed, 0);
  assert.deepEqual(mock.read(FLASH_BASE, app.length), app);
});

await test('unplugging mid-write gives a disconnect message', async () => {
  const mock = daisy({ ...FAST, unplugAfterWrites: 3 });
  const dfu = new StmDfuse(mock, { chip: H750 });
  await dfu.open();
  await assert.rejects(dfu.flash(image()), /disconnected/);
});

await test('the F411 path still works (shared code with the MC8P flasher)', async () => {
  const mock = new MockStm32Dfu({ fillFlash: 0x00 });
  const dfu = new StmDfuse(mock, { log: () => {} });
  await dfu.open();
  const small = fakeFirmware(40000);
  await dfu.flash(image(small), { verify: true });
  assert.deepEqual(mock.read(FLASH_BASE, small.length), small);
});

console.log('SysEx protocol (editor <-> simulated WEASEL)');

await test('frames carry the WEASEL header and stay 7-bit', () => {
  const m = P.msg(P.CMD.SET_PANEL, ...P.encodePanel(P.PANEL_DEFAULTS));
  assert.deepEqual(m.slice(0, 4), [0xf0, 0x7d, 0x57, 0x53]);
  assert.equal(m[m.length - 1], 0xf7);
  assert.ok(m.slice(1, -1).every((b) => b < 0x80));
});

await test('MIDI settings are just the channel; bad values clamp to omni', () => {
  assert.deepEqual(P.MIDI_FIELDS.map((f) => f.key), ['channel']);
  assert.deepEqual(P.decodeMidi(P.encodeMidi({ channel: 9 })), { channel: 9 });
  assert.equal(P.decodeMidi([99]).channel, P.OMNI);
});

await test('panel: 14 modes, every section field on the wire once, defaults match the panel stars', () => {
  assert.equal(P.PANEL_FIELDS.length, 14);
  const keys = P.PANEL_SECTIONS.flatMap((s) => s.fields.map((f) => f.key));
  assert.deepEqual([...keys].sort(), P.PANEL_FIELDS.map((f) => f.key).sort());
  assert.equal(P.PANEL_DEFAULTS.lpg1Mode, 2);   // combination*
  assert.equal(P.PANEL_DEFAULTS.seqTrigger, 0); // clock*
  const p = { ...P.PANEL_DEFAULTS, modWave: 3, clockMode: 1 };
  assert.deepEqual(P.decodePanel(P.encodePanel(p)), p);
  assert.deepEqual(P.encodePanel({ ...P.PANEL_DEFAULTS, modWave: 40 })[6], 3);
});

await test('stream parser pulls frames out of serial noise and split reads', () => {
  const got = [];
  const parser = new P.FrameParser((f) => got.push(f));
  const frame = P.msg(P.CMD.INFO);
  parser.push(new TextEncoder().encode('boot log line\r\n'));
  parser.push(Uint8Array.from(frame.slice(0, 3)));
  parser.push(Uint8Array.from([...frame.slice(3), 0x41, 0x42]));
  parser.push(Uint8Array.from([0xf0, 1, 2])); // abandoned by the next F0
  parser.push(Uint8Array.from(frame));
  assert.equal(got.length, 2);
  assert.deepEqual(got[0], frame);
});

await test('simulated WEASEL: channel set, save, revert, reset', () => {
  const dev = new P.DemoWeasel();
  const replies = [];
  dev.onReply = (r) => replies.push(r);
  dev.receive(P.msg(P.CMD.SET_MIDI, 3));
  assert.equal(dev.live.channel, 3);
  assert.equal(dev.status().dirty, true);
  dev.receive(P.msg(P.CMD.SAVE, ...P.MAGIC.SAVE));
  assert.equal(dev.saved.channel, 3);
  dev.receive(P.msg(P.CMD.SET_MIDI, 7));
  dev.receive(P.msg(P.CMD.REVERT));
  assert.equal(dev.live.channel, 3);
  dev.receive(P.msg(P.CMD.RESET, 0, 0, 0)); // wrong confirmation: refused
  assert.equal(replies[replies.length - 1][4], P.RPL.NAK);
  dev.receive(P.msg(P.CMD.RESET, ...P.MAGIC.RESET));
  assert.deepEqual(dev.live, P.MIDI_DEFAULTS);
});

await test('simulated WEASEL: panel recall and the status checksum', () => {
  const dev = new P.DemoWeasel();
  let last;
  dev.onReply = (r) => { last = r; };
  const preset = { ...P.PANEL_DEFAULTS, envMode: 2, polarity: 1 };
  dev.receive(P.msg(P.CMD.SET_PANEL, ...P.encodePanel(preset)));
  assert.deepEqual(P.decodePanel(P.payload(last)), preset);
  assert.equal(dev.status().panelSum, P.checksum(P.encodePanel(preset)));
  dev.press('envMode');
  assert.equal(dev.panel.envMode, 0);
  assert.notEqual(dev.status().panelSum, P.checksum(P.encodePanel(preset)));
});

console.log(`\n${passed} passed${process.exitCode ? ', some FAILED' : ''}`);
