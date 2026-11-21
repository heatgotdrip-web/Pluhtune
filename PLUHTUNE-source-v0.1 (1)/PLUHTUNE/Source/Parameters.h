#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pluh
{
namespace ids
{
    inline constexpr const char* retune = "retune";
    inline constexpr const char* amount = "amount";
    inline constexpr const char* humanize = "humanize";
    inline constexpr const char* transition = "transition";
    inline constexpr const char* mode = "mode";
    inline constexpr const char* formantPreserve = "formantPreserve";
    inline constexpr const char* formantShift = "formantShift";
    inline constexpr const char* key = "key";
    inline constexpr const char* scale = "scale";
    inline constexpr const char* midiMode = "midiMode";
    inline constexpr const char* range = "range";
    inline constexpr const char* refA4 = "refA4";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* inGain = "inGain";
    inline constexpr const char* outGain = "outGain";
    inline constexpr const char* bypass = "bypass";
    inline juce::String note (int i) { return "note" + juce::String (i); }
}

inline const juce::StringArray& noteNames()
{ static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n; }

enum Scale { Chromatic = 0, Major, Minor, MajorPent, MinorPent, Custom };

inline uint16_t scaleMaskFor (int key, int scale, uint16_t customMask)
{
    static const int major[]   = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[]   = { 0, 2, 3, 5, 7, 8, 10 };
    static const int majPent[] = { 0, 2, 4, 7, 9 };
    static const int minPent[] = { 0, 3, 5, 7, 10 };
    auto build = [key] (const int* iv, int n) { uint16_t m = 0; for (int i = 0; i < n; ++i) m |= (uint16_t) (1u << ((iv[i] + key) % 12)); return m; };
    switch (scale)
    {
        case Major:     return build (major, 7);
        case Minor:     return build (minor, 7);
        case MajorPent: return build (majPent, 5);
        case MinorPent: return build (minPent, 5);
        case Custom:    return customMask;
        default:        return 0x0FFF;
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using P = AudioProcessorValueTreeState;
    P::ParameterLayout l;
    auto pid = [] (const char* s) { return ParameterID { s, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v * 100.f)) + " %"; });
    auto ms  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.f ? 1 : 0) + " ms"; });
    auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto st  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; });

    l.add (std::make_unique<AudioParameterFloat> (pid (ids::retune),   "Retune Speed", NormalisableRange<float> (0.f, 500.f, 0.1f, 0.45f), 40.f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::amount),   "Amount",       NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::humanize), "Humanize",     NormalisableRange<float> (0.f, 1.f, 0.001f), 0.2f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (ids::transition), "Transition", NormalisableRange<float> (0.f, 300.f, 0.1f, 0.5f), 25.f, ms));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::mode), "Mode", StringArray { "Natural", "Aggressive" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (pid (ids::formantPreserve), "Formant Preserve", true));
    l.add (std::make_unique<AudioParameterFloat>  (pid (ids::formantShift), "Formant Shift", NormalisableRange<float> (-12.f, 12.f, 0.01f), 0.f, st));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::key), "Key", noteNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::scale), "Scale", StringArray { "Chromatic", "Major", "Minor", "Major Pent.", "Minor Pent.", "Custom" }, 0));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::note (i), 1 }, "Note " + noteNames()[i], true));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::midiMode), "MIDI Control", StringArray { "Off", "MIDI (fallback to scale)", "MIDI (strict)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (ids::range), "Voice Range", StringArray { "Low (60 Hz+)", "Standard (80 Hz+)", "High (130 Hz+)" }, 1));
    l.add (std::make_unique<AudioParameterFloat>  (pid (ids::refA4), "Reference A4", NormalisableRange<float> (415.f, 466.f, 0.1f), 440.f));
    l.add (std::make_unique<AudioParameterFloat>  (pid (ids::mix), "Mix", NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (pid (ids::inGain), "Input Gain", NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, db));
    l.add (std::make_unique<AudioParameterFloat>  (pid (ids::outGain), "Output Gain", NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, db));
    l.add (std::make_unique<AudioParameterBool>   (pid (ids::bypass), "Bypass", false));
    return l;
}
} // namespace pluh
