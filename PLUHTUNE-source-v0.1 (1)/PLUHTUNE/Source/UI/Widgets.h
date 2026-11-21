#pragma once
#include <deque>
#include <functional>
#include "PluginProcessor.h"
#include "PluhLookAndFeel.h"

namespace pluh::ui
{
// ---------------------------------------------------------------- level meter
class MeterBar : public juce::Component, private juce::Timer
{
public:
    explicit MeterBar (std::atomic<float>& src) : source (src) { startTimerHz (30); }
    void timerCallback() override
    {
        const float v = source.exchange (0.f);
        level = juce::jmax (v, level * 0.86f);
        if (v >= hold) { hold = v; holdFrames = 45; } else if (--holdFrames < 0) hold *= 0.9f;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (panel); g.fillRoundedRectangle (r, 4.f);
        auto pos = [&] (float lin) { return juce::jmap (juce::Decibels::gainToDecibels (lin, -60.f), -60.f, 0.f, 0.f, 1.f); };
        auto bar = r.reduced (3.f);
        const float h = bar.getHeight() * pos (level);
        g.setGradientFill (juce::ColourGradient (mint, 0, bar.getBottom(), pink, 0, bar.getY(), false));
        g.fillRoundedRectangle (bar.removeFromBottom (h), 2.f);
        const float hy = r.reduced (3.f).getBottom() - r.reduced (3.f).getHeight() * pos (hold);
        g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (r.getX() + 3.f, hy, r.getWidth() - 6.f, 1.5f);
    }
private:
    std::atomic<float>& source; float level = 0.f, hold = 0.f; int holdFrames = 0;
};

// ---------------------------------------------------------------- pitch display
class PitchView : public juce::Component, private juce::Timer
{
public:
    explicit PitchView (PluhtuneProcessor& p) : proc (p) { drain(); trace.clear(); startTimerHz (30); }

    void timerCallback() override
    {
        drain();
        float sum = 0.f; int n = 0;
        for (auto it = trace.rbegin(); it != trace.rend() && n < 60; ++it) if (it->voiced) { sum += it->inMidi; ++n; }
        if (n > 10) centre += 0.04f * (juce::jlimit (36.f, 96.f, sum / (float) n) - centre);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel); g.fillRoundedRectangle (b, 10.f);
        g.saveState(); juce::Path clip; clip.addRoundedRectangle (b, 10.f); g.reduceClipRegion (clip);

        const float keyW = 44.f, span = 28.f, lo = centre - span / 2.f;
        const auto plot = b.withTrimmedLeft (keyW);
        auto yOf = [&] (float m) { return b.getBottom() - (m - lo) / span * b.getHeight(); };
        const uint16_t mask = proc.getTargetMask();
        static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };

        for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (lo + span) + 1; ++n)
        {
            const int pc = ((n % 12) + 12) % 12; const bool allowed = ((mask >> pc) & 1) != 0;
            const float y0 = yOf ((float) n + 0.5f), y1 = yOf ((float) n - 0.5f);
            g.setColour (allowed ? mint.withAlpha (black[pc] ? 0.10f : 0.15f) : juce::Colours::white.withAlpha (black[pc] ? 0.00f : 0.025f));
            g.fillRect (plot.getX(), y0, plot.getWidth(), y1 - y0);
            if (allowed) { g.setColour (mint.withAlpha (0.35f)); g.fillRect (plot.getX(), (y0 + y1) * 0.5f - 0.5f, plot.getWidth(), 1.f); }
            g.setColour (black[pc] ? juce::Colour (0xff0d1018) : juce::Colour (0xff202638));
            g.fillRect (b.getX(), y0 + 0.5f, keyW - 2.f, y1 - y0 - 1.f);
            g.setColour (allowed ? mint : textDim); g.setFont (9.5f);
            g.drawText (noteNames()[pc] + juce::String (n / 12 - 1), juce::Rectangle<float> (b.getX() + 4.f, y0, keyW - 8.f, y1 - y0), juce::Justification::centredLeft);
        }

        const int N = (int) trace.size(); const float dx = plot.getWidth() / (float) kTrace;
        auto draw = [&] (bool out, juce::Colour c, float thick)
        {
            juce::Path p; bool pen = false;
            for (int i = 0; i < N; ++i)
            {
                const auto& v = trace[(size_t) i];
                if (! v.voiced) { pen = false; continue; }
                const float x = plot.getRight() - (float) (N - 1 - i) * dx, y = yOf (out ? v.outMidi : v.inMidi);
                if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
            }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        draw (false, violet.withAlpha (0.85f), 1.6f);
        draw (true, pink, 2.6f);

        g.setFont (11.f);
        g.setColour (violet); g.drawText ("IN", plot.getRight() - 90.f, b.getY() + 8.f, 30.f, 14.f, juce::Justification::centredRight);
        g.setColour (pink);   g.drawText ("OUT", plot.getRight() - 50.f, b.getY() + 8.f, 36.f, 14.f, juce::Justification::centredRight);
        g.restoreState();
        g.setColour (outline); g.drawRoundedRectangle (b.reduced (0.5f), 10.f, 1.f);
    }

private:
    static constexpr int kTrace = 640;
    void drain()
    {
        int ready = proc.vizFifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2; proc.vizFifo.prepareToRead (ready, s1, n1, s2, n2);
        for (int k = 0; k < n1; ++k) trace.push_back (proc.vizData[(size_t) (s1 + k)]);
        for (int k = 0; k < n2; ++k) trace.push_back (proc.vizData[(size_t) (s2 + k)]);
        proc.vizFifo.finishedRead (n1 + n2);
        while ((int) trace.size() > kTrace) trace.pop_front();
    }
    PluhtuneProcessor& proc; std::deque<pluh::VizPoint> trace; float centre = 60.f;
};

// ---------------------------------------------------------------- 12 note switches
class NoteSwitches : public juce::Component, private juce::Timer
{
public:
    explicit NoteSwitches (PluhtuneProcessor& p) : proc (p) { startTimerHz (20); }
    void timerCallback() override { const auto m = proc.getTargetMask(); if (m != lastMask) { lastMask = m; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
        const float w = (float) getWidth() / 12.f;
        for (int i = 0; i < 12; ++i)
        {
            auto r = juce::Rectangle<float> (w * (float) i, 0.f, w, (float) getHeight()).reduced (2.f);
            const bool on = ((lastMask >> i) & 1) != 0;
            g.setColour (on ? (black[i] ? violet : mint) : panel2); g.fillRoundedRectangle (r, 6.f);
            g.setColour (on ? juce::Colours::transparentBlack : outline); g.drawRoundedRectangle (r, 6.f, 1.f);
            g.setColour (on ? juce::Colours::black : textDim); g.setFont (juce::Font (juce::FontOptions (13.f)));
            g.drawText (noteNames()[i], r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 11, (int) (e.position.x / ((float) getWidth() / 12.f)));
        proc.toggleNote (i); lastMask = proc.getTargetMask(); repaint();
    }
private:
    PluhtuneProcessor& proc; uint16_t lastMask = 0xFFFF;
};
}
