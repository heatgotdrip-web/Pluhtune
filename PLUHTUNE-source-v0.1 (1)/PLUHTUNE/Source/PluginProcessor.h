#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Parameters.h"
#include "Presets.h"
#include "PitchEngine.h"

class PluhtuneProcessor : public juce::AudioProcessor
{
public:
    PluhtuneProcessor();

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); } // bypass is handled (latency-aligned) inside processBlock
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PLUHTUNE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (pluh::ids::bypass); }

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- shared with the editor (message thread) ----
    uint16_t getTargetMask() const;                // notes the tuner is currently allowed to land on (scale or MIDI)
    void toggleNote (int pitchClass);              // switches to Custom scale if needed
    double getLatencyMs() const { return sampleRateHz > 0 ? 1000.0 * getLatencySamples() / sampleRateHz : 0.0; }

    juce::AudioProcessorValueTreeState apvts;
    pluh::PresetManager presets { apvts };

    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };
    static constexpr int kVizFifoSize = 4096;
    juce::AbstractFifo vizFifo { kVizFifoSize };
    std::array<pluh::VizPoint, kVizFifoSize> vizData;

private:
    uint16_t scaleMaskFromParams() const;

    pluh::PitchEngine engine;
    double sampleRateHz = 0.0;
    int currentRange = -1, maxChunk = 512, activeChannels = 2;

    std::atomic<float> *pRetune, *pAmount, *pHumanize, *pTransition, *pMode, *pFPreserve, *pFShift, *pKey, *pScale,
                       *pMidiMode, *pRange, *pRefA4, *pMix, *pIn, *pOut, *pBypass;
    std::array<std::atomic<float>*, 12> pNote {};

    std::array<int, 128> noteCount {};
    std::atomic<uint16_t> heldMask { 0 };

    std::vector<float> inBuf[2], wetBuf[2], dryBuf[2], rawDelayed[2], rawRing[2];
    int rawMask = 0; int64_t rawPos = 0;
    juce::SmoothedValue<float> inGainSm, outGainSm, mixSm, bypassSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluhtuneProcessor)
};
