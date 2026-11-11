# TB Digital Multi Echo

A VST 2.4 plugin model of the H/H Digital Multi-Echo, built from its circuit
diagram (C/AC20031, issue 4, 28-2-79, in `Docs/`).

- 64-bit Intel, **Mac OS X 10.8 and later**, made for Pro Tools 11 through Blue Cat PatchWork
- Built and tested on GitHub Actions; the build fails unless the binary declares
  10.8 as its minimum and links only libraries that ship with 10.8
- Use it as an insert, or as a send/return effect with **Dry** out
- Personal, non-commercial use

## Controls

| # | Name | Control | Range |
|---|------|---------|-------|
| 1 | Volume 1 | Channel 1 volume (left input) | 0 – 10 |
| 2 | Volume 2 | Channel 2 volume (right input) | 0 – 10 |
| 3 | Reverb | Spring reverb level (the drawing's 1K Reverb pot) | 0 – 10 |
| 4 | Echo Vol | Echo level; 0 is ECHO OFF | 0 – 10 |
| 5 | Rept Vol | Repeat level (feedback): runs away at 10 | 0 – 10 |
| 6–7 | Bass, Treble | Tone of the echo (IC8a Baxandall) | 5 – 0 – 5 |
| 8–9 | Echo In1/In2 | The Echo switch beside each input: sends that channel to the echo and reverb | On / Off |
| 10–13 | Echo 1–4 | Which taps you hear, any combination | In / Out |
| 14–17 | Repeat A–D | Which taps feed back, any combination | In / Out |
| 18 | EchoRate | *plugin only*: the delay board's clock, shown as the longest tap (Echo 4) | 600 – 150 ms |
| 19 | Echo Sw | *plugin only*: footswitch, echo | On / Off |
| 20 | Rev Sw | *plugin only*: footswitch, reverb | On / Off |
| 21 | Dry | *plugin only*: dry signal; Out for send/return use | In / Out |
| 22 | Output | *plugin only*: output trim | ±24 dB |

The left input is Channel 1 and the right input is Channel 2. The unit has one
output, so both output sides carry the same signal. Volume 2 starts at 0, so a
mono track goes through Channel 1 only; turn Volume 2 up to mix in the right
side of a stereo send.

The input stages are fixed in the Line position (the drawing's Mic/Line
switches aren't needed in a plugin). The dry signal is at unity with
Volume 1 at its default (6).

## What's modelled

From the drawing:
- IC1a/b input amps in the Line position (100K || 10K over 6K8, 150p) and the 4K7
  Volume pots, their wipers loaded by 8K2 and the 15K into IC2
- IC2's pre-emphasis (47K-47K T network, 4K7 + 4n7): about +15 dB of treble lift
  before the converter
- the "36 dB" anti-alias filter: two unity-gain 3rd-order sections (12K, 2n2,
  6n8 || 1n, 150p), flat to 10 kHz
- the overload comparator (15K / 4K7 from +15 V) driving the Overload lamp
- ECHO buttons into IC6 (39K into 82K); REPEAT buttons into IC5 (39K onto 100K,
  gain 1 + 47K/22K), the 18 dB repeat filter, and back into the delay board
  through R26 alongside the input through R25
- the reconstruction filter: two 3rd-order sections with 27K (about 5.5 kHz, 36 dB/oct)
- IC7d de-emphasis **as drawn** (47K in, 47K || (4K7 + 150p)): it hardly undoes
  the pre-emphasis, so the echo is about 6 dB brighter at 3 kHz than at 100 Hz before
  the reconstruction filter rolls it off. If the real unit is darker, the 150p is the
  first value to check (`kCde` in `Source/DSP/MultiEcho.cpp`)
- the signal programmed output switch: the echo opens when you play and mutes
  (JFET, about -30 dB) when you stop
- the IC8a tone circuit and the 22K Echo Volume pot into R94
- the spring reverb: 470p / 270K into the IC9 driver (1 + 100K / (2K2 + 47n)),
  the tank (the same two-spring model as HH VS Musician), the IC8c recovery stage
  (1 + 100K / (4K7 + 0.1µ)) and the Reverb pot into R96
- op-amp rails (±15 V supply) and slew rate

Not on the drawing, so estimated (all named constants in `Source/DSP/MultiEcho.cpp`):
- **the delay board AS00069** (a sealed box on the sheet): an 8K-word memory,
  a 10-bit linear converter with 4 V full scale, and four equally spaced taps.
  The Echo Rate clock gives 75 / 150 / 225 / 300 ms at its centre and
  150 – 600 ms for Echo 4 over its range (`kTap4Slow`, `kTap4Fast`). At slow
  rates the clock drops below twice the anti-alias filter's bandwidth, so the
  echo picks up the aliasing grit of an early digital delay
- **the repeat curve** (the drawing doesn't give P7b's setting): about 2 repeats
  at 4, 5 at 6, 20 at 8, runaway at 10 (`kRepeatLoop`)
- **the output mixer weights** (dry, echo through R94, reverb through R96) and the
  reverb drive level (`kMixDry`, `kMixEcho`, `kMixRev`)
- the gate's threshold and timing (`kGateOpen`, `kGateFloor`)
- the tone circuit is modelled as two shelves, about ±18 dB at 250 Hz and 2.5 kHz

## The panel

On the Mac the plugin opens its own panel (1200 × 440), laid out like the
unit: the knob row (Volume, Volume, Reverb, Echo Volume, Repeat Volume, Bass,
Treble) with gold caps and dark grey knurled skirts, and the lower rail with the input
jacks and their Echo switches, the ECHO 1–4 and REPEAT A–D push buttons (ECHO glows bright red when in,
dark red when out; REPEAT glows light grey when in, black when out), the output jacks and the Overload lamp. The
strip along the bottom has the TB electronic logo, Echo Rate, the footswitch
rockers, Dry, Output, the presets and the readout.

Drag knobs up/down (hold Shift for fine control), double-click a knob to reset
it, or use the scroll wheel. Click a button or slide switch to toggle it.
Rocker switches: press the top half for On/In, the bottom half for Off/Out.

**Presets:** click SAVE, then a slot 1–8. Click a saved slot to recall it; the slot in use lights orange.
Presets are shared by every instance, in
`~/Library/Application Support/TB/TB Digital Multi Echo/Presets.txt`.

## Install

1. GitHub → Actions → latest run → Artifacts → `TB-Digital-Multi-Echo-mac-vst`
2. Unzip twice to get `TB Digital Multi Echo.vst`
3. Copy it to `/Library/Audio/Plug-Ins/VST/` and rescan in PatchWork

## Build and test

- `make -C Tests run` — preset store and DSP tests, demo WAVs in `Tests/renders`
- `make -C Tests vst-run` — loads the plugin in a mini VST host
- `scripts/build_vst_mac.sh` — the Mac bundle (CI runs this, plus the panel test,
  which publishes screenshots to the `gui-preview` branch)
