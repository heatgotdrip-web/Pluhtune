#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pluh::ui
{
const juce::Colour bg       { 0xff0a0c11 };
const juce::Colour panel    { 0xff121620 };
const juce::Colour panel2   { 0xff1a1f2c };
const juce::Colour outline  { 0xff262c3c };
const juce::Colour text     { 0xffd7dcef };
const juce::Colour textDim  { 0xff7c85a3 };
const juce::Colour violet   { 0xff8a6bff };
const juce::Colour mint     { 0xff3df0c0 };
const juce::Colour pink     { 0xffff4d9a };

class PluhLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PluhLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, textDim);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::arrowColourId, violet);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, violet);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, bg);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, outline);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (d, d);
        const float rad = d * 0.5f, arcW = 4.f, cx = r.getCentreX(), cy = r.getCentreY();
        const juce::PathStrokeType st (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track; track.addCentredArc (cx, cy, rad - arcW, rad - arcW, 0.f, a0, a1, true);
        g.setColour (outline); g.strokePath (track, st);

        const float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc (cx, cy, rad - arcW, rad - arcW, 0.f, a0, ang, true);
        g.setGradientFill (juce::ColourGradient (violet, r.getX(), r.getBottom(), mint, r.getRight(), r.getY(), false));
        g.strokePath (val, st);

        g.setColour (panel2); g.fillEllipse (r.reduced (arcW * 2.4f));
        juce::Path p; p.addRoundedRectangle (-1.6f, -(rad - arcW * 2.8f), 3.2f, rad * 0.36f, 1.6f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (juce::Colours::white); g.fillPath (p);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        const bool on = b.getToggleState();
        const auto accent = b.getName() == "bypass" ? pink : violet;
        g.setColour (on ? accent : (hover ? panel2.brighter (0.15f) : panel2));
        g.fillRoundedRectangle (r, 7.f);
        g.setColour (on ? accent.brighter (0.3f) : outline); g.drawRoundedRectangle (r, 7.f, 1.f);
        g.setColour (on ? juce::Colours::white : textDim);
        g.setFont (juce::Font (juce::FontOptions (13.f)));
        g.drawText (b.getButtonText(), r, juce::Justification::centred);
    }
};
}
