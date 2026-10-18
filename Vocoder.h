#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// 16-band channel vocoder. The input is the modulator; the carrier is an internal
// chord of band-limited saw oscillators (detuned left/right) plus a little noise.
class Vocoder
{
public:
    static constexpr int numBands = 16;
    static constexpr int maxVoices = 4;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const float lo = 120.0f;
        const float hi = std::min (7000.0f, 0.45f * sr);
        for (int k = 0; k < numBands; ++k)
        {
            const float fc = lo * std::pow (hi / lo, (float) k / (float) (numBands - 1));
            modF[k].setBandpass (fc, 4.5f, sr);
            carF[0][k].setBandpass (fc, 4.5f, sr);
            carF[1][k].setBandpass (fc, 4.5f, sr);
        }
        reset();
        setEnvelope (5.0f, 60.0f);
        setCarrier (48, 2, 8.0f);
    }

    void reset()
    {
        for (int k = 0; k < numBands; ++k)
        {
            modF[k].reset(); carF[0][k].reset(); carF[1][k].reset();
            env[k] = 0.0f;
        }
        for (int v = 0; v < maxVoices; ++v) { oscL[v].phase = 0.0f; oscR[v].phase = 0.37f; }
    }

    void setCarrier (int rootMidi, int chordIndex, float detuneCents)
    {
        static const int chords[6][4] = { {0,0,0,0}, {0,12,0,0}, {0,3,7,0}, {0,4,7,0}, {0,3,7,10}, {0,5,7,0} };
        static const int counts[6] = { 1, 2, 3, 3, 4, 3 };
        chordIndex = std::min (5, std::max (0, chordIndex));
        numVoices = counts[chordIndex];
        voiceGain = 1.0f / std::sqrt ((float) numVoices);
        const float up = std::pow (2.0f, detuneCents / 1200.0f);
        for (int v = 0; v < numVoices; ++v)
        {
            const float note = (float) (rootMidi + chords[chordIndex][v]);
            const float f = 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
            incL[v] = f * up / sr;
            incR[v] = f / up / sr;
        }
    }

    void setEnvelope (float attackMs, float releaseMs)
    {
        attCoef = 1.0f - std::exp (-1.0f / (std::max (0.1f, attackMs)  * 0.001f * sr));
        relCoef = 1.0f - std::exp (-1.0f / (std::max (0.1f, releaseMs) * 0.001f * sr));
    }

    void setHiss (float h)   { hiss = h; }
    void setLevel (float l)  { level = l; }

    void process (float mod, float& outL, float& outR)
    {
        float c0 = 0.0f, c1 = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            c0 += oscL[v].process (incL[v]);
            c1 += oscR[v].process (incR[v]);
        }
        c0 = c0 * voiceGain + hiss * 0.5f * noise();
        c1 = c1 * voiceGain + hiss * 0.5f * noise();

        float o0 = 0.0f, o1 = 0.0f;
        for (int k = 0; k < numBands; ++k)
        {
            const float a = std::fabs (modF[k].process (mod));
            env[k] += (a > env[k] ? attCoef : relCoef) * (a - env[k]);
            o0 += carF[0][k].process (c0) * env[k];
            o1 += carF[1][k].process (c1) * env[k];
        }
        outL = o0 * makeup * level;
        outR = o1 * makeup * level;
    }

private:
    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void setBandpass (float fc, float q, float sampleRate)
        {
            const float w0 = 6.28318530718f * fc / sampleRate;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float a0 = 1.0f + alpha;
            b0 = alpha / a0;  b1 = 0.0f;  b2 = -alpha / a0;
            a1 = -2.0f * std::cos (w0) / a0;
            a2 = (1.0f - alpha) / a0;
        }
        void reset() { z1 = z2 = 0.0f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct SawOsc
    {
        float phase = 0.0f;
        static float polyBlep (float t, float dt)
        {
            if (t < dt)         { t /= dt;           return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt)  { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
            return 0.0f;
        }
        float process (float inc)
        {
            const float v = 2.0f * phase - 1.0f - polyBlep (phase, inc);
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            return v;
        }
    };

    float noise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) rng * (2.0f / 4294967295.0f) - 1.0f;
    }

    Biquad modF[numBands], carF[2][numBands];
    SawOsc oscL[maxVoices], oscR[maxVoices];
    float env[numBands] = {};
    float incL[maxVoices] = {}, incR[maxVoices] = {};
    int numVoices = 3;
    float voiceGain = 0.577f;
    float attCoef = 0.01f, relCoef = 0.001f;
    float hiss = 0.15f, level = 1.0f;
    const float makeup = 3.0f;
    float sr = 44100.0f;
    uint32_t rng = 22222u;
};
