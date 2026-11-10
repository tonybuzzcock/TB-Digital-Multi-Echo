// multiecho_test.cpp
// Offline checks for the TB Digital Multi Echo DSP. Builds on Linux or macOS:
//   make -C Tests run      (writes demo WAVs to Tests/renders)
//
// 1. Dry path: unity at the default Volume 1, Volume monotonic, Dry Out, Echo switches
// 2. The drawn filters: 36 dB anti-alias and reconstruction, pre/de-emphasis
// 3. Tap times for ECHO 1-4 and the Echo Rate range
// 4. Echo Volume monotonic; Bass and Treble cut and boost their bands
// 5. Repeat Volume: repeats decay below the top, runaway stays bounded; REPEAT A-D
// 6. Signal programmed output switch closes after the playing stops
// 7. Overload LED and converter clipping
// 8. Reverb level, footswitch Echo and Reverb
// 9. Stability sweep: switch combinations x knob extremes x 10 mV-4 V x 41 Hz-15 kHz, plus noise
// 10. Automation of every control while playing; CPU; demo renders

#include "../Source/DSP/MultiEcho.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using namespace tbme;

static const double kPiT = 3.14159265358979323846;
static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

// A short tone burst at t = 0, then silence
static std::vector<float> burst (double f, double amp, double burstSec, double totalSec)
{
    std::vector<float> v ((size_t) (totalSec * kSr), 0.0f);
    const size_t n = (size_t) (burstSec * kSr);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = std::sin (kPiT * (double) i / n);       // Hann-ish envelope
        v[i] = (float) (amp * w * w * std::sin (2.0 * kPiT * f * i / kSr));
    }
    return v;
}


static std::vector<float> run (MultiEcho& a, const std::vector<float>& in1, const std::vector<float>& in2)
{
    std::vector<float> out (in1.size());
    for (size_t pos = 0; pos < in1.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, in1.size() - pos);
        a.process (&in1[pos], &in2[pos], &out[pos], n);
    }
    return out;
}
static std::vector<float> run (MultiEcho& a, const std::vector<float>& in) { return run (a, in, in); }

// Defaults as on the plugin, then a clean setup for measuring the echo alone
static void defaults (MultiEcho& a)
{
    a.setVolume (0, 0.6); a.setVolume (1, 0.0); a.setReverb (0.3); a.setEchoVolume (0.5);
    a.setRepeatVolume (0.4); a.setBass (0.5); a.setTreble (0.5);
    a.setChannelEcho (0, true); a.setChannelEcho (1, true);
    for (int t = 0; t < 4; ++t) { a.setEchoTap (t, t == 2); a.setRepeatTap (t, t == 2); }
    a.setEchoRate (0.5); a.setEchoOn (true); a.setReverbOn (true); a.setDryOn (true);
    a.setOutputGainDb (0.0);
}
static void echoOnly (MultiEcho& a, int tap)
{
    defaults (a);
    a.setReverbOn (false); a.setDryOn (false); a.setRepeatVolume (0.0);
    for (int t = 0; t < 4; ++t) a.setEchoTap (t, t == tap);
}

static double peakTimeMs (const std::vector<float>& x, double fromMs, double toMs)
{
    size_t from = (size_t) (fromMs * kSr / 1000.0), to = std::min (x.size(), (size_t) (toMs * kSr / 1000.0));
    size_t at = from; double best = 0.0;
    for (size_t i = from; i < to; ++i) if (std::fabs (x[i]) > best) { best = std::fabs (x[i]); at = i; }
    return 1000.0 * (double) ((int) at - MultiEcho::latencySamples()) / kSr;
}

