#include "PluginEditor.h"
using namespace pluh;
using namespace pluh::ui;

// ============================================================ Advanced panel
AdvancedPanel::AdvancedPanel (PluhtuneProcessor& p)
{
    auto& s = p.apvts;
    title.setText ("ADVANCED", juce::dontSendNotification); title.setFont (juce::Font (juce::FontOptions (13.f).withStyle ("Bold"))); title.setColour (juce::Label::textColourId, violet);
    auto cap = [this] (juce::Label& l, const char* t) { l.setText (t, juce::dontSendNotification); l.setFont (juce::Font (juce::FontOptions (11.f))); addAndMakeVisible (l); };
    addAndMakeVisible (title);
    cap (lMode, "TUNING MODE"); cap (lMidi, "MIDI CONTROL"); cap (lRange, "VOICE RANGE (sets latency)"); cap (lRef, "REFERENCE A4");
    mode.init (*this, s, ids::mode); midi.init (*this, s, ids::midiMode); range.init (*this, s, ids::range);
    transition.init (*this, s, ids::transition, "Transition"); fShift.init (*this, s, ids::formantShift, "Formant Shift");
    addAndMakeVisible (fPreserve);
    fpAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ids::formantPreserve, fPreserve);
    refSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    addAndMakeVisible (refSlider);
    refAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, ids::refA4, refSlider);
    addAndMakeVisible (close); close.onClick = [this] { if (onClose) onClose(); };
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    g.setColour (panel.withAlpha (0.97f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.f);
    g.setColour (violet.withAlpha (0.6f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.f, 1.5f);
}

void AdvancedPanel::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto top = r.removeFromTop (24); title.setBounds (top.removeFromLeft (200)); close.setBounds (top.removeFromRight (70));
    r.removeFromTop (10);
    auto col1 = r.removeFromLeft (250); r.removeFromLeft (20); auto col2 = r.removeFromLeft (250); r.removeFromLeft (20); auto col3 = r;
    auto place = [] (juce::Rectangle<int>& c, juce::Label& l, juce::Component& comp) { l.setBounds (c.removeFromTop (16)); comp.setBounds (c.removeFromTop (28)); c.removeFromTop (14); };
    place (col1, lMode, mode.box); place (col1, lMidi, midi.box); place (col1, lRange, range.box);
    lRef.setBounds (col2.removeFromTop (16)); refSlider.setBounds (col2.removeFromTop (28)); col2.removeFromTop (14);
    fPreserve.setBounds (col2.removeFromTop (30));
    transition.setBounds (col3.removeFromLeft (120)); fShift.setBounds (col3.removeFromLeft (120));
}

// ============================================================ Editor
PluhtuneEditor::PluhtuneEditor (PluhtuneProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    auto& s = proc.apvts;

    addAndMakeVisible (pitchView); addAndMakeVisible (inMeter); addAndMakeVisible (outMeter); addAndMakeVisible (notes);
    retune.init (*this, s, ids::retune, "Retune Speed"); humanize.init (*this, s, ids::humanize, "Humanize"); amount.init (*this, s, ids::amount, "Amount");
    mix.init (*this, s, ids::mix, "Mix"); inGain.init (*this, s, ids::inGain, "Input"); outGain.init (*this, s, ids::outGain, "Output");
    key.init (*this, s, ids::key); scale.init (*this, s, ids::scale);

    auto cap = [this] (juce::Label& l, const char* t, juce::Justification j = juce::Justification::centredLeft)
    { l.setText (t, juce::dontSendNotification); l.setJustificationType (j); l.setFont (juce::Font (juce::FontOptions (11.f))); addAndMakeVisible (l); };
    cap (lKey, "KEY"); cap (lScale, "SCALE"); cap (lMeterIn, "IN", juce::Justification::centred); cap (lMeterOut, "OUT", juce::Justification::centred);
    footer.setJustificationType (juce::Justification::centredRight); footer.setFont (juce::Font (juce::FontOptions (11.f))); addAndMakeVisible (footer);

    bypass.setName ("bypass"); addAndMakeVisible (bypass);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ids::bypass, bypass);

    addAndMakeVisible (presetBox); addAndMakeVisible (prevBtn); addAndMakeVisible (nextBtn); addAndMakeVisible (saveBtn); addAndMakeVisible (advBtn);
    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id < 100) proc.presets.loadFactory (id - 1);
        else if (id >= 100 && id - 100 < userFiles.size()) proc.presets.loadUser (userFiles[id - 100]);
    };
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (+1); };
    saveBtn.onClick = [this] { promptSavePreset(); };
    advBtn.onClick  = [this] { advanced.setVisible (! advanced.isVisible()); };
    advanced.onClose = [this] { advanced.setVisible (false); };
    addChildComponent (advanced);
    rebuildPresetMenu();

    setSize (960, 600);
    startTimerHz (4); timerCallback();
}

