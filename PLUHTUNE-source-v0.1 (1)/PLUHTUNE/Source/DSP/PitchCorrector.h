// PLUHTUNE - pitch correction logic: note selection, glide, retune, humanize.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace pluh
{
struct CorrectorParams
{
    float    retuneMs     = 40.f;   // time constant of the correction (0 = instant)
    float    amount       = 1.f;    // 0..1
    float    humanize     = 0.2f;   // 0..1
    float    transitionMs = 25.f;   // glide time between target notes
    bool     aggressive   = false;
    float    refA4        = 440.f;
    uint16_t allowedMask  = 0x0FFF; // bit 0 = C ... bit 11 = B (absolute pitch classes)
};

class PitchCorrector
{
public:
    void reset()
    {
        haveNote = false; lpInit = false; c = 0.f; smoothedTarget = 0.f; curNote = 0;
        lastInMidi = lastOutMidi = 60.f;
    }

    // Voiced input. Returns frequency ratio (output/input).
    float process (float inHz, double dt, const CorrectorParams& p)
    {
        const float inS = 69.f + 12.f * std::log2 (inHz / p.refA4);
        lastInMidi = inS;
        if (p.allowedMask == 0) { idle (dt); lastOutMidi = inS + c; return std::exp2 (c / 12.f); }

        // --- target note with hysteresis (natural mode only) ---
        int cand = nearestAllowed (inS, p.allowedMask);
        bool changed = false;
        if (!haveNote || !((p.allowedMask >> pc (curNote)) & 1)) { curNote = cand; changed = true; if (!haveNote) smoothedTarget = (float) cand; haveNote = true; }
        else if (cand != curNote)
        {
            const float hyst = p.aggressive ? 0.f : 0.2f;
            if (std::abs (inS - (float) curNote) > std::abs (inS - (float) cand) + hyst) { curNote = cand; changed = true; }
        }
        if (changed || !lpInit) { lp = inS; lpInit = true; }

        // slow reference for humanize (vibrato / drift pass-through)
        lp += (1.f - std::exp (-(float) dt / 0.12f)) * (inS - lp);

        // glide between target notes
        const float tT = p.transitionMs / 3000.f;
        smoothedTarget += (tT < 1e-5f ? 1.f : 1.f - std::exp (-(float) dt / tT)) * ((float) curNote - smoothedTarget);

        const float h = p.humanize;
        const float ref = inS - h * (inS - lp);
        const float cTarget = p.amount * (smoothedTarget - ref);

        // retune speed
        float tauR = p.retuneMs * 0.001f * (p.aggressive ? 0.25f : 1.f) * (1.f + 1.5f * h);
        const float aR = tauR < 1e-4f ? 1.f : 1.f - std::exp (-(float) dt / tauR);
        c += aR * (cTarget - c);
        c = std::max (-12.f, std::min (12.f, c));
        lastOutMidi = inS + c;
        return std::exp2 (c / 12.f);
    }

    // Unvoiced / no pitch: relax correction and forget the slow reference.
    void idle (double dt)
    {
        c *= std::exp (-(float) dt / 0.05f);
        lpInit = false;
    }

    float getLastInMidi() const  { return lastInMidi; }
    float getLastOutMidi() const { return lastOutMidi; }

private:
    static int pc (int n) { return ((n % 12) + 12) % 12; }
    static int nearestAllowed (float s, uint16_t mask)
    {
        const int r = (int) std::lround (s);
        int best = r; float bd = 1e9f;
        for (int n = r - 12; n <= r + 12; ++n)
            if ((mask >> pc (n)) & 1) { const float dd = std::abs (s - (float) n); if (dd < bd) { bd = dd; best = n; } }
        return best;
    }

    bool haveNote = false, lpInit = false;
    int curNote = 0;
    float c = 0.f, lp = 0.f, smoothedTarget = 0.f, lastInMidi = 60.f, lastOutMidi = 60.f;
};
} // namespace pluh
