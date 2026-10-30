// ********************** //
// ==== WEASEL ========== //
// Buchla Music Easel     //
// Inspired Digital Synth //
// firmware version 0.2   //
// Hardware Version: 1.0  //
// ********************** //
// designed by            //
// .axs instruments       //
// ********************** //
// Description: Buchla Music Easel nspired digital synth
//              Virtual patch bay simulating modular
//              experience.
// ********************** //

// ********************** //
// LICENSE & COPYRIGHT
//
// This project is released under the MIT License.
// Copyright (c) 2024 .axs instruments
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ********************* //


#include "DaisyDuino.h"

// EXTRA INCLUSIONS
#include <MIDI.h>          // MIDI
#include <CpuLoadMeter.h>  // CPU LOAD METER
#include "wavefolder.h"    // EXTRACTED FROM DAISYSP
#include "WeaselEditor.h"  // WEB PAGES: MIDI CHANNEL + PANEL MODES OVER USB SERIAL
#include <math.h>

using namespace daisysp;

// DEBUG
#pragma GCC optimize("O2")

// -- MUX PINS - FIRST MUX
#define MUX1_S0 D0
#define MUX1_S1 D1
#define MUX1_S2 D2
#define MUX1_S3 D3
#define MUX1_SIG A0  // MUX signal pin

// -- MUX PINS - SECOND MUX
#define MUX2_S0 D4
#define MUX2_S1 D5
#define MUX2_S2 D6
#define MUX2_S3 D7
#define MUX2_SIG A1  // Second MUX signal pin

// -- 4x7 BUTTON MATRIX PINS // VIRTUAL PATCH BAY
#define MATRIX_COL0 19  // Column 0 (B0) // SEQUENCER CV OUT
#define MATRIX_COL1 20  // Column 1 (B1) // EG CV OUT
#define MATRIX_COL2 21  // Column 2 (B2) // PULSAR CV OUT
#define MATRIX_COL3 22  // Column 3 (B3) // RANDOM VOLTAGE CV OUT
#define MATRIX_ROW0 23  // Row 0 (A0) // PULSAR PERIOD MODULATION
#define MATRIX_ROW1 24  // Row 1 (A1) // MOD OSC FREQ
#define MATRIX_ROW2 25  // Row 2 (A2) // MOD OSC MOD AMOUNT
#define MATRIX_ROW3 26  // Row 3 (A3) // COMPLEX OSC PITCH
#define MATRIX_ROW4 27  // Row 4 (A4) // COMPLEX OSC WAVEFOLDER AMOUNT
#define MATRIX_ROW5 28  // Row 5 (A5) // LPG CH1 LEVEL MODULATION
#define MATRIX_ROW6 29  // Row 6 (A6) // LPG CH2 LEVEL MODULATION

// -- SECOND BUTTON MATRIX PINS
#define MATRIX2_COL0 12  // X0
#define MATRIX2_COL1 13  // X1
#define MATRIX2_COL2 17  // X2
#define MATRIX2_COL3 18  // X3
#define MATRIX2_ROW0 8   // Y0
#define MATRIX2_ROW1 9   // Y1
#define MATRIX2_ROW2 10  // Y2
#define MATRIX2_ROW3 11  // Y3
#define MATRIX2_ROW4 30  // Y4

// -- BUTTON MATRIX 1 VARIABLES --
bool matrixStates[4][7] = { { false } };
bool lastMatrixStates[4][7] = { { false } };
unsigned long lastMatrixRead = 0;
const unsigned long MATRIX_READ_INTERVAL = 20;

const char* matrix1Functions[4][7] = {
  { "Seq CV -> Pulsar Decay", "Seq CV -> ModOsc Pitch", "Seq CV -> Mod Amount", "Seq CV -> CompOsc Pitch", "Seq CV -> WF Mod", "Seq CV -> LPG1", "Seq CV -> LPG2" },
  { "Env -> Pulsar Decay", "Env -> ModOsc Pitch", "Env -> Mod Amount", "Env -> CompOsc Pitch", "Env -> WF Mod", "Env -> LPG1", "Env -> LPG2" },
  { "Pulsar -> Pulsar Decay", "Pulsar -> ModOsc Pitch", "Pulsar -> Mod Amount", "Pulsar -> CompOsc Pitch", "Pulsar -> WF Mod", "Pulsar -> LPG1", "Pulsar -> LPG2" },
  { "S&H -> Pulsar Decay", "S&H -> ModOsc Pitch", "S&H -> Mod Amount", "S&H -> CompOsc Pitch", "S&H -> WF Mod", "S&H -> LPG1", "S&H -> LPG2" }
};

// -- BUTTON MATRIX 2 --
bool matrix2CurrentStates[4][5] = { { false } };
bool matrix2PreviousStates[4][5] = { { false } };
bool matrix2DebouncedStates[4][5] = { { false } };
unsigned long lastMatrix2Read = 0;

// -- POT VARIABLES --
const int NUM_POTS_PER_MUX = 16;
const int TOTAL_POTS = 32;  // 2 muxes * 16 channels each
int potValues[TOTAL_POTS] = { 0 };
int lastPotValues[TOTAL_POTS] = { 0 };
unsigned long lastPotRead = 0;
const unsigned long POT_READ_INTERVAL = 5;  // Read pots every Xms
String potNames[TOTAL_POTS];                // Array to store pot names
const int POT_THRESHOLD = 2;                //


// -- DAISY DUINO OBJECTS --
DaisyHardware hw;  // DAISY SEED
// -- OSCILLATORS
Oscillator complexOsc;       // COMPLEX OSCILLATOR
Oscillator complexOscMorph;  // COMPLEX MORPH OSCILLATOR
Oscillator modulationOsc;    // COMPLEX OSCILLATOR
// -- ENVELOPE GENERATOR
Adsr env;  // ENV GENERATOR
// -- PULSAR GENERATOR
Adsr pulsar;  // ENV GENERATOR FOR PULSAR
// -- LOW-PASS - LPG
Svf lpGateFilter1;  // FILTER FOR LPG CHAN1
Svf lpGateFilter2;  // FILTER FOR LPG CHAN2
// -- OUTPUT ANALOGUE FILTER
MoogLadder outputFilter;
// -- REVERB
ReverbSc reverb;  // REVERB
// -- WAVEFOLDER
daisysp::Wavefolder waveFolder;


// -- OBJECT VARIABLES --
float sampleRate;
// -- OSCILLATORS
// -- COMPLEX
float complexOscFreq;                 // CONTROLS FREQUENCY
float complexOscFine;                 // CONTROLS FINE TUNE
float complexOscFreqCoeff;            // COEFF FOR FREQUENCY MODULATION USING MATRIX
float complexOscWF;                   // CONTROLS WF AMOUNT
float complexOscWFCoeff;              // COEFF FOR WF MODULATION USING MATRIX
float complexOscWFModDepth = 0.0f;    // WAVEFOLDER MOD DEPTH
float complexOscFreqModDepth = 0.0f;  // FREQ MOD DEPTH
bool complexOscInverted = false;      // TOGGLE POLARITY
bool complexOscMidiEnabled = false;   // TOGGLE MIDI
float complexOscMorphMix;             // COMPLEX OSC TIMBRE AMOUNT
int morphWaveformIndex = 0;           // 0 = SAW, 1 = SQUARE, 2 = TRIANGLE (panel order)
float complexOscSigLevel = 0.0f;      // LPG CONTROL

// -- MODULATION
float modulationOscFreq = 0.0f;           // CONTROLS FREQUENCY
float modulationFine = 0.0f;              // CONTROLS FINE TUNE
float modulationFreqCoeff = 0.0f;         // COEFF FOR FREQUENCY MODULATION USING MATRIX
float modulationOscMod = 0.0f;            // CONTROLS MOD AMOUNT APPLIED TO COMPLEXOSC
float modulationOscModCoeff;              // COEFF FOR MOD AMOUNT MODULATION USING MATRIX
float modulationFreqModDepth = 0.0f;      // FREQ MOD DEPTH
float modulationOscModCoeffDepth = 0.0f;  // MODULATION MOD DEPTH
bool useAmplitudeMod = false;             // False = FM, True = AM
bool modulationMidiEnabled = false;       // TOGGLE MIDI
int modWaveformIndex = 0;                 // 0: SIN, 1: TRI, 2: SAW, 3: SQUARE
float modOscSigLevel = 0.0f;              // LPG CONTROL

// -- LPG --
float vcaComplexOsc;              // AMPLITUDE COEFF FOR COMPLEXOSC
float vcaModulationOsc;           // AMPLITUDE COEFF FOR MODULATIONOSC
float foldedLpgModAmount = 0.0f;  // Multiplier for Folded LPG mod
float modOscLpgModAmount = 0.0f;  // Multiplier for Mod Osc LPG mod
enum LpgMode { LPG_MODE_VCA,
               LPG_MODE_LP,
               LPG_MODE_COMBI };
LpgMode foldedLpgMode = LPG_MODE_COMBI;  // Default to COMBI mode
LpgMode modOscLpgMode = LPG_MODE_COMBI;  // Default to COMBI mode
float lastCtrl0 = -1.0f;
float lastCtrl1 = -1.0f;
int lastMode0 = -1;
int lastMode1 = -1;
#define LPG_TABLE_SIZE 64
float lpgFreqTable[LPG_TABLE_SIZE];


