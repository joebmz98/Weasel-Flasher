// Drives the host build of WeaselEditor.h (harness.cpp) with frames made by
// protocol.js, and checks the replies against what the web pages expect.
import { spawn } from 'node:child_process';
import assert from 'node:assert/strict';
import * as P from '../../protocol.js';

const proc = spawn(new URL('./harness', import.meta.url).pathname);
let wait = null;
let buf = '';
proc.stdout.on('data', (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const l = buf.slice(0, i);
    buf = buf.slice(i + 1);
    if (wait) { const w = wait; wait = null; w(l); }
  }
});
const send = (text) => new Promise((r) => { wait = r; proc.stdin.write(text + '\n'); });
const req = async (frame, noise = '') => {
  const l = await send(noise + frame.map((b) => b.toString(16)).join(' '));
  const [hex, boots] = l.split('|');
  const bytes = hex.trim() ? hex.trim().split(/\s+/).map((h) => parseInt(h, 16)) : [];
  const frames = [];
  new P.FrameParser((f) => frames.push(f)).push(bytes);
  return { frames, boots: Number(boots.split('=')[1]), p: frames[0] ? P.payload(frames[0]) : [] };
};
const ok = (name) => console.log(`  ok    ${name}`);

let r = await req(P.msg(P.CMD.INFO), '41 42 0d 0a ');
const info = P.decodeInfo(r.p);
assert.deepEqual([info.protocol, info.midiCount, info.panelCount, info.version], [1, 1, P.PANEL_FIELDS.length, '0.2']);
ok('INFO through serial noise: protocol 1, 1 MIDI byte, 14 panel bytes');

r = await req(P.msg(P.CMD.GET_MIDI));
assert.deepEqual(P.decodeMidi(r.p), P.MIDI_DEFAULTS);
ok('factory channel is omni');

r = await req(P.msg(P.CMD.SET_MIDI, 4));
assert.equal(P.decodeMidi(r.p).channel, 4);
r = await req(P.msg(P.CMD.STATUS));
let st = P.decodeStatus(r.p);
assert.equal(st.dirty, true);
assert.equal(st.liveSum, P.checksum(P.encodeMidi({ channel: 4 })), 'firmware and page checksums agree');
assert.equal(st.storage, 'factory');
ok('SET_MIDI is live and flagged unsaved');

assert.match(await send('NOTE 5 60 64'), /accepts=1/);
assert.match(await send('NOTE 6 60 64'), /accepts=0/);
ok('channel 5 accepted, channel 6 ignored');

r = await req(P.msg(P.CMD.SAVE, 1, 2, 3));
assert.deepEqual([r.frames[0][4], r.p[1]], [P.RPL.NAK, 3]);
r = await req(P.msg(P.CMD.SAVE, ...P.MAGIC.SAVE));
st = P.decodeStatus(r.p);
assert.equal(st.dirty, false);
assert.equal(st.storage, 'saved');
await send('REBOOT');
r = await req(P.msg(P.CMD.GET_MIDI));
assert.equal(P.decodeMidi(r.p).channel, 4);
ok('SAVE needs its confirmation bytes, and the channel survives a reboot');

r = await req(P.msg(P.CMD.SET_MIDI, 99));
assert.equal(P.decodeMidi(r.p).channel, P.OMNI);
r = await req(P.msg(P.CMD.REVERT));
r = await req(P.msg(P.CMD.GET_MIDI));
assert.equal(P.decodeMidi(r.p).channel, 4);
ok('out-of-range channel clamps to omni; REVERT goes back to the save');

r = await req(P.msg(P.CMD.SET_MIDI, 1, 2));
assert.equal(r.p[1], 2);
ok('SET_MIDI with the wrong length is refused');

r = await req(P.msg(P.CMD.GET_PANEL));
assert.deepEqual(P.decodePanel(r.p), P.PANEL_DEFAULTS);
ok('GET_PANEL decodes to the power-on modes');

const preset = { ...P.PANEL_DEFAULTS, seqTrigger: 2, seqStages: 1, envMode: 2, modWave: 3, modType: 1, lpg1Mode: 0, complexWave: 2, polarity: 1, midiFollow: 1, clockMode: 1 };
r = await req(P.msg(P.CMD.SET_PANEL, ...P.encodePanel(preset)));
assert.deepEqual(P.decodePanel(r.p), preset);
r = await req(P.msg(P.CMD.STATUS));
assert.equal(P.decodeStatus(r.p).panelSum, P.checksum(P.encodePanel(preset)));
ok('SET_PANEL applies a preset; STATUS carries the matching panel checksum');

r = await req(P.msg(P.CMD.SET_PANEL, ...new Array(P.PANEL_FIELDS.length).fill(9)));
assert.deepEqual(r.p, P.PANEL_FIELDS.map((f) => f.options.length - 1));
ok('SET_PANEL clamps every mode to its last option');

r = await req(P.msg(P.CMD.SET_PANEL, 1, 2, 3));
assert.equal(r.p[1], 2);
ok('SET_PANEL with the wrong length is refused');

await send('PRESS');
r = await req(P.msg(P.CMD.STATUS));
const before = P.decodeStatus(r.p).panelSum;
await send('PRESS');
r = await req(P.msg(P.CMD.STATUS));
assert.notEqual(P.decodeStatus(r.p).panelSum, before);
ok('a panel button press changes the panel checksum (the page re-reads)');

r = await req(P.msg(P.CMD.RESET, ...P.MAGIC.RESET));
assert.equal(P.decodeStatus(r.p).storage, 'factory');
await send('REBOOT');
r = await req(P.msg(P.CMD.GET_MIDI));
assert.deepEqual(P.decodeMidi(r.p), P.MIDI_DEFAULTS);
ok('RESET goes back to omni and survives a reboot');

r = await req(P.msg(0x33));
assert.equal(r.p[1], 1);
r = await req([0xf0, 0x7d, 0x4d, 0x38, 0x01, 0xf7]);
assert.equal(r.frames.length, 0);
ok('unknown commands NAK; MC8P frames are ignored');

r = await req(P.msg(P.CMD.BOOTLOADER, ...P.MAGIC.BOOTLOADER));
assert.equal(r.frames[0][4], P.RPL.BOOTLOADER);
assert.equal(r.boots, 1);
ok('BOOTLOADER replies, then calls the reset hook');

const demo = new P.DemoWeasel();
let last;
demo.onReply = (f) => { last = f; };
for (const f of [P.msg(P.CMD.GET_MIDI), P.msg(P.CMD.GET_PANEL), P.msg(P.CMD.STATUS)]) {
  demo.receive(f);
  r = await req(f);
  assert.equal(r.frames[0][4], last[4]);
  assert.equal(r.frames[0].length, last.length);
}
ok('replies have the same shape as the demo WEASEL in the page');

console.log('\nALL FIRMWARE CHECKS PASSED');
proc.stdin.end();
