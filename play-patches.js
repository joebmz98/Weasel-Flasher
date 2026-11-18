// Built-in patches for the WEASEL web demo. A patch is the whole panel:
// pots (by firmware pot index), panel modes, step switches and patch cables
// ([source, destination], sources 0-3, destinations 0-6, as in matrixStates).
const WEASEL_PATCHES = (() => {
  const MODES = { seqTrigger: 0, seqStages: 0, envTrigger: 0, envMode: 0, pulsarTrigger: 0, randomTrigger: 0,
    modWave: 0, modType: 0, lpg2Mode: 2, complexWave: 0, polarity: 0, lpg1Mode: 2, midiFollow: 0, clockMode: 0 };
  const POTS = {
    0: 0.5, 1: 0.5, 2: 0.5, 3: 0.5, 4: 0.5,
    5: 0.1, 6: 0.3, 7: 0.4, 8: 0, 9: 0.3,
    10: 0, 11: 0.3, 12: 0.5, 13: 0, 14: 0,
    15: 0, 16: 0.35, 17: 0.5, 18: 0, 19: 0, 20: 0,
    21: 0, 22: 0.6, 23: 0, 24: 0, 25: 0.25, 26: 0.2,
  };
  const mk = (name, pots, modes, cables, steps = [true, true, true, true, true]) =>
    ({ name, builtin: true, pots: { ...POTS, ...pots }, modes: { ...MODES, ...modes }, cables, steps });
  return {
    MODES, POTS,
    list: [
      mk('init', {}, {}, []),
      mk('Bongo sequence',
        { 0: 0.25, 1: 0.55, 2: 0.4, 3: 0.75, 4: 0.15, 5: 0.02, 6: 0.04, 7: 0.33, 15: 0.3, 16: 0.3, 19: 0.12, 20: 0.35,
          21: 0.95, 22: 0.12, 25: 0.24, 26: 0.25 },
        { lpg1Mode: 1 }, [[0, 3], [1, 5]]),
      mk('Pulsar plucks',
        { 0: 0.6, 1: 0.2, 2: 0.8, 3: 0.4, 4: 0.3, 9: 0.38, 10: 0.12, 11: 0.4, 14: 0.25, 15: 0.22, 16: 0.28,
          21: 0.75, 22: 0, 23: 0.35, 24: 0, 25: 0.21, 26: 0.3 },
        { modWave: 1 }, [[2, 5], [2, 6], [3, 1], [0, 3]], [true, true, false, true, true]),
      mk('Drone cycler',
        { 5: 0.55, 6: 0.35, 7: 0.7, 9: 0.6, 11: 0.12, 14: 0.35, 15: 0.08, 16: 0.2, 18: 0.45, 19: 0.18, 20: 0.6,
          21: 0.55, 22: 0.3, 26: 0.6 },
        { envMode: 2, pulsarTrigger: 2, randomTrigger: 1, modType: 1, complexWave: 2 }, [[1, 4], [1, 5], [3, 3]]),
      mk('Keys from MIDI',
        { 5: 0.05, 6: 0.3, 7: 0.42, 16: 0.369, 18: 0.35, 19: 0.1, 20: 0.5, 21: 0.85, 22: 0.06, 26: 0.3 },
        { seqTrigger: 2, envTrigger: 2, envMode: 1, pulsarTrigger: 1, randomTrigger: 2, midiFollow: 1 }, [[1, 5], [1, 4]]),
    ],
  };
})();
if (typeof module !== 'undefined') module.exports = WEASEL_PATCHES;