// -- ENVELOPE [BUCHLA ASD/ASR] --
enum EnvelopeMode { ENV_MODE_TRANSIENT,            // One-shot, cannot sustain
                    ENV_MODE_SUSTAIN,              // Can sustain with gate
                    ENV_MODE_OSCILLATE };          // Looping envelope
EnvelopeMode currentEnvMode = ENV_MODE_TRANSIENT;  // Default to Transient
float attackTime = 0.0f;
float releaseTime = 0.0f;
float sustainDuration = 0.0f;
uint32_t gateRemainingSamples = 0;
uint32_t transientGateSamples = 0;
uint32_t sustainGateSamples = 0;
bool envRetriggerFlag = false;  // For oscillating mode

// -- ENVELOPE TRIGGER SELECT --
enum EnvTriggerMode { ENV_TRIGGER_SEQ,                   // Sequencer steps (default)
                      ENV_TRIGGER_PULSAR,                // Pulsar trigger
                      ENV_TRIGGER_MIDI };                // MIDI note on
EnvTriggerMode currentEnvTriggerMode = ENV_TRIGGER_SEQ;  // Default to Sequencer
bool envTrigger = false;                                 // Separate flag for  triggering
bool envMidiTrigger = false;                             // Separate flag for MIDI triggering
static uint32_t oscGateSamples = 0;


// -- ENVELOPE STATE --
bool midiNoteHeld = false;  // True while a MIDI note is physically held
bool envGateHeld = false;   // Current gate state fed to env.Process()

// -- PULSAR ENV --
enum PulsarMode { PULSAR_MODE_SEQ,
                  PULSAR_MODE_MIDI,
                  PULSAR_MODE_OSC };
PulsarMode currentPulsarMode = PULSAR_MODE_SEQ;
float pulsarReleaseTime = 0.02f;
bool lastPulsarEnvActive = false;
float pulsarPeriodModCoeff = 0.0f;
bool manualPulsarTrigger = false;
bool pulsarMidiTrigger = false;


// -- SAMPLE&HOLD / RANDOM VOLTAGE --
enum RandomMode { RANDOM_MODE_SEQ,
                  RANDOM_MODE_PULSAR,
                  RANDOM_MODE_MIDI };
RandomMode currentRandomMode = RANDOM_MODE_SEQ;
float currentRandomValue = 0.0f;
bool randomMidiTrigger = false;

// -- REVERB --
float reverbMix;  // REVERB MIX COEFF

// -- MIDI --
#define MIDI_RX_PIN 14  // USART1 Rx (Digital pin 30)
  // MIDI OBJECT
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
// -- WEB EDITOR (MIDI CHANNEL + PANEL MODES) --
weasel::Editor editor;
// --
bool quantizeToMidi = false;       // Enable/disable MIDI quantization mode
float baseFreqComplex = 261.63f;   // Base frequency for complex osc (C4 = 261.63Hz)
float baseFreqMod = 261.63f;       // Base frequency for mod osc (C4 = 261.63Hz)
float lastMidiNoteFreq = 261.63f;  // Last received MIDI note frequency
bool midiNoteReceived = false;     // Track if we've received a MIDI note in quantize mode

// -- MIDI CLOCK SYNC --
bool midiClockSync = false;               // Enable/disable MIDI clock sync
bool midiClockActive = false;             // Whether MIDI clock is currently being received
uint32_t midiClockTicks = 0;              // Counter for MIDI clock ticks
uint32_t midiClockSamplesPerTick = 0;     // Samples between MIDI clock ticks (calculated from tempo)
float midiTempo = 120.0f;                 // Current MIDI tempo in BPM
uint32_t lastMidiClockTime = 0;           // Last time we received a MIDI clock message
const uint32_t MIDI_CLOCK_TIMEOUT = 500;  // Timeout in milliseconds (0.5 seconds)


// -- CLOCK DIVISIONS (for MIDI sync mode) --
const float clockDivisions[] = { 1.0f, 2.0f, 4.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f };
const int NUM_DIVISIONS = 8;
int currentDivisionIndex = 0;  // 0 = 1/1, 1 = 1/2, etc.
float currentDivision = 1.0f;  // Current clock division multiplier


// -- SEQUENCER --
int seqCurrentStep = 0;
bool seqStepEnabled[5] = { true, true, true, true, true };  // All steps ON by default
float seqStepCV[5] = { 0.0f };                              // Stores current CV from MUX1 0-4
float seqClockSpeed = 100.0f;
int seqMaxSteps = 5;  // Default sequence length
// AUDIO INTERRUPT TIMING
uint32_t seqSampleCounter = 0;  // Counts samples to trigger the next step
float activeSeqCV = 0.0f;       // The CV of the currently active step
// SAMPLE RATE
uint32_t samplesPerTick = 48000;  // default, will be updated in updateParameters()

// SEQUENCER MODES
enum SeqTriggerMode { SEQ_TRIGGER_CLOCK,
                      SEQ_TRIGGER_PULSAR,
                      SEQ_TRIGGER_MIDI };
SeqTriggerMode currentSeqTriggerMode = SEQ_TRIGGER_CLOCK;  // Default Mode
bool midiTriggerPending = false;                           // Flag to catch MIDI notes for the sequencer
int currentMidiNote = 60;                                  // Default to Middle C when no midi is detected
uint8_t midiTriggerCounter = 0;
const uint8_t MIDI_TRIGGER_HOLDOFF = 10;  // Adjust as needed (5-20 is good)

// -- OPTIMIZATION VARIABLES --
float rawModulationValues[7] = { 0 };  // Pre-calculated modulation values
float ln2 = 0.69314718056f;            // ln(2) for fast exp approximations
uint32_t modUpdateCounter = 0;         // REMOVE THE DUPLICATE IN AudioCallback
const uint32_t MOD_UPDATE_RATE = 16;   //

// -- FAST MATH APPROXIMATIONS --
inline float fast_exp2_approx(float x) {
  // Fast 2^x approximation using Taylor series expansion
  x = 1.0f + x * ln2;  // First-order approximation
  // Second-order improvement for better accuracy
  return x * (1.0f + (x - 1.0f) * 0.5f);
}

inline float fast_pow_approx(float base, float exponent) {
  // Simple pow approximation for audio use
  // Works well for exponent in range 0-1
  return 1.0f + (base - 1.0f) * exponent;
}

inline float fast_lpg_freq(float ctrl) {
  // Clamp ctrl to [0,1]
  if (ctrl < 0.0f) ctrl = 0.0f;
  if (ctrl > 1.0f) ctrl = 1.0f;

  // Linear interpolation between table values
  float idx = ctrl * (LPG_TABLE_SIZE - 1);
  int i = (int)idx;
  if (i >= LPG_TABLE_SIZE - 1) return lpgFreqTable[LPG_TABLE_SIZE - 1];
  float frac = idx - i;
  return lpgFreqTable[i] + frac * (lpgFreqTable[i + 1] - lpgFreqTable[i]);
}

// Soft clipper for SVF input protection
inline float svf_input_protect(float x) {
  // Prevent extreme values that cause SVF instability
  if (x > 1.5f) return 1.5f;
  if (x < -1.5f) return -1.5f;
  // Soft clip near the edges to prevent harsh edges
  if (x > 1.0f) return 1.0f + (x - 1.0f) * 0.25f;
  if (x < -1.0f) return -1.0f + (x + 1.0f) * 0.25f;
  return x;
}

// NaN and bad float detection
inline bool isBadFloat(float x) {
  // Check for NaN (x != x) and infinity
  return (x != x) || (x > 1e10f) || (x < -1e10f);
}

// Get frequency ratio from MIDI note relative to C4 (note 60)
inline float getMidiTransposeRatio(int midiNote) {
  // C4 = note 60, ratio = 2^((note - 60)/12)
  float semitones = (float)(midiNote - 60);
  return powf(2.0f, semitones / 12.0f);
}

// -- WAVEFORM SELECTION (shared by the panel buttons and the web presets) --
void setModWaveform(int index) {
  modWaveformIndex = index;
  if (index == 0) modulationOsc.SetWaveform(modulationOsc.WAVE_SIN);
  else if (index == 1) modulationOsc.SetWaveform(modulationOsc.WAVE_TRI);
  else if (index == 2) modulationOsc.SetWaveform(modulationOsc.WAVE_SAW);
  else modulationOsc.SetWaveform(modulationOsc.WAVE_SQUARE);
}

// Panel order: saw* -> square -> triangle. (0.1 started on saw but its first
// press went to triangle and the second back to saw.)
void setComplexWaveform(int index) {
  morphWaveformIndex = index;
  if (index == 0) complexOscMorph.SetWaveform(complexOscMorph.WAVE_SAW);
  else if (index == 1) complexOscMorph.SetWaveform(complexOscMorph.WAVE_SQUARE);
  else complexOscMorph.SetWaveform(complexOscMorph.WAVE_TRI);
}

// SVF state reset function
void resetSvfFilters() {
  lpGateFilter1.Init(sampleRate);
  lpGateFilter2.Init(sampleRate);
  // Reset cached values to force parameter updates
  lastCtrl0 = -1.0f;
  lastCtrl1 = -1.0f;
  lastMode0 = -1;
  lastMode1 = -1;

  // Reset the cached processed signals to avoid residual DC
  // (Add these as static variables in AudioCallback if needed)
}

// -- MUX INIT --
void selectMuxChannel(int channel, int s0, int s1, int s2, int s3) {
  digitalWrite(s0, bitRead(channel, 0));
  digitalWrite(s1, bitRead(channel, 1));
  digitalWrite(s2, bitRead(channel, 2));
  digitalWrite(s3, bitRead(channel, 3));
}

