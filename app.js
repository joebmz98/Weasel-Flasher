// The connection module shared by the MIDI settings and presets pages:
// connect / demo / disconnect buttons, quiet reconnect to a port the site
// already knows, and a STATUS poll that pages listen to.
//
// Expects these ids in the page: linkLed, deviceName, deviceInfo, linkKind,
// edgeInfo, jackUsb, connectBtn, demoBtn, midiBtn, disconnectBtn.

import { msg, CMD, RPL, PROTOCOL, decodeStatus, decodeInfo } from './protocol.js';
import { SerialTransport, MidiTransport, DemoTransport, Link } from './transport.js';

const $ = (id) => document.getElementById(id);

export const isDemo = () => new URLSearchParams(location.search).has('demo');

// Keep ?demo on links between the tool pages, so the demo carries over.
export function keepDemoInLinks() {
  if (!isDemo()) return;
  document.querySelectorAll('a[data-tool]').forEach((a) => {
    const href = a.getAttribute('href');
    if (!href.includes('?')) a.setAttribute('href', href.replace(/(#.*)?$/, (h) => '?demo' + h));
  });
}

export function createConnection({ setStatus, onConnect = async () => {}, onStatus = () => {}, onDisconnect = () => {} }) {
  let transport = null;
  let link = null;
  let pollTimer = null;
  let connected = false;
  let misses = 0;

  function show(on, t = null, info = null) {
    connected = on;
    document.body.classList.toggle('offline', !on);
    $('linkLed').classList.toggle('on', on);
    $('jackUsb').classList.toggle('lit', on);
    $('connectBtn').hidden = on; $('demoBtn').hidden = on; $('midiBtn').hidden = on;
    $('disconnectBtn').hidden = !on;
    if (on) {
      $('deviceName').textContent = t.kind === 'demo' ? 'WEASEL (demo)' : 'WEASEL';
      $('deviceInfo').textContent = `firmware ${info.version} · ${t.name}`;
      $('linkKind').textContent = t.kind;
      $('edgeInfo').textContent = `connected · ${t.kind}`;
    } else {
      $('deviceName').textContent = 'not connected';
      $('deviceInfo').textContent = 'Plug WEASEL in with a micro-USB data cable, let it start up, then connect.';
      $('edgeInfo').textContent = 'not connected';
    }
  }

  async function hello() {
    const info = decodeInfo(await link.request(msg(CMD.INFO), RPL.INFO, 1200));
    if (info.protocol !== PROTOCOL) {
      throw new Error(`This WEASEL speaks editor protocol ${info.protocol}; this page speaks ${PROTOCOL}. Update the firmware or reload the page.`);
    }
    return info;
  }

  async function poll() {
    if (!connected) return;
    try {
      const st = decodeStatus(await link.request(msg(CMD.STATUS), RPL.STATUS));
      misses = 0;
      onStatus(st);
    } catch (e) {
      if (!(e.timeout && ++misses < 4)) { fail(e); return; }
    }
    if (connected) pollTimer = setTimeout(poll, 280);
  }

  async function connect(kind, { port = null, quiet = false } = {}) {
    if (kind === 'serial' && !SerialTransport.supported) return setStatus('Web Serial is not available here. Use Chrome or Edge on a computer, in its own tab.', 'error');
    if (kind === 'midi' && !MidiTransport.supported) return setStatus('Web MIDI is not available here. Use Chrome or Edge on a computer.', 'error');
    const t = kind === 'demo' ? new DemoTransport() : kind === 'midi' ? new MidiTransport() : new SerialTransport();
    link = new Link(t);
    transport = t;
    t.onClose = () => { if (transport === t) { stop(); setStatus('WEASEL disconnected.', 'error'); } };
    try {
      if (!quiet) setStatus(kind === 'serial' ? "Choose WEASEL's USB serial port in the popup" : kind === 'midi' ? 'Allow MIDI access when asked' : 'Starting the demo');
      let info = null;
      if (kind === 'midi') {
        await t.open({ probe: async () => { try { info = await hello(); return true; } catch (e) { return false; } } });
      } else {
        await t.open({ port });
        info = await hello();
      }
      if (kind === 'demo') window.demoWeasel = t.device;
      await onConnect(link, info, t);
      show(true, t, info);
      setStatus(kind === 'demo' ? 'Demo: a simulated WEASEL, nothing is sent anywhere.' : 'Connected.', 'ok');
      misses = 0;
      poll();
    } catch (e) {
      transport = null;
      await t.close().catch(() => {});
      if (quiet) { setStatus(''); return; }
      if (e.name === 'NotFoundError' && /No port selected|No device/i.test(e.message)) return setStatus('No port chosen.');
      if (e.name === 'SecurityError') return setStatus('The browser blocked access. If this page is embedded in another site, open it in its own tab.', 'error');
      if (e.name === 'NetworkError' || e.name === 'InvalidStateError') return setStatus('Could not open the port. Close the Arduino serial monitor or any other app using it, then try again.', 'error');
      setStatus(e.timeout ? 'WEASEL did not answer. It needs firmware 0.2 or later; update it with the firmware flasher.' : e.message, 'error');
    }
  }

  function stop() {
    clearTimeout(pollTimer);
    show(false);
    onDisconnect();
  }

  async function disconnect(text = 'Disconnected.') {
    const t = transport;
    transport = null;
    stop();
    if (t) await t.close().catch(() => {});
    setStatus(text);
  }

  function fail(e) {
    if (e && e.timeout) { disconnect(); setStatus('WEASEL stopped answering. Check the cable, then connect again.', 'error'); return; }
    setStatus((e && e.message) || String(e), 'error');
  }

  $('connectBtn').addEventListener('click', () => connect('serial'));
  $('midiBtn').addEventListener('click', () => connect('midi'));
  $('demoBtn').addEventListener('click', () => connect('demo'));
  $('disconnectBtn').addEventListener('click', () => disconnect());
  show(false);

  // On load: the demo if asked for, otherwise reopen the one port this site
  // already has permission for (no popup), so switching pages stays connected.
  (async () => {
    if (isDemo()) return connect('demo');
    const ports = await SerialTransport.knownPorts();
    if (ports.length === 1) {
      setStatus('Reconnecting to WEASEL…');
      await connect('serial', { port: ports[0], quiet: true });
    }
  })();

  return {
    get connected() { return connected; },
    request: (frame, rpl, timeout) => link.request(frame, rpl, timeout),
    fail,
  };
}
