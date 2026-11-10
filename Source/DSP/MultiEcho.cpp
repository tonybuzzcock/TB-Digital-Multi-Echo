// MultiEcho.cpp
// See MultiEcho.h for the signal path. Component values are from drawing
// C/AC20031; the estimates (things the drawing doesn't show) are marked.

#include "MultiEcho.h"

#include <cmath>
#include <cstring>

namespace tbme
{

namespace
{
    // Op-amps on +/-15 V (power supply AS00032)
    const double kRail     = 13.5;
    const double kSlewRate = 0.5e6;            // V/s (741-class)

    // Input channels, Line position: 100K || 10K = 9K09 over 6K8 (+ 6u8)
    const double kRf1 = 100.0e3 * 10.0e3 / 110.0e3, kRg1 = 6.8e3, kCg1 = 6.8e-6, kCf1 = 150.0e-12;
    const double kLineGain = 1.0 + kRf1 / kRg1;
    // Volume: 4K7 pot, wiper loaded by 8K2 to ground and the 15K into IC2
    const double kPotVol = 4.7e3, kRwip = 8.2e3, kRsum = 15.0e3;
    // IC2: 15K in, 47K + 47K feedback, 4K7 + 4n7 from their junction to ground
    const double kRa = 47.0e3, kRb = 47.0e3, kRs = 4.7e3, kCs = 4.7e-9;
    // 36 dB filters: 3rd-order sections, 2n2 / 6n8 || 1n / 150p
    const double kC1 = 2.2e-9, kCf = 6.8e-9 + 1.0e-9, kC3 = 150.0e-12;
    const double kRaa = 12.0e3, kRrec = 27.0e3, kRrep = 12.0e3;
    // Delay board inputs: R25 (filtered input) and R26 (repeat), 10K each
    const double kBoardMix = 0.5;
    // Echo mix IC6: taps through 39K, 82K feedback
    const double kEchoMixGain = 82.0e3 / 39.0e3;
    // Repeat mix IC5: taps through 39K onto 100K to ground, gain 1 + 47K/22K
    const double kRtap = 39.0e3, kR69 = 100.0e3, kRepMixGain = 1.0 + 47.0e3 / 22.0e3;
    // De-emphasis IC7d: 47K in, 47K || (4K7 + 150p) feedback (as drawn)
    const double kRde = 47.0e3, kRde2 = 4.7e3, kCde = 150.0e-12;
    // Echo Volume 22K log pot, into the mixer through R94 15K
    const double kPotEcho = 22.0e3, kR94 = 15.0e3;
    // Reverb: 470p / 270K into IC9 (1 + 100K / (2K2 + 47n)); IC8c recovery
    // 1 + 100K / (4K7 + 0.1u), 150p across; 1K Reverb pot into R96 5K6
    const double kC60 = 470.0e-12, kR85 = 270.0e3;
    const double kR87 = 100.0e3, kR86 = 2.2e3, kC52 = 0.047e-6;
    const double kR91 = 100.0e3, kR90 = 4.7e3, kC54 = 0.1e-6, kC55 = 150.0e-12;
    const double kPotRev = 1.0e3, kR96 = 5.6e3;
    // Overload comparator threshold: 15K / 4K7 from +15 V
    const double kOverloadV = 15.0 * 4.7 / (15.0 + 4.7);