// -- INIT POTS --
void initPotentiometers() {
  // Initialize MUX control pins as outputs
  pinMode(MUX1_S0, OUTPUT);
  pinMode(MUX1_S1, OUTPUT);
  pinMode(MUX1_S2, OUTPUT);
  pinMode(MUX1_S3, OUTPUT);

  pinMode(MUX2_S0, OUTPUT);
  pinMode(MUX2_S1, OUTPUT);
  pinMode(MUX2_S2, OUTPUT);
  pinMode(MUX2_S3, OUTPUT);

  // Set initial states
  digitalWrite(MUX1_S0, LOW);
  digitalWrite(MUX1_S1, LOW);
  digitalWrite(MUX1_S2, LOW);
  digitalWrite(MUX1_S3, LOW);

  digitalWrite(MUX2_S0, LOW);
  digitalWrite(MUX2_S1, LOW);
  digitalWrite(MUX2_S2, LOW);
  digitalWrite(MUX2_S3, LOW);

  // Initialize analog pins
  pinMode(MUX1_SIG, INPUT);
  pinMode(MUX2_SIG, INPUT);

  // Initialize pot names
  for (int i = 0; i < TOTAL_POTS; i++) {
    if (i < NUM_POTS_PER_MUX) {
      // Pots on MUX1
      if (i == 0) potNames[i] = "MUX1-CH0: Sequencer Step 1 CV";
      else if (i == 1) potNames[i] = "MUX1-CH1: Sequencer Step 2 CV";
      else if (i == 2) potNames[i] = "MUX1-CH2: Sequencer Step 3 CV";
      else if (i == 3) potNames[i] = "MUX1-CH3: Sequencer Step 4 CV";
      else if (i == 4) potNames[i] = "MUX1-CH4: Sequencer Step 5 CV";
      else if (i == 5) potNames[i] = "MUX1-CH5: Attack";
      else if (i == 6) potNames[i] = "MUX1-CH6: Sustain";
      else if (i == 7) potNames[i] = "MUX1-CH7: Decay/Release";
      else if (i == 8) potNames[i] = "MUX1-CH8: Pulsar Period Modulation Control";
      else if (i == 9) potNames[i] = "MUX1-CH9: Pulsar Period";
      else if (i == 10) potNames[i] = "MUX1-CH10: Modulation Oscillator Frequency Control";
      else if (i == 11) potNames[i] = "MUX1-CH11: Modulation Oscillator Frequency";
      else if (i == 12) potNames[i] = "MUX1-CH12: Modulation Oscillator Fine Tune +/-3";
      else if (i == 13) potNames[i] = "MUX1-CH13: Modulation Oscillator Modulation Control";
      else if (i == 14) potNames[i] = "MUX1-CH14: Modulation Oscillator Modulation Control";
      else if (i == 15) potNames[i] = "MUX1-CH15: Complex Oscillator Frequency Control";
    } else {
      // Pots on MUX2 (indices 16-31)
      int mux2Index = i - NUM_POTS_PER_MUX;
      if (mux2Index == 0) potNames[i] = "MUX2-CH0: Complex Oscillator Frequency";
      else if (mux2Index == 1) potNames[i] = "MUX2-CH1: Complex Oscillator Fine Tune";
      else if (mux2Index == 2) potNames[i] = "MUX2-CH2: Complex Oscillator Wavefolder Control";
      else if (mux2Index == 3) potNames[i] = "MUX2-CH3: Complex Oscillator Wavefolder";
      else if (mux2Index == 4) potNames[i] = "MUX2-CH4: Complex Oscillator Timbre";
      else if (mux2Index == 5) potNames[i] = "MUX2-CH5: LPG1 Control";
      else if (mux2Index == 6) potNames[i] = "MUX2-CH6: LPG1 Level";
      else if (mux2Index == 7) potNames[i] = "MUX2-CH7: LPG2 Control";
      else if (mux2Index == 8) potNames[i] = "MUX2-CH8: LPG2 Level";
      else if (mux2Index == 9) potNames[i] = "MUX2-CH9: Clock Speed";
      else if (mux2Index == 10) potNames[i] = "MUX2-CH10: Reverb Mix";
      else if (mux2Index == 11) potNames[i] = "MUX2-CH11: N/C";
      else if (mux2Index == 12) potNames[i] = "MUX2-CH12: N/C";
      else if (mux2Index == 13) potNames[i] = "MUX2-CH13: N/C";
      else if (mux2Index == 14) potNames[i] = "MUX2-CH14: N/C";
      else if (mux2Index == 15) potNames[i] = "MUX2-CH15: N/C";
    }
  }

  // Read initial pot values
  //readPotentiometers();
  // Copy to last values to avoid initial spurious changes
  for (int i = 0; i < TOTAL_POTS; i++) {
    lastPotValues[i] = potValues[i];
  }
}

// -- READ POTS --
void readPotentiometers() {
  for (int i = 0; i < TOTAL_POTS; i++) {
    if (i < NUM_POTS_PER_MUX) {
      selectMuxChannel(i, MUX1_S0, MUX1_S1, MUX1_S2, MUX1_S3);
      delayMicroseconds(10);  // Use the stable delay from your test
      potValues[i] = analogRead(MUX1_SIG);
    } else {
      int mux2Channel = i - NUM_POTS_PER_MUX;
      selectMuxChannel(mux2Channel, MUX2_S0, MUX2_S1, MUX2_S2, MUX2_S3);
      delayMicroseconds(10);
      potValues[i] = analogRead(MUX2_SIG);
    }
  }
}

// -- INIT BUTTON MATRICES --
// -- MATRIX 1
void initButtonMatrix() {
  // Initialize column pins as outputs
  pinMode(MATRIX_COL0, OUTPUT);
  pinMode(MATRIX_COL1, OUTPUT);
  pinMode(MATRIX_COL2, OUTPUT);
  pinMode(MATRIX_COL3, OUTPUT);

  // Initialize row pins as inputs with pulldown
  pinMode(MATRIX_ROW0, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW1, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW2, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW3, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW4, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW5, INPUT_PULLDOWN);
  pinMode(MATRIX_ROW6, INPUT_PULLDOWN);

  // Start with all columns LOW
  digitalWrite(MATRIX_COL0, LOW);
  digitalWrite(MATRIX_COL1, LOW);
  digitalWrite(MATRIX_COL2, LOW);
  digitalWrite(MATRIX_COL3, LOW);
}
// -- MATRIX 2
void initButtonMatrix2() {
  // Initialize column pins as outputs
  pinMode(MATRIX2_COL0, OUTPUT);
  pinMode(MATRIX2_COL1, OUTPUT);
  pinMode(MATRIX2_COL2, OUTPUT);
  pinMode(MATRIX2_COL3, OUTPUT);

  // Initialize row pins as inputs with pulldown
  pinMode(MATRIX2_ROW0, INPUT_PULLDOWN);
  pinMode(MATRIX2_ROW1, INPUT_PULLDOWN);
  pinMode(MATRIX2_ROW2, INPUT_PULLDOWN);
  pinMode(MATRIX2_ROW3, INPUT_PULLDOWN);
  pinMode(MATRIX2_ROW4, INPUT_PULLDOWN);

  // Start with all columns LOW
  digitalWrite(MATRIX2_COL0, LOW);
  digitalWrite(MATRIX2_COL1, LOW);
  digitalWrite(MATRIX2_COL2, LOW);
  digitalWrite(MATRIX2_COL3, LOW);
}
// -- READ BUTTON MATRICES --
// -- MATRIX 1
void readButtonMatrix() {
  for (int col = 0; col < 4; col++) {
    // Activate current column
    switch (col) {
      case 0:
        digitalWrite(MATRIX_COL0, HIGH);
        digitalWrite(MATRIX_COL1, LOW);
        digitalWrite(MATRIX_COL2, LOW);
        digitalWrite(MATRIX_COL3, LOW);
        break;
      case 1:
        digitalWrite(MATRIX_COL0, LOW);
        digitalWrite(MATRIX_COL1, HIGH);
        digitalWrite(MATRIX_COL2, LOW);
        digitalWrite(MATRIX_COL3, LOW);
        break;
      case 2:
        digitalWrite(MATRIX_COL0, LOW);
        digitalWrite(MATRIX_COL1, LOW);
        digitalWrite(MATRIX_COL2, HIGH);
        digitalWrite(MATRIX_COL3, LOW);
        break;
      case 3:
        digitalWrite(MATRIX_COL0, LOW);
        digitalWrite(MATRIX_COL1, LOW);
        digitalWrite(MATRIX_COL2, LOW);
        digitalWrite(MATRIX_COL3, HIGH);
        break;
    }

    delayMicroseconds(10);

    // Read all rows for this column
    for (int row = 0; row < 7; row++) {
      lastMatrixStates[col][row] = matrixStates[col][row];

      switch (row) {
        case 0: matrixStates[col][row] = (digitalRead(MATRIX_ROW0) == HIGH); break;
        case 1: matrixStates[col][row] = (digitalRead(MATRIX_ROW1) == HIGH); break;
        case 2: matrixStates[col][row] = (digitalRead(MATRIX_ROW2) == HIGH); break;
        case 3: matrixStates[col][row] = (digitalRead(MATRIX_ROW3) == HIGH); break;
        case 4: matrixStates[col][row] = (digitalRead(MATRIX_ROW4) == HIGH); break;
        case 5: matrixStates[col][row] = (digitalRead(MATRIX_ROW5) == HIGH); break;
        case 6: matrixStates[col][row] = (digitalRead(MATRIX_ROW6) == HIGH); break;
      }
    }
  }

  // Deactivate all columns
  digitalWrite(MATRIX_COL0, LOW);
  digitalWrite(MATRIX_COL1, LOW);
  digitalWrite(MATRIX_COL2, LOW);
  digitalWrite(MATRIX_COL3, LOW);
}
// -- MATRIX 2
void readButtonMatrix2() {
  int colPins[] = { MATRIX2_COL0, MATRIX2_COL1, MATRIX2_COL2, MATRIX2_COL3 };  // FIXED: Use COL3
  int rowPins[] = { MATRIX2_ROW0, MATRIX2_ROW1, MATRIX2_ROW2, MATRIX2_ROW3, MATRIX2_ROW4 };

  for (int col = 0; col < 4; col++) {
    // 1. ACTIVATE: Set column to OUTPUT and HIGH to source current
    pinMode(colPins[col], OUTPUT);
    digitalWrite(colPins[col], HIGH);

    delayMicroseconds(10);

    for (int row = 0; row < 5; row++) {
      matrix2PreviousStates[col][row] = matrix2CurrentStates[col][row];
      // Reading HIGH means button is pressed
      matrix2CurrentStates[col][row] = (digitalRead(rowPins[row]) == HIGH);
    }

    // 2. DEACTIVATE: Instead of digitalWrite(LOW), set to INPUT
    pinMode(colPins[col], INPUT);
  }
}

