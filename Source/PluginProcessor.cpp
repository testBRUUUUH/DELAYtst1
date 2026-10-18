#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

static const float kBeats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };   // 1/16, 1/8, 1/4, 1/2, 1 bar

DubDelayProcessor::DubDelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pDivL = get ("divL");       pDivR = get ("divR");
    pFeel = get ("feel");       pX2 = get ("x2");
    pSmooth = get ("smooth");   pFeedback = get ("feedback");
    pMix = get ("mix");         pLowCut = get ("lowCut");
    pHighCut = get ("highCut"); pDrive = get ("drive");
    pWow = get ("wow");         pFlutter = get ("flutter");
    pFreeze = get ("freeze");   pPingPong = get ("pingPong");
    pClearStop = get ("clearOnStop");
}

juce::AudioProcessorValueTreeState::ParameterLayout DubDelayProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    using PID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray divs { "1/16", "1/8", "1/4", "1/2", "1 bar" };

    layout.add (std::make_unique<juce::AudioParameterChoice> (PID { "divL", 1 }, "Time L", divs, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (PID { "divR", 1 }, "Time R", divs, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "feel", 1 }, "Feel (triplet-dotted)",
                                                              Range (0.6667f, 1.5f, 0.0001f), 1.5f));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "x2", 1 }, "x2", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "smooth", 1 }, "Smooth (ms)",
                                                              Range (1.0f, 1500.0f, 0.1f, 0.4f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "feedback", 1 }, "Feedback",
                                                              Range (0.0f, 1.1f, 0.001f), 0.55f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "mix", 1 }, "Mix",
                                                              Range (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "lowCut", 1 }, "Low cut (Hz)",
                                                              Range (20.0f, 1000.0f, 1.0f, 0.4f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "highCut", 1 }, "High cut (Hz)",
                                                              Range (500.0f, 16000.0f, 1.0f, 0.4f), 4500.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "drive", 1 }, "Tape drive",
                                                              Range (0.0f, 1.0f, 0.001f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "wow", 1 }, "Wow",
                                                              Range (0.0f, 1.0f, 0.001f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (PID { "flutter", 1 }, "Flutter",
                                                              Range (0.0f, 1.0f, 0.001f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "freeze", 1 }, "Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "pingPong", 1 }, "Ping-pong", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "clearOnStop", 1 }, "Clear on stop", true));

    return layout;
}

void DubDelayProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    bufSize = (int) (sampleRate * 10.0) + 8;
    for (auto& b : buf)
        b.assign ((size_t) bufSize, 0.0f);
    writePos = 0;
    needSnap = true;
    wowPhase = flutterPhase = 0.0;
    clearBuffers();
}

void DubDelayProcessor::clearBuffers()
{
    for (auto& b : buf)
        std::fill (b.begin(), b.end(), 0.0f);
    for (auto& ch : lpf) for (auto& f : ch) f.z = 0.0f;
    for (auto& ch : hpf) for (auto& f : ch) f.z = 0.0f;
}

bool DubDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

float DubDelayProcessor::readDelay (int ch, float d) const
{
    d = juce::jlimit (1.0f, (float) (bufSize - 2), d);
    float rp = (float) writePos - d;
    while (rp < 0.0f) rp += (float) bufSize;
    int i0 = (int) rp;
    float fr = rp - (float) i0;
    int i1 = i0 + 1;
    if (i1 >= bufSize) i1 = 0;
    if (i0 >= bufSize) i0 = 0;
    return buf[ch][(size_t) i0] * (1.0f - fr) + buf[ch][(size_t) i1] * fr;
}

void DubDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || bufSize == 0)
        return;

    // --- host tempo / transport ---
    double bpm = 120.0;
    bool playing = true, haveInfo = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jmax (20.0, *b);
            playing = pos->getIsPlaying();
            haveInfo = true;
        }

    if (haveInfo && lastPlaying && ! playing && pClearStop->load() > 0.5f)
        clearBuffers();
    lastPlaying = haveInfo ? playing : true;

    // --- parameters ---
    const float fb       = pFeedback->load();
    const float mix      = pMix->load();
    const float drive    = pDrive->load();
    const bool  freeze   = pFreeze->load() > 0.5f;
    const bool  pingPong = pPingPong->load() > 0.5f;
    const float feel     = pFeel->load();
    const float x2       = pX2->load() > 0.5f ? 2.0f : 1.0f;
    const int   divL     = juce::jlimit (0, 4, (int) pDivL->load());
    const int   divR     = juce::jlimit (0, 4, (int) pDivR->load());

    const double secPerBeat = 60.0 / bpm;
    const float maxDelay = (float) (bufSize - 2048);
    float target[2];
    target[0] = juce::jlimit (8.0f, maxDelay, (float) (secPerBeat * kBeats[divL] * feel * x2 * sr));
    target[1] = juce::jlimit (8.0f, maxDelay, (float) (secPerBeat * kBeats[divR] * feel * x2 * sr));
    if (needSnap) { delayCur[0] = target[0]; delayCur[1] = target[1]; needSnap = false; }

    const float smoothSec = juce::jmax (0.001f, pSmooth->load() * 0.001f);
    const float smoothCoef = 1.0f - std::exp (-1.0f / (smoothSec * (float) sr));

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float hc = juce::jlimit (200.0f, (float) (0.45 * sr), pHighCut->load());
    const float lc = juce::jlimit (10.0f, 2000.0f, pLowCut->load());
    const float aHigh = 1.0f - std::exp (-twoPi * hc / (float) sr);
    const float aLow  = 1.0f - std::exp (-twoPi * lc / (float) sr);
    const float g = 1.0f + drive * 4.0f;

    const float wowDepth  = pWow->load()     * 0.0025f  * (float) sr;   // up to 2.5 ms
    const float flutDepth = pFlutter->load() * 0.00025f * (float) sr;   // up to 0.25 ms
    const double wowInc  = juce::MathConstants<double>::twoPi * 0.7 / sr;
    const double flutInc = juce::MathConstants<double>::twoPi * 6.5 / sr;

    // signal that goes back into the delay line: saturate, then roll off lows and highs
    auto fbProc = [&] (int ch, float x) -> float
    {
        if (freeze) return x;
        x = std::tanh (g * x) / g;
        x = lpf[ch][1].lp (lpf[ch][0].lp (x, aHigh), aHigh);
        float h = x - hpf[ch][0].lp (x, aLow);
        h = h - hpf[ch][1].lp (h, aLow);
        return h;
    };

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        wowPhase += wowInc;      if (wowPhase > juce::MathConstants<double>::twoPi)  wowPhase -= juce::MathConstants<double>::twoPi;
        flutterPhase += flutInc; if (flutterPhase > juce::MathConstants<double>::twoPi) flutterPhase -= juce::MathConstants<double>::twoPi;
        const float mod = wowDepth * (float) std::sin (wowPhase) + flutDepth * (float) std::sin (flutterPhase);

        delayCur[0] += (target[0] - delayCur[0]) * smoothCoef;
        delayCur[1] += (target[1] - delayCur[1]) * smoothCoef;

        const float dl = readDelay (0, delayCur[0] + mod);
        const float dr = readDelay (1, delayCur[1] + mod * 0.9f);

        const float fl = fbProc (0, dl);
        const float fr = fbProc (1, dr);

        const float inL = l[i], inR = r[i];
        const float inGain = freeze ? 0.0f : 1.0f;
        const float fbAmt  = freeze ? 1.0f : fb;

        float wl, wr;
        if (pingPong)
        {
            const float mono = 0.5f * (inL + inR) * inGain;
            wl = mono + fbAmt * fr;
            wr = fbAmt * fl;
        }
        else
        {
            wl = inL * inGain + fbAmt * fl;
            wr = inR * inGain + fbAmt * fr;
        }

        buf[0][(size_t) writePos] = juce::jlimit (-8.0f, 8.0f, wl);
        buf[1][(size_t) writePos] = juce::jlimit (-8.0f, 8.0f, wr);
        if (++writePos >= bufSize) writePos = 0;

        l[i] = inL * (1.0f - mix) + dl * mix;
        r[i] = inR * (1.0f - mix) + dr * mix;
    }
}

juce::AudioProcessorEditor* DubDelayProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void DubDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DubDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DubDelayProcessor();
}