PluhtuneEditor::~PluhtuneEditor() { setLookAndFeel (nullptr); }

void PluhtuneEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    const auto& names = PresetManager::factoryNames();
    for (int i = 0; i < names.size(); ++i) presetBox.addItem (names[i], i + 1);
    userFiles = proc.presets.getUserPresets();
    if (userFiles.size() > 0) { presetBox.addSeparator(); presetBox.addSectionHeading ("User"); }
    for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), 100 + i);
    presetBox.setText (proc.presets.getCurrentName(), juce::dontSendNotification);
}

void PluhtuneEditor::stepPreset (int dir)
{
    juce::Array<int> idsInOrder;
    for (int i = 0; i < presetBox.getNumItems(); ++i) { const int id = presetBox.getItemId (i); if (id > 0) idsInOrder.add (id); }
    if (idsInOrder.isEmpty()) return;
    int idx = idsInOrder.indexOf (presetBox.getSelectedId());
    idx = idx < 0 ? 0 : (idx + dir + idsInOrder.size()) % idsInOrder.size();
    presetBox.setSelectedId (idsInOrder[idx], juce::sendNotificationSync);
}

void PluhtuneEditor::promptSavePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets.getCurrentName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PluhtuneEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            const auto name = w->getTextEditorContents ("name");
            if (safe->proc.presets.saveUser (name)) safe->rebuildPresetMenu();
        }
    }), true);
}

void PluhtuneEditor::timerCallback()
{
    footer.setText ("v0.1  |  latency " + juce::String (proc.getLatencyMs(), 1) + " ms  |  " + proc.presets.getCurrentName(), juce::dontSendNotification);
}

void PluhtuneEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.10f), 0.f, 0.f, juce::Colours::transparentBlack, 0.f, 120.f, false));
    g.fillRect (0, 0, getWidth(), 120);

    // brand
    g.setFont (juce::Font (juce::FontOptions (30.f).withStyle ("Bold")));
    g.setColour (juce::Colours::white); g.drawText ("PLUH", 22, 10, 90, 40, juce::Justification::centredLeft);
    g.setColour (mint);                 g.drawText ("TUNE", 98, 10, 90, 40, juce::Justification::centredLeft);
    g.setColour (textDim); g.setFont (10.f); g.drawText ("VOCAL PITCH CORRECTION", 24, 44, 200, 12, juce::Justification::centredLeft);

    g.setColour (outline); g.drawHorizontalLine (66, 20.f, (float) getWidth() - 20.f);
}

void PluhtuneEditor::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (66).reduced (20, 14);
    advBtn.setBounds (head.removeFromRight (90)); head.removeFromRight (8);
    bypass.setBounds (head.removeFromRight (80)); head.removeFromRight (20);
    saveBtn.setBounds (head.removeFromRight (60)); head.removeFromRight (6);
    nextBtn.setBounds (head.removeFromRight (34)); head.removeFromRight (4);
    presetBox.setBounds (head.removeFromRight (220)); head.removeFromRight (4);
    prevBtn.setBounds (head.removeFromRight (34));

    auto body = r.reduced (20, 8);
    auto view = body.removeFromTop (290);
    auto metersCol = view.removeFromRight (70); view.removeFromRight (10);
    pitchView.setBounds (view);
    auto mi = metersCol.withTrimmedTop (0);
    lMeterIn.setBounds (mi.removeFromTop (14).removeFromLeft (30)); lMeterOut.setBounds (juce::Rectangle<int> (metersCol.getX() + 38, metersCol.getY(), 30, 14));
    inMeter.setBounds (mi.removeFromLeft (30).reduced (0, 2)); mi.removeFromLeft (8); outMeter.setBounds (mi.removeFromLeft (30).reduced (0, 2));

    body.removeFromTop (10);
    auto controls = body.removeFromTop (118);
    auto selectors = controls.removeFromRight (250); controls.removeFromRight (10);
    const int kw = controls.getWidth() / 6;
    for (auto* k : { &retune, &humanize, &amount, &mix, &inGain, &outGain }) k->setBounds (controls.removeFromLeft (kw));
    selectors.removeFromTop (6);
    lKey.setBounds (selectors.removeFromTop (14)); key.box.setBounds (selectors.removeFromTop (28)); selectors.removeFromTop (8);
    lScale.setBounds (selectors.removeFromTop (14)); scale.box.setBounds (selectors.removeFromTop (28));

    body.removeFromTop (8);
    notes.setBounds (body.removeFromTop (44));
    footer.setBounds (body.removeFromBottom (20));
    advanced.setBounds (getLocalBounds().withSizeKeepingCentre (760, 210).withY (150));
}