// -- UPDATE MATRIX MODULATION VALUES --
void updateMatrixModulation(float envSig, float pulsarEnvSig) {
  // Reset modulation values
  for (int i = 0; i < 7; i++) {
    rawModulationValues[i] = 0.0f;
  }

  // Calculate modulation from all sources
  for (int col = 0; col < 4; col++) {
    float src = 0.0f;

    // Get source value
    switch (col) {
      case 0: src = activeSeqCV; break;
      case 1: src = envSig; break;
      case 2: src = pulsarEnvSig; break;
      case 3: src = currentRandomValue; break;
    }

    // Apply different scaling for pulsar source
    float scaleFactor = (col == 2) ? 100.0f : 1.0f;
    float freqScaleFactor = (col == 2) ? 200.0f : 1.0f;

    // Accumulate modulation for each destination
    // Only process if matrix connection is active
    if (matrixStates[col][0]) rawModulationValues[0] += src * scaleFactor;
    if (matrixStates[col][1]) rawModulationValues[1] += src * freqScaleFactor;
    if (matrixStates[col][2]) rawModulationValues[2] += src * scaleFactor;
    if (matrixStates[col][3]) rawModulationValues[3] += src * freqScaleFactor;
    if (matrixStates[col][4]) rawModulationValues[4] += src * scaleFactor;
    if (matrixStates[col][5]) rawModulationValues[5] += src * freqScaleFactor;
    if (matrixStates[col][6]) rawModulationValues[6] += src * freqScaleFactor;
  }
}

// -- PRINT BUTTON PRESSES --
// -- SERIAL PRINT
// -- BUTTON FUNCTIONS
void printButtonChanges() {
  // Check Matrix 1 (4x7) - Modulation Matrix
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 7; row++) {
      if (matrixStates[col][row] != lastMatrixStates[col][row]) {
        // Matrix 1 buttons don't have state changes to process
        // They just connect modulation sources to destinations
        // No action needed on press/release
      }
    }
  }

  // Check Matrix 2 (4x5) - XY Matrix
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 5; row++) {
      if (matrix2CurrentStates[col][row] != matrix2PreviousStates[col][row]) {

        // --- ROW 0 BUTTONS ---

        if (col == 1 && row == 0) {
          // SEQ_LENGTH_CYCLE - Cycles between 3, 4, and 5 steps
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            if (seqMaxSteps == 5) seqMaxSteps = 4;
            else if (seqMaxSteps == 4) seqMaxSteps = 3;
            else seqMaxSteps = 5;
            seqCurrentStep %= seqMaxSteps;
          }
        }

        else if (col == 0 && row == 0) {
          // SEQUENCER_MODE_BUTTON - Cycles trigger mode: CLOCK -> PULSAR -> MIDI
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            currentSeqTriggerMode = static_cast<SeqTriggerMode>((currentSeqTriggerMode + 1) % 3);
          }
        }

        else if (col == 2 && row == 0) {
          // ENVELOPE_TRIGGER_SELECT - Cycles: SEQ -> MIDI -> PULSAR
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            currentEnvTriggerMode = static_cast<EnvTriggerMode>((currentEnvTriggerMode + 1) % 3);
          }
        }

        else if (col == 3 && row == 0) {
          // ENVELOPE_MODE - Cycles: TRANSIENT -> SUSTAIN -> OSCILLATE
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            currentEnvMode = static_cast<EnvelopeMode>((currentEnvMode + 1) % 3);
            // When switching to oscillate mode, ensure the envelope can restart
            if (currentEnvMode == ENV_MODE_OSCILLATE) {
              envRetriggerFlag = false;
            }
          }
        }

        // --- ROW 1 BUTTONS ---

        else if (col == 0 && row == 1) {
          // PULSAR_MODE_TOGGLE - Cycles: SEQ -> MIDI -> OSC (free running)
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            currentPulsarMode = static_cast<PulsarMode>((currentPulsarMode + 1) % 3);
          }
        }

        else if (col == 1 && row == 1) {
          // PULSER MANUAL TRIGGER
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            manualPulsarTrigger = true;  // Set flag to trigger on next audio callback
          }
        }

        else if (col == 2 && row == 1) {
          // MOD_WAVE_CYCLE - Cycles modulation oscillator waveform: SIN -> TRI -> SAW -> SQUARE
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            setModWaveform((modWaveformIndex + 1) % 4);
          }
        }

        else if (col == 3 && row == 1) {
          // FM/AM_TOGGLE - Switches between Frequency Modulation and Amplitude Modulation
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            useAmplitudeMod = !useAmplitudeMod;
          }
        }

        // --- ROW 2 BUTTONS ---

        else if (col == 0 && row == 2) {
          // OSC_POLARITY_INVERT - Inverts the complex oscillator output
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            complexOscInverted = !complexOscInverted;
          }
        }

        else if (col == 1 && row == 2) {
          // QUANTIZE_TO_MIDI - Enables MIDI note following (C4 reference mode)
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            quantizeToMidi = !quantizeToMidi;
            if (quantizeToMidi) {
              // When enabling, reset the "note received" flag
              midiNoteReceived = false;
            }
          }
        }

        else if (col == 2 && row == 2) {
          // COMPLEX_WAVE_CYCLE - Cycles complex oscillator morph waveform: SAW -> SQUARE -> TRIANGLE
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            setComplexWaveform((morphWaveformIndex + 1) % 3);
          }
        }

        else if (col == 3 && row == 2) {
          // LPG_MODE_TOGGLE - Cycles LPG mode: VCA -> LP -> COMBI
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            foldedLpgMode = static_cast<LpgMode>((foldedLpgMode + 1) % 3);
          }
        }

        // --- ROW 3 BUTTONS ---

        else if (col == 0 && row == 3) {
          // MODOSC_LPG_MODE - Cycles modulation oscillator LPG mode: VCA -> LP -> COMBI
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            modOscLpgMode = static_cast<LpgMode>((modOscLpgMode + 1) % 3);
          }
        }

        else if (col == 1 && row == 3) {
          // RANDOM_TRIGGER_MODE_TOGGLE - Cycles random source: SEQ -> PULSAR -> MIDI
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            currentRandomMode = static_cast<RandomMode>((currentRandomMode + 1) % 3);
          }
        }

        else if (col == 2 && row == 3) {
          // CLOCK SYNC MODE - Toggles between free-running and MIDI clock sync
          if (matrix2CurrentStates[col][row] && !matrix2PreviousStates[col][row]) {
            midiClockSync = !midiClockSync;
            if (midiClockSync) {
              // Reset sequencer position when enabling sync
              seqCurrentStep = 0;
              seqSampleCounter = 0;
              midiClockTicks = 0;
              // Initialize division from current pot position
              updateClockDivision();
            }
          }
        }

        else if (col == 3 && row == 3) {
          // SEQ_STEP_1_TOGGLE - Enables/disables sequencer step 1
          seqStepEnabled[0] = !matrix2CurrentStates[col][row];
        }

        // --- ROW 4 BUTTONS ---

        else if (col == 0 && row == 4) {
          // SEQ_STEP_2_TOGGLE - Enables/disables sequencer step 2
          seqStepEnabled[1] = !matrix2CurrentStates[col][row];
        }

        else if (col == 1 && row == 4) {
          // SEQ_STEP_3_TOGGLE - Enables/disables sequencer step 3
          seqStepEnabled[2] = !matrix2CurrentStates[col][row];
        }

        else if (col == 2 && row == 4) {
          // SEQ_STEP_4_TOGGLE - Enables/disables sequencer step 4
          seqStepEnabled[3] = !matrix2CurrentStates[col][row];
        }

        else if (col == 3 && row == 4) {
          // SEQ_STEP_5_TOGGLE - Enables/disables sequencer step 5
          seqStepEnabled[4] = !matrix2CurrentStates[col][row];
        }
      }
    }
  }
}

