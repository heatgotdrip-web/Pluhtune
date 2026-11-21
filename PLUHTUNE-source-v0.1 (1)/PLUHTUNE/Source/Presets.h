#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pluh
{
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    static const juce::StringArray& factoryNames();
    void loadFactory (int index);

    juce::File getUserDirectory() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUser (const juce::String& name);
    bool loadUser (const juce::File& file);

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Natural").toString(); }

private:
    void setParam (const char* id, float plainValue);
    juce::AudioProcessorValueTreeState& apvts;
};
}
