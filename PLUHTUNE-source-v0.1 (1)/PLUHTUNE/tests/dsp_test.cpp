// PLUHTUNE DSP core tests - no JUCE needed.  Build:  g++ -O2 -std=c++17 -I../Source/DSP dsp_test.cpp -o dsp_test
#include "PitchEngine.h"
#include <cstdio>
#include <random>
#include <chrono>
#include <functional>
using namespace pluh;

static const double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const char* name, const char* detail = "")
{ std::printf ("  [%s] %s %s\n", ok ? "PASS" : "FAIL", name, detail); if (!ok) ++failures; }

// ---- synthetic "voice": impulse train (fractional-phase) through 3 formant resonators ----
struct Res { double a1, a2, g, z1 = 0, z2 = 0;
    void set (double fs, double f, double bw) { const double r = std::exp (-PI * bw / fs); a1 = -2 * r * std::cos (2 * PI * f / fs); a2 = r * r; g = 1 - r; }
    double run (double x) { const double y = g * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; } };
struct Voice
{
    double fs, phase = 0; Res r[3];
    Voice (double fs_, double f1 = 700, double f2 = 1200, double f3 = 2600) : fs (fs_) { r[0].set (fs, f1, 90); r[1].set (fs, f2, 110); r[2].set (fs, f3, 160); }
    float next (double f0) { phase += f0 / fs; double imp = 0; if (phase >= 1.0) { phase -= 1.0; imp = 1.0; }
        double y = r[0].run (imp) + 0.6 * r[1].run (imp) + 0.3 * r[2].run (imp); return (float) (y * 6.0); }
};
static std::vector<float> makeVoice (double fs, int n, std::function<double(double)> f0ofT, double f1 = 700)
{ Voice v (fs, f1); std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = v.next (f0ofT (i / fs)); return x; }

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

// Independent pitch measurement: normalised autocorrelation + parabolic peak
static double measureHz (const std::vector<float>& x, int start, int len, double fs, double expect)
{
    const int lo = (int) (fs / (expect * 1.25)), hi = (int) (fs / (expect * 0.8));
    std::vector<double> ac ((size_t) hi + 2, 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag) { double num = 0, e = 1e-12; for (int i = 0; i < len; ++i) { num += (double) x[(size_t) (start + i)] * x[(size_t) (start + i + lag)]; e += (double) x[(size_t) (start + i + lag)] * x[(size_t) (start + i + lag)]; } ac[(size_t) lag] = num / std::sqrt (e); }
    int b = lo; for (int l = lo; l <= hi; ++l) if (ac[(size_t) l] > ac[(size_t) b]) b = l;
    const double a = ac[(size_t) b - 1], c0 = ac[(size_t) b], c = ac[(size_t) b + 1], den = a - 2 * c0 + c;
    return fs / ((double) b + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0));
}

struct Run { std::vector<float> wet, dry; int latency; };
static Run runEngine (const std::vector<float>& in, double fs, const EngineParams& p, int block = 512, float lowest = 60.f, float minHz = 80.f, int ch = 1)
{
    PitchEngine e; e.prepare (fs, ch, lowest); e.setMinFrequency (minHz);
    Run r; r.latency = e.getLatency(); r.wet.resize (in.size()); r.dry.resize (in.size());
    std::vector<float> bi ((size_t) block), bw ((size_t) block), bd ((size_t) block);
    std::vector<float> bi2 ((size_t) block), bw2 ((size_t) block), bd2 ((size_t) block);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        for (int i = 0; i < n; ++i) { bi[(size_t) i] = in[pos + (size_t) i]; bi2[(size_t) i] = in[pos + (size_t) i]; }
        const float* ins[2] = { bi.data(), bi2.data() }; float* w[2] = { bw.data(), bw2.data() }; float* d[2] = { bd.data(), bd2.data() };
        e.process (ins, w, d, n, p); e.clearViz();
        for (int i = 0; i < n; ++i) { r.wet[pos + (size_t) i] = bw[(size_t) i]; r.dry[pos + (size_t) i] = bd[(size_t) i]; }
    }
    return r;
}
static EngineParams hardTune() { EngineParams p; p.retuneMs = 0; p.amount = 1; p.humanize = 0; p.transitionMs = 0; p.aggressive = true; return p; }

