// ********************** //
// WEASEL editor link     //
// MIDI receive channel   //
// (kept in QSPI flash)   //
// and panel modes, over  //
// USB from the web pages //
// ********************** //
//
// Protocol: see protocol.js in the WEASEL-Flasher repo. Frames are
//   F0 7D 57 53 <cmd> <data...> F7
// carried over the Daisy Seed's USB port as a CDC serial port.
//
// Arduino IDE: Tools > USB support > "CDC (generic 'Serial' supersede U(S)ART)"
// (or "CDC (no generic 'Serial')"); either way SerialUSB is the USB port.
// MIDI keeps using Serial1 (TRS in on D14).
//
// The receive channel lives at the top of the Seed's 8 MB QSPI flash, so
// flashing new firmware into internal flash keeps it. Panel modes are not
// stored here: the sketch hands them over through readPanel / writePanel, and
// presets are kept by the web page.

#pragma once

#include "DaisyDuino.h"
#include <string.h>

#ifndef WEASEL_EDITOR_SERIAL
#define WEASEL_EDITOR_SERIAL SerialUSB
#endif

namespace weasel {

static const char *const FW_VERSION = "0.2";
static const uint8_t PROTOCOL = 1;
static const uint8_t OMNI = 16;
static const uint32_t STORAGE_OFFSET = 0x7F0000;  // last 64 KB of the 8 MB QSPI chip

struct MidiSettings {
  uint8_t channel;      // 0-15 = MIDI channel 1-16, 16 = omni
  uint8_t reserved[3];  // room for later settings; always 0

  bool operator!=(const MidiSettings &o) const { return memcmp(this, &o, sizeof(*this)) != 0; }
  bool operator==(const MidiSettings &o) const { return !(*this != o); }
};

static const MidiSettings DEFAULTS = { OMNI, { 0, 0, 0 } };

// Panel modes, in wire order (protocol.js PANEL_FIELDS). Each is an index into
// the order a press of that button steps through.
enum PanelField : uint8_t {
  P_SEQ_TRIGGER,     // clock, pulsar, midi
  P_SEQ_STAGES,      // 5, 4, 3
  P_ENV_TRIGGER,     // sequencer, pulsar, midi
  P_ENV_MODE,        // transient, sustain, oscillating
  P_PULSAR_TRIGGER,  // sequencer, midi, free
  P_RANDOM_TRIGGER,  // sequencer, pulsar, midi
  P_MOD_WAVE,        // sine, triangle, sawtooth, square
  P_MOD_TYPE,        // fm, am
  P_LPG2_MODE,       // vca, lo pass, combination (modulation osc channel)
  P_COMPLEX_WAVE,    // saw, square, triangle
  P_POLARITY,        // +, -
  P_LPG1_MODE,       // vca, lo pass, combination (complex osc channel)
  P_MIDI_FOLLOW,     // off, on
  P_CLOCK_MODE,      // free, sync
  PANEL_COUNT
};
static const uint8_t PANEL_MAX[PANEL_COUNT] = { 2, 2, 2, 2, 2, 2, 3, 1, 2, 2, 1, 2, 1, 1 };

// Filled in by the sketch, reported to the MIDI IN monitor.
struct LiveStatus {
  uint8_t lastNote = 60;
  uint8_t lastChannel = 0;   // 0-15
  uint8_t lastVelocity = 0;
  bool anyNote = false;
  bool noteHeld = false;
  bool clockReceiving = false;
  bool clockSync = false;
  float bpm = 0.0f;
  uint8_t activity = 0;      // bumps on every MIDI message; the page blinks its data LED
};

class Editor {
 public:
  LiveStatus live;
  void (*onBootloader)() = nullptr;                  // e.g. []() { hw.ResetToBootloader(); }
  void (*readPanel)(uint8_t *out) = nullptr;         // fill PANEL_COUNT bytes from the sketch's state
  void (*writePanel)(const uint8_t *in) = nullptr;   // apply PANEL_COUNT bytes (already clamped)

  void begin() {
    daisy::QSPIHandle::Config cfg;
    cfg.device = daisy::QSPIHandle::Config::IS25LP064A;
    cfg.mode = daisy::QSPIHandle::Config::MEMORY_MAPPED;
    // Daisy Seed QSPI pins (same as libDaisy's DaisySeed::Init)
    cfg.pin_config.io0 = { DSY_GPIOF, 8 };
    cfg.pin_config.io1 = { DSY_GPIOF, 9 };
    cfg.pin_config.io2 = { DSY_GPIOF, 7 };
    cfg.pin_config.io3 = { DSY_GPIOF, 6 };
    cfg.pin_config.clk = { DSY_GPIOF, 10 };
    cfg.pin_config.ncs = { DSY_GPIOG, 6 };
    qspiOk_ = qspi_.Init(cfg) == daisy::QSPIHandle::OK;

    if (qspiOk_) {
      storage_.Init(DEFAULTS, STORAGE_OFFSET);
      saved_ = sanitize(storage_.GetSettings());
    } else {
      saved_ = DEFAULTS;
    }
    settings_ = saved_;
    WEASEL_EDITOR_SERIAL.begin(115200);
  }

