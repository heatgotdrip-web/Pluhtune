#pragma once
#include "PluginProcessor.h"
#include "UI/PluhLookAndFeel.h"
#include "UI/Widgets.h"

namespace pluh::ui
{
struct Knob
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;

    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& name)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
        slider.setDoubleClickReturnValue (true, s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (11.f)));
        parent.addAndMakeVisible (slider); parent.addAndMakeVisible (label);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    }
    void setBounds (juce::Rectangle<int> r) { label.setBounds (r.removeFromTop (16)); slider.setBounds (r); }
};

struct ChoiceBox
{
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const char* id)
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id))) box.addItemList (cp->choices, 1);
        parent.addAndMakeVisible (box);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    }
};

class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (PluhtuneProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
private:
    juce::Label title, lMode, lMidi, lRange, lRef;
    ChoiceBox mode, midi, range;
    Knob transition, fShift;
    juce::ToggleButton fPreserve { "Formant Preserve" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fpAttach;
    juce::Slider refSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> refAttach;
    juce::TextButton close { "Close" };
};
}

class PluhtuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluhtuneEditor (PluhtuneProcessor&);
    ~PluhtuneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildPresetMenu();
    void stepPreset (int dir);
    void promptSavePreset();

    PluhtuneProcessor& proc;
    pluh::ui::PluhLookAndFeel laf;

    pluh::ui::PitchView pitchView { proc };
    pluh::ui::MeterBar inMeter { proc.inPeak }, outMeter { proc.outPeak };
    pluh::ui::NoteSwitches notes { proc };
    pluh::ui::Knob retune, humanize, amount, mix, inGain, outGain;
    pluh::ui::ChoiceBox key, scale;
    juce::Label lKey, lScale, lMeterIn, lMeterOut, footer;

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, advBtn { "Advanced" };
    juce::ToggleButton bypass { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    juce::Array<juce::File> userFiles;
    pluh::ui::AdvancedPanel advanced { proc };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluhtuneEditor)
};
