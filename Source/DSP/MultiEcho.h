// MultiEcho.h
// H/H Digital Multi-Echo (circuit diagram C/AC20031, issue 4, 28-2-79),
// modelled from the drawing:
//
//   two input channels: IC1a/b, 100K||150p over 6K8 + 6u8, fixed in the Line
//     position (10K across the feedback), 4K7 Volume pots (8K2 to ground)
//   -> IC2 summing amp with pre-emphasis (15K in, 47K-47K T network with
//      4K7 + 4n7 to ground): the channels' Echo switches feed this
//   -> 36 dB low-pass: two 3rd-order unity-gain sections (12K, 2n2, 6n8||1n, 150p)
//   -> delay board AS00069 (a sealed module on the drawing: modelled as a clocked
//      sample-and-hold, a linear ADC, a memory with four taps and a DAC)
//        taps -> ECHO buttons 1-4 -> 39K -> IC6 echo mix (82K)
//        taps -> REPEAT buttons A-D -> 39K -> IC5 repeat mix (100K to ground,
//                47K over 22K) -> 18 dB repeat filter (12K section)
//                -> Repeat Volume (22K negative log) -> repeat amp -> board input
//   -> 36 dB reconstruction filter (two 27K sections) -> de-emphasis (IC7d:
//      47K in, 47K || (4K7 + 150p) feedback, as drawn)
//   -> signal programmed output switch (IC7a-c detector, JFET Q1 mute)
//   -> tone circuit (IC8a Baxandall, Bass and Treble 10K) -> Echo Volume (22K log)
//   -> output mixer (BC413) with the dry channels and the spring reverb
//      (IC9 driver from IC2 via 470p, tank, IC8c recovery, 1K Reverb pot)
//
// Not on the drawing, so estimated: the delay board's clock range, memory size,
// converter resolution and full scale; the repeat amp's gain; the output mixer's
// weights. All are named constants in MultiEcho.cpp.
//
// Volts in, volts out (1.0 = 1 V), scaled so the dry signal is unity with
// Volume 1 at its default. Runs at 4x with Oversampler4x. Plain C++98, no
// allocation after construction.

#ifndef TBME_MULTIECHO_H
#define TBME_MULTIECHO_H

#include "Filters.h"
#include "Oversampler4x.h"
#include "SpringTank.h"

namespace tbme
{

class MultiEcho
{
public:
    MultiEcho();

    void prepare (double sampleRate);
    void reset();

    // Front panel (0..1 pot rotation; switches as bool)
    void setVolume (int channel, double v);     // 0, 1
    void setReverb (double v);
    void setEchoVolume (double v);
    void setRepeatVolume (double v);
    void setBass (double v);                    // 0.5 = flat
    void setTreble (double v);
    void setChannelEcho (int channel, bool on); // the Echo switch beside each input
    void setEchoTap (int index, bool on);       // ECHO 1-4
    void setRepeatTap (int index, bool on);     // REPEAT A-D
    // Plugin only
    void setEchoRate (double v);                // 0..1, slow .. fast clock
    void setEchoOn (bool on);                   // footswitch: echo
    void setReverbOn (bool on);                 // footswitch: reverb
    void setDryOn (bool on);                    // off for send/return use
    void setOutputGainDb (double dB);

    // in1 = Channel 1 input, in2 = Channel 2 input (may be the same buffer)
    void process (const float* in1, const float* in2, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Introspection for the panel and tests
    static double tapDelayMs (int tap, double rate);   // tap 0..3
    double currentTapDelayMs (int tap) const;
    static double clockHz (double rate);
    static double audioTaper (double x);
    static double negLogTaper (double x);
    static double repeatAmpGain (double pot);
    double repeatLoopGain (int numTaps) const;         // small-signal, midband
    double overload() const { return overloadHold; }   // 0..1, the Overload LED
    double peakAdc() const { return peakAdcLevel; }
    void   clearPeakAdc() { peakAdcLevel = 0.0; }
    double gateGain() const { return gateG; }

    enum { kMemWords = 8192, kTaps = 4 };

private:
    void updateTone (bool force);
    double tickOs (double x1, double x2);
    double channelGain (double pot) const;
    double echoPot (double pot) const;
    double reverbPot (double pot) const;

    double fsBase, fsOs;
    Oversampler4x up1, up2, down;
    SpringTank tank;

    // Targets and smoothed values
    double volT[2], reverbT, echoVolT, repeatT, bassT, trebleT, chEchoT[2], echoTapT[4], repTapT[4];
    double rateT, echoOnT, reverbOnT, dryT, outT;
    double vol[2], reverb, echoVol, repeat, bass, treble, chEcho[2], echoTap[4], repTap[4];
    double rate, echoOn, reverbOn, dry, outG;
    double smKnob, smRate, smSwitch;

    // Derived per base sample
    double kCh[2], kEcho, kRepeat, kReverb, outCal;
    double appliedBass, appliedTreble;
    int toneCountdown;

    // Circuit filters
    FirstOrder inHp1, inHp2, inLp1, inLp2;
    FirstOrder preEmph;                   // IC2 T network
    ThirdOrder aa1, aa2, rec1, rec2, repFilt;
    FirstOrder deEmph, toneBass, toneTreble, outHp, revHp, revDrv, revRec, revLp, repHp;
    Slew slewIc2, slewRep;

    // Delay board
    float mem[kMemWords];
    int memPos;
    double clkPhase, adcHeld, dacOut[kTaps];
    double lastRepeat;

    // Signal programmed output switch
    double gateEnv, gateG;

    // Reverb runs at the base rate on the oversampled drive's last value
    double revDriveSum, revOut;

    double overloadHold, peakAdcLevel;
};

} // namespace tbme

#endif
