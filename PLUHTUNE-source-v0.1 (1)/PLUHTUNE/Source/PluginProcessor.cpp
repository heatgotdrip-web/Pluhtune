#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pluh;

namespace { constexpr int kChunk = 2048; constexpr float kRangeHz[] = { 60.f, 80.f, 130.f }; }

PluhtuneProcessor::PluhtuneProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PLUHTUNE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pRetune = get (ids::retune); pAmount = get (ids::amount); pHumanize = get (ids::humanize); pTransition = get (ids::transition);
    pMode = get (ids::mode); pFPreserve = get (ids::formantPreserve); pFShift = get (ids::formantShift);
    pKey = get (ids::key); pScale = get (ids::scale); pMidiMode = get (ids::midiMode); pRange = get (ids::range);
    pRefA4 = get (ids::refA4); pMix = get (ids::mix); pIn = get (ids::inGain); pOut = get (ids::outGain); pBypass = get (ids::bypass);
    for (int i = 0; i < 12; ++i) pNote[(size_t) i] = apvts.getRawParameterValue (ids::note (i));
    presets.loadFactory (0);
}

bool PluhtuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void PluhtuneProcessor::prepareToPlay (double sr, int)
{
    sampleRateHz = sr;
    activeChannels = juce::jlimit (1, 2, getTotalNumInputChannels());
    engine.prepare (sr, activeChannels, kRangeHz[0]);
    currentRange = -1;

    for (int c = 0; c < 2; ++c) { inBuf[c].assign (kChunk, 0.f); wetBuf[c].assign (kChunk, 0.f); dryBuf[c].assign (kChunk, 0.f); rawDelayed[c].assign (kChunk, 0.f); }
    int sz = 1; const int maxLat = 2 * (int) std::ceil (sr / kRangeHz[0]) + (int) std::lround (sr * 0.0027) * 2 + 512;
    while (sz < maxLat * 2) sz <<= 1;
    for (int c = 0; c < 2; ++c) rawRing[c].assign ((size_t) sz, 0.f);
    rawMask = sz - 1; rawPos = 0;

    const double ramp = 0.02;
    inGainSm.reset (sr, ramp);  inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGainSm.reset (sr, ramp); outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    mixSm.reset (sr, ramp);     mixSm.setCurrentAndTargetValue (pMix->load());
    bypassSm.reset (sr, 0.015); bypassSm.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);
    noteCount.fill (0); heldMask.store (0);
    vizFifo.reset();
}

void PluhtuneProcessor::reset()
{
    engine.reset();
    for (int c = 0; c < 2; ++c) std::fill (rawRing[c].begin(), rawRing[c].end(), 0.f);
    noteCount.fill (0); heldMask.store (0);
}

uint16_t PluhtuneProcessor::scaleMaskFromParams() const
{
    uint16_t custom = 0;
    for (int i = 0; i < 12; ++i) if (pNote[(size_t) i]->load() > 0.5f) custom |= (uint16_t) (1u << i);
    return scaleMaskFor ((int) pKey->load(), (int) pScale->load(), custom);
}

uint16_t PluhtuneProcessor::getTargetMask() const
{
    const uint16_t scaleMask = scaleMaskFromParams(), held = heldMask.load();
    switch ((int) pMidiMode->load())
    {
        case 1:  return held != 0 ? held : scaleMask;
        case 2:  return held;
        default: return scaleMask;
    }
}

void PluhtuneProcessor::toggleNote (int pc)
{
    auto setBool = [this] (const juce::String& id, bool v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (v ? 1.f : 0.f); p->endChangeGesture(); }
    };
    const uint16_t cur = scaleMaskFromParams();
    if ((int) pScale->load() != Custom)
    {
        for (int i = 0; i < 12; ++i) setBool (ids::note (i), ((cur >> i) & 1) != 0);
        if (auto* sp = apvts.getParameter (ids::scale)) { sp->beginChangeGesture(); sp->setValueNotifyingHost (sp->convertTo0to1 ((float) Custom)); sp->endChangeGesture(); }
    }
    setBool (ids::note (pc), ((cur >> pc) & 1) == 0);
}

void PluhtuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int nCh = juce::jmin (buffer.getNumChannels(), activeChannels);
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, numSamples);
    if (nCh < 1 || sampleRateHz <= 0.0) return;

    // ---- MIDI note tracking (block resolution) ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       ++noteCount[(size_t) m.getNoteNumber()];
        else if (m.isNoteOff()) noteCount[(size_t) m.getNoteNumber()] = juce::jmax (0, noteCount[(size_t) m.getNoteNumber()] - 1);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) noteCount.fill (0);
    }
    uint16_t held = 0;
    for (int n = 0; n < 128; ++n) if (noteCount[(size_t) n] > 0) held |= (uint16_t) (1u << (n % 12));
    heldMask.store (held);

    // ---- parameters ----
    const int range = juce::jlimit (0, 2, (int) pRange->load());
    if (range != currentRange) { currentRange = range; engine.setMinFrequency (kRangeHz[range]); setLatencySamples (engine.getLatency()); }

    EngineParams ep;
    ep.retuneMs = pRetune->load(); ep.amount = pAmount->load(); ep.humanize = pHumanize->load(); ep.transitionMs = pTransition->load();
    ep.aggressive = pMode->load() > 0.5f; ep.refA4 = pRefA4->load();
    ep.formantPreserve = pFPreserve->load() > 0.5f; ep.formantShiftSemis = pFShift->load();
    ep.allowedMask = getTargetMask();

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    mixSm.setTargetValue (pMix->load());
    bypassSm.setTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);

    const int latency = engine.getLatency();
    float peakIn = 0.f, peakOut = 0.f;

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int len = juce::jmin (kChunk, numSamples - start);

        for (int i = 0; i < len; ++i)
        {
            const float g = inGainSm.getNextValue();
            for (int c = 0; c < nCh; ++c)
            {
                const float x = buffer.getReadPointer (c)[start + i];
                peakIn = juce::jmax (peakIn, std::abs (x));
                rawRing[c][(size_t) (rawPos & rawMask)] = x;
                rawDelayed[c][(size_t) i] = rawRing[c][(size_t) ((rawPos - latency) & rawMask)];
                inBuf[c][(size_t) i] = x * g;
            }
            ++rawPos;
        }

        const float* ins[2]  = { inBuf[0].data(),  inBuf[nCh > 1 ? 1 : 0].data() };
        float* wets[2]       = { wetBuf[0].data(), wetBuf[nCh > 1 ? 1 : 0].data() };
        float* drys[2]       = { dryBuf[0].data(), dryBuf[nCh > 1 ? 1 : 0].data() };
        engine.process (ins, wets, drys, len, ep);

        for (int i = 0; i < len; ++i)
        {
            const float m = mixSm.getNextValue(), bp = bypassSm.getNextValue(), og = outGainSm.getNextValue();
            for (int c = 0; c < nCh; ++c)
            {
                const float processed = (m * wetBuf[c][(size_t) i] + (1.f - m) * dryBuf[c][(size_t) i]) * og;
                const float out = processed + bp * (rawDelayed[c][(size_t) i] - processed);
                buffer.getWritePointer (c)[start + i] = out;
                peakOut = juce::jmax (peakOut, std::abs (out));
            }
        }
    }

    if (peakIn  > inPeak.load())  inPeak.store (peakIn);
    if (peakOut > outPeak.load()) outPeak.store (peakOut);

    int cnt = 0; const auto* v = engine.getViz (cnt);
    if (cnt > 0)
    {
        int s1, n1, s2, n2;
        vizFifo.prepareToWrite (cnt, s1, n1, s2, n2);
        for (int k = 0; k < n1; ++k) vizData[(size_t) (s1 + k)] = v[k];
        for (int k = 0; k < n2; ++k) vizData[(size_t) (s2 + k)] = v[n1 + k];
        vizFifo.finishedWrite (n1 + n2);
        engine.clearViz();
    }
}

juce::AudioProcessorEditor* PluhtuneProcessor::createEditor() { return new PluhtuneEditor (*this); }

void PluhtuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PluhtuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PluhtuneProcessor(); }
