#include "Presets.h"
#include "Parameters.h"

namespace pluh
{
namespace
{
    // Factory presets set the *character* of the tuning. Key, scale, gains and bypass are left alone
    // because they belong to the song, not the sound.
    struct Factory { const char* name; float retune, amount, humanize, transition; int mode; bool fPreserve; float fShift; float mix; };
    const Factory kFactory[] =
    {
        { "Natural",           70.f, 0.85f, 0.55f,  40.f, 0, true,   0.f, 1.f },
        { "Hard Tune",          0.f, 1.00f, 0.00f,   0.f, 1, true,   0.f, 1.f },
        { "Melodic Rap",       18.f, 1.00f, 0.15f,  25.f, 0, true,   0.f, 1.f },
        { "Underground",        8.f, 1.00f, 0.05f,  10.f, 1, true,  -2.f, 1.f },
        { "Hyperpop",           0.f, 1.00f, 0.00f,   0.f, 1, false,  3.f, 1.f },
        { "Smooth Correction",120.f, 0.90f, 0.60f,  90.f, 0, true,   0.f, 1.f },
        { "Fast Retune",       10.f, 1.00f, 0.20f,  15.f, 0, true,   0.f, 1.f },
        { "Transparent",      150.f, 0.60f, 0.80f,  60.f, 0, true,   0.f, 1.f },
    };
}

const juce::StringArray& PresetManager::factoryNames()
{
    static const juce::StringArray n = [] { juce::StringArray a; for (auto& f : kFactory) a.add (f.name); return a; }();
    return n;
}

void PresetManager::setParam (const char* id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void PresetManager::loadFactory (int i)
{
    if (i < 0 || i >= (int) std::size (kFactory)) return;
    const auto& f = kFactory[i];
    setParam (ids::retune, f.retune);       setParam (ids::amount, f.amount);
    setParam (ids::humanize, f.humanize);   setParam (ids::transition, f.transition);
    setParam (ids::mode, (float) f.mode);   setParam (ids::formantPreserve, f.fPreserve ? 1.f : 0.f);
    setParam (ids::formantShift, f.fShift); setParam (ids::mix, f.mix);
    apvts.state.setProperty ("presetName", f.name, nullptr);
}

juce::File PresetManager::getUserDirectory() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PLUHTUNE").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

juce::Array<juce::File> PresetManager::getUserPresets() const
{
    auto files = getUserDirectory().findChildFiles (juce::File::findFiles, false, "*.pluhpreset");
    files.sort();
    return files;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) return false;
    apvts.state.setProperty ("presetName", name, nullptr);
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return false;
    return getUserDirectory().getChildFile (juce::File::createLegalFileName (name) + ".pluhpreset").replaceWithText (xml->toString());
}

bool PresetManager::loadUser (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    const bool wasBypassed = apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    setParam (ids::bypass, wasBypassed ? 1.f : 0.f);
    apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    return true;
}
}