int main()
{
    std::printf ("1. Detector accuracy (steady voice, 3 sample rates)\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0; int missed = 0;
        for (double f0 : { 82.0, 110.0, 196.0, 330.0, 523.0, 880.0 })
        {
            PitchDetector d; d.prepare (fs, 60.f); d.configure (70.f);
            auto x = makeVoice (fs, (int) (fs * 1.0), [=] (double) { return f0; });
            PitchFrame f; std::vector<double> est;
            for (size_t i = 0; i < x.size(); ++i) if (d.push (x[i], f) && i > fs * 0.5) { if (f.voiced) est.push_back (fs / f.period); else ++missed; }
            double mean = 0; for (double v : est) mean += v; mean /= std::max<size_t> (1, est.size());
            worst = std::max (worst, std::abs (cents (mean, f0)));
        }
        char b[96]; std::snprintf (b, sizeof b, "fs=%.0f worst error %.2f cents, unvoiced frames %d", fs, worst, missed);
        check (worst < 5.0 && missed == 0, "steady-tone detection", b);
    }

    std::printf ("2. Hard-tune accuracy (output pitch vs nearest chromatic note)\n");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        double worst = 0;
        for (double base : { 130.8128, 220.0, 440.0 })
            for (double off : { -45.0, -20.0, 25.0, 40.0 })
            {
                const double f0 = base * std::pow (2.0, off / 1200.0);
                auto x = makeVoice (fs, (int) (fs * 2.5), [=] (double) { return f0; });
                auto r = runEngine (x, fs, hardTune());
                const double got = measureHz (r.wet, (int) (fs * 1.8), (int) (fs * 0.3), fs, base);
                worst = std::max (worst, std::abs (cents (got, base)));
            }
        char b[96]; std::snprintf (b, sizeof b, "fs=%.0f worst residual error %.2f cents", fs, worst);
        check (worst < 10.0, "snaps to target note", b);
    }

    std::printf ("3. Amount = 0 passthrough fidelity (wet vs latency-aligned dry, best sub-period alignment)\n");
    {
        const double fs = 48000; auto x = makeVoice (fs, (int) (fs * 2), [] (double t) { return 180.0 * std::pow (2.0, 0.4 * std::sin (2 * PI * 0.7 * t) / 12.0); });
        EngineParams p = hardTune(); p.amount = 0;
        auto r = runEngine (x, fs, p);
        const size_t s0 = (size_t) (fs * 0.5), N = 48000; double best = -1; int bestLag = 0;
        for (int lag = -400; lag <= 400; ++lag)
        { double num = 0, ew = 1e-12, ed = 1e-12;
          for (size_t i = s0; i < s0 + N; ++i) { const double w = r.wet[i], d = r.dry[(size_t) ((long) i + lag)]; num += w * d; ew += w * w; ed += d * d; }
          const double cc = num / std::sqrt (ew * ed); if (cc > best) { best = cc; bestLag = lag; } }
        char b[128]; std::snprintf (b, sizeof b, "correlation %.4f at lag %d samples, latency %d samples (%.1f ms)", best, bestLag, r.latency, 1000.0 * r.latency / fs);
        check (best > 0.97, "passthrough faithful (within one period of alignment)", b);
    }

    std::printf ("4. Block-size independence (bit exact)\n");
    {
        const double fs = 48000; auto x = makeVoice (fs, (int) (fs * 1.5), [] (double t) { return 200.0 + 30.0 * std::sin (2 * PI * 3 * t); });
        auto ref = runEngine (x, fs, hardTune(), 512); double worst = 0;
        for (int blk : { 1, 7, 64, 333, 1024 }) { auto r = runEngine (x, fs, hardTune(), blk); for (size_t i = 0; i < x.size(); ++i) worst = std::max (worst, (double) std::abs (r.wet[i] - ref.wet[i])); }
        check (worst == 0.0, "identical output for block sizes 1..1024");
    }

    std::printf ("5. Smoothness on gliding/vibrato input (click check)\n");
    {
        const double fs = 48000; auto x = makeVoice (fs, (int) (fs * 3), [] (double t) { return 200.0 * std::pow (2.0, (0.5 * std::sin (2 * PI * 5.5 * t) + 0.8 * t) / 12.0); });
        EngineParams p = hardTune(); p.aggressive = false; p.retuneMs = 40; p.humanize = 0.2f; p.transitionMs = 25;
        auto r = runEngine (x, fs, p);
        auto peakD2 = [&] (const std::vector<float>& s) { double m = 0, rms = 0; for (size_t i = (size_t) (fs * 0.5) + 2; i < s.size(); ++i) { m = std::max (m, (double) std::abs (s[i] - 2 * s[i - 1] + s[i - 2])); rms += (double) s[i] * s[i]; } return m / std::sqrt (rms / s.size()); };
        const double ratio = peakD2 (r.wet) / peakD2 (x);
        char b[96]; std::snprintf (b, sizeof b, "peak 2nd-difference ratio out/in = %.2f", ratio);
        check (ratio < 2.0, "no added clicks", b);
    }

    std::printf ("6. Formant preservation vs follow (target C4 from G3, formant at 900 Hz)\n");
    {
        const double fs = 48000; auto x = makeVoice (fs, (int) (fs * 2.5), [] (double) { return 196.0; }, 900.0);
        auto envPeak = [&] (const std::vector<float>& s)
        {
            const int N = 8192, st = (int) (fs * 1.8); double bestF = 0, bestE = -1; std::vector<double> mag;
            for (double f = 300; f <= 2500; f += 10) { double re = 0, im = 0; for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * PI * i / N), v = w * s[(size_t) (st + i)]; re += v * std::cos (2 * PI * f * i / fs); im -= v * std::sin (2 * PI * f * i / fs); } mag.push_back (re * re + im * im); }
            for (size_t k = 25; k + 25 < mag.size(); ++k) { double e = 0; for (int j = -25; j <= 25; ++j) e += mag[(size_t) ((int) k + j)]; if (e > bestE) { bestE = e; bestF = 300 + 10.0 * k; } }
            return bestF;
        };
        EngineParams p = hardTune(); p.allowedMask = 1; // only C
        p.formantPreserve = true;  auto a = runEngine (x, fs, p);
        p.formantPreserve = false; auto b = runEngine (x, fs, p);
        const double pa = envPeak (a.wet), pb = envPeak (b.wet), pi = envPeak (x);
        const double outHz = measureHz (a.wet, (int) (fs * 1.8), (int) (fs * 0.3), fs, 261.63);
        char s1[128]; std::snprintf (s1, sizeof s1, "pitch %.1f Hz (C4=261.6); envelope peak in=%.0f preserve=%.0f follow=%.0f Hz", outHz, pi, pa, pb);
        check (std::abs (cents (outHz, 261.6256)) < 10 && std::abs (pa - pi) < 150 && pb > pi + 150, "formants stay (preserve) / move (follow)", s1);
    }

    std::printf ("7. Unvoiced material passes at unity level\n");
    {
        const double fs = 48000; std::mt19937 g (1); std::normal_distribution<float> nd (0.f, 0.1f);
        std::vector<float> x ((size_t) (fs * 2)); for (auto& v : x) v = nd (g);
        auto r = runEngine (x, fs, hardTune());
        double a = 0, b = 0; size_t cnt = 0; for (size_t i = (size_t) fs; i < x.size(); ++i) { a += (double) r.wet[i] * r.wet[i]; b += (double) x[i] * x[i]; ++cnt; }
        const double db = 10 * std::log10 (a / b); char s[64]; std::snprintf (s, sizeof s, "level change %.2f dB", db);
        check (std::abs (db) < 1.5, "noise level preserved", s);
    }

    std::printf ("8. Robustness (silence, DC, square, noise bursts, sweep, stereo)\n");
    {
        const double fs = 44100; std::mt19937 g (5); std::uniform_real_distribution<float> u (-1.f, 1.f);
        std::vector<float> x ((size_t) (fs * 4), 0.f);
        for (size_t i = 0; i < x.size(); ++i) { const double t = i / fs;
            if (t < 0.5) x[i] = 0; else if (t < 1.0) x[i] = 0.5f; else if (t < 1.8) x[i] = ((int) (t * 440 * 2) & 1) ? 1.f : -1.f;
            else if (t < 2.2) x[i] = u (g); else x[i] = 0.5f * (float) std::sin (2 * PI * (80 * t + 150 * t * t)); }
        auto r = runEngine (x, fs, hardTune(), 256, 60.f, 60.f, 2);
        bool finite = true; double pk = 0; for (float v : r.wet) { if (!std::isfinite (v)) finite = false; pk = std::max (pk, (double) std::abs (v)); }
        char s[64]; std::snprintf (s, sizeof s, "peak %.2f", pk);
        check (finite && pk < 4.0, "finite & bounded output", s);
    }

    std::printf ("9. CPU cost (stereo, 48 kHz, 10 s voice, hard tune)\n");
    {
        const double fs = 48000; auto x = makeVoice (fs, (int) (fs * 10), [] (double t) { return 150.0 + 40.0 * std::sin (2 * PI * 0.3 * t); });
        const auto t0 = std::chrono::steady_clock::now(); auto r = runEngine (x, fs, hardTune(), 256, 60.f, 70.f, 2);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        char s[96]; std::snprintf (s, sizeof s, "%.1fx real-time (%.2f%% of one core)", 10.0 / sec, 100.0 * sec / 10.0);
        check (10.0 / sec > 10.0, "runs comfortably in real time", s); (void) r;
    }

    std::printf ("10. Corrector behaviour (retune time, humanize, scale masks)\n");
    {
        const double dt = 0.002; const double A = 440.0;
        { PitchCorrector c; CorrectorParams p; p.retuneMs = 100; p.amount = 1; p.humanize = 0; p.transitionMs = 0; p.aggressive = true; p.refA4 = 440; // aggressive scales tau by 0.25
          p.aggressive = false; float ratio = 1; double tt = 0;
          for (; tt < 0.1; tt += dt) ratio = c.process ((float) (A * std::pow (2.0, 40.0 / 1200.0)), dt, p);
          const double corr = 1.0 - 1200.0 * std::log2 (ratio) / -40.0; // fraction of the -40 cent correction applied
          const double applied = (-1200.0 * std::log2 (ratio)) / 40.0;
          char b[96]; std::snprintf (b, sizeof b, "applied %.2f of correction after one time-constant (ideal 0.63)", applied); (void) corr;
          check (std::abs (applied - 0.63) < 0.08, "retune speed = time constant", b); }
        auto vibratoDepth = [&] (float hum) { PitchCorrector c; CorrectorParams p; p.retuneMs = 0; p.amount = 1; p.humanize = hum; p.transitionMs = 0; p.aggressive = true;
            double mn = 1e9, mx = -1e9; for (double t = 0; t < 2.0; t += dt) { const double inC = 30.0 * std::sin (2 * PI * 5.5 * t);
              const float r = c.process ((float) (A * std::pow (2.0, inC / 1200.0)), dt, p); if (t > 1.0) { const double o = inC + 1200.0 * std::log2 (r) ; mn = std::min (mn, o); mx = std::max (mx, o); } }
            return (mx - mn) / 60.0; };
        const double d0 = vibratoDepth (0.f), d1 = vibratoDepth (1.f);
        char b2[96]; std::snprintf (b2, sizeof b2, "vibrato kept: humanize 0 -> %.2f, humanize 1 -> %.2f", d0, d1);
        check (d0 < 0.05 && d1 > 0.85, "humanize passes natural pitch movement", b2);
        { PitchCorrector c; CorrectorParams p = hardTune(); p.allowedMask = 0b000010101011; // C major bits: C D E F G A B -> 0xAB5
          p.allowedMask = 0xAB5; float r = 1; for (int i = 0; i < 50; ++i) r = c.process ((float) (261.6256 * std::pow (2.0, 0.8 / 12.0)), dt, p);
          const double outSemi = 0.8 + 12 * std::log2 (r); check (std::abs (outSemi) < 0.01, "C major: C+0.8 semitones snaps to C (not C#)"); }
        { PitchCorrector c; CorrectorParams p = hardTune(); p.allowedMask = 0; float r = 1; for (int i = 0; i < 50; ++i) r = c.process (300.f, dt, p);
          check (std::abs (r - 1.f) < 1e-6, "empty note mask leaves pitch untouched"); }
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
