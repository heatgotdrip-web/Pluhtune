// PLUHTUNE - real-time pitch engine (original implementation, no JUCE dependency)
//
// Pitch-synchronous overlap-add (PSOLA-style) shifter driven by an independent
// pitch detector and a correction stage. Because every grain is a windowed copy of
// the original waveform spanning ~2 pitch periods, the spectral envelope (formants)
// is preserved by construction; 'formant follow' / 'formant shift' are done by
// resampling the grain read-out.
//
// Everything is processed sample-by-sample, so the output does not depend on the
// host's block size. No allocation happens in process().
#pragma once
#include "PitchDetector.h"
#include "PitchCorrector.h"
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace pluh
{
struct EngineParams : CorrectorParams
{
    bool  formantPreserve    = true;
    float formantShiftSemis  = 0.f;
};

struct VizPoint { float inMidi = 0.f, outMidi = 0.f; bool voiced = false; };

class PitchEngine
{
public:
    static constexpr int kMaxCh  = 2;
    static constexpr int kVizCap = 1024;

    // Allocates everything. 'lowestHz' is the lowest range you will ever select later.
    void prepare (double sampleRate, int numChannels, float lowestHz)
    {
        fs = sampleRate;
        nCh = std::max (1, std::min (numChannels, kMaxCh));
        detector.prepare (fs, lowestHz);
        hop = detector.getHop();
        const int pMaxAlloc = (int) std::ceil (fs / lowestHz) + 4;
        int sz = 1; while (sz < 10 * pMaxAlloc + 4 * hop) sz <<= 1;
        mask = sz - 1;
        for (int c = 0; c < kMaxCh; ++c) { inRing[(size_t) c].assign ((size_t) sz, 0.f); outRing[(size_t) c].assign ((size_t) sz, 0.f); }
        monoRing.assign ((size_t) sz, 0.f);
        viz.assign ((size_t) kVizCap, VizPoint());
        vAlpha = 1.f - std::exp (-1.f / (0.004f * (float) fs));
        wAbs = 0;
        setMinFrequency (lowestHz);
    }

    // Changes the latency/range without allocating (causes a short glitch).
    void setMinFrequency (float hz)
    {
        detector.configure (hz);
        pMax = (int) std::ceil (fs / hz) + 2;
        latency = 2 * pMax + hop + 16;
        clearState();
    }

    void reset() { detector.reset(); clearState(); }

    int getLatency() const { return latency; }

    // wet/dry: output arrays (dry = input delayed by getLatency(), for latency-aligned mixing)
    void process (const float* const* in, float* const* wet, float* const* dry, int num, const EngineParams& ep)
    {
        params = ep;
        formantMul = std::exp2 (ep.formantShiftSemis / 12.f);
        for (int i = 0; i < num; ++i)
        {
            float mono = 0.f;
            for (int c = 0; c < nCh; ++c) { const float x = in[c][i]; inRing[(size_t) c][(size_t) (wAbs & mask)] = x; mono += x; }
            mono /= (float) nCh;
            monoRing[(size_t) (wAbs & mask)] = mono;
            ++wAbs;
            const int64_t n = wAbs - 1;

            PitchFrame f;
            if (detector.push (mono, f)) { recs[(size_t) (recCount & 255)] = f; ++recCount; }

            while (nextSynth <= (double) (n + pMax)) scheduleGrain();

            if ((n & 31) == 0) vTarget = lookup (n - latency).voiced ? 1.f : 0.f;
            vGain += (vTarget - vGain) * vAlpha;

            for (int c = 0; c < nCh; ++c)
            {
                const size_t idx = (size_t) (n & mask);
                const float w = outRing[(size_t) c][idx];
                outRing[(size_t) c][idx] = 0.f;
                const float dr = inRing[(size_t) c][(size_t) ((n - latency) & mask)];
                dry[c][i] = dr;
                wet[c][i] = vGain * w + (1.f - vGain) * dr;
            }
        }
    }

    const VizPoint* getViz (int& count) const { count = vizCount; return viz.data(); }
    void clearViz() { vizCount = 0; }

private:
    void clearState()
    {
        for (int c = 0; c < kMaxCh; ++c) std::fill (outRing[(size_t) c].begin(), outRing[(size_t) c].end(), 0.f);
        nMarks = 0; recCount = 0; nextSynth = (double) wAbs; vGain = 0.f; vTarget = 0.f;
        lastVoicedPeriod = (float) (fs / 150.0); vizAccum = 0.0; vizCount = 0;
        corrector.reset();
    }

    const PitchFrame& lookup (int64_t a) const
    {
        const int64_t cnt = std::min<int64_t> (recCount, 256);
        for (int64_t k = 0; k < cnt; ++k)
        {
            const PitchFrame& r = recs[(size_t) ((recCount - 1 - k) & 255)];
            if (r.centreSample <= a) return r;
        }
        return noFrame;
    }

    float mono (int64_t i) const { return monoRing[(size_t) (i & mask)]; }

    void pushMark (int64_t p) { marks[(size_t) (nMarks & 63)] = p; ++nMarks; lastMark = p; }

    void addMark (float P)
    {
        const int Pi = std::max (8, (int) std::lround (P));
        const int half = Pi / 2, Q = std::max (1, Pi / 4), step = Pi > 256 ? 2 : 1;
        const int64_t cand = lastMark + Pi;
        auto score = [&] (int s)
        {
            double num = 0.0, en = 1e-9;
            for (int k = -half; k < half; k += step) { const float r = mono (lastMark + k), x = mono (cand + s + k); num += (double) r * x; en += (double) x * x; }
            return num / std::sqrt (en);
        };
        int bestS = 0; double best = score (0);
        for (int s = -Q; s <= Q; ++s) { if (s == 0) continue; const double sc = score (s); if (sc > best + 1e-9) { best = sc; bestS = s; } }
        pushMark (cand + bestS);
    }

    void ensureMarks (int64_t a, float P)
    {
        if (nMarks == 0) pushMark (a);
        int guard = 0;
        while ((double) lastMark + 1.25 * P <= (double) a) { if (++guard > 64) { pushMark (a); break; } addMark (P); }
    }

    int64_t nearestMark (int64_t a) const
    {
        int64_t best = marks[(size_t) ((nMarks - 1) & 63)];
        int64_t bd = std::llabs (best - a);
        for (int64_t k = 2; k <= std::min<int64_t> (nMarks, 64); ++k)
        {
            const int64_t m = marks[(size_t) ((nMarks - k) & 63)], dd = std::llabs (m - a);
            if (dd < bd) { bd = dd; best = m; } else break;
        }
        return best;
    }

    void addGrain (int64_t c, double m, float Hg, float gain, double beta)
    {
        const int64_t mi = (int64_t) std::floor (m);
        const double frac = m - (double) mi;
        const int H = (int) std::ceil (Hg) + 1;
        const double lim = (double) (wAbs - 2);
        for (int j = -H; j <= H; ++j)
        {
            const double u = (double) j - frac;
            if (std::abs (u) >= (double) Hg) continue;
            const float w = gain * 0.5f * (1.f + (float) std::cos (3.14159265358979 * u / (double) Hg));
            double pos = (double) c + u * beta; if (pos > lim) pos = lim;
            const int64_t p0 = (int64_t) std::floor (pos); const float fr = (float) (pos - (double) p0);
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float* x = inRing[(size_t) ch].data();
                const float s = x[p0 & mask] * (1.f - fr) + x[(p0 + 1) & mask] * fr;
                outRing[(size_t) ch][(size_t) ((mi + j) & mask)] += w * s;
            }
        }
    }

    void scheduleGrain()
    {
        const double m = nextSynth;
        const int64_t a = (int64_t) std::llround (m) - latency;
        const PitchFrame& rec = lookup (a);
        const bool voiced = rec.voiced && rec.period > 1.f;
        float Pin = voiced ? rec.period : lastVoicedPeriod;
        Pin = std::max (8.f, std::min ((float) pMax, Pin));
        if (voiced) lastVoicedPeriod = Pin;

        ensureMarks (a, Pin);
        const int64_t c = nearestMark (a);

        float ratio = 1.f;
        const double dt = (double) Pin / fs;
        if (voiced) ratio = corrector.process ((float) (fs / (double) Pin), dt, params); else corrector.idle (dt);
        ratio = std::max (0.5f, std::min (2.f, ratio));

        const double Ps = (double) Pin / ratio;
        double beta = (params.formantPreserve ? 1.0 : (double) ratio) * (double) formantMul;
        beta = std::max (0.6, std::min (1.7, beta));
        addGrain (c, m, Pin, (float) (Ps / Pin), beta);
        nextSynth += Ps;

        vizAccum += Ps;
        if (vizAccum >= 256.0 && vizCount < kVizCap)
        {
            vizAccum = 0.0;
            VizPoint& v = viz[(size_t) vizCount++];
            v.voiced = voiced; v.inMidi = corrector.getLastInMidi(); v.outMidi = corrector.getLastOutMidi();
        }
    }

    double fs = 44100.0;
    int nCh = 1, hop = 128, pMax = 600, latency = 1300, mask = 0;
    int64_t wAbs = 0;
    PitchDetector detector;
    PitchCorrector corrector;
    EngineParams params;
    float formantMul = 1.f;

    std::array<std::vector<float>, kMaxCh> inRing, outRing;
    std::vector<float> monoRing;

    std::array<PitchFrame, 256> recs {};
    int64_t recCount = 0;
    PitchFrame noFrame;

    std::array<int64_t, 64> marks {};
    int64_t nMarks = 0, lastMark = 0;
    double nextSynth = 0.0;
    float lastVoicedPeriod = 300.f;

    float vGain = 0.f, vTarget = 0.f, vAlpha = 0.01f;

    std::vector<VizPoint> viz;
    int vizCount = 0; double vizAccum = 0.0;
};
} // namespace pluh
