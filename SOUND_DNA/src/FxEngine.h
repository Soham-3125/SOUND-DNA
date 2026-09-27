#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Comb Filter with Lowpass Damping for Reverb
// ---------------------------------------------------------------------------
class CombFilter
{
public:
    void init(int delaySamples, float feedback, float damp)
    {
        m_buffer.assign(delaySamples, 0.0f);
        m_head = 0;
        m_feedback = (std::min)(0.80f, feedback);
        m_damp = damp;
        m_filterStore = 0.0f;
    }

    float process(float input)
    {
        float output = m_buffer[m_head];
        m_filterStore = (output * (1.0f - m_damp)) + (m_filterStore * m_damp);
        m_buffer[m_head] = input + (m_filterStore * m_feedback);
        m_head = (m_head + 1) % m_buffer.size();
        return output;
    }

private:
    std::vector<float> m_buffer;
    size_t m_head = 0;
    float m_feedback = 0.5f;
    float m_damp = 0.35f;
    float m_filterStore = 0.0f;
};

// ---------------------------------------------------------------------------
// Allpass Filter for Reverb Diffusion
// ---------------------------------------------------------------------------
class AllpassFilter
{
public:
    void init(int delaySamples, float feedback = 0.5f)
    {
        m_buffer.assign(delaySamples, 0.0f);
        m_head = 0;
        m_feedback = feedback;
    }

    float process(float input)
    {
        float bufOut = m_buffer[m_head];
        float output = -input + bufOut;
        m_buffer[m_head] = input + (bufOut * m_feedback);
        m_head = (m_head + 1) % m_buffer.size();
        return output;
    }

private:
    std::vector<float> m_buffer;
    size_t m_head = 0;
    float m_feedback = 0.5f;
};

// ---------------------------------------------------------------------------
// High-Pass Filter (eliminates sub-bass reverb mud below 150 Hz)
// ---------------------------------------------------------------------------
class HighPassFilter
{
public:
    void init(float sampleRate, float cutoffHz = 150.0f)
    {
        float rc = 1.0f / (2.0f * (float)M_PI * cutoffHz);
        float dt = 1.0f / sampleRate;
        m_alpha = rc / (rc + dt);
        m_prevIn = 0.0f;
        m_prevOut = 0.0f;
    }

    float process(float input)
    {
        float out = m_alpha * (m_prevOut + input - m_prevIn);
        m_prevIn = input;
        m_prevOut = out;
        return out;
    }

private:
    float m_alpha = 0.95f;
    float m_prevIn = 0.0f;
    float m_prevOut = 0.0f;
};