// Echo level (tap 3 at the default rate) relative to the input, dB
static double echoDb (MultiEcho& a, double f, double amp)
{
    a.prepare (kSr);
    std::vector<float> in = sine (f, amp, 0.6);
    in.resize ((size_t) (kSr * 0.9), 0.0f);
    std::vector<float> y = run (a, in);
    const size_t s0 = (size_t) (0.45 * kSr), s1 = (size_t) (0.75 * kSr);
    return 20.0 * std::log10 (rmsRange (y, s0, s1) / (amp / std::sqrt (2.0)) + 1e-12);
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("  could not write %s\n", path.c_str()); return; }
    const unsigned int dataBytes = (unsigned int) (x.size() * 4);
    const unsigned int riff = 36 + dataBytes;
    const unsigned short fmt = 3, ch = 1, bits = 32, align = 4;
    const unsigned int rate = (unsigned int) sr, bps = rate * 4, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmtLen, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    std::fwrite (&x[0], 4, x.size(), f);
    std::fclose (f);
}

// Karplus-Strong plucked string as a stand-in for a DI guitar track.
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = (int) (kSr / freq + 0.5);
    std::vector<double> line (period);
    unsigned int seed = (unsigned int) (freq * 1000.0);
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 8) / 8388608.0 - 1.0);
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 1; i < period; ++i) line[i] = 0.5 * (line[i] + line[i - 1]);
    const size_t n = (size_t) (dur * kSr);
    int idx = 0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const int next = (idx + 1) % period;
        const double y = line[idx];
        line[idx] = 0.996 * 0.5 * (line[idx] + line[next]);
        idx = next;
        const double env = (i > n - 2000) ? (double) (n - i) / 2000.0 : 1.0;
        buf[start + i] += (float) (amp * y * env);
    }
}

// A twangy single-note line with gaps for the echoes, then a held chord
static std::vector<float> makeLine (double seconds)
{
    std::vector<float> g ((size_t) (kSr * seconds), 0.0f);
    const double notes[][3] = {
        { 0.00, 329.63, 0.30 }, { 0.40, 392.00, 0.30 }, { 0.80, 440.00, 0.30 }, { 1.20, 493.88, 0.30 },
        { 1.60, 587.33, 0.35 }, { 2.60, 493.88, 0.30 }, { 3.00, 440.00, 0.30 }, { 3.40, 329.63, 0.35 },
        { 4.60, 164.81, 0.30 }, { 4.60, 246.94, 0.25 }, { 4.60, 329.63, 0.20 }, { 4.60, 415.30, 0.18 },
    };
    for (size_t i = 0; i < sizeof (notes) / sizeof (notes[0]); ++i)
        pluck (g, (size_t) (notes[i][0] * kSr), notes[i][1], notes[i][2], notes[i][0] < 4.5 ? 0.35 : 2.5);
    return g;
}


