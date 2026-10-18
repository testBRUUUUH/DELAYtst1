#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Two-tap crossfading delay-line pitch shifter (classic "doppler + window" design).
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        size = (int) (sampleRate * 0.2) + 8;
        buf.assign ((size_t) size, 0.0f);
        wp = 0;
        phase = 0.0f;
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        phase = 0.0f;
    }

    float process (float x, float ratio, float winSamples)
    {
        if (size == 0) return x;
        buf[(size_t) wp] = x;

        phase += (1.0f - ratio) / winSamples;
        phase -= std::floor (phase);

        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;

        const float twoPi = 6.28318530718f;
        const float w1 = 0.5f - 0.5f * std::cos (twoPi * phase);
        const float w2 = 0.5f - 0.5f * std::cos (twoPi * p2);

        const float out = readAt (phase * winSamples) * w1 + readAt (p2 * winSamples) * w2;

        if (++wp >= size) wp = 0;
        return out;
    }

private:
    float readAt (float d) const
    {
        float rp = (float) wp - d;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        if (i0 >= size) i0 = 0;
        const float fr = rp - (float) i0;
        int i1 = i0 + 1;
        if (i1 >= size) i1 = 0;
        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
    }

    std::vector<float> buf;
    int size = 0, wp = 0;
    float phase = 0.0f;
};