// ---------------------------------------------------------------------------
// Clean Studio Stereo Reverb (Full Spectrum — No High-Pass)
// ---------------------------------------------------------------------------
class StereoReverb
{
public:
    void init(float sampleRate = 44100.0f, float roomSize = 0.5f, float damping = 0.45f, float wet = 0.20f)
    {
        m_sampleRate = sampleRate;
        m_roomSize = (std::min)(0.72f, roomSize);
        m_damping = (std::max)(0.40f, damping);
        m_wet = (std::min)(0.35f, (std::max)(0.0f, wet));
        m_dry = 1.0f;

        static const int combTunings[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int allpassTunings[4] = { 556, 441, 341, 225 };

        float scale = sampleRate / 44100.0f;
        for (int i = 0; i < 8; ++i)
        {
            int del = static_cast<int>(combTunings[i] * scale);
            m_combs[i].init(del, m_roomSize, m_damping);
        }

        for (int i = 0; i < 4; ++i)
        {
            int del = static_cast<int>(allpassTunings[i] * scale);
            m_allpasses[i].init(del, 0.5f);
        }
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        if (m_wet <= 0.001f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        // Feed full-spectrum signal into reverb (no HPF — preserves warmth)
        float monoIn = (inL + inR) * 0.5f * 0.012f;

        float combSumL = 0.0f;
        float combSumR = 0.0f;

        for (int i = 0; i < 4; ++i)
            combSumL += m_combs[i].process(monoIn);
        for (int i = 4; i < 8; ++i)
            combSumR += m_combs[i].process(monoIn);

        float allpassL = m_allpasses[1].process(m_allpasses[0].process(combSumL));
        float allpassR = m_allpasses[3].process(m_allpasses[2].process(combSumR));

        outL = inL * m_dry + allpassL * m_wet * 2.2f;
        outR = inR * m_dry + allpassR * m_wet * 2.2f;
    }

private:
    float m_sampleRate = 44100.0f;
    float m_roomSize = 0.5f;
    float m_damping = 0.45f;
    float m_wet = 0.20f;
    float m_dry = 1.0f;

    CombFilter m_combs[8];
    AllpassFilter m_allpasses[4];
};

// ---------------------------------------------------------------------------
// Feedback Echo / Delay with High-Pass Filter
// ---------------------------------------------------------------------------
class FeedbackDelay
{
public:
    void init(float sampleRate = 44100.0f, float delayTimeMs = 250.0f, float feedback = 0.25f, float wet = 0.15f)
    {
        m_sampleRate = sampleRate;
        m_feedback = (std::min)(0.60f, (std::max)(0.0f, feedback));
        m_wet = (std::min)(0.35f, (std::max)(0.0f, wet));
        m_dry = 1.0f;

        int maxSamples = static_cast<int>(sampleRate * 2.0f);
        m_buffer.assign(maxSamples, 0.0f);
        m_delaySamples = (std::min)(maxSamples - 1, (std::max)(1, static_cast<int>(delayTimeMs * 0.001f * sampleRate)));
        m_head = 0;
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        if (m_wet <= 0.001f || m_delaySamples <= 1)
        {
            outL = inL;
            outR = inR;
            return;
        }

        size_t readPos = (m_head + m_buffer.size() - m_delaySamples) % m_buffer.size();
        float delayed = m_buffer[readPos];

        // Full-spectrum delay (no HPF — preserves warmth and body)
        float inputMono = (inL + inR) * 0.5f;
        m_buffer[m_head] = inputMono + (delayed * m_feedback);
        m_head = (m_head + 1) % m_buffer.size();

        outL = inL * m_dry + delayed * m_wet;
        outR = inR * m_dry + delayed * m_wet;
    }

private:
    float m_sampleRate = 44100.0f;
    int m_delaySamples = 0;
    float m_feedback = 0.25f;
    float m_wet = 0.0f;
    float m_dry = 1.0f;
    std::vector<float> m_buffer;
    size_t m_head = 0;
};

// ---------------------------------------------------------------------------
// Subtle Stereo Chorus / Dimension
// ---------------------------------------------------------------------------
class StereoChorus
{
public:
    void init(float sampleRate = 44100.0f, float rateHz = 1.0f, float depthMs = 1.8f, float wet = 0.18f)
    {
        m_sampleRate = sampleRate;
        m_rateHz = rateHz;
        m_depthSamples = depthMs * 0.001f * sampleRate;
        m_wet = (std::min)(0.30f, wet);
        m_dry = 1.0f;

        m_bufferSize = static_cast<int>(0.050f * sampleRate);
        m_bufferL.assign(m_bufferSize, 0.0f);
        m_bufferR.assign(m_bufferSize, 0.0f);
        m_head = 0;
        m_lfoPhase = 0.0f;
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        if (m_wet <= 0.001f || m_depthSamples <= 0.1f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        m_bufferL[m_head] = inL;
        m_bufferR[m_head] = inR;

        float lfoL = std::sin(m_lfoPhase);
        float lfoR = std::cos(m_lfoPhase);

        float baseDelay = m_depthSamples * 1.5f;
        float curDelayL = baseDelay + lfoL * m_depthSamples;
        float curDelayR = baseDelay + lfoR * m_depthSamples;

        auto readInterp = [](const std::vector<float>& buf, size_t head, float delay, size_t bufSize) {
            float rPos = static_cast<float>(head) + static_cast<float>(bufSize) - delay;
            while (rPos >= bufSize) rPos -= bufSize;
            size_t idx0 = static_cast<size_t>(rPos);
            size_t idx1 = (idx0 + 1) % bufSize;
            float frac = rPos - idx0;
            return buf[idx0] + frac * (buf[idx1] - buf[idx0]);
        };

        float modL = readInterp(m_bufferL, m_head, curDelayL, m_bufferSize);
        float modR = readInterp(m_bufferR, m_head, curDelayR, m_bufferSize);

        m_head = (m_head + 1) % m_bufferSize;
        m_lfoPhase += 2.0f * (float)M_PI * m_rateHz / m_sampleRate;
        if (m_lfoPhase > 2.0f * (float)M_PI) m_lfoPhase -= 2.0f * (float)M_PI;

        outL = inL * m_dry + modL * m_wet;
        outR = inR * m_dry + modR * m_wet;
    }

private:
    float m_sampleRate = 44100.0f;
    float m_rateHz = 1.0f;
    float m_depthSamples = 0.0f;
    float m_wet = 0.0f;
    float m_dry = 1.0f;
    std::vector<float> m_bufferL;
    std::vector<float> m_bufferR;
    size_t m_bufferSize = 2048;
    size_t m_head = 0;
    float m_lfoPhase = 0.0f;
};
