// PLUHTUNE - pitch detector (original implementation, no JUCE dependency)
// Cumulative-mean-normalised-difference detector (de Cheveigne & Kawahara style),
// run on a boxcar-decimated (~24 kHz) copy of the signal, then refined at full rate.
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace pluh
{
struct PitchFrame
{
    int64_t centreSample = 0;   // absolute input-sample index this estimate describes
    float   period       = 0.f; // full-rate samples (0 when unvoiced)
    float   confidence   = 0.f; // 0..1
    bool    voiced       = false;
};

class PitchDetector
{
public:
    // Allocates for 'lowestHz'; configure() may later be called with any minHz >= lowestHz without allocating.
    void prepare (double sampleRate, float lowestHz, float maxHz = 1000.f)
    {
        fs = sampleRate;
        maxFreq = maxHz;
        decim = std::max (1, (int) std::lround (fs / 24000.0)); // ~24 kHz analysis rate keeps fractional-lag error small up to ~1 kHz
        hop = std::max (64, (int) std::lround (fs * 0.0027));
        const int tauMaxAlloc = (int) std::ceil ((fs / decim) / lowestHz) + 2;
        const int frameFullAlloc = 2 * tauMaxAlloc * decim;
        int sz = 1; while (sz < frameFullAlloc + 16) sz <<= 1;
        ring.assign ((size_t) sz, 0.f); mask = sz - 1;
        dec.assign ((size_t) (2 * tauMaxAlloc), 0.f);
        d.assign ((size_t) tauMaxAlloc + 2, 0.f);
        cmnd.assign ((size_t) tauMaxAlloc + 2, 0.f);
        configure (lowestHz);
    }

    void configure (float minHz)
    {
        const double dfs = fs / decim;
        tauMax = std::min ((int) std::ceil (dfs / minHz) + 1, (int) d.size() - 2);
        tauMin = std::max (2, (int) std::floor (dfs / maxFreq));
        frameDec = 2 * tauMax;
        frameFull = frameDec * decim;
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.f);
        writePos = 0; sinceHop = 0;
    }

    int getFrameLength() const { return frameFull; }
    int getHop() const         { return hop; }

    // Push one sample. Returns true when 'out' holds a fresh estimate.
    bool push (float x, PitchFrame& out)
    {
        ring[(size_t) (writePos & mask)] = x;
        ++writePos;
        if (++sinceHop < hop) return false;
        sinceHop = 0;
        out = PitchFrame();
        out.centreSample = writePos - frameFull / 2;
        if (writePos >= frameFull) analyse (out);
        return true;
    }

private:
    float at (int64_t i) const { return ring[(size_t) (i & mask)]; }

    void analyse (PitchFrame& out)
    {
        const int64_t s0 = writePos - frameFull;

        // decimate (boxcar) + remove DC
        double mean = 0.0;
        for (int k = 0; k < frameDec; ++k)
        {
            float acc = 0.f;
            const int64_t b = s0 + (int64_t) k * decim;
            for (int i = 0; i < decim; ++i) acc += at (b + i);
            dec[(size_t) k] = acc / (float) decim;
            mean += dec[(size_t) k];
        }
        mean /= frameDec;
        double energy = 0.0;
        for (int k = 0; k < frameDec; ++k) { dec[(size_t) k] -= (float) mean; energy += dec[(size_t) k] * dec[(size_t) k]; }
        if (std::sqrt (energy / frameDec) < 3.0e-4) return; // ~ -70 dBFS: treat as silence

        // difference function + CMND
        const int W = tauMax;
        cmnd[0] = 1.f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float acc = 0.f;
            const float* p0 = dec.data(); const float* p1 = dec.data() + tau;
            for (int j = 0; j < W; ++j) { const float df = p0[j] - p1[j]; acc += df * df; }
            d[(size_t) tau] = acc;
            running += acc;
            cmnd[(size_t) tau] = running > 1e-12 ? (float) (acc * tau / running) : 1.f;
        }

        // Global minimum first, then take the *earliest* local minimum that is nearly as good.
        // (Plain "first dip under a threshold" locks onto formant-driven sub-period dips -> octave errors.)
        float gMin = 1e9f;
        for (int tau = tauMin; tau < tauMax; ++tau) gMin = std::min (gMin, cmnd[(size_t) tau]);
        if (gMin > 0.32f) return;
        const float accept = std::min (0.20f, gMin + 0.07f);
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[(size_t) tau] <= accept && cmnd[(size_t) tau] <= cmnd[(size_t) tau - 1] && cmnd[(size_t) tau] <= cmnd[(size_t) tau + 1])
            { best = tau; break; }
        if (best < 0) return;

        // parabolic interpolation (decimated domain)
        float tauDec = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const float den = a - 2.f * b + c;
            if (std::abs (den) > 1e-9f) tauDec += juce_clamp (0.5f * (a - c) / den, -1.f, 1.f);
        }
        const float conf = 1.f - cmnd[(size_t) best];

        // full-rate refinement around the coarse estimate
        const float p0 = tauDec * (float) decim;
        const int Wf = frameFull / 2 - decim - 2;
        const int pLo = std::max (2, (int) std::floor (p0) - decim - 1);
        const int pHi = (int) std::floor (p0) + decim + 1;
        float dn[64]; int nLags = std::min (pHi - pLo + 1, 64);
        double e0 = 0.0; for (int j = 0; j < Wf; ++j) e0 += (double) at (s0 + j) * at (s0 + j);
        int bi = 0; float bv = 1e9f;
        for (int k = 0; k < nLags; ++k)
        {
            const int p = pLo + k;
            double num = 0.0, e1 = 0.0;
            for (int j = 0; j < Wf; ++j) { const float u = at (s0 + j), v = at (s0 + j + p); num += (double) (u - v) * (u - v); e1 += (double) v * v; }
            dn[k] = (float) (num / (e0 + e1 + 1e-12));
            if (dn[k] < bv) { bv = dn[k]; bi = k; }
        }
        float period = p0;
        if (bi > 0 && bi < nLags - 1)
        {
            const float a = dn[bi - 1], b = dn[bi], c = dn[bi + 1], den = a - 2.f * b + c;
            period = (float) (pLo + bi) + (std::abs (den) > 1e-12f ? juce_clamp (0.5f * (a - c) / den, -1.f, 1.f) : 0.f);
        }
        else if (nLags > 0 && std::abs ((float) (pLo + bi) - p0) <= (float) decim) period = (float) (pLo + bi);

        out.period = period; out.confidence = conf; out.voiced = true;
    }

    static float juce_clamp (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    double fs = 44100.0; float maxFreq = 1000.f;
    int decim = 4, hop = 128, tauMax = 0, tauMin = 2, frameDec = 0, frameFull = 0;
    std::vector<float> ring, dec, d, cmnd;
    int mask = 0; int64_t writePos = 0; int sinceHop = 0;
};
} // namespace pluh