  const MidiSettings &settings() const { return settings_; }

  // channel as the Arduino MIDI library gives it: 1-16
  bool acceptsChannel(uint8_t channel) const {
    return settings_.channel == OMNI || settings_.channel + 1 == channel;
  }

  void noteIn(uint8_t channel, uint8_t note, uint8_t velocity) {
    live.lastNote = note & 0x7F;
    live.lastChannel = (channel - 1) & 0x0F;
    live.lastVelocity = velocity & 0x7F;
    live.anyNote = true;
    activity();
  }
  void activity() { live.activity = (live.activity + 1) & 0x7F; }

  // ---- call from loop() ----
  void poll() {
    while (WEASEL_EDITOR_SERIAL.available() > 0) {
      int c = WEASEL_EDITOR_SERIAL.read();
      if (c < 0) break;
      uint8_t b = (uint8_t)c;
      if (b == 0xF0) { len_ = 0; buf_[len_++] = b; inFrame_ = true; continue; }
      if (!inFrame_) continue;  // anything between frames is ignored
      if (b == 0xF7) {
        inFrame_ = false;
        if (len_ < sizeof(buf_)) { buf_[len_++] = b; handle(buf_, len_); }
        continue;
      }
      if ((b & 0x80) || len_ >= sizeof(buf_) - 1) { inFrame_ = false; continue; }
      buf_[len_++] = b;
    }
  }

  // Same checksum as protocol.js checksum().
  static uint8_t sum(const uint8_t *b, uint8_t n) {
    uint8_t c = 0;
    for (uint8_t i = 0; i < n; i++) c = (uint8_t)((c * 3 + b[i] + 1) & 0x7F);
    return c;
  }

 private:
  enum : uint8_t {
    CMD_INFO = 0x01, CMD_GET_MIDI = 0x02, CMD_SET_MIDI = 0x03, CMD_GET_PANEL = 0x04, CMD_SET_PANEL = 0x05,
    CMD_STATUS = 0x06, CMD_SAVE = 0x07, CMD_REVERT = 0x08, CMD_RESET = 0x09, CMD_BOOTLOADER = 0x0A,
    RPL_NAK = 0x7F,
    NAK_UNKNOWN = 1, NAK_LENGTH = 2, NAK_MAGIC = 3, NAK_STORAGE = 5,
  };
  static const uint8_t MIDI_COUNT = 1;

  static MidiSettings sanitize(const MidiSettings &s) {
    MidiSettings o = DEFAULTS;
    o.channel = s.channel > OMNI ? OMNI : s.channel;
    return o;
  }

  uint8_t midiSum(const MidiSettings &s) const { uint8_t b = s.channel; return sum(&b, 1); }

  void panelBytes(uint8_t *out) {
    memset(out, 0, PANEL_COUNT);
    if (readPanel) readPanel(out);
    for (uint8_t i = 0; i < PANEL_COUNT; i++) if (out[i] > PANEL_MAX[i]) out[i] = PANEL_MAX[i];
  }

  void send(uint8_t cmd, const uint8_t *data, uint8_t n) {
    uint8_t out[64];
    uint8_t k = 0;
    out[k++] = 0xF0; out[k++] = 0x7D; out[k++] = 0x57; out[k++] = 0x53; out[k++] = cmd;
    for (uint8_t i = 0; i < n && k < sizeof(out) - 1; i++) out[k++] = data[i] & 0x7F;
    out[k++] = 0xF7;
    WEASEL_EDITOR_SERIAL.write(out, k);
  }

  void nak(uint8_t cmd, uint8_t reason) { uint8_t d[2] = { (uint8_t)(cmd & 0x7F), reason }; send(RPL_NAK, d, 2); }
  void sendSettings() { uint8_t d = settings_.channel; send(0x42, &d, 1); }
  void sendPanel() { uint8_t d[PANEL_COUNT]; panelBytes(d); send(0x44, d, PANEL_COUNT); }

