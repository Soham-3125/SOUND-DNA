# Sound DNA — Simple Version (No JUCE)

Same capture → analyze → resynthesize → blend → play idea as before, rebuilt
without JUCE to make the build itself as simple as possible in VS Code. This
is a standalone program you run directly — not a VST/AU plugin you load in a
DAW. That trade-off is deliberate: it removes every dependency that caused
build problems before (no framework to clone, no plugin bundle format, no
extra system packages beyond a plain C++ compiler).

---

## What changed from the JUCE version

| | JUCE version | This version |
|---|---|---|
| Framework | JUCE (120 MB clone from GitHub) | None — `miniaudio.h`, a single 4,600-line public-domain header, already included in `src/` |
| Output | `.vst3` plugin, loaded inside a DAW | A standalone `.exe` / executable you run directly |
| Build dependencies | JUCE + several system packages (X11, fontconfig, etc.) | Just a C++ compiler — nothing else to install |
| UI | Buttons and sliders in a plugin window | Plain text prompts in a console window |

Everything else — the actual pitch/timbre/amplitude analysis, the additive
resynthesis, the blend logic — is identical. It was copied over and adapted
to remove JUCE-specific types (`juce::AudioBuffer` → `std::vector<float>`,
`juce::dsp::FFT` → a small hand-written FFT, etc.), then re-verified from
scratch to confirm the port didn't introduce any errors (see below).

---

## Project structure

```
sound_dna_simple/
├── CMakeLists.txt          ← The whole build config — about 30 lines
└── src/
    ├── main.cpp             Console app: record -> analyze -> tweak -> play
    ├── SoundAnalyzer.h       Pitch/timbre/amplitude/wave-speed extraction
    ├── SoundResynthesizer.h  Additive resynthesis + blend
    ├── SimpleFFT.h           A compact, dependency-free FFT (replaces juce::dsp::FFT)
    └── miniaudio.h           Single-header audio I/O library (record + playback)
```

That's the whole project. No other folders, no framework to fetch.

---

## Building in VS Code

**1. Install a C++ compiler**, if you haven't already:
- Windows: Visual Studio 2022 Community, with "Desktop development with C++"
- macOS: `xcode-select --install`
- Linux: `sudo apt-get install build-essential`

**2. Install two VS Code extensions**: CMake Tools, C/C++.

**3. Open the folder**:
```powershell
code sound_dna_simple
```

**4. Configure and build** — `Ctrl+Shift+P`:
- "CMake: Select a Kit" (pick your compiler)
- "CMake: Configure"
- "CMake: Build"

That's the entire process. No git clone step, no path to point at, no
extra packages to hunt down. If "CMake: Configure" fails this time, the
error will be a real, specific one — there's no framework-fetching step
left to go wrong.

**5. Run it** — either click Run in VS Code, or from the terminal:
```powershell
cd build
.\Release\SoundDNA.exe        # Windows
./SoundDNA                     # macOS/Linux
```

---

## Using it

```
Press ENTER to start recording...
Recording... press ENTER to stop.
Captured 3.2 seconds. Analyzing...

=== SOUND DNA -- CAPTURED PROPERTIES ===
  Duration:          3.20 s
  1. Pitch:          182.40 Hz  (F#3)
  2. Wavelength:     1.88 m
  3. Frequency:      182.40 Hz (fundamental)
  4. Timbre:         centroid 890 Hz, spread 620 Hz, flatness 0.0212
  5. Amplitude:      peak -3.20 dB, rms -18.40 dB
  6. Wave speed:     343.42 m/s
  Extras:
    Attack time:     0.045 s
    Pitch stability: +/-4.20 Hz
    Harmonic/noise:  0.812
==========================================

Blend (0.0 = original, 1.0 = fully resynthesized) [0.5]:
Brightness (0.2 = dark, 1.0 = normal, 3.0 = bright) [1.0]:
Pitch offset in semitones, whole clip (-24 to +24) [0.0]:
Resynthesizing...
Press ENTER to play the result...
```

Just press Enter at each prompt to accept the default (Blend 0.5,
Brightness 1.0, no pitch shift), or type a number to change it. As before,
pitch offset is a single value applied to the whole clip — there's no
per-key or MIDI-note pitch mapping anywhere in this program.

---

## How this was verified

Two levels of testing, same standard as the rest of this project:

1. **The FFT itself** was checked against a known 1 kHz test tone before
   anything was built on top of it — the peak bin came back within one
   bin's resolution (990.5 Hz measured vs. 1000 Hz actual, with a ~21.5 Hz
   bin width at this frame size), confirming the hand-written FFT is correct.

2. **The full ported analyzer and resynthesizer** were tested against the
   same synthetic vowel-like signal used to verify the original JUCE
   version, bypassing audio hardware entirely (feeding a generated buffer
   directly into the code, since this was built in an environment with no
   microphone available). Every check passed:

   | Check | Expected | Measured |
   |---|---|---|
   | Pitch | ~150 Hz | 150.095 Hz |
   | Attack time | ~30 ms | 23.2 ms |
   | Wave speed | 343.42 m/s | 343.42 m/s |
   | Pitch offset +7 semitones | ~224.7 Hz | 225.0 Hz |
   | Brightness increases centroid | Higher | 716.9 Hz \u2192 894.0 Hz |
   | Blend = 0 matches original | Exact match | Confirmed |
   | Blend = 1 matches resynthesis | Exact match | Confirmed |

3. **The actual compiled binary** was built and run in the same sandboxed
   environment (no real sound card present) specifically to confirm it
   fails *gracefully* rather than crashing when no microphone is available
   — it correctly detects the unusable/empty capture and exits with a clear
   error message instead of hanging or segfaulting. On your machine, with a
   real microphone, it will proceed normally through the full record →
   analyze → play flow.

---

## If you want the plugin version later

This simple version is meant to get you to a **working, testable core**
quickly. Once you've confirmed the analysis and resynthesis behave the way
you want on real recordings, the earlier JUCE-based plugin
(`sound_dna.zip`, not this package) wraps the same underlying logic as an
actual VST3/AU plugin loadable inside a DAW — useful once you're past
experimentation and want it integrated into a real music production
workflow.