int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : "renders";
    MultiEcho* ap = new MultiEcho();
    MultiEcho& a = *ap;

    std::printf ("\n1. Dry path\n");
    {
        defaults (a); a.setReverbOn (false); a.setEchoOn (false); a.prepare (kSr);
        std::vector<float> y = run (a, sine (1000.0, 0.1, 0.5));
        const double g = rmsRange (y, kSr / 4, kSr / 2) / (0.1 / std::sqrt (2.0));
        std::printf ("  dry gain at Volume 1 = 6: %.3f\n", g);
        check (std::fabs (g - 1.0) < 0.02, "dry is unity at the default Volume 1");
        double prev = -1.0; bool mono = true;
        for (int v = 0; v <= 10; ++v)
        {
            a.setVolume (0, v / 10.0); a.prepare (kSr);
            y = run (a, sine (1000.0, 0.05, 0.3));
            const double r = rmsRange (y, kSr / 6, (size_t) (0.3 * kSr));
            if (r < prev - 1e-9) mono = false;
            prev = r;
        }
        check (mono, "Volume 1 is monotonic");
        defaults (a); a.setReverbOn (false); a.setEchoOn (false); a.setDryOn (false); a.prepare (kSr);
        y = run (a, sine (1000.0, 0.1, 0.5));
        check (peakRange (y, kSr / 4, kSr / 2) < 1e-4, "Dry Out removes the dry signal");
        echoOnly (a, 2); a.setChannelEcho (0, false); a.prepare (kSr);
        y = run (a, burst (1000.0, 0.2, 0.01, 0.6));
        check (peakRange (y, 0, y.size()) < 1e-3, "Channel 1's Echo switch off: no echo from input 1");
    }

    std::printf ("\n2. The drawn filters\n");
    {
        ThirdOrder s; const double fs = 4.0 * kSr;
        s.setSection (12.0e3, 2.2e-9, 7.8e-9, 150.0e-12, fs);
        const double aa10 = 2.0 * 20.0 * std::log10 (s.magnitude (10000.0, fs));
        const double aa20 = 2.0 * 20.0 * std::log10 (s.magnitude (20000.0, fs));
        s.setSection (27.0e3, 2.2e-9, 7.8e-9, 150.0e-12, fs);
        const double rc3 = 2.0 * 20.0 * std::log10 (s.magnitude (3000.0, fs));
        const double rc6 = 2.0 * 20.0 * std::log10 (s.magnitude (6000.0, fs));
        const double rc12 = 2.0 * 20.0 * std::log10 (s.magnitude (12000.0, fs));
        std::printf ("  anti-alias (2 x 12K): %.1f dB at 10 kHz, %.1f dB at 20 kHz\n", aa10, aa20);
        std::printf ("  reconstruction (2 x 27K): %.1f dB at 3 kHz, %.1f at 6 kHz, %.1f at 12 kHz\n", rc3, rc6, rc12);
        check (aa10 > -4.0 && aa20 < -25.0, "anti-alias filter: flat to 10 kHz, steep above");
        check (rc3 > -1.5 && rc6 < -6.0 && rc12 < -40.0, "reconstruction filter: about 5.5 kHz, 36 dB/oct");
        echoOnly (a, 2);
        const double e100 = echoDb (a, 100.0, 0.05), e1k = echoDb (a, 1000.0, 0.05), e3k = echoDb (a, 3000.0, 0.05);
        const double e8k = echoDb (a, 8000.0, 0.05);
        std::printf ("  echo response: 100 Hz %.1f, 1 kHz %.1f, 3 kHz %.1f, 8 kHz %.1f dB\n", e100, e1k, e3k, e8k);
        check (e3k > e100 && e8k < e1k - 12.0, "echo: pre-emphasis lift (as drawn), then the 36 dB roll-off");
    }

    std::printf ("\n3. Tap times and Echo Rate\n");
    {
        for (int t = 0; t < 4; ++t)
        {
            echoOnly (a, t); a.prepare (kSr);
            std::vector<float> y = run (a, burst (1000.0, 0.2, 0.01, 0.8));
            const double ms = peakTimeMs (y, 30.0, 780.0) - 5.0;
            const double expect = MultiEcho::tapDelayMs (t, 0.5);
            char msg[96];
            std::snprintf (msg, sizeof (msg), "Echo %d at %.0f ms (measured %.1f)", t + 1, expect, ms);
            check (std::fabs (ms - expect) < 2.5, msg);
        }
        check (std::fabs (MultiEcho::tapDelayMs (3, 0.0) - 600.0) < 0.5 && std::fabs (MultiEcho::tapDelayMs (3, 1.0) - 150.0) < 0.5,
               "Echo Rate: longest tap 600 ms (slow) to 150 ms (fast)");
        echoOnly (a, 3); a.setEchoRate (0.0); a.prepare (kSr);
        std::vector<float> y = run (a, burst (1000.0, 0.2, 0.01, 0.9));
        const double ms = peakTimeMs (y, 30.0, 880.0) - 5.0;
        std::printf ("  Echo 4 fully slow: %.1f ms\n", ms);
        check (std::fabs (ms - 600.0) < 3.0, "slow clock delays the echo");
    }

    std::printf ("\n4. Echo Volume and tone\n");
    {
        double prev = -200.0; bool mono = true;
        for (int v = 1; v <= 10; ++v)
        {
            echoOnly (a, 2); a.setEchoVolume (v / 10.0);
            const double d = echoDb (a, 1000.0, 0.05);
            if (d < prev) mono = false;
            prev = d;
        }
        check (mono, "Echo Volume is monotonic");
        echoOnly (a, 2); a.setEchoVolume (0.0); a.prepare (kSr);
        std::vector<float> y = run (a, burst (1000.0, 0.2, 0.01, 0.6));
        check (peakRange (y, 0, y.size()) < 1e-4, "Echo Volume at 0 (ECHO OFF) is silent");
        echoOnly (a, 2); const double b0 = echoDb (a, 80.0, 0.05), t0 = echoDb (a, 4000.0, 0.05);
        echoOnly (a, 2); a.setBass (1.0); const double bUp = echoDb (a, 80.0, 0.05);
        echoOnly (a, 2); a.setBass (0.0); const double bDn = echoDb (a, 80.0, 0.05);
        echoOnly (a, 2); a.setTreble (1.0); const double tUp = echoDb (a, 4000.0, 0.05);
        echoOnly (a, 2); a.setTreble (0.0); const double tDn = echoDb (a, 4000.0, 0.05);
        std::printf ("  Bass at 80 Hz: %+.1f / %+.1f dB; Treble at 4 kHz: %+.1f / %+.1f dB\n", bUp - b0, bDn - b0, tUp - t0, tDn - t0);
        check (bUp - b0 > 8.0 && bDn - b0 < -8.0, "Bass boosts and cuts");
        check (tUp - t0 > 8.0 && tDn - t0 < -8.0, "Treble boosts and cuts");
    }

    std::printf ("\n5. Repeats\n");
    {
        int prevCount = -1; bool more = true;
        for (int r = 0; r <= 8; r += 2)
        {
            echoOnly (a, 0); a.setRepeatVolume (r / 10.0);
            for (int t = 0; t < 4; ++t) a.setRepeatTap (t, t == 0);
            a.prepare (kSr);
            std::vector<float> y = run (a, burst (1000.0, 0.2, 0.01, 4.0));
            const double first = peakRange (y, (size_t) (0.06 * kSr), (size_t) (0.10 * kSr));
            int count = 0;
            for (int k = 1; k < 50; ++k)
            {
                const size_t c = (size_t) ((0.080 + 0.075 * k) * kSr);
                if (c + kSr / 50 >= y.size()) break;
                if (peakRange (y, c - kSr / 50, c + kSr / 50) > 0.1 * first) ++count;
            }
            std::printf ("  Repeat Volume %d: %d repeats above -20 dB\n", r, count);
            if (count < prevCount) more = false;
            prevCount = count;
        }
        check (more, "more Repeat Volume gives more repeats");
        echoOnly (a, 0); a.setRepeatVolume (1.0); a.prepare (kSr);
        for (int t = 0; t < 4; ++t) a.setRepeatTap (t, true);
        std::vector<float> y = run (a, burst (1000.0, 0.3, 0.02, 6.0));
        const double late = peakRange (y, 5 * kSr, 6 * kSr);
        std::printf ("  all REPEAT buttons at full: peak %.2f after 5 s\n", late);
        check (allFinite (y, 50.0) && late > 0.05, "runaway at full repeat stays bounded (converter and rails)");
        echoOnly (a, 0); a.setRepeatVolume (0.8); for (int t = 0; t < 4; ++t) a.setRepeatTap (t, false);
        a.prepare (kSr);
        y = run (a, burst (1000.0, 0.2, 0.01, 1.0));
        check (peakRange (y, (size_t) (0.13 * kSr), kSr) < 0.02 * peakRange (y, 0, (size_t) (0.12 * kSr)),
               "no REPEAT button in: a single echo");
    }

    std::printf ("\n6. Signal programmed output switch\n");
    {
        echoOnly (a, 0); a.setRepeatVolume (0.0); a.prepare (kSr);
        std::vector<float> y = run (a, burst (1000.0, 0.2, 0.2, 0.4));
        const double g1 = a.gateGain();
        std::vector<float> z (4 * kSr, 0.0f);
        run (a, z);
        const double g2 = a.gateGain();
        std::printf ("  gate after playing %.2f, after 4 s of silence %.3f\n", g1, g2);
        check (g1 > 0.9 && g2 < 0.1, "echo path opens with signal and mutes in silence");
    }

    std::printf ("\n7. Overload LED and converter\n");
    {
        defaults (a); a.prepare (kSr);
        run (a, sine (1000.0, 0.05, 0.3));
        const double quiet = a.overload();
        a.setVolume (0, 1.0);
        run (a, sine (1000.0, 1.0, 0.3));
        const double loud = a.overload();
        std::printf ("  overload: %.2f quiet, %.2f loud; ADC peak %.2f of full scale\n", quiet, loud, a.peakAdc());
        check (quiet < 0.1 && loud > 0.5, "Overload LED lights on a hot input only");
    }

    std::printf ("\n8. Reverb and footswitch\n");
    {
        defaults (a); a.setEchoOn (false); a.setDryOn (false); a.prepare (kSr);
        std::vector<float> y = run (a, burst (800.0, 0.2, 0.05, 2.0));
        const double rv = peakRange (y, 0, y.size()), tail = rmsRange (y, kSr, (size_t) (1.5 * kSr));
        std::printf ("  reverb peak %.3f, tail rms after 1 s %.4f\n", rv, tail);
        check (rv > 0.01 && rv < 0.3 && tail > 1e-4, "reverb is present, below the dry level, with a tail");
        a.setReverbOn (false); a.prepare (kSr);
        y = run (a, burst (800.0, 0.2, 0.05, 1.0));
        check (peakRange (y, 0, y.size()) < 1e-5, "footswitch Reverb off silences it");
        defaults (a); a.setReverbOn (false); a.setDryOn (false); a.setEchoOn (false); a.prepare (kSr);
        y = run (a, burst (800.0, 0.2, 0.05, 1.0));
        check (peakRange (y, 0, y.size()) < 1e-5, "footswitch Echo off silences it");
    }

    std::printf ("\n9. Stability sweep\n");
    {
        const double levels[3] = { 0.01, 0.5, 4.0 };
        const double freqs[4] = { 41.0, 440.0, 4000.0, 15000.0 };
        bool ok = true; int runs = 0;
        unsigned int seed = 99u;
        for (int c = 0; c < 24 && ok; ++c)
        {
            defaults (a);
            for (int k = 0; k < 9; ++k)
            {
                seed = seed * 1664525u + 1013904223u;
                const double v = (c < 2) ? (double) c : (double) (seed >> 8) / 16777216.0;
                switch (k)
                {
                    case 0: a.setVolume (0, v); break;      case 1: a.setVolume (1, 1.0 - v); break;
                    case 2: a.setEchoVolume (v); break;     case 3: a.setRepeatVolume (v); break;
                    case 4: a.setBass (v); break;           case 5: a.setTreble (1.0 - v); break;
                    case 6: a.setEchoRate (v); break;       case 7: a.setReverb (v); break;
                    default: break;
                }
            }
            for (int t = 0; t < 4; ++t) { a.setEchoTap (t, ((c >> t) & 1) != 0); a.setRepeatTap (t, ((c >> (t + 1)) & 1) != 0); }
            for (int l = 0; l < 3 && ok; ++l)
                for (int f = 0; f < 4 && ok; ++f)
                {
                    a.prepare (kSr);
                    std::vector<float> y = run (a, sine (freqs[f], levels[l], 0.25));
                    ok = allFinite (y, 200.0);
                    ++runs;
                }
        }
        {
            defaults (a); a.setRepeatVolume (1.0); a.prepare (kSr);
            std::vector<float> n ((size_t) kSr);
            unsigned int s2 = 5u;
            for (size_t i = 0; i < n.size(); ++i) { s2 = s2 * 1664525u + 1013904223u; n[i] = (float) (8.0 * ((s2 >> 8) / 8388608.0 - 1.0)); }
            ok = ok && allFinite (run (a, n), 200.0);
        }
        {
            // Odd and extreme sample rates (hosts and validators try these)
            const double rates[4] = { 1234.57, 8000.0, 192000.0, 384000.0 };
            for (int k = 0; k < 4 && ok; ++k)
            {
                MultiEcho* t = new MultiEcho();
                defaults (*t); t->setRepeatVolume (0.8); t->setReverb (0.8);
                t->prepare (rates[k]);
                std::vector<float> in ((size_t) rates[k]), out (in.size());
                for (size_t i = 0; i < in.size(); ++i) in[i] = (float) (0.5 * std::sin (0.3 * (double) i));
                t->process (&in[0], &in[0], &out[0], (int) in.size());
                ok = allFinite (out, 200.0);
                delete t;
            }
        }
        std::printf ("  %d runs plus loud noise and 4 odd sample rates\n", runs);
        check (ok, "no NaNs or blow-ups at any setting");
    }

    std::printf ("\n10. Automation, CPU, renders\n");
    {
        defaults (a); a.prepare (kSr);
        std::vector<float> in = sine (110.0, 0.3, 2.0), out (in.size());
        for (size_t pos = 0; pos < in.size(); pos += 64)
        {
            const double t = (double) pos / in.size();
            a.setVolume (0, 0.5 + 0.5 * std::sin (6.28 * t)); a.setEchoVolume (0.5 + 0.5 * std::sin (12.5 * t));
            a.setRepeatVolume (0.5 + 0.5 * std::sin (9.0 * t)); a.setEchoRate (0.5 + 0.5 * std::sin (5.0 * t));
            a.setBass (0.5 + 0.5 * std::sin (7.0 * t)); a.setTreble (0.5 + 0.5 * std::sin (8.0 * t));
            a.setReverb (0.5 + 0.5 * std::sin (3.0 * t));
            for (int k = 0; k < 4; ++k) { a.setEchoTap (k, ((pos / 4096 + k) & 1) != 0); a.setRepeatTap (k, ((pos / 8192 + k) & 1) != 0); }
            a.setChannelEcho (0, (pos / 16384) % 3 != 0); a.setDryOn ((pos / 20000) % 2 == 0);
            const int n = (int) std::min ((size_t) 64, in.size() - pos);
            a.process (&in[pos], &in[pos], &out[pos], n);
        }
        check (allFinite (out, 200.0), "every control automated while playing");

        defaults (a); a.prepare (kSr);
        std::vector<float> g = makeLine (10.0);
        std::clock_t t0 = std::clock();
        std::vector<float> y = run (a, g);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        std::printf ("  CPU: %.1f%% of one core\n", 100.0 * secs / 10.0);
        check (secs / 10.0 < 0.3, "runs well under real time");
        writeWav (outDir + "/01_dry_input.wav", g, kSr);
        writeWav (outDir + "/02_defaults.wav", y, kSr);
        defaults (a); for (int t = 0; t < 4; ++t) { a.setEchoTap (t, true); a.setRepeatTap (t, t == 3); }
        a.setRepeatVolume (0.6); a.prepare (kSr);
        writeWav (outDir + "/03_all_echo_taps.wav", run (a, g), kSr);
        defaults (a); a.setEchoRate (0.0); a.setRepeatVolume (0.75); a.prepare (kSr);
        writeWav (outDir + "/04_slow_rate_long_repeats.wav", run (a, g), kSr);
        defaults (a); a.setEchoRate (1.0); for (int t = 0; t < 4; ++t) a.setEchoTap (t, t == 0);
        a.setReverb (0.6); a.prepare (kSr);
        writeWav (outDir + "/05_fast_slapback_reverb.wav", run (a, g), kSr);
        defaults (a); a.setDryOn (false); a.prepare (kSr);
        writeWav (outDir + "/06_wet_only_send.wav", run (a, g), kSr);
    }

    delete ap;
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