    // --- Estimates (not on the drawing) ---------------------------------------
    // Delay board AS00069: clock, memory, converters
    const double kTap4Slow  = 0.600;           // s, Echo Rate fully slow
    const double kTap4Fast  = 0.150;           // s, Echo Rate fully fast
    const double kAdcFull   = 4.0;             // V, converter full scale
    const double kAdcLevels = 511.0;           // 10-bit linear
    // Repeat Volume (22K neg-log P6 into the IC4a repeat amp, whose gain P7b
    // also sets): the loop gain through one REPEAT tap against rotation. The
    // drawing doesn't give P7b's setting, so the curve is set like a classic
    // echo: a couple of repeats low down, long repeats near 9, runaway at 10.
    const double kRepeatLoop[11] = { 0.0, 0.06, 0.13, 0.22, 0.33, 0.46, 0.58, 0.70, 0.82, 0.95, 1.08 };
    // Output mixer (BC413): relative weights of dry, echo (R94) and reverb (R96)
    // (the reverb weight also stands for the tank's drive level, which the
    // drawing doesn't fix: set so Reverb at 3 sits well under the dry signal)
    const double kMixDry = 1.0, kMixEcho = 10.0e3 / 15.0e3, kMixRev = 0.15;
    // Signal programmed output switch
    const double kGateOpen  = 0.010;           // V at the detector
    const double kGateFloor = 0.03;            // JFET mute depth (-30 dB)
    // Tone circuit: Baxandall, about +/-18 dB, centred near 250 Hz and 2.5 kHz
    const double kToneRangeDb = 18.0, kBassHz = 250.0, kTrebleHz = 2500.0;

    const double kDefaultVol = 0.6;            // dry is unity here (matches Params.h)

