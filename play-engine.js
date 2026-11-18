// WEASEL engine: a line-by-line port of WEASEL_FW 0.2 (AudioCallback,
// updateParameters, the MIDI handlers) and of the DaisySP modules it uses.
// It runs inside an AudioWorklet, or on the main thread as a fallback.
// Pots are 0..1 here (the firmware reads 0..65535 and divides).
function WEASEL_ENGINE() {
  const TWO_PI = Math.PI * 2;
  const LN2 = 0.69314718056;
  const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
  // DaisySP fmap: EXP is a square law, LINEAR is linear
  const fmapExp = (v, lo, hi) => clamp(lo + v * v * (hi - lo), lo, hi);
  const fmapLin = (v, lo, hi) => clamp(lo + v * (hi - lo), lo, hi);
  // firmware fast maths, kept as is: it is part of the sound
  const fastExp2 = (x) => { x = 1 + x * LN2; return x * (1 + (x - 1) * 0.5); };
  const isBad = (x) => x !== x || x > 1e10 || x < -1e10;

  // ---------- DaisySP Oscillator ----------
  const WAVE_SIN = 0, WAVE_TRI = 1, WAVE_SAW = 2, WAVE_SQUARE = 4;
  class Osc {
    constructor(sr) { this.sr = sr; this.phase = 0; this.inc = 440 / sr; this.wave = WAVE_SIN; }
    setFreq(f) { this.inc = f / this.sr; }
    process() {
      const p = this.phase; let out;
      switch (this.wave) {
        case WAVE_SIN: out = Math.sin(p * TWO_PI); break;
        case WAVE_TRI: { const t = -1 + 2 * p; out = 2 * (Math.abs(t) - 0.5); break; }
        case WAVE_SAW: out = -1 * (p * 2 - 1); break;
        default: out = p < 0.5 ? 1 : -1;
      }
      let ph = p + this.inc;
      if (ph >= 1 || ph < 0) ph -= Math.floor(ph);
      this.phase = ph;
      return out;
    }
  }

  // ---------- DaisySP Adsr ----------
  const IDLE = 0, ATTACK = 1, DECAY = 2, RELEASE = 3;
  class Adsr {
    constructor(sr) {
      this.sr = sr; this.susLevel = 0.7; this.x = 0; this.gate = false; this.mode = IDLE;
      this.attackTime = -1; this.attackShape = -1; this.decayTime = -1; this.releaseTime = -1;
      this.attackD0 = 1; this.decayD0 = 1; this.releaseD0 = 1; this.attackTarget = 0;
      this.setAttackTime(0.1); this.setDecayTime(0.1); this.setReleaseTime(0.1);
    }
    setAttackTime(t, shape = 0) {
      if (t === this.attackTime && shape === this.attackShape) return;
      this.attackTime = t; this.attackShape = shape;
      if (t > 0) {
        const target = 9 * Math.pow(shape, 10) + 0.3 * shape + 1.01;
        this.attackTarget = target;
        this.attackD0 = 1 - Math.exp(Math.log(1 - 1 / target) / (t * this.sr));
      } else this.attackD0 = 1;
    }
    tc(t) { return t > 0 ? 1 - Math.exp(-1 / (t * this.sr)) : 1; }
    setDecayTime(t) { if (t !== this.decayTime) { this.decayTime = t; this.decayD0 = this.tc(t); } }
    setReleaseTime(t) { if (t !== this.releaseTime) { this.releaseTime = t; this.releaseD0 = this.tc(t); } }
    setSustainLevel(l) { this.susLevel = l <= 0 ? -0.01 : l > 1 ? 1 : l; }
    retrigger(hard) { this.mode = ATTACK; if (hard) this.x = 0; }
    isRunning() { return this.mode !== IDLE; }
    process(gate) {
      if (gate && !this.gate) this.mode = ATTACK;
      else if (!gate && this.gate) this.mode = RELEASE;
      this.gate = gate;
      let D0 = this.attackD0;
      if (this.mode === DECAY) D0 = this.decayD0; else if (this.mode === RELEASE) D0 = this.releaseD0;
      const target = this.mode === DECAY ? this.susLevel : -0.01;
      let out = 0;
      if (this.mode === ATTACK) {
        this.x += D0 * (this.attackTarget - this.x); out = this.x;
        if (out > 1) { this.x = out = 1; this.mode = DECAY; }
      } else if (this.mode === DECAY || this.mode === RELEASE) {
        this.x += D0 * (target - this.x); out = this.x;
        if (out < 0) { this.x = out = 0; this.mode = IDLE; }
      }
      return out;
    }
  }

  // ---------- DaisySP Svf (double-sampled Chamberlin) ----------
  class Svf {
    constructor(sr) { this.sr = sr; this.init(); }
    init() {
      this.fc = 200; this.res = 0.5; this.drive = 0.5; this.preDrive = 0.5; this.freq = 0.25; this.damp = 0;
      this.notch = 0; this.low = 0; this.high = 0; this.band = 0; this.outLow = 0; this.fcMax = this.sr / 3;
    }
    setFreq(f) {
      this.fc = clamp(f, 1e-6, this.fcMax);
      this.freq = 2 * Math.sin(Math.PI * Math.min(0.25, this.fc / (this.sr * 2)));
      this.damp = Math.min(2 * (1 - Math.pow(this.res, 0.25)), Math.min(2, 2 / this.freq - this.freq * 0.5));
    }
    setRes(r) {
      this.res = clamp(r, 0, 1);
      this.damp = Math.min(2 * (1 - Math.pow(this.res, 0.25)), Math.min(2, 2 / this.freq - this.freq * 0.5));
      this.drive = this.preDrive * this.res;
    }
    process(input) {
      let { low, band, freq, damp, drive } = this;
      let notch = input - damp * band; low += freq * band; let high = notch - low;
      band = freq * high + band - drive * band * band * band;
      let outLow = 0.5 * low;
      notch = input - damp * band; low += freq * band; high = notch - low;
      band = freq * high + band - drive * band * band * band;
      outLow += 0.5 * low;
      this.low = low; this.band = band; this.outLow = outLow;
    }
  }

  // ---------- DaisySP MoogLadder (Huovilainen, as in DaisySP) ----------
  class Moog {
    constructor(sr) { this.sr = sr; this.delay = new Float64Array(6); this.tanhstg = new Float64Array(3); this.freq = 1000; this.res = 0.4; this.oldF = -1; this.oldR = -1; }
    process(input) {
      const THERMAL = 0.000025; const d = this.delay, ts = this.tanhstg;
      if (this.oldF !== this.freq || this.oldR !== this.res) {
        const fc = this.freq / this.sr, f = 0.5 * fc, fc2 = fc * fc, fc3 = fc2 * fc2;
        const fcr = 1.873 * fc3 + 0.4955 * fc2 - 0.649 * fc + 0.9988;
        this.acr = -3.9364 * fc2 + 1.8409 * fc + 0.9968;
        this.tune = (1 - Math.exp(-(TWO_PI * f * fcr))) / THERMAL;
        this.oldF = this.freq; this.oldR = this.res;
      }
      const res4 = 4 * this.res * this.acr, tune = this.tune; let inp = input;
      const stg = [0, 0, 0, 0];
      for (let j = 0; j < 2; j++) {
        inp -= res4 * d[5];
        d[0] = stg[0] = d[0] + tune * (Math.tanh(inp * THERMAL) - ts[0]);
        for (let k = 1; k < 4; k++) {
          inp = stg[k - 1];
          ts[k - 1] = Math.tanh(inp * THERMAL);
          const b = k !== 3 ? (ts[k] = Math.tanh(d[k] * THERMAL)) : Math.tanh(d[k] * THERMAL);
          stg[k] = d[k] + tune * (ts[k - 1] - b);
          d[k] = stg[k];
        }
        d[5] = (stg[3] + d[4]) * 0.5; d[4] = stg[3];
      }
      return d[5];
    }
  }

  // ---------- DaisySP ReverbSc (Costello FDN, 8 lines) ----------
  const REV = [[2473, 0.0010, 3.100], [2767, 0.0011, 3.500], [3217, 0.0017, 1.110], [3557, 0.0006, 3.973],
               [3907, 0.0010, 2.341], [4127, 0.0011, 1.897], [2143, 0.0017, 0.891], [1933, 0.0006, 3.221]];
  class Reverb {
    constructor(sr) {
      this.sr = sr; this.feedback = 0.85;
      this.lines = REV.map(([len, v, rate], n) => {
        const base = (len / 29761) * sr, dev = v * sr;
        const size = Math.ceil(base + dev + 8);
        return { buf: new Float32Array(size), size, w: 0, base, dev, ph: n * 0.37, inc: rate / sr, fs: 0 };
      });
      this.setLp(17000);
    }
    setLp(f) { let d = 2 - Math.cos(f * TWO_PI / this.sr); this.damp = d - Math.sqrt(d * d - 1); }
    process(inL, inR, out) {
      const L = this.lines; let jp = 0;
      for (let n = 0; n < 8; n++) jp += L[n].fs;
      jp *= 0.25;
      const aL = jp + inL, aR = jp + inR; let oL = 0, oR = 0;
      for (let n = 0; n < 8; n++) {
        const l = L[n];
        l.buf[l.w] = (n & 1 ? aR : aL) - l.fs;
        l.ph += l.inc; if (l.ph >= 1) l.ph -= 1;
        const dl = l.base + l.dev * Math.sin(l.ph * TWO_PI);
        let rp = l.w - dl; while (rp < 0) rp += l.size;
        const i0 = rp | 0, fr = rp - i0, i1 = i0 + 1 >= l.size ? 0 : i0 + 1;
        let v = l.buf[i0] + fr * (l.buf[i1] - l.buf[i0]);
        if (++l.w >= l.size) l.w = 0;
        v *= this.feedback;
        v = (l.fs - v) * this.damp + v;
        l.fs = v;
        if (n & 1) oR += v; else oL += v;
      }
      out[0] = oL * 0.35; out[1] = oR * 0.35;
    }
  }

  // ---------- DaisySP Wavefolder ----------
  const fold = (x, gain) => { x *= gain; const ft = Math.floor((x + 1) * 0.5); const sgn = ft % 2 === 0 ? 1 : -1; return sgn * (x - 2 * ft); };

  // ---------- WEASEL ----------
  const CLOCK_DIVS = [1, 2, 4, 8, 12, 16, 24, 32];

  class Weasel {
    constructor(sr) {
      this.sr = sr;
      this.pots = new Float32Array(32);
      this.matrix = [0, 1, 2, 3].map(() => [false, false, false, false, false, false, false]);
      this.stepOn = [true, true, true, true, true];
      // panel modes (cycled by the mode buttons)
      this.m = { seqTrigger: 0, seqStages: 0, envTrigger: 0, envMode: 0, pulsarTrigger: 0, randomTrigger: 0,
                 modWave: 0, modType: 0, lpg2Mode: 2, complexWave: 0, polarity: 0, lpg1Mode: 2, midiFollow: 0, clockMode: 0 };
      this.master = 0.6;

      this.complexOsc = new Osc(sr); this.complexOsc.wave = WAVE_SIN;
      this.morphOsc = new Osc(sr); this.morphOsc.wave = WAVE_SAW;
      this.modOsc = new Osc(sr); this.modOsc.wave = WAVE_SIN;
      this.env = new Adsr(sr);
      this.pulsar = new Adsr(sr); this.pulsar.setAttackTime(0.02); this.pulsar.setDecayTime(0.02); this.pulsar.setSustainLevel(1);
      this.lpg1 = new Svf(sr); this.lpg2 = new Svf(sr);
      this.outFilter = new Moog(sr); this.outFilter.freq = 8000; this.outFilter.res = 0.2;
      this.reverb = new Reverb(sr); this.revOut = [0, 0];
      this.lpgTable = new Float32Array(64);
      for (let i = 0; i < 64; i++) this.lpgTable[i] = clamp(20 * Math.pow(2, (i / 63) * 9.81), 30, 18000);

      // state, as in the firmware
      Object.assign(this, {
        seqCurrentStep: 0, seqMaxSteps: 5, seqSampleCounter: 0, samplesPerTick: sr, activeSeqCV: 0, seqStepCV: [0, 0, 0, 0, 0],
        midiTriggerPending: false, lastPulsarEnvActive: false, manualPulsarTrigger: false, pulsarMidiTrigger: false,
        randomMidiTrigger: false, envMidiTrigger: false, currentRandomValue: 0, gateRemainingSamples: 0,
        transientGateSamples: 0, sustainGateSamples: 0, oscGateSamples: 0, totalCycleSamples: 0,
        midiNoteHeld: false, lastMidiNoteFreq: 1, midiNoteReceived: false,
        midiClockActive: false, midiClockSamplesPerTick: 0, midiSampleCounter: 0, midiTempo: 120, lastMidiClockTime: 0,
        midiClockTicks: 0, currentDivision: 1, currentDivisionIndex: 0, raw: new Float32Array(7), modUpdateCounter: 0,
        pulsarReleaseTime: 0.02, pulsarPeriodModCoeff: 0, attackTime: 0, releaseTime: 0, sustainDuration: 0,
        lastCtrl0: -1, lastCtrl1: -1, lastMode0: -1, lastMode1: -1, frame: 0, paramCounter: 0,
      });
      // telemetry for the panel's LEDs and the scope
      this.tele = { steps: 0, env: 0, pulsar: 0, ctrl0: 0, ctrl1: 0, peak: 0 };
      this.scope = new Float32Array(1024); this.scopeW = 0;
      this.updateParameters();
    }

    millis() { return (this.frame / this.sr) * 1000; }

    // ---- panel input ----
    setModes(m) {
      const old = this.m;
      if (m.seqStages !== undefined && m.seqStages !== old.seqStages) { this.seqMaxSteps = 5 - m.seqStages; this.seqCurrentStep %= this.seqMaxSteps; }
      if (m.envMode === 2 && old.envMode !== 2) { /* envRetriggerFlag = false */ }
      if (m.midiFollow && !old.midiFollow) this.midiNoteReceived = false;
      if (m.clockMode && !old.clockMode) { this.seqCurrentStep = 0; this.seqSampleCounter = 0; this.midiClockTicks = 0; this.m.clockMode = 1; this.updateClockDivision(); }
      Object.assign(this.m, m);
      this.modOsc.wave = [WAVE_SIN, WAVE_TRI, WAVE_SAW, WAVE_SQUARE][this.m.modWave];
      this.morphOsc.wave = [WAVE_SAW, WAVE_SQUARE, WAVE_TRI][this.m.complexWave];
    }
    pulsarButton() { this.manualPulsarTrigger = true; }
    noteOn(note) {
      this.midiTriggerPending = true;
      this.lastMidiNoteFreq = Math.pow(2, (note - 60) / 12);
      this.midiNoteReceived = true; this.midiNoteHeld = true;
      this.pulsarMidiTrigger = true; this.randomMidiTrigger = true; this.envMidiTrigger = true;
    }
    noteOff() { this.midiNoteHeld = false; }
    midiClock(t) {
      // t: when the tick arrived (ms), so tempo is not skewed by message batching
      const now = t !== undefined ? t : this.millis();
      this.lastClockArrival = this.millis();
      this.midiClockActive = true;
      if (this.m.clockMode) {
        if (this.lastMidiClockTime > 0) {
          const q = (now - this.lastMidiClockTime) * 24;
          if (q > 0) {
            this.midiTempo = clamp(60000 / q, 40, 240);
            const spt = 60 / (this.midiTempo * 24);
            this.midiClockSamplesPerTick = Math.max(1, Math.floor(spt * this.sr * this.currentDivision));
          }
        }
      }
      this.lastMidiClockTime = now;
      this.midiClockTicks++;
    }
    midiStart() { this.seqCurrentStep = 0; this.seqSampleCounter = 0; this.midiClockTicks = 0; }
    midiStop() { this.seqSampleCounter = 0; this.midiClockTicks = 0; }
    checkMidiClockActivity() {
      if (this.m.clockMode && this.millis() - (this.lastClockArrival || 0) > 500) { this.midiClockActive = false; this.midiClockSamplesPerTick = 0; }
    }
    updateClockDivision() {
      let i = Math.floor((1 - this.pots[25]) * 8); i = clamp(i, 0, 7);
      this.currentDivisionIndex = i; this.currentDivision = CLOCK_DIVS[i];
    }

    // ---- updateParameters(): the firmware runs it every 5 ms ----
    updateParameters() {
      const p = this.pots, sr = this.sr, fineRange = 3 / 12;
      this.complexOscFreq = fmapExp(p[16], 25, 1760);
      this.complexOscFreqModDepth = p[15];
      this.complexOscFine = (p[17] * 2 - 1) * fineRange;
      this.complexOscMorphMix = p[20];
      this.complexOscWF = 1 + 19 * p[19];
      this.complexOscWFModDepth = p[18];
      this.foldedLpgModAmount = fmapLin(p[21], 0, 0.98);
      this.complexOscSigLevel = fmapExp(p[22], 0, 0.98);
      this.modulationOscFreq = fmapExp(p[11], 0.1, 1000);
      this.modulationFreqModDepth = p[10];
      this.modulationFine = (p[12] * 2 - 1) * fineRange;
      this.modulationOscMod = p[14];
      this.modulationOscModCoeffDepth = p[13];
      this.modOscLpgModAmount = fmapLin(p[23], 0, 0.98);
      this.modOscSigLevel = fmapExp(p[24], 0, 0.98);

      const minTime = 0.002, maxTime = 5;
      this.attackTime = fmapExp(p[5], minTime, maxTime);
      this.env.setAttackTime(this.attackTime);
      this.sustainDuration = fmapExp(p[6], 0.02, maxTime);
      this.transientGateSamples = Math.max(Math.floor(this.sustainDuration * sr), Math.floor(0.02 * sr));
      this.sustainGateSamples = Math.max(Math.floor(this.sustainDuration * sr), Math.floor(0.1 * sr));
      this.releaseTime = this.m.envMode === 2 ? fmapExp(p[7], minTime, maxTime * 0.2) : fmapExp(p[7], minTime, maxTime);
      this.env.setReleaseTime(this.releaseTime);
      this.env.setDecayTime(0);
      this.env.setSustainLevel(1);

      const pMin = 0.02, pMax = 5;
      const base = pMin * Math.pow(pMax / pMin, p[9]);
      this.pulsarReleaseTime = clamp(base - 2 * this.pulsarPeriodModCoeff, pMin, pMax);
      this.pulsar.setReleaseTime(this.pulsarReleaseTime);

      for (let i = 0; i < 5; i++) this.seqStepCV[i] = fmapExp(p[i], 0, 1);

      if (this.m.clockMode) this.updateClockDivision();
      else {
        this.seqClockSpeed = clamp(0.1 * Math.pow(40000, 1 - p[25]), 0.1, 4000);
        this.samplesPerTick = Math.max(1, Math.floor((this.seqClockSpeed / 1000) * sr));
      }
      this.reverbMix = p[26];
    }

    // ---- AudioCallback ----
    process(outL, outR, n) {
      const m = this.m, sr = this.sr, raw = this.raw, revOut = this.revOut;
      for (let i = 0; i < n; i++) {
        if (++this.paramCounter >= sr * 0.005) { this.paramCounter = 0; this.checkMidiClockActivity(); this.updateParameters(); }
        this.frame++;

        // 1. triggers and sequencer
        let seqStep = false;
        if (m.seqTrigger === 0) {
          if (m.clockMode && this.midiClockActive && this.midiClockSamplesPerTick > 0) {
            if (++this.midiSampleCounter >= this.midiClockSamplesPerTick) { seqStep = true; this.midiSampleCounter = 0; }
          } else if (m.clockMode && !this.midiClockActive) {
            // sync on, no clock: the sequencer stops
          } else if (++this.seqSampleCounter >= this.samplesPerTick) { seqStep = true; this.seqSampleCounter = 0; }
        } else if (m.seqTrigger === 1) {
          const on = this.pulsar.isRunning();
          if (on && !this.lastPulsarEnvActive) seqStep = true;
          this.lastPulsarEnvActive = on;
        } else if (this.midiTriggerPending) { seqStep = true; this.midiTriggerPending = false; }

        if (seqStep) {
          this.seqCurrentStep = (this.seqCurrentStep + 1) % this.seqMaxSteps;
          if (this.stepOn[this.seqCurrentStep]) this.activeSeqCV = this.seqStepCV[this.seqCurrentStep];
          else this.gateRemainingSamples = 0;
          this.tele.steps++;
        }

        // 2. pulsar
        let pulsarTrig = false;
        if (this.manualPulsarTrigger) { pulsarTrig = true; this.manualPulsarTrigger = false; }
        else if (m.pulsarTrigger === 0) pulsarTrig = seqStep;
        else if (m.pulsarTrigger === 2) { if (!this.pulsar.isRunning()) pulsarTrig = true; }
        else if (this.pulsarMidiTrigger) { pulsarTrig = true; this.pulsarMidiTrigger = false; }
        if (pulsarTrig) {
          this.pulsar.setAttackTime(0.02); this.pulsar.setDecayTime(0.02); this.pulsar.setSustainLevel(0);
          this.pulsar.setReleaseTime(this.pulsarReleaseTime); this.pulsar.retrigger(true);
        }

        // 3. random voltage
        let rnd = false;
        if (m.randomTrigger === 0) rnd = seqStep;
        else if (m.randomTrigger === 1) rnd = pulsarTrig;
        else if (this.randomMidiTrigger) { rnd = true; this.randomMidiTrigger = false; }
        if (rnd) this.currentRandomValue = Math.random();

        // 4. envelope
        let envTrig = false;
        if (m.envTrigger === 0) envTrig = seqStep;
        else if (m.envTrigger === 2) { if (this.envMidiTrigger) { envTrig = true; this.envMidiTrigger = false; } }
        else envTrig = pulsarTrig;

        let gate = false;
        if (m.envMode === 0) {
          if (envTrig) { this.gateRemainingSamples = this.transientGateSamples; this.env.retrigger(true); }
          if (this.gateRemainingSamples > 0) { gate = true; this.gateRemainingSamples--; }
        } else if (m.envMode === 1) {
          if (envTrig) { this.env.retrigger(true); this.gateRemainingSamples = this.sustainGateSamples; }
          if (m.envTrigger === 2) { gate = this.midiNoteHeld; this.gateRemainingSamples = 0; }
          else if (this.gateRemainingSamples > 0) { gate = true; this.gateRemainingSamples--; }
        } else {
          if (!this.env.isRunning() && this.oscGateSamples === 0) {
            this.env.retrigger(true);
            this.totalCycleSamples = Math.floor((this.attackTime + this.sustainDuration + this.releaseTime) * sr * 0.5);
            this.oscGateSamples = this.totalCycleSamples;
          }
          if (this.oscGateSamples > 0) { gate = true; this.oscGateSamples--; }
        }
        const envSig = this.env.process(gate);
        const pulsarSig = this.pulsar.process(pulsarTrig);

        // 5. virtual patch bay, every 16 samples
        if (++this.modUpdateCounter >= 16) {
          this.modUpdateCounter = 0;
          raw.fill(0);
          for (let c = 0; c < 4; c++) {
            const src = c === 0 ? this.activeSeqCV : c === 1 ? envSig : c === 2 ? pulsarSig : this.currentRandomValue;
            const s = c === 2 ? 100 : 1, fs = c === 2 ? 200 : 1, row = this.matrix[c];
            if (row[0]) raw[0] += src * s;
            if (row[1]) raw[1] += src * fs;
            if (row[2]) raw[2] += src * s;
            if (row[3]) raw[3] += src * fs;
            if (row[4]) raw[4] += src * s;
            if (row[5]) raw[5] += src * fs;
            if (row[6]) raw[6] += src * fs;
          }
        }

        // 6. attenuators
        this.pulsarPeriodModCoeff = raw[0] * this.pots[8];
        const modFreqCoeff = raw[1] * this.modulationFreqModDepth;
        const modModCoeff = raw[2] * this.modulationOscModCoeffDepth;
        const compFreqCoeff = raw[3] * this.complexOscFreqModDepth;
        const wfCoeff = raw[4] * this.complexOscWFModDepth;
        const vca0 = raw[5] * this.foldedLpgModAmount;
        const vca1 = raw[6] * this.modOscLpgModAmount;

        // 7. oscillators
        const transpose = m.midiFollow && this.midiNoteReceived ? this.lastMidiNoteFreq : 1;
        this.modOsc.setFreq(this.modulationOscFreq * transpose * fastExp2(modFreqCoeff * 5 + this.modulationFine));
        let modSig = this.modOsc.process();
        if (m.modWave >= 2) modSig = clamp(modSig, -1.2, 1.2);
        modSig = clamp(modSig, -1.5, 1.5);
        const depth = clamp(this.modulationOscMod + modModCoeff * 2, 0, 2);
        const fm = m.modType ? 0 : modSig * depth;

        const cf = this.complexOscFreq * transpose * fastExp2((compFreqCoeff + fm) * 5 + this.complexOscFine);
        this.complexOsc.setFreq(cf); this.morphOsc.setFreq(cf);
        const cs = this.complexOsc.process(), ms = this.morphOsc.process();
        let mixed = cs + (ms - cs) * this.complexOscMorphMix;
        if (m.polarity) mixed = -mixed;

        // 8. wavefolder and am
        const wfGain = clamp(this.complexOscWF + wfCoeff * 20, 1, 20);
        let folded = fold(mixed, wfGain);
        if (m.modType) folded *= 1 + modSig * depth;

        // 9. dual lo pass gate
        let mix = 0;
        const ctrl0 = clamp(this.complexOscSigLevel + vca0, 0, 0.9);
        let s0 = folded;
        if (m.lpg1Mode === 0) s0 *= ctrl0 * ctrl0;
        else if (ctrl0 < 0.0005) s0 = 0;
        else {
          if (Math.abs(ctrl0 - this.lastCtrl0) > 0.002 || m.lpg1Mode !== this.lastMode0) {
            this.lpg1.setFreq(this.lpgFreq(ctrl0));
            this.lpg1.setRes(Math.min(m.lpg1Mode === 1 ? ctrl0 * ctrl0 * 0.2 : 0.1, 0.95));
            this.lastCtrl0 = ctrl0; this.lastMode0 = m.lpg1Mode;
          }
          this.lpg1.process(s0); s0 = this.lpg1.outLow;
          if (isBad(s0)) { this.lpg1.init(); s0 = 0; this.lastCtrl0 = -1; } else s0 = clamp(s0, -1, 1);
          if (m.lpg1Mode === 2) s0 *= ctrl0;
        }
        mix += s0;

        let s1 = modSig;
        if (m.modWave >= 2) s1 = clamp(s1, -1, 1);
        const ctrl1 = clamp(this.modOscSigLevel + vca1, 0, 0.9);
        if (m.lpg2Mode === 0) s1 *= ctrl1;
        else if (ctrl1 < 0.0001) s1 = 0;
        else {
          if (Math.abs(ctrl1 - this.lastCtrl1) > 0.002 || m.lpg2Mode !== this.lastMode1) {
            this.lpg2.setFreq(this.lpgFreq(ctrl1));
            this.lpg2.setRes(Math.min(m.lpg2Mode === 1 ? ctrl1 * ctrl1 * 0.2 : 0, 0.95));
            this.lastCtrl1 = ctrl1; this.lastMode1 = m.lpg2Mode;
          }
          this.lpg2.process(s1); s1 = this.lpg2.outLow;
          if (isBad(s1)) { this.lpg2.init(); s1 = 0; this.lastCtrl1 = -1; } else s1 = clamp(s1, -1, 1);
          if (m.lpg2Mode === 2) s1 *= ctrl1;
        }
        mix += s1;

        if (isBad(mix)) mix = 0;
        mix = clamp(mix, -1, 1);

        // 10. output filter, 11. reverb
        mix = this.outFilter.process(mix);
        if (isBad(mix)) { this.outFilter = new Moog(sr); this.outFilter.freq = 8000; this.outFilter.res = 0.2; mix = 0; }
        this.reverb.process(mix, mix, revOut);
        const dry = 1 - this.reverbMix, g = 0.4 * this.master * 4; // master volume: the panel's analogue pot, web only
        const l = (mix * dry + revOut[0] * this.reverbMix) * g;
        const r = (mix * dry + revOut[1] * this.reverbMix) * g;
        outL[i] = Math.tanh(l); outR[i] = Math.tanh(r);

        // telemetry
        const t = this.tele;
        t.env = envSig; t.pulsar = pulsarSig; t.ctrl0 = ctrl0; t.ctrl1 = ctrl1;
        const a = Math.abs(l); if (a > t.peak) t.peak = a;
        this.scope[this.scopeW] = l; this.scopeW = (this.scopeW + 1) & 1023;
      }
    }
    lpgFreq(c) {
      c = clamp(c, 0, 1); const idx = c * 63, i = idx | 0;
      if (i >= 63) return this.lpgTable[63];
      return this.lpgTable[i] + (idx - i) * (this.lpgTable[i + 1] - this.lpgTable[i]);
    }
    snapshot() {
      const t = this.tele;
      const s = {
        step: this.seqCurrentStep, steps: t.steps, env: t.env, pulsar: t.pulsar, random: this.currentRandomValue,
        ctrl0: t.ctrl0, ctrl1: t.ctrl1, peak: t.peak, clockActive: this.midiClockActive, bpm: this.midiTempo,
        division: this.currentDivision, stepMs: this.seqClockSpeed, noteHeld: this.midiNoteHeld,
        scope: this.scope.slice(), scopeW: this.scopeW,
      };
      t.peak = 0;
      return s;
    }
    message(d) {
      switch (d.type) {
        case 'pots': for (const k in d.pots) this.pots[k] = d.pots[k]; break;
        case 'modes': this.setModes(d.modes); break;
        case 'matrix': this.matrix = d.matrix.map((r) => r.slice()); break;
        case 'steps': this.stepOn = d.steps.slice(); break;
        case 'master': this.master = d.value; break;
        case 'pulsar': this.pulsarButton(); break;
        case 'noteOn': this.noteOn(d.note); break;
        case 'noteOff': this.noteOff(); break;
        case 'clock': this.midiClock(d.t); break;
        case 'start': this.midiStart(); break;
        case 'stop': this.midiStop(); break;
      }
    }
  }

  if (typeof registerProcessor === 'function') {
    registerProcessor('weasel', class extends AudioWorkletProcessor {
      constructor() {
        super();
        this.w = new Weasel(sampleRate); this.n = 0;
        this.port.onmessage = (e) => {
          if (e.data.type === 'batch') e.data.items.forEach((d) => this.w.message(d)); else this.w.message(e.data);
        };
      }
      process(inputs, outputs) {
        const o = outputs[0];
        this.w.process(o[0], o[1] || o[0], o[0].length);
        if ((this.n += o[0].length) >= 1536) { this.n = 0; this.port.postMessage(this.w.snapshot()); }
        return true;
      }
    });
  }
  return Weasel;
}
if (typeof module !== 'undefined') module.exports = WEASEL_ENGINE;