// -- AUDIO PROCESSING --
void AudioCallback(float** in, float** out, size_t size) {

  for (size_t i = 0; i < size; i++) {

    // --- 1. TRIGGERS & SEQUENCER (optimized) ---
    bool seqStepTriggered = false;

    if (currentSeqTriggerMode == SEQ_TRIGGER_CLOCK) {
      // Check if we're using MIDI clock sync or free-running clock
      if (midiClockSync && midiClockActive && midiClockSamplesPerTick > 0) {
        // MIDI Clock Sync Mode - step on MIDI clock ticks (only if clock is active)
        static uint32_t midiSampleCounter = 0;
        midiSampleCounter++;
        if (midiSampleCounter >= midiClockSamplesPerTick) {
          seqStepTriggered = true;
          midiSampleCounter = 0;
        }
      } else if (midiClockSync && !midiClockActive) {
        // MIDI sync mode enabled but no clock - DO NOTHING (sequencer stops)
        // seqStepTriggered remains false
      } else {
        // Free-running clock mode (original behavior)
        seqSampleCounter++;
        if (seqSampleCounter >= samplesPerTick) {
          seqStepTriggered = true;
          seqSampleCounter = 0;
        }
      }
    } else if (currentSeqTriggerMode == SEQ_TRIGGER_PULSAR) {
      bool isPulsarActive = pulsar.IsRunning();
      if (isPulsarActive && !lastPulsarEnvActive) {
        seqStepTriggered = true;
      }
      lastPulsarEnvActive = isPulsarActive;
    } else if (currentSeqTriggerMode == SEQ_TRIGGER_MIDI) {
      // MIDI trigger mode - step on each MIDI note
      if (midiTriggerPending) {
        seqStepTriggered = true;
        midiTriggerPending = false;  // Clear immediately so next note can trigger
      }
    }

    if (seqStepTriggered) {
      seqCurrentStep = (seqCurrentStep + 1) % seqMaxSteps;
      if (seqStepEnabled[seqCurrentStep]) {
        activeSeqCV = seqStepCV[seqCurrentStep];

      } else {
        gateRemainingSamples = 0;
      }
    }

    // --- 2. PULSAR TRIGGER LOGIC ---
    bool pulsarTrigger = false;

    // Check for manual trigger first (highest priority)
    if (manualPulsarTrigger) {
      pulsarTrigger = true;
      manualPulsarTrigger = false;  // Clear the flag
    }
    // Then check mode-based triggers
    else if (currentPulsarMode == PULSAR_MODE_SEQ) {
      pulsarTrigger = seqStepTriggered;
    } else if (currentPulsarMode == PULSAR_MODE_OSC) {
      if (!pulsar.IsRunning()) {
        pulsarTrigger = true;
      }
    } else if (currentPulsarMode == PULSAR_MODE_MIDI) {
      // Use separate flag that gets cleared immediately
      if (pulsarMidiTrigger) {
        pulsarTrigger = true;
        pulsarMidiTrigger = false;  // Clear immediately so it only triggers once per note
      }
    }

    if (pulsarTrigger) {
      pulsar.SetAttackTime(0.02f);
      pulsar.SetDecayTime(0.02f);
      pulsar.SetSustainLevel(0.0f);
      pulsar.SetReleaseTime(pulsarReleaseTime);
      pulsar.Retrigger(true);
    }

    // --- 3. RANDOM TRIGGER LOGIC ---
    bool randomTrigger = false;
    if (currentRandomMode == RANDOM_MODE_SEQ) {
      randomTrigger = seqStepTriggered;
    } else if (currentRandomMode == RANDOM_MODE_PULSAR) {
      randomTrigger = pulsarTrigger;
    } else if (currentRandomMode == RANDOM_MODE_MIDI) {
      // Use separate flag that gets cleared immediately
      if (randomMidiTrigger) {
        randomTrigger = true;
        randomMidiTrigger = false;  // Clear immediately so it only triggers once per note
      }
    }

    if (randomTrigger) {
      // Fast random value generation
      currentRandomValue = (float)rand() * (1.0f / RAND_MAX);
    }

    // --- 4. ENVELOPE GENERATION ---
    bool envTrigger = false;

    if (currentEnvTriggerMode == ENV_TRIGGER_SEQ) {
      if (seqStepTriggered) envTrigger = true;
    } else if (currentEnvTriggerMode == ENV_TRIGGER_MIDI) {
      if (envMidiTrigger) {
        envTrigger = true;
        envMidiTrigger = false;
      }
    } else if (currentEnvTriggerMode == ENV_TRIGGER_PULSAR) {
      if (pulsarTrigger) envTrigger = true;
    }

    bool gate = false;

    if (currentEnvMode == ENV_MODE_TRANSIENT) {
      // TRANSIENT MODE: Use transientGateSamples (pot value only, 20ms min)
      if (envTrigger) {
        gateRemainingSamples = transientGateSamples;
        env.Retrigger(true);
      }
      if (gateRemainingSamples > 0) {
        gate = true;
        gateRemainingSamples--;
      }

    } else if (currentEnvMode == ENV_MODE_SUSTAIN) {
      if (envTrigger) {
        env.Retrigger(true);
        // SUSTAIN MODE: Use sustainGateSamples (pot value + 300ms min)
        gateRemainingSamples = sustainGateSamples;
      }

      if (currentEnvTriggerMode == ENV_TRIGGER_MIDI) {
        // MIDI mode: Gate stays open as long as MIDI note is physically held
        gate = midiNoteHeld;
        // Don't use the sample counter in MIDI mode
        gateRemainingSamples = 0;
      } else {
        // SEQ or PULSAR mode: Gate lasts for sustainGateSamples duration
        if (gateRemainingSamples > 0) {
          gate = true;
          gateRemainingSamples--;
        }
      }

    } else if (currentEnvMode == ENV_MODE_OSCILLATE) {
      // OSCILLATE MODE: Self-oscillating envelope
      // Calculate total cycle length once when needed
      static uint32_t totalCycleSamples = 0;
      const float SPEED_MULTIPLIER = 0.5f;

      // Retrigger when envelope is completely finished
      if (!env.IsRunning() && oscGateSamples == 0) {
        env.Retrigger(true);
        // Include release time in the gate pulse
        // Attack + Sustain + Release
        totalCycleSamples = (uint32_t)((attackTime + sustainDuration + releaseTime) * sampleRate * SPEED_MULTIPLIER);
        oscGateSamples = totalCycleSamples;
      }

      if (oscGateSamples > 0) {
        gate = true;
        oscGateSamples--;
      } else {
        gate = false;
      }
    }

    float envSig = env.Process(gate);
    float pulsarEnvSig = pulsar.Process(pulsarTrigger);

    // --- 5. UPDATE MODULATION MATRIX (less frequently) ---
    // Use the GLOBAL modUpdateCounter, not a local static one
    if (++modUpdateCounter >= MOD_UPDATE_RATE) {
      updateMatrixModulation(envSig, pulsarEnvSig);
      modUpdateCounter = 0;
    }

    // --- 6. APPLY ATTENUATORS (using pre-calculated modulation values) ---
    float pot8Norm = potValues[8] * (1.0f / 65535.0f);
    pulsarPeriodModCoeff = rawModulationValues[0] * pot8Norm;
    modulationFreqCoeff = rawModulationValues[1] * modulationFreqModDepth;
    modulationOscModCoeff = rawModulationValues[2] * modulationOscModCoeffDepth;
    complexOscFreqCoeff = rawModulationValues[3] * complexOscFreqModDepth;
    complexOscWFCoeff = rawModulationValues[4] * complexOscWFModDepth;
    vcaComplexOsc = rawModulationValues[5] * foldedLpgModAmount;
    vcaModulationOsc = rawModulationValues[6] * modOscLpgModAmount;

    // --- 7. OSCILLATORS (optimized with fast approximations) ---

    // MODULATION OSCILLATOR
    float modFinalFreq;

    if (quantizeToMidi) {
      // QUANTIZE MODE: Only transpose if a MIDI note has been received
      if (midiNoteReceived) {
        // Use the pot frequency as C4 reference and transpose by MIDI note
        float baseFromPot = modulationOscFreq;
        float transposedFreq = baseFromPot * lastMidiNoteFreq;
        modFinalFreq = transposedFreq * fast_exp2_approx((modulationFreqCoeff * 5.0f) + modulationFine);
      } else {
        // No MIDI note received yet - use pot frequency directly (no transpose)
        modFinalFreq = modulationOscFreq * fast_exp2_approx((modulationFreqCoeff * 5.0f) + modulationFine);
      }
    } else {
      // FREE RUNNING MODE
      modFinalFreq = modulationOscFreq * fast_exp2_approx((modulationFreqCoeff * 5.0f) + modulationFine);
    }
    modulationOsc.SetFreq(modFinalFreq);
    float modOscSig = modulationOsc.Process();

    // Protect SVF from harsh waveforms (sawtooth and square waves)
    if (modWaveformIndex == 2 || modWaveformIndex == 3) {  // 2 = SAW, 3 = SQUARE
      if (modOscSig > 1.2f) modOscSig = 1.2f;
      if (modOscSig < -1.2f) modOscSig = -1.2f;
    }
    // General safety clamp - keep within SVF safe range
    if (modOscSig > 1.5f) modOscSig = 1.5f;
    if (modOscSig < -1.5f) modOscSig = -1.5f;

    float totalModDepth = modulationOscMod + (modulationOscModCoeff * 2.0f);
    if (totalModDepth < 0.0f) totalModDepth = 0.0f;
    if (totalModDepth > 2.0f) totalModDepth = 2.0f;

    float fmSignal = 0.0f;
    if (!useAmplitudeMod) {
      fmSignal = modOscSig * totalModDepth;
    }

    // COMPLEX OSCILLATOR
    float compFinalFreq;

    if (quantizeToMidi) {
      // QUANTIZE MODE: Only transpose if a MIDI note has been received
      if (midiNoteReceived) {
        // Use the pot frequency as C4 reference and transpose by MIDI note
        float baseFromPot = complexOscFreq;
        float transposedFreq = baseFromPot * lastMidiNoteFreq;
        float totalFreqMod = complexOscFreqCoeff + fmSignal;
        compFinalFreq = transposedFreq * fast_exp2_approx(totalFreqMod * 5.0f + complexOscFine);
      } else {
        // No MIDI note received yet - use pot frequency directly (no transpose)
        float totalFreqMod = complexOscFreqCoeff + fmSignal;
        compFinalFreq = complexOscFreq * fast_exp2_approx(totalFreqMod * 5.0f + complexOscFine);
      }
    } else if (complexOscMidiEnabled) {
      // ORIGINAL MIDI MODE: Direct MIDI note control
      float rawNote = 24.0f + (potValues[16] * (72.0f / 65535.0f));
      int quantizedNote = (int)(rawNote + 0.5f);
      float totalFreqMod = complexOscFreqCoeff + fmSignal;
      compFinalFreq = mtof(quantizedNote) * fast_exp2_approx(totalFreqMod * 5.0f + complexOscFine);
    } else {
      // FREE RUNNING MODE
      float totalFreqMod = complexOscFreqCoeff + fmSignal;
      compFinalFreq = complexOscFreq * fast_exp2_approx(totalFreqMod * 5.0f + complexOscFine);
    }

    complexOsc.SetFreq(compFinalFreq);
    complexOscMorph.SetFreq(compFinalFreq);

    float complexSig = complexOsc.Process();
    float morphSig = complexOscMorph.Process();

    float mixedSig = complexSig + (morphSig - complexSig) * complexOscMorphMix;

    if (complexOscInverted) {
      mixedSig = -mixedSig;
    }

    // --- 8. WAVEFOLDER & AM ---
    float wfGain = complexOscWF + (complexOscWFCoeff * 20.0f);
    if (wfGain < 1.0f) wfGain = 1.0f;
    if (wfGain > 20.0f) wfGain = 20.0f;

    waveFolder.SetGain(wfGain);
    float foldedSig = waveFolder.Process(mixedSig);

    if (useAmplitudeMod) {
      foldedSig *= (1.0f + (modOscSig * totalModDepth));
    }

    // --- 9. BUCHLA LPG MIXER (optimized) ---
    float finalMix = 0.0f;

    // Process folded signal (channel 0)
    float signal0 = foldedSig;
    float ctrl0 = complexOscSigLevel + vcaComplexOsc;
    if (ctrl0 < 0.0f) ctrl0 = 0.0f;
    if (ctrl0 > 0.90f) ctrl0 = 0.90f;  // Safer clamping for SVF

    float processedSig0 = signal0;
    if (foldedLpgMode == LPG_MODE_VCA) {
      processedSig0 *= ctrl0 * ctrl0;
    } else {
      if (ctrl0 < 0.0005f) {
        processedSig0 = 0.0f;  // Hard mute below threshold
      } else {
        int currentMode = (int)foldedLpgMode;
        if (fabsf(ctrl0 - lastCtrl0) > 0.002f || currentMode != lastMode0) {
          float filterFreq0 = fast_lpg_freq(ctrl0);
          lpGateFilter1.SetFreq(filterFreq0);

          float res0 = (foldedLpgMode == LPG_MODE_LP) ? ctrl0 * ctrl0 * 0.2f : 0.1f;
          if (res0 > 0.95f) res0 = 0.95f;
          lpGateFilter1.SetRes(res0);

          lastCtrl0 = ctrl0;
          lastMode0 = currentMode;
        }

        lpGateFilter1.Process(processedSig0);
        processedSig0 = lpGateFilter1.Low();

        // Check for SVF instability
        if (isBadFloat(processedSig0)) {
          lpGateFilter1.Init(sampleRate);
          processedSig0 = 0.0f;
          lastCtrl0 = -1.0f;
        } else {
          if (processedSig0 > 1.0f) processedSig0 = 1.0f;
          if (processedSig0 < -1.0f) processedSig0 = -1.0f;
        }

        if (foldedLpgMode == LPG_MODE_COMBI) {
          processedSig0 *= ctrl0;
        }
      }
    }
    finalMix += processedSig0;

    // Process modulation oscillator signal (channel 1)
    float signal1 = modOscSig;

    // Extra protection for sawtooth/square waves before SVF
    if (modWaveformIndex == 2 || modWaveformIndex == 3) {
      if (signal1 > 1.0f) signal1 = 1.0f;
      if (signal1 < -1.0f) signal1 = -1.0f;
    }

    float ctrl1 = modOscSigLevel + vcaModulationOsc;
    if (ctrl1 < 0.0f) ctrl1 = 0.0f;
    if (ctrl1 > 0.90f) ctrl1 = 0.90f;  // Safer clamping for SVF

    float processedSig1 = signal1;
    if (modOscLpgMode == LPG_MODE_VCA) {
      processedSig1 *= ctrl1;
    } else {
      if (ctrl1 < 0.0001f) {
        processedSig1 = 0.0f;  // Hard mute below threshold
      } else {
        int currentMode = (int)modOscLpgMode;
        if (fabsf(ctrl1 - lastCtrl1) > 0.002f || currentMode != lastMode1) {
          float filterFreq1 = fast_lpg_freq(ctrl1);
          lpGateFilter2.SetFreq(filterFreq1);

          float res1 = (modOscLpgMode == LPG_MODE_LP) ? ctrl1 * ctrl1 * 0.2f : 0.0f;
          if (res1 > 0.95f) res1 = 0.95f;
          lpGateFilter2.SetRes(res1);

          lastCtrl1 = ctrl1;
          lastMode1 = currentMode;
        }

        lpGateFilter2.Process(processedSig1);
        processedSig1 = lpGateFilter2.Low();

        // Check for SVF instability
        if (isBadFloat(processedSig1)) {
          lpGateFilter2.Init(sampleRate);
          processedSig1 = 0.0f;
          lastCtrl1 = -1.0f;
        } else {
          if (processedSig1 > 1.0f) processedSig1 = 1.0f;
          if (processedSig1 < -1.0f) processedSig1 = -1.0f;
        }

        if (modOscLpgMode == LPG_MODE_COMBI) {
          processedSig1 *= ctrl1;
        }
      }
    }
    finalMix += processedSig1;

    // --- 9.5 CLAMP FINAL MIX ---
    if (isBadFloat(finalMix)) {
      finalMix = 0.0f;
    }
    if (finalMix > 1.0f) finalMix = 1.0f;
    if (finalMix < -1.0f) finalMix = -1.0f;

    // --- 10. OUTPUT ANALOGUE FILTER ---
    finalMix = outputFilter.Process(finalMix);

    // --- 11. REVERB & OUTPUT ---
    float revL, revR;
    reverb.Process(finalMix, finalMix, &revL, &revR);

    float dryWet = 1.0f - reverbMix;
    out[0][i] = (finalMix * dryWet + revL * reverbMix) * 0.4f;
    out[1][i] = (finalMix * dryWet + revR * reverbMix) * 0.4f;
  }
}
// -- SETUP --
void setup() {
  //Serial.begin(115200);
  delay(1000);  // Wait for serial to initialize

  //Serial.println("=== DaisySeed Button & Potentiometer Monitor ===");
  //Serial.println("Displaying button state changes and potentiometer values...");
  //Serial.println();

  // -- INIT BUTTON MATRICES --
  initButtonMatrix();
  initButtonMatrix2();

  // -- INIT POTS --
  analogReadResolution(16);  // 16-BIT ADC
  initPotentiometers();

  //Serial.println("Potentiometer Configuration:");
  //Serial.println("MUX1: Channels 0-15 connected to A0");
  //Serial.println("MUX2: Channels 0-15 connected to A1");
  //Serial.println("Threshold for reporting changes: ±50 units");
  //Serial.println("==============================================");

  // -- DAISY SEED INIT AT 48kHz --
  hw = DAISY.init(DAISY_SEED, AUDIO_SR_48K);
  sampleRate = DAISY.get_samplerate();
  int block_size = 64;  // Get the actual block size
  DAISY.SetAudioBlockSize(block_size);

  // -- LPG LUT
  for (int i = 0; i < LPG_TABLE_SIZE; i++) {
    float ctrl = (float)i / (float)(LPG_TABLE_SIZE - 1);
    float freq = 20.0f * powf(2.0f, ctrl * 9.81f);
    if (freq < 30.0f) freq = 30.0f;
    if (freq > 18000.0f) freq = 18000.0f;
    lpgFreqTable[i] = freq;
  }

  // -- START AUDIO
  DAISY.begin(AudioCallback);

  // -- DAISY DUINO INIT --
  // -- COMPLEX OSC // SINE + MORPH --
  complexOsc.Init(sampleRate);
  complexOsc.SetWaveform(complexOsc.WAVE_SIN);
  complexOsc.SetFreq(440.0f);
  complexOsc.SetAmp(1.0f);
  complexOscMorph.Init(sampleRate);
  complexOscMorph.SetWaveform(complexOsc.WAVE_SAW);
  complexOscMorph.SetFreq(440.0f);
  complexOscMorph.SetAmp(1.0f);
  // -- WAVEFOLDER --
  waveFolder.Init();
  // -- MODULATION OSC --
  modulationOsc.Init(sampleRate);
  modulationOsc.SetWaveform(modulationOsc.WAVE_SIN);
  modulationOsc.SetFreq(440.0f);
  modulationOsc.SetAmp(1.0f);
  // -- LPG FILTER --
  lpGateFilter1.Init(sampleRate);
  lpGateFilter2.Init(sampleRate);
  // -- ENVELOPE --
  env.Init(sampleRate);
  gateRemainingSamples = 0;
  // -- PULSAR ENV --
  pulsar.Init(sampleRate);
  pulsar.SetAttackTime(0.02f);
  pulsar.SetDecayTime(0.02f);
  pulsar.SetSustainLevel(1.0f);
  // -- ANALOGUE OUTPUT FILTER --
  outputFilter.Init(sampleRate);
  outputFilter.SetFreq(8000.0f);
  outputFilter.SetRes(0.2f);
  // -- REVERB --
  reverb.Init(sampleRate);
  reverb.SetFeedback(0.85f);
  reverb.SetLpFreq(17000.0f);

  // -- MIDI INIT --
  // Configure Serial1 to use pin D14 for RX
  Serial1.setRx(MIDI_RX_PIN);
  Serial1.begin(31250);  // Standard MIDI baud rate
  Serial1.flush();       // Clear any pending data

  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleClock(handleMidiClock);
  MIDI.setHandleStart(handleMidiStart);
  MIDI.setHandleStop(handleMidiStop);

  MIDI.begin(MIDI_CHANNEL_OMNI);  // channel filtering is done in the handlers (editor setting)
  MIDI.turnThruOff();
  currentMidiNote = 60;
  midiTriggerPending = false;

  // -- WEB EDITOR: MIDI channel (QSPI), panel modes, restart into DFU --
  editor.onBootloader = []() { hw.ResetToBootloader(); };
  editor.readPanel = readPanelModes;
  editor.writePanel = writePanelModes;
  editor.begin();

  //Serial.println("=============================================");
  //Serial.println("MIDI initialized with pin 30 (Digital pin 30)");
  //Serial.println("=============================================");
}

