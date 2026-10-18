#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "Vocoder.h"
#include "PitchShifter.h"

class DubDelayProcessor : public juce::AudioProcessor
{
public:
    DubDelayProcessor();
    ~DubDelayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dub Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct OnePole
    {
        float z = 0.0f;
        float lp (float x, float a) { z += a * (x - z); return z; }
    };

    void clearBuffers();
    float readDelay (int ch, float delaySamples) const;

    std::vector<float> buf[2];
    int bufSize = 0;
    int writePos = 0;

    float delayCur[2] = { 0.0f, 0.0f };
    bool needSnap = true;

    OnePole lpf[2][2];   // high-cut in the loop
    OnePole hpf[2][2];   // low-cut in the loop

    Vocoder vocoder;
    PitchShifter shifter[2];

    double sr = 44100.0;
    double wowPhase = 0.0, flutterPhase = 0.0;
    bool lastPlaying = true;

    std::atomic<float> *pDivL, *pDivR, *pFeel, *pX2, *pSmooth, *pFeedback, *pMix,
                       *pLowCut, *pHighCut, *pDrive, *pWow, *pFlutter,
                       *pFreeze, *pPingPong, *pClearStop,
                       *pPitch, *pPitchMix, *pPitchWin,
                       *pVocOn, *pVocMix, *pVocLevel, *pVocNote, *pVocOct, *pVocChord,
                       *pVocDetune, *pVocHiss, *pVocAtt, *pVocRel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubDelayProcessor)
};