  void sendStatus() {
    uint16_t bpm10 = live.bpm <= 0 ? 0 : (live.bpm * 10.0f > 16383.0f ? 16383 : (uint16_t)(live.bpm * 10.0f + 0.5f));
    uint8_t storageState = 0;
    if (qspiOk_) storageState = storage_.GetState() == daisy::PersistentStorage<MidiSettings>::State::USER ? 2 : 1;
    uint8_t panel[PANEL_COUNT];
    panelBytes(panel);
    uint8_t d[13] = {
      (uint8_t)(settings_ != saved_ ? 1 : 0), midiSum(settings_), midiSum(saved_), (uint8_t)(live.lastNote & 0x7F),
      (uint8_t)((live.noteHeld ? 1 : 0) | (live.anyNote ? 2 : 0)), (uint8_t)(live.lastChannel & 0x0F),
      (uint8_t)(live.lastVelocity & 0x7F), (uint8_t)((live.clockReceiving ? 1 : 0) | (live.clockSync ? 2 : 0)),
      (uint8_t)(bpm10 >> 7), (uint8_t)(bpm10 & 0x7F), (uint8_t)(live.activity & 0x7F), storageState,
      sum(panel, PANEL_COUNT),
    };
    send(0x46, d, sizeof(d));
  }

  bool store(const MidiSettings &s, bool factory) {
    if (!qspiOk_) return false;
    storage_.GetSettings() = s;
    if (factory) storage_.RestoreDefaults(); else storage_.Save();   // erases one 4 KB block in QSPI
    saved_ = s;
    return true;
  }

  void handle(const uint8_t *f, uint8_t n) {
    if (n < 6 || f[1] != 0x7D || f[2] != 0x57 || f[3] != 0x53) return;  // someone else's SysEx
    uint8_t cmd = f[4];
    const uint8_t *p = f + 5;
    uint8_t pn = n - 6;
    auto magic = [&](uint8_t a, uint8_t b, uint8_t c) { return pn >= 3 && p[0] == a && p[1] == b && p[2] == c; };

    switch (cmd) {
      case CMD_INFO: {
        uint8_t d[3 + 16];
        d[0] = PROTOCOL; d[1] = MIDI_COUNT; d[2] = PANEL_COUNT;
        uint8_t k = 3;
        for (const char *c = FW_VERSION; *c && k < sizeof(d); c++) d[k++] = (uint8_t)*c;
        send(0x41, d, k);
        break;
      }
      case CMD_GET_MIDI: sendSettings(); break;
      case CMD_SET_MIDI:
        if (pn != MIDI_COUNT) { nak(cmd, NAK_LENGTH); break; }
        settings_.channel = p[0] > OMNI ? OMNI : p[0];   // live straight away; kept only after SAVE
        sendSettings();
        break;
      case CMD_GET_PANEL: sendPanel(); break;
      case CMD_SET_PANEL: {
        if (pn != PANEL_COUNT) { nak(cmd, NAK_LENGTH); break; }
        uint8_t v[PANEL_COUNT];
        for (uint8_t i = 0; i < PANEL_COUNT; i++) v[i] = p[i] > PANEL_MAX[i] ? PANEL_MAX[i] : p[i];
        if (writePanel) writePanel(v);
        sendPanel();
        break;
      }
      case CMD_STATUS: sendStatus(); break;
      case CMD_SAVE:
        if (!magic(0x53, 0x41, 0x56)) { nak(cmd, NAK_MAGIC); break; }
        if (!store(settings_, false)) { nak(cmd, NAK_STORAGE); break; }
        sendStatus();
        break;
      case CMD_REVERT: settings_ = saved_; sendStatus(); break;
      case CMD_RESET:
        if (!magic(0x44, 0x45, 0x46)) { nak(cmd, NAK_MAGIC); break; }
        settings_ = DEFAULTS;
        if (!store(DEFAULTS, true)) { nak(cmd, NAK_STORAGE); break; }
        sendStatus();
        break;
      case CMD_BOOTLOADER:
        if (!magic(0x44, 0x46, 0x55)) { nak(cmd, NAK_MAGIC); break; }
        send(0x4A, nullptr, 0);
        WEASEL_EDITOR_SERIAL.flush();
        delay(100);                     // let the reply leave before USB drops
        if (onBootloader) onBootloader();
        break;
      default: nak(cmd, NAK_UNKNOWN); break;
    }
  }

  daisy::QSPIHandle qspi_;
  daisy::PersistentStorage<MidiSettings> storage_{ qspi_ };
  bool qspiOk_ = false;
  MidiSettings settings_ = DEFAULTS;
  MidiSettings saved_ = DEFAULTS;
  uint8_t buf_[64];
  uint8_t len_ = 0;
  bool inFrame_ = false;
};

}  // namespace weasel