// -- LOOP --
void loop() {

  // MIDI PROCESSING
  while (MIDI.read()) {
    // MIDI callbacks handle the events
  }

  checkMidiClockActivity();  // ADD THIS LINE

  // WEB EDITOR: report MIDI state, answer requests from the USB port
  editor.live.noteHeld = midiNoteHeld;
  editor.live.clockReceiving = midiClockActive;
  editor.live.clockSync = midiClockSync;
  editor.live.bpm = midiClockActive ? midiTempo : 0.0f;
  editor.poll();


  // READ BUTTON MATRICES
  if (millis() - lastMatrixRead > MATRIX_READ_INTERVAL) {
    readButtonMatrix();
    readButtonMatrix2();
    printButtonChanges();  //BUTTON FNCs
    lastMatrixRead = millis();
  }

  // READ POTENTIOMETERS AND UPDATE PARAMETERS
  if (millis() - lastPotRead > POT_READ_INTERVAL) {
    readPotentiometers();
    updateParameters();  // UPDATE PARAMS
    lastPotRead = millis();
  }

  // CPU & DEBUG PRINTING (Runs every 0.5 seconds)
  /*
  if (millis() - lastCpuPrint >= 500) {
    lastCpuPrint = millis();

    // CPU LOAD REPORTING
    float avgLoad = cpuMeter.GetAvgCpuLoad();
    float maxLoad = cpuMeter.GetMaxCpuLoad();

    Serial.print("CPU Load: ");
    Serial.print(avgLoad * 100.0f);
    Serial.print("% | Max: ");
    Serial.print(maxLoad * 100.0f);
    Serial.println("%");
  }*/
}

