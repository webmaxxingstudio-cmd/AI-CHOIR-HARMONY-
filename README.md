# Vocal Choir Studio

A JUCE/C++ audio plugin (VST3, AU on macOS, Standalone) that turns one sung vocal into a
humanised, harmonised choir. Work in progress: see the phase status below.

## Status

| Phase | State |
|---|---|
| 1. Architecture, JUCE project, design system | Done |
| 2. Core engine: pitch detection, harmony, multi-voice, humanisation | Done (tested) |
| 3. Choir engine: per-voice controls, groups, draggable stage | Next |
| 4. Vocal production chain (EQ, comp, de-ess, reverb, delay, saturation, Auto Mix) | Not started |
| 5. Mastering (LUFS, true peak, limiter) | Not started |
| 6. Full interface, preset library (60+), harmony editor | First screen only |
| 7-8. Optimisation, FL Studio testing, delivery | Not started |

What works today, in the plugin:

- Pitch tracking of the lead (YIN), stable-note tracking with vibrato hysteresis.
- Automatic key and scale detection (major, natural and harmonic minor), or a manual key with 9 scales.
- Diatonic harmony arrangements (2-part up/down, triad, 4-, 5-, 8-part, octaves, fifths) for 1-16 singers.
- MIDI harmony mode: each part sings a note held on the MIDI input.
- Formant-preserving pitch shifting (TD-PSOLA) with formant control.
- Per-singer humanisation: timing, pitch drift, independent vibrato, formant, tone, level; stereo spread.
- Lead/choir/output levels, A/B comparison, undo/redo, full state recall, host automation of all 17 parameters.
- Live displays: lead pitch over the key grid, choir notes, stage view, meters, CPU and latency.

Latency is about 38 ms (reported to the host, so FL Studio compensates it automatically).

## Build

Requirements: CMake 3.22+, a C++20 compiler (Visual Studio 2022 on Windows, Xcode 15+ on macOS).
JUCE 8.0.9 is downloaded automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
```

The VST3 ends up in `build/plugin/VocalChoirStudio_artefacts/Release/VST3/`. On Windows copy
`Vocal Choir Studio.vst3` into `C:\Program Files\Common Files\VST3\`, then rescan plugins in FL Studio.

Pushing to GitHub runs `.github/workflows/build.yml`, which builds Windows, macOS and Linux,
runs the tests, and uploads the plugins as downloadable artifacts.

## Layout

- `core/` the DSP engine, plain C++ with no JUCE dependency. See `docs/DSP.md`.
- `tests/` unit tests for the engine (run with `ctest`).
- `plugin/` the JUCE plugin: parameters, processor, editor, design system.
- `tools/` host-side checks: `vcs_host_check` loads the built VST3 like a DAW and exercises it;
  `vcs_snapshot` renders the editor to PNG with live engine data.

## Licensing notes

JUCE is dual-licensed (AGPLv3 or commercial). Selling a closed-source build requires a JUCE
commercial licence. No third-party audio samples or ML models are included.
