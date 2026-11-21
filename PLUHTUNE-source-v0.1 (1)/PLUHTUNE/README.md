# PLUHTUNE  v0.1 (work in progress)

Original real-time vocal pitch-correction plugin. C++17 + JUCE 8, VST3 (Windows target).
All DSP (pitch detector, correction logic, pitch-synchronous overlap-add shifter) is original
code in `Source/DSP/` and has no JUCE dependency, so it is unit-tested stand-alone.

## Honest status

| Area | Status |
|---|---|
| Pitch detector, correction logic, PSOLA shifter, formant preserve/follow/shift | Implemented and **tested** (`tests/dsp_test.cpp`, 10 groups, clean under ASan/UBSan) |
| JUCE plugin layer: parameters, processor, MIDI, presets, UI, state | Written, **never compiled or run** (no JUCE / Windows / network in the dev sandbox) |
| VST3 build | **Not built yet** |
| FL Studio testing, automation, host crash/perf testing | **Not done** |
| MIDI routing to an effect plugin inside FL Studio | **Unverified** (may need Patcher) |

Nothing in the "not done" rows should be considered finished until you build it and run the checklist below.

## Build (Windows 10/11)
Requirements: Visual Studio 2022 (Desktop C++ workload), CMake >= 3.22, Git, internet (CMake fetches JUCE 8.0.4).

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target PLUHTUNE_VST3

Output: `build\PLUHTUNE_artefacts\Release\VST3\PLUHTUNE.vst3`

(`--target PLUHTUNE_Standalone` builds a standalone app, useful for a first smoke test.)

## Install in FL Studio
1. Copy `PLUHTUNE.vst3` to `C:\Program Files\Common Files\VST3\`.
2. FL Studio: Options > Manage plugins > Find installed plugins (verify "VST3" scan is on).
3. Mixer: load PLUHTUNE on an insert carrying the vocal. Mono or stereo.
4. Keep FL's plugin delay compensation on. Reported latency is ~19 / 28 / 36 ms (High / Standard / Low voice range at 48 kHz) and is shown in the footer.
   Changing Voice Range changes latency; some hosts only read latency at load, so pick the range, then reload/restart playback.

## DSP unit tests (no JUCE needed)
    g++ -O2 -std=c++17 -ISource/DSP tests/dsp_test.cpp -o dsp_test && ./dsp_test
(or with MSVC: `cl /O2 /std:c++17 /EHsc /ISource\DSP tests\dsp_test.cpp`)

## Test checklist to run once it builds
- Loads/scans in FL Studio; opens/closes UI repeatedly; no crash on project save/load.
- Sung/spoken vocal at 44.1/48/96 kHz and buffer sizes 64..2048: no clicks, level unity at Mix 100%.
- Automate Retune, Amount, Key, Scale, Bypass from FL; check smooth, zipper-free.
- Bypass: audible level/time matched (should be latency-aligned).
- Presets: all 8 factory presets load; Save then reload a user preset (stored in `%APPDATA%\PLUHTUNE\Presets`).
- Note switches: clicking in Major/Minor converts to Custom with the current scale as the starting point.
- MIDI mode: needs MIDI delivered to the plugin; verify in FL (Patcher) or another host.
- CPU: stereo vocal should be a few % of one core (measured ~4% in the DSP test, unoptimised build).

## Algorithm notes
- Detector: cumulative-mean-normalised difference on a ~24 kHz boxcar-decimated copy, earliest-near-global-minimum
  selection (avoids formant-induced octave errors), then full-rate refinement; ~1 estimate / 2.7 ms.
- Correction: nearest allowed note (hysteresis in Natural mode) -> glide (Transition) -> one-pole retune on the
  correction offset (so vibrato survives slow retune) -> Humanize passes slow-reference-relative pitch movement.
- Shifter: pitch-mark (waveform-similarity snapped) PSOLA, 2-period Hann grains, fractional grain placement,
  voiced/unvoiced crossfade to latency-aligned dry. Formant Follow/Shift resample the grain read-out.
- Whole engine runs sample-by-sample: output is bit-identical for any host block size. No allocation in process().

## Known limitations (v0.1)
- Monophonic only (one voice); detection runs on the L+R mono sum.
- Latency 19-36 ms depending on range (inherent to look-ahead PSOLA); not a zero-latency tuner.
- Range change causes a brief glitch. MIDI is read at block resolution.
- No pitch-edit/graph mode, no tuning to reference MIDI lines/notes outside the held-note pitch classes.