// -- UPDATE PARAMETERS --
void updateParameters() {
  // -- OSCILLATORS --

  // FINE TUNE
  // 3 cents is 3/1200 of an octave.
  float fineRange = 3.0f / 12.0f;

  // -- COMPLEX OSCILLATOR --
  // PITCH
  complexOscFreq = fmap(potValues[16] / 65535.0f, 25.0f, 1760.0f, Mapping::EXP);

  // Pot 15: Complex Osc Frequency Modulation Amount
  complexOscFreqModDepth = potValues[15] / 65535.0f;

  // Pot 17: Fine Tune Complex Oscillator (+/- 3 cents)
  complexOscFine = ((potValues[17] / 65535.0f) * 2.0f - 1.0f) * fineRange;

  // TIMBRE/MORPH CONTROL
  complexOscMorphMix = potValues[20] / 65535.0f;

  // WAVEFOLDER (0.0 to 1.0f)
  float complexOscWFPot = potValues[19] / 65535.0f;
  complexOscWF = 1.0f + (19.0f * complexOscWFPot);

  // Pot 18: Wavefolder Modulation Amount
  complexOscWFModDepth = potValues[18] / 65535.0f;

  // LPG
  // Pot 21: Amount of modulation for Folded Signal LPG
  foldedLpgModAmount = fmap(potValues[21] / 65535.0f, 0.0f, 0.98f, Mapping::LINEAR);

  // Pot 22: Channel 1 Sig Level
  complexOscSigLevel = fmap(potValues[22] / 65535.0f, 0.0f, 0.98f, Mapping::EXP);

  // -- MODULATION OSCILLATOR --
  // PITCH
  modulationOscFreq = fmap(potValues[11] / 65535.0f, 0.1f, 1000.0f, Mapping::EXP);

  // Pot 10: Amount of modulation applied to Mod Osc Frequency
  modulationFreqModDepth = potValues[10] / 65535.0f;

  // Pot 12: Fine Tune Modulation Oscillator (+/- 3 cents)
  modulationFine = ((potValues[12] / 65535.0f) * 2.0f - 1.0f) * fineRange;

  // MODULATION (FM/AM)
  // Pot 14: Modulation Depth (FM or AM depth)
  modulationOscMod = potValues[14] / 65535.0f;

  // Pot 13: Amount of modulation applied to Mod Osc Modulation Depth (FM/AM amount)
  modulationOscModCoeffDepth = potValues[13] / 65535.0f;

  // LPG
  // Pot 23: Amount of modulation for Mod Osc Signal LPG
  modOscLpgModAmount = fmap(potValues[23] / 65535.0f, 0.0f, 0.98f, Mapping::LINEAR);

  // Pot 24: Channel 2 Sig Level
  modOscSigLevel = fmap(potValues[24] / 65535.0f, 0.0f, 0.98f, Mapping::EXP);

  // -- ENVELOPE [Buchla ASD config] --
  float minTime = 0.002f;
  float maxTime = 5.0f;

  // Attack Time (Logarithmic)
  attackTime = fmap(potValues[5] / 65535.0f, minTime, maxTime, Mapping::EXP);
  env.SetAttackTime(attackTime);

  // Sustain Duration (Logarithmic)
  sustainDuration = fmap(potValues[6] / 65535.0f, 0.02f, maxTime, Mapping::EXP);

  // TRANSIENT mode: gate pulse length = sustain pot value (minimum 20ms)
  transientGateSamples = (uint32_t)(sustainDuration * sampleRate);
  uint32_t minTransientSamples = (uint32_t)(0.02f * sampleRate);  // 20ms minimum
  if (transientGateSamples < minTransientSamples) transientGateSamples = minTransientSamples;

  // SUSTAIN mode: gate length = sustain pot value + 300ms minimum
  uint32_t minSustainGate = (uint32_t)(0.1f * sampleRate);  // 300ms minimum
  sustainGateSamples = (uint32_t)(sustainDuration * sampleRate);
  if (sustainGateSamples < minSustainGate) sustainGateSamples = minSustainGate;

    // Release Time (Logarithmic)
  // When in oscillate mode, reduce the range to 50%
  float releasePotNorm = potValues[7] / 65535.0f;
  
  if (currentEnvMode == ENV_MODE_OSCILLATE) {
    // OSCILLATE MODE: 50% of normal range
    // Map pot (0-1) to range (minTime to maxTime/2)
    float oscillateMaxTime = maxTime * 0.2f;
    releaseTime = fmap(releasePotNorm, minTime, oscillateMaxTime, Mapping::EXP);
  } else {
    // Normal mode: full range
    releaseTime = fmap(releasePotNorm, minTime, maxTime, Mapping::EXP);
  }
  
  env.SetReleaseTime(releaseTime);
  env.SetDecayTime(0.0f);
  env.SetSustainLevel(1.0f);

  // -- PULSAR --
  // Pot 9: Base Pulsar Release Time
  float pulsarReleasePot = potValues[9] / 65535.0f;

  // Logarithmic mapping: 20ms to 5000ms (0.02s to 5.0s)
  float pulsarMinTime = 0.02f;  // 20ms - very short decay
  float pulsarMaxTime = 5.0f;   // 5000ms - long decay
  float pulsarTimeRatio = pulsarMaxTime / pulsarMinTime;

  // Base release time from pot (logarithmic)
  float basePulsarRelease = pulsarMinTime * powf(pulsarTimeRatio, pulsarReleasePot);

  // Subtract modulation (clamped) to ensure time decreases
  float adjustedPulsarTime = basePulsarRelease - (2.0f * pulsarPeriodModCoeff);  // Scale modulation appropriately
  if (adjustedPulsarTime < pulsarMinTime) adjustedPulsarTime = pulsarMinTime;
  if (adjustedPulsarTime > pulsarMaxTime) adjustedPulsarTime = pulsarMaxTime;

  pulsarReleaseTime = adjustedPulsarTime;
  pulsar.SetReleaseTime(pulsarReleaseTime);

  // -- SEQUENCER --
  // Update Sequencer CV steps from MUX1 Channels 0-4
  for (int i = 0; i < 5; i++) {
    // Store normalized 0.0 to 1.0 values
    seqStepCV[i] = fmap(potValues[i] / 65535.0f, 0.0f, 1.0f, Mapping::EXP);
  }
  // -- CLOCK --
  if (midiClockSync) {
    // When in MIDI sync mode, the pot controls clock division
    updateClockDivision();
    // Don't update seqClockSpeed or samplesPerTick in this mode
    // The sequencer timing is handled by MIDI clock
  } else {
    // Free-running clock mode - original behavior
    float clockPot = potValues[25] / 65535.0f;
    seqClockSpeed = 0.1f * powf(40000.0f, (1.0f - clockPot));

    // CLOCK CLAMPING
    if (seqClockSpeed < 0.1f) seqClockSpeed = 0.1f;
    if (seqClockSpeed > 4000.0f) seqClockSpeed = 4000.0f;

    samplesPerTick = (uint32_t)((seqClockSpeed / 1000.0f) * DAISY.get_samplerate());
    if (samplesPerTick < 1) samplesPerTick = 1;
  }


  // -- REVERB --
  reverbMix = potValues[26] / 65535.0f;  // REVERB MIX CONTROL
}

