// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls in the Multi-Echo's left-to-right order (the knob row,
// then the Echo switch beside each input, the ECHO 1-4 and REPEAT A-D
// buttons), then the plugin-only controls in the editor's bottom strip.

#ifndef TBME_PARAMS_H
#define TBME_PARAMS_H

namespace tbme
{

enum Param
{
    // Front panel: knobs
    kVolume1 = 0, kVolume2, kReverb, kEchoVol, kRepeatVol, kBass, kTreble,
    // Front panel: switches and buttons
    kChEcho1, kChEcho2,
    kEcho1, kEcho2, kEcho3, kEcho4,
    kRepeatA, kRepeatB, kRepeatC, kRepeatD,
    // Plugin only (bottom strip of the editor)
    kEchoRate, kEchoSw, kReverbSw, kDry, kOutput,
    kNumParams
};

enum Kind { kKnob, kCutBoost, kSwitch, kButton, kRateMs, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Volume 1", "Volume 2", "Reverb", "Echo Vol", "Rept Vol", "Bass", "Treble",
    "Echo In1", "Echo In2",
    "Echo 1", "Echo 2", "Echo 3", "Echo 4",
    "Repeat A", "Repeat B", "Repeat C", "Repeat D",
    "EchoRate", "Echo Sw", "Rev Sw", "Dry", "Output"
};

const int kParamKind[kNumParams] = {
    kKnob, kKnob, kKnob, kKnob, kKnob, kCutBoost, kCutBoost,
    kSwitch, kSwitch,
    kButton, kButton, kButton, kButton,
    kButton, kButton, kButton, kButton,
    kRateMs, kSwitch, kSwitch, kSwitch, kDb24
};

const float kDefaults[kNumParams] = {
    0.6f, 0.0f, 0.3f, 0.5f, 0.4f, 0.5f, 0.5f,
    1.0f, 1.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.5f, 1.0f, 1.0f, 1.0f, 0.5f
};

const double kOutputRangeDb = 24.0;

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbme

#endif
