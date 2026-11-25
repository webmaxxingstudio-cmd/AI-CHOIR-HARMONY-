# DSP components

All engine code lives in `core/` and is real-time safe: memory is allocated in `prepare()`,
`process()` never allocates or locks (a test counts heap allocations to prove it).

## Signal flow

```
input (mono sum) -> VocalAnalyzer -> marks, pitch, stable note, key
                         |-> Voice 1..16: harmony target -> humanise -> PSOLA -> tone tilt -> pan -> choir bus
                         '-> lead, delayed by the engine latency -> lead bus
```

## Pitch detection: `YinPitchDetector`

YIN (de Cheveigné & Kawahara 2002). The difference function is computed with an FFT
cross-correlation plus energy prefix sums, so each analysis costs three FFTs. Range 80-1100 Hz.
Parabolic interpolation gives sub-sample lag accuracy. Accuracy in tests: under 3 cents on sines
from 82 Hz to 1 kHz at 44.1-96 kHz, no octave errors on harmonic-rich vowels, silence and noise
reported as unvoiced. A median of three estimates removes isolated octave jumps.

## Analysis marks: `VocalAnalyzer`

Runs YIN every 128 samples and places one mark per glottal period while voiced (aligned to the
waveform peak, then tracked period by period) and a fixed 5 ms grid while unvoiced. A mark is only
placed once one maximum period of signal after it exists, so any mark can be the centre of a grain.

`NoteTracker` turns the pitch curve into note decisions: a new note must sit more than 0.7 semitones
away for 45 ms, so a wide 60-cent vibrato never flips the note, but a real note change does.

## Key detection: `KeyDetector`

A duration-and-confidence weighted pitch-class histogram, correlated (Pearson) with the
Krumhansl-Kessler major and minor key profiles for all 24 keys. A minor key whose raised 7th is
sung more than its flat 7th is reported as harmonic minor. In the live analyser the histogram
fades with a 45 s time constant, a first key needs 3 s of singing, and a key change must win
for 2 s in a row. Until a key is known, harmonies use fixed (parallel) intervals.

## Pitch shifting: `PsolaVoice` (TD-PSOLA)

Each grain is a two-period Hann-windowed slice centred on a mark. Grains are re-spaced at the new
period: the pitch changes but each pulse keeps its shape, so the formants (vowel, voice identity)
are preserved. Formants are moved on purpose by reading each grain faster or slower (factor
0.75-1.5, about -5 to +7 semitones).

- Unison: when the ratio is 1 the synthesis grid locks onto the analysis grid, so a unison voice
  is a sample-exact delayed copy (61 dB SNR in tests). No comb filtering against the lead.
- PSOLA gain: overlap-adding one pulse per period raises power with the pulse rate, so grains are
  scaled by 1/sqrt(ratio). Level stays within 2 dB from -12 to +12 semitones.
- Unvoiced grains are not pitch shifted and are spaced to keep a constant overlap, so noise has no
  ripple. Backing voices play consonants 6 dB down to avoid smeared sibilance.
- Latency = longest grain reach + 1.5 x longest period + one hop: about 38 ms.

Measured in tests: pitch error under 5 cents from -12 to +12 semitones; the first-formant centroid
after shifting matches the same vowel synthesised at the target pitch (plain resampling would move it
by the full interval).

## Harmony: `diatonicTranspose`, `ChoirEngine`

Harmony parts are diatonic steps (+2 = third above, -3 = fourth below, -7 = octave down) inside the
key, so the interval quality follows the scale (D->F minor third, E->G major third in C major).
Out-of-scale notes move in parallel with the scale note below them.

Per grain, each singer aims at: `harmony note + (lead pitch - lead note) x (1 - correction) + humanise cents`.
At 0 % correction the harmony copies every scoop and vibrato of the lead; at 100 % it sings the exact note.
Each singer's formant follows 15 % of its interval from the lead (lower parts sound a little larger).

## Humanisation

Per singer, deterministic seeds: static detune and slow pitch drift (cosine-interpolated random,
0.6-1.6 s segments), its own vibrato (4.8-6.2 Hz, depth set by Vibrato variation), a static plus
slowly drifting late timing offset up to the Timing spread (changing which grains are read, never
resampling, so no Doppler), formant offset, tone tilt and level drift (+-1.5 dB). Singers are
placed on even stereo slots, alternating sides, so consecutive parts are far apart. Two humanised
unison doubles panned apart measure 0.07 L/R correlation (identical copies would be 1.0).

The choir bus is scaled by 1/sqrt(singers) so loudness doesn't jump with choir size.