// -- MIDI NOTE HANDLING
void handleNoteOn(byte channel, byte note, byte velocity) {
  if (!editor.acceptsChannel(channel)) return;  // MIDI receive channel (web editor)
  if (velocity > 0) {
    editor.noteIn(channel, note, velocity);
    currentMidiNote = note;
    midiTriggerPending = true;
    midiTriggerCounter = MIDI_TRIGGER_HOLDOFF;
    lastMidiNoteFreq = getMidiTransposeRatio(note);
    midiNoteReceived = true;
    midiNoteHeld = true;
    pulsarMidiTrigger = true;
    randomMidiTrigger = true;
    envMidiTrigger = true;
    //envTrigger = true;
  }
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  if (!editor.acceptsChannel(channel)) return;
  editor.activity();
  midiNoteHeld = false;
}

// -- MIDI CLOCK HANDLING --
void handleMidiClock() {
  // Called on each MIDI clock tick (24 ticks per quarter note)
  uint32_t now = millis();

  // Mark that MIDI clock is active
  midiClockActive = true;

  // Calculate tempo from tick interval (only when sync is enabled)
  if (midiClockSync) {
    if (lastMidiClockTime > 0) {
      uint32_t tickInterval = now - lastMidiClockTime;
      // 24 ticks per quarter note, so quarter note interval = tickInterval * 24
      float quarterNoteInterval = tickInterval * 24.0f;
      if (quarterNoteInterval > 0) {
        midiTempo = 60000.0f / quarterNoteInterval;
        // Clamp tempo to reasonable range
        if (midiTempo < 40.0f) midiTempo = 40.0f;
        if (midiTempo > 240.0f) midiTempo = 240.0f;

        // Calculate samples per MIDI clock tick based on current division
        // One tick = 1/24 of a quarter note, then multiply by division
        float secondsPerTick = 60.0f / (midiTempo * 24.0f);
        // Apply division: larger division = longer interval between steps
        midiClockSamplesPerTick = (uint32_t)(secondsPerTick * sampleRate * currentDivision);
        if (midiClockSamplesPerTick < 1) midiClockSamplesPerTick = 1;
      }
    }
  }
  lastMidiClockTime = now;
  midiClockTicks++;
}

// -- CHECK MIDI CLOCK ACTIVITY --
void checkMidiClockActivity() {
  // If MIDI clock sync is enabled, check if we've received a clock message recently
  if (midiClockSync) {
    uint32_t now = millis();
    if (now - lastMidiClockTime > MIDI_CLOCK_TIMEOUT) {
      midiClockActive = false;
      midiClockSamplesPerTick = 0;  // Invalidate the samples per tick
    }
  }
}

// -- UPDATE CLOCK DIVISION FROM POT --
void updateClockDivision() {
  // Map pot value (0-65535) to division index (0 to NUM_DIVISIONS-1)
  // REVERSED: 0% = fastest (1/32), 100% = slowest (1/1)
  float potNorm = potValues[25] / 65535.0f;
  // Reverse the pot direction: 1.0 - potNorm
  currentDivisionIndex = (int)((1.0f - potNorm) * NUM_DIVISIONS);
  if (currentDivisionIndex >= NUM_DIVISIONS) currentDivisionIndex = NUM_DIVISIONS - 1;
  if (currentDivisionIndex < 0) currentDivisionIndex = 0;

  currentDivision = clockDivisions[currentDivisionIndex];
}

// -- MIDI START HANDLING --
void handleMidiStart() {
  // Reset sequencer to step 0 when MIDI start received
  seqCurrentStep = 0;
  seqSampleCounter = 0;
  midiClockTicks = 0;
}

// -- MIDI STOP HANDLING --
void handleMidiStop() {
  // Stop the sequencer
  seqSampleCounter = 0;
  midiClockTicks = 0;
}

// -- PANEL MODES FOR THE WEB PRESETS PAGE --
// Byte order and meaning: weasel::PanelField in WeaselEditor.h (PANEL_FIELDS in
// protocol.js). Every value is the position in the order the button steps through.
// Not included: the virtual patch bay, and the sequencer step on/off switches
// (latching switches, so the panel always shows their real position).
void readPanelModes(uint8_t *out) {
  out[weasel::P_SEQ_TRIGGER] = (uint8_t)currentSeqTriggerMode;
  out[weasel::P_SEQ_STAGES] = (uint8_t)(5 - seqMaxSteps);
  out[weasel::P_ENV_TRIGGER] = (uint8_t)currentEnvTriggerMode;
  out[weasel::P_ENV_MODE] = (uint8_t)currentEnvMode;
  out[weasel::P_PULSAR_TRIGGER] = (uint8_t)currentPulsarMode;
  out[weasel::P_RANDOM_TRIGGER] = (uint8_t)currentRandomMode;
  out[weasel::P_MOD_WAVE] = (uint8_t)modWaveformIndex;
  out[weasel::P_MOD_TYPE] = useAmplitudeMod ? 1 : 0;
  out[weasel::P_LPG2_MODE] = (uint8_t)modOscLpgMode;
  out[weasel::P_COMPLEX_WAVE] = (uint8_t)morphWaveformIndex;
  out[weasel::P_POLARITY] = complexOscInverted ? 1 : 0;
  out[weasel::P_LPG1_MODE] = (uint8_t)foldedLpgMode;
  out[weasel::P_MIDI_FOLLOW] = quantizeToMidi ? 1 : 0;
  out[weasel::P_CLOCK_MODE] = midiClockSync ? 1 : 0;
}

// Same effect as pressing the buttons until each mode is reached.
void writePanelModes(const uint8_t *in) {
  currentSeqTriggerMode = static_cast<SeqTriggerMode>(in[weasel::P_SEQ_TRIGGER]);

  int steps = 5 - in[weasel::P_SEQ_STAGES];
  if (steps != seqMaxSteps) {
    seqMaxSteps = steps;
    seqCurrentStep %= seqMaxSteps;
  }

  currentEnvTriggerMode = static_cast<EnvTriggerMode>(in[weasel::P_ENV_TRIGGER]);
  EnvelopeMode envMode = static_cast<EnvelopeMode>(in[weasel::P_ENV_MODE]);
  if (envMode != currentEnvMode) {
    currentEnvMode = envMode;
    if (currentEnvMode == ENV_MODE_OSCILLATE) envRetriggerFlag = false;
  }

  currentPulsarMode = static_cast<PulsarMode>(in[weasel::P_PULSAR_TRIGGER]);
  currentRandomMode = static_cast<RandomMode>(in[weasel::P_RANDOM_TRIGGER]);

  if (in[weasel::P_MOD_WAVE] != modWaveformIndex) setModWaveform(in[weasel::P_MOD_WAVE]);
  useAmplitudeMod = in[weasel::P_MOD_TYPE] != 0;
  modOscLpgMode = static_cast<LpgMode>(in[weasel::P_LPG2_MODE]);

  if (in[weasel::P_COMPLEX_WAVE] != morphWaveformIndex) setComplexWaveform(in[weasel::P_COMPLEX_WAVE]);
  complexOscInverted = in[weasel::P_POLARITY] != 0;
  foldedLpgMode = static_cast<LpgMode>(in[weasel::P_LPG1_MODE]);

  bool follow = in[weasel::P_MIDI_FOLLOW] != 0;
  if (follow && !quantizeToMidi) midiNoteReceived = false;
  quantizeToMidi = follow;

  bool sync = in[weasel::P_CLOCK_MODE] != 0;
  if (sync && !midiClockSync) {
    seqCurrentStep = 0;
    seqSampleCounter = 0;
    midiClockTicks = 0;
    midiClockSync = true;
    updateClockDivision();
  } else {
    midiClockSync = sync;
  }
}