    inline double parallel (double a, double b) { return (a <= 0.0 || b <= 0.0) ? 0.0 : a * b / (a + b); }
}

double MultiEcho::audioTaper (double x)
{
    const double k = 4.394449154672439;        // 10% of the track at mid-rotation
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
}

double MultiEcho::negLogTaper (double x)
{
    return 1.0 - audioTaper (1.0 - x);         // "NEG LOG": fast at the start
}

double MultiEcho::clockHz (double r)
{
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    const double t4 = kTap4Slow * std::pow (kTap4Fast / kTap4Slow, r);
    return kMemWords / t4;
}

double MultiEcho::tapDelayMs (int tap, double r)
{
    if (tap < 0 || tap >= kTaps) return 0.0;
    return 1000.0 * (tap + 1) * (kMemWords / kTaps) / clockHz (r);
}

double MultiEcho::currentTapDelayMs (int tap) const { return tapDelayMs (tap, rate); }

MultiEcho::MultiEcho()
    : fsBase (44100.0), fsOs (176400.0),
      reverbT (0.3), echoVolT (0.5), repeatT (0.4), bassT (0.5), trebleT (0.5),
      rateT (0.5), echoOnT (1.0), reverbOnT (1.0), dryT (1.0), outT (1.0),
      kEcho (0.0), kRepeat (0.0), kReverb (0.0), outCal (1.0),
      appliedBass (-1.0), appliedTreble (-1.0), toneCountdown (0),
      memPos (0), clkPhase (0.0), adcHeld (0.0), lastRepeat (0.0),
      gateEnv (0.0), gateG (kGateFloor), revDriveSum (0.0), revOut (0.0),
      overloadHold (0.0), peakAdcLevel (0.0)
{
    volT[0] = kDefaultVol; volT[1] = 0.0;
    chEchoT[0] = chEchoT[1] = 1.0;
    for (int i = 0; i < kTaps; ++i) { echoTapT[i] = (i == 2) ? 1.0 : 0.0; repTapT[i] = (i == 2) ? 1.0 : 0.0; }
    prepare (44100.0);
}

//==============================================================================
double MultiEcho::channelGain (double pot) const
{
    // Line amp, then the Volume pot with its wiper loaded by 8K2 || 15K
    const double rBot = kPotVol * audioTaper (pot), rTop = kPotVol - rBot;
    const double rp = parallel (rBot, parallel (kRwip, kRsum));
    return kLineGain * rp / (rTop + rp + 1.0e-9);
}

double MultiEcho::echoPot (double pot) const
{
    const double rBot = kPotEcho * audioTaper (pot), rTop = kPotEcho - rBot;
    const double rp = parallel (rBot, kR94);
    return rp / (rTop + rp + 1.0e-9);
}

double MultiEcho::reverbPot (double pot) const
{
    // Linear 1K, wiper into R96 5K6
    const double rBot = kPotRev * pot, rTop = kPotRev - rBot;
    const double rp = parallel (rBot, kR96);
    return rp / (rTop + rp + 1.0e-9);
}

double MultiEcho::repeatAmpGain (double pot)
{
    // Gain from the repeat filter to the board input that gives kRepeatLoop
    // through one tap: loop = board mix x bus share x IC5 x amp
    if (pot <= 0.0) return 0.0;
    double loop;
    if (pot >= 1.0) loop = kRepeatLoop[10];
    else
    {
        const double x = pot * 10.0;
        const int i = (int) x;
        loop = kRepeatLoop[i] + (kRepeatLoop[i + 1] - kRepeatLoop[i]) * (x - i);
    }
    const double share = (1.0 / kRtap) / (1.0 / kRtap + 1.0 / kR69);
    return loop / (kBoardMix * share * kRepMixGain);
}

double MultiEcho::repeatLoopGain (int numTaps) const
{
    if (numTaps < 1) return 0.0;
    const double n = (double) numTaps;
    const double bus = (n / kRtap) / (n / kRtap + 1.0 / kR69) / n;   // per tap
    return kBoardMix * n * bus * kRepMixGain * kRepeat;
}

//==============================================================================
void MultiEcho::prepare (double sampleRate)
{
    fsBase = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOs = fsBase * Oversampler4x::kFactor;

    smKnob   = std::exp (-1.0 / (0.020 * fsBase));
    smRate   = std::exp (-1.0 / (0.120 * fsBase));
    smSwitch = std::exp (-1.0 / (0.006 * fsBase));

    // Input channels: 6K8 + 6u8 sets the low corner, 150p the high one
    inHp1.highpass (1.0 / (2.0 * kPi * kRg1 * kCg1), fsOs);
    inHp2.highpass (1.0 / (2.0 * kPi * kRg1 * kCg1), fsOs);
    inLp1.lowpass (1.0 / (2.0 * kPi * kRf1 * kCf1), fsOs);
    inLp2.lowpass (1.0 / (2.0 * kPi * kRf1 * kCf1), fsOs);

    // IC2: Zt / 15K with Zt = Ra + Rb + Ra Rb / (Rs + 1/sC)
    preEmph.setAnalog (kCs * (kRa + kRb) * kRs / kRsum + kCs * kRa * kRb / kRsum, (kRa + kRb) / kRsum,
                       kRs * kCs, 1.0, fsOs);

    aa1.setSection (kRaa, kC1, kCf, kC3, fsOs);
    aa2.setSection (kRaa, kC1, kCf, kC3, fsOs);
    rec1.setSection (kRrec, kC1, kCf, kC3, fsOs);
    rec2.setSection (kRrec, kC1, kCf, kC3, fsOs);
    repFilt.setSection (kRrep, kC1, kCf, kC3, fsOs);
    repHp.highpass (1.0 / (2.0 * kPi * 22.0e3 * 6.8e-6), fsOs);    // C24 into the 22K pot

    // IC7d: (1 + s 4K7 C) / (1 + s (47K + 4K7) C)
    deEmph.setAnalog (kRde2 * kCde, 1.0, (kRde + kRde2) * kCde, 1.0, fsOs);
    outHp.highpass (5.0, fsOs);

    // Reverb chain (base rate)
    revHp.highpass (1.0 / (2.0 * kPi * kC60 * kR85), fsBase);
    revDrv.setAnalog (kC52 * (kR86 + kR87), 1.0, kC52 * kR86, 1.0, fsBase);
    revRec.setAnalog (kC54 * (kR90 + kR91), 1.0, kC54 * kR90, 1.0, fsBase);
    {
        const double f = 1.0 / (2.0 * kPi * kR91 * kC55);
        revLp.lowpass (f < 0.45 * fsBase ? f : 0.45 * fsBase, fsBase);
    }
    tank.prepare (fsBase);

    slewIc2.step = slewRep.step = kSlewRate / fsOs;
    outCal = 1.0 / (channelGain (kDefaultVol) * kMixDry);

    reset();
}

void MultiEcho::reset()
{
    for (int c = 0; c < 2; ++c) { vol[c] = volT[c]; chEcho[c] = chEchoT[c]; }
    reverb = reverbT; echoVol = echoVolT; repeat = repeatT; bass = bassT; treble = trebleT;
    for (int i = 0; i < kTaps; ++i) { echoTap[i] = echoTapT[i]; repTap[i] = repTapT[i]; }
    rate = rateT; echoOn = echoOnT; reverbOn = reverbOnT; dry = dryT; outG = outT;

    up1.reset(); up2.reset(); down.reset();
    tank.reset();
    inHp1.reset(); inHp2.reset(); inLp1.reset(); inLp2.reset();
    preEmph.reset(); aa1.reset(); aa2.reset(); rec1.reset(); rec2.reset(); repFilt.reset(); repHp.reset();
    deEmph.reset(); toneBass.reset(); toneTreble.reset(); outHp.reset();
    revHp.reset(); revDrv.reset(); revRec.reset(); revLp.reset();
    slewIc2.reset(); slewRep.reset();

    std::memset (mem, 0, sizeof (mem));
    memPos = 0;
    clkPhase = 0.0; adcHeld = 0.0;
    for (int i = 0; i < kTaps; ++i) dacOut[i] = 0.0;
    lastRepeat = 0.0;
    gateEnv = 0.0; gateG = kGateFloor;
    revDriveSum = 0.0; revOut = 0.0;
    overloadHold = 0.0; peakAdcLevel = 0.0;

    for (int c = 0; c < 2; ++c) kCh[c] = channelGain (vol[c]);
    kEcho = echoPot (echoVol);
    kRepeat = repeatAmpGain (repeat);
    kReverb = reverbPot (reverb);
    updateTone (true);
    toneCountdown = 0;
}

void MultiEcho::setVolume (int c, double v)        { if (c >= 0 && c < 2) volT[c] = v; }
void MultiEcho::setReverb (double v)               { reverbT = v; }
void MultiEcho::setEchoVolume (double v)           { echoVolT = v; }
void MultiEcho::setRepeatVolume (double v)         { repeatT = v; }
void MultiEcho::setBass (double v)                 { bassT = v; }
void MultiEcho::setTreble (double v)               { trebleT = v; }
void MultiEcho::setChannelEcho (int c, bool on)    { if (c >= 0 && c < 2) chEchoT[c] = on ? 1.0 : 0.0; }
void MultiEcho::setEchoTap (int i, bool on)        { if (i >= 0 && i < kTaps) echoTapT[i] = on ? 1.0 : 0.0; }
void MultiEcho::setRepeatTap (int i, bool on)      { if (i >= 0 && i < kTaps) repTapT[i] = on ? 1.0 : 0.0; }
void MultiEcho::setEchoRate (double v)             { rateT = v; }
void MultiEcho::setEchoOn (bool on)                { echoOnT = on ? 1.0 : 0.0; }
void MultiEcho::setReverbOn (bool on)              { reverbOnT = on ? 1.0 : 0.0; }
void MultiEcho::setDryOn (bool on)                 { dryT = on ? 1.0 : 0.0; }
void MultiEcho::setOutputGainDb (double dB)        { outT = std::pow (10.0, dB / 20.0); }

void MultiEcho::updateTone (bool force)
{
    if (! force && std::fabs (bass - appliedBass) < 1.0e-4 && std::fabs (treble - appliedTreble) < 1.0e-4)
        return;
    appliedBass = bass; appliedTreble = treble;

    // Baxandall shelves: gain A, half-way (in dB) at the centre frequency
    const double lim = 0.42 * fsOs;
    {
        const double A = std::pow (10.0, kToneRangeDb * (2.0 * bass - 1.0) / 20.0);
        const double r = std::sqrt (A);
        const double wp = 2.0 * kPi * kBassHz / r;          // pole
        toneBass.setAnalog (1.0 / wp, A, 1.0 / wp, 1.0, fsOs);  // (s/wp + A) / (s/wp + 1)
    }
    {
        const double A = std::pow (10.0, kToneRangeDb * (2.0 * treble - 1.0) / 20.0);
        const double r = std::sqrt (A);
        double fp = kTrebleHz * r;
        if (fp > lim) fp = lim;
        const double wp = 2.0 * kPi * fp;
        toneTreble.setAnalog (A / wp, 1.0, 1.0 / wp, 1.0, fsOs);  // (A s/wp + 1) / (s/wp + 1)
    }
}

//==============================================================================
double MultiEcho::tickOs (double x1, double x2)
{
    // Input channels (Line) and Volume pots
    const double c1 = railClip (inLp1.tick (inHp1.tick (x1)) * kLineGain, kRail) * (kCh[0] / kLineGain);
    const double c2 = railClip (inLp2.tick (inHp2.tick (x2)) * kLineGain, kRail) * (kCh[1] / kLineGain);
    const double dryV = c1 + c2;

    // IC2: the Echo switches feed it; pre-emphasis; overload comparator
    const double ic2 = slewIc2.tick (railClip (preEmph.tick (c1 * chEcho[0] + c2 * chEcho[1]), kRail));
    if (std::fabs (ic2) > kOverloadV) overloadHold = 1.0;
    revDriveSum += ic2;

    // 36 dB anti-alias filter
    const double vaa = aa2.tick (aa1.tick (ic2));

    // Repeat path from the held DAC outputs: REPEAT buttons -> IC5 -> filter
    double nRep = 0.0, sRep = 0.0;
    for (int i = 0; i < kTaps; ++i) { nRep += repTap[i]; sRep += repTap[i] * dacOut[i]; }
    double vrep = 0.0;
    if (nRep > 1.0e-6)
    {
        const double bus = (sRep / kRtap) / (nRep / kRtap + 1.0 / kR69);
        vrep = railClip (kRepMixGain * bus, kRail);
        vrep = repHp.tick (repFilt.tick (vrep));
        vrep = slewRep.tick (railClip (vrep * kRepeat, kRail));
    }
    else
    {
        vrep = slewRep.tick (repHp.tick (repFilt.tick (0.0)) * kRepeat);
    }
    lastRepeat = vrep;

    // Delay board: sample-and-hold at the clock, ADC, memory, DAC
    const double boardIn = kBoardMix * (vaa + vrep);
    clkPhase += clockHz (rate) / fsOs;
    while (clkPhase >= 1.0)
    {
        clkPhase -= 1.0;
        double a = boardIn;
        const double aa = std::fabs (a) / kAdcFull;
        if (aa > peakAdcLevel) peakAdcLevel = aa;
        if (a > kAdcFull) a = kAdcFull;
        if (a < -kAdcFull) a = -kAdcFull;
        const double q = std::floor (a / kAdcFull * kAdcLevels + 0.5) / kAdcLevels * kAdcFull;
        adcHeld = q;
        // read the taps (tap 4 is the oldest word, about to be overwritten)
        for (int i = 0; i < kTaps; ++i)
        {
            int idx = memPos - (i + 1) * (kMemWords / kTaps);
            if (idx < 0) idx += kMemWords;
            dacOut[i] = mem[idx];
        }
        mem[memPos] = (float) q;
        if (++memPos >= kMemWords) memPos = 0;
    }

    // ECHO buttons -> IC6 echo mix
    double sEcho = 0.0;
    for (int i = 0; i < kTaps; ++i) sEcho += echoTap[i] * dacOut[i];
    double ve = railClip (kEchoMixGain * sEcho, kRail);

    // Reconstruction filter and de-emphasis
    ve = rec2.tick (rec1.tick (ve));
    ve = railClip (deEmph.tick (ve), kRail);

    // Signal programmed output switch: detector, comparator, JFET mute
    {
        const double a = std::fabs (ve);
        const double att = 1.0 - std::exp (-1.0 / (0.002 * fsOs));
        const double rel = 1.0 - std::exp (-1.0 / (0.700 * fsOs));
        gateEnv += (a - gateEnv) * (a > gateEnv ? att : rel);
        const double target = gateEnv > kGateOpen ? 1.0 : (gateEnv < 0.5 * kGateOpen ? kGateFloor : gateG);
        gateG += (target - gateG) * (1.0 - std::exp (-1.0 / (0.010 * fsOs)));
    }
    ve *= gateG;

    // Tone circuit and Echo Volume
    ve = railClip (toneTreble.tick (toneBass.tick (ve)), kRail);
    const double echoOut = ve * kEcho * echoOn;

    return outHp.tick (kMixDry * dryV * dry + kMixEcho * echoOut);
}

void MultiEcho::process (const float* in1, const float* in2, float* out, int numSamples)
{
    const double aK = 1.0 - smKnob, aR = 1.0 - smRate, aW = 1.0 - smSwitch;
    double xs1[Oversampler4x::kFactor], xs2[Oversampler4x::kFactor], ys[Oversampler4x::kFactor];
    const double holdDecay = std::exp (-1.0 / (0.25 * fsBase));

    for (int n = 0; n < numSamples; ++n)
    {
        for (int c = 0; c < 2; ++c)
        {
            vol[c] += (volT[c] - vol[c]) * aK;
            chEcho[c] += (chEchoT[c] - chEcho[c]) * aW;
            kCh[c] = channelGain (vol[c]);
        }
        reverb += (reverbT - reverb) * aK;
        echoVol += (echoVolT - echoVol) * aK;
        repeat += (repeatT - repeat) * aK;
        bass += (bassT - bass) * aK;
        treble += (trebleT - treble) * aK;
        outG += (outT - outG) * aK;
        rate += (rateT - rate) * aR;
        echoOn += (echoOnT - echoOn) * aW;
        reverbOn += (reverbOnT - reverbOn) * aW;
        dry += (dryT - dry) * aW;
        for (int i = 0; i < kTaps; ++i)
        {
            echoTap[i] += (echoTapT[i] - echoTap[i]) * aW;
            repTap[i] += (repTapT[i] - repTap[i]) * aW;
        }
        kEcho = echoPot (echoVol);
        kRepeat = repeatAmpGain (repeat);
        kReverb = reverbPot (reverb);
        if (--toneCountdown <= 0) { updateTone (false); toneCountdown = 16; }

        float a = in1[n], b = in2[n];
        if (! (a == a)) a = 0.0f;
        if (! (b == b)) b = 0.0f;
        up1.upsample ((double) a, xs1);
        up2.upsample ((double) b, xs2);
        revDriveSum = 0.0;
        for (int k = 0; k < Oversampler4x::kFactor; ++k)
            ys[k] = tickOs (xs1[k], xs2[k]);
        double y = down.downsample (ys);

        // Spring reverb at the base rate: 470p/270K, IC9 driver, tank, IC8c
        {
            double d = revDrv.tick (revHp.tick (revDriveSum / Oversampler4x::kFactor));
            d = railClip (d, kRail);
            double r = tank.process (d);
            r = revLp.tick (railClip (revRec.tick (r), kRail));
            revOut = r * kReverb * reverbOn;
        }
        y += kMixRev * revOut;

        overloadHold *= holdDecay;
        out[n] = (float) (y * outCal * outG);
    }
}

} // namespace tbme
