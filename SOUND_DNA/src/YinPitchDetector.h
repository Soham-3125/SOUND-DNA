#pragma once

#include <vector>
#include <cmath>
#include <string>

class YinPitchDetector
{
public:
    explicit YinPitchDetector(int bufferSize = 1024, float sampleRate = 44100.0f, float threshold = 0.15f)
        : m_bufferSize(bufferSize),
          m_sampleRate(sampleRate),
          m_yinThreshold(threshold),
          m_yinBuffer(bufferSize / 2, 0.0f)
    {
    }

    void setSampleRate(float sampleRate) { m_sampleRate = sampleRate; }
    void setThreshold(float threshold) { m_yinThreshold = threshold; }

    // Returns pitch in Hz, or -1.0f if unpitched / confidence below threshold
    float detectPitch(const float* buffer, int length, float* outConfidence = nullptr)
    {
        if (length < m_bufferSize)
            return -1.0f;

        int halfBufferSize = m_bufferSize / 2;
        if (m_yinBuffer.size() != (size_t)halfBufferSize)
            m_yinBuffer.resize(halfBufferSize, 0.0f);

        // Step 1: Difference function
        difference(buffer, halfBufferSize);

        // Step 2: Cumulative mean normalized difference function
        cumulativeMeanNormalizedDifference(halfBufferSize);

        // Step 3: Absolute thresholding
        int tau = absoluteThreshold(halfBufferSize);

        if (tau == -1)
        {
            if (outConfidence) *outConfidence = 0.0f;
            return -1.0f;
        }

        // Step 4: Parabolic interpolation for sub-sample accuracy
        float betterTau = parabolicInterpolation(tau, halfBufferSize);

        float confidence = 1.0f - m_yinBuffer[tau];
        if (outConfidence) *outConfidence = confidence;

        if (betterTau <= 0.0f)
            return -1.0f;

        float pitch = m_sampleRate / betterTau;
        // Basic guitar range sanity check: 40 Hz (Low E on bass / drop tunings) to 2500 Hz
        if (pitch < 40.0f || pitch > 2500.0f)
            return -1.0f;

        return pitch;
    }

    static int freqToMidi(float freq)
    {
        if (freq <= 0.0f) return -1;
        float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
        return static_cast<int>(std::round(midi));
    }

    static std::string midiToNoteName(int midi)
    {
        if (midi < 0 || midi > 127) return "---";
        static const char* noteNames[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        int noteIndex = midi % 12;
        int octave = (midi / 12) - 1;
        return std::string(noteNames[noteIndex]) + std::to_string(octave);
    }

    static float midiToFreq(int midi)
    {
        return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
    }

private:
    int m_bufferSize;
    float m_sampleRate;
    float m_yinThreshold;
    std::vector<float> m_yinBuffer;

    void difference(const float* buffer, int halfBufferSize)
    {
        for (int tau = 0; tau < halfBufferSize; ++tau)
        {
            m_yinBuffer[tau] = 0.0f;
            for (int i = 0; i < halfBufferSize; ++i)
            {
                float delta = buffer[i] - buffer[i + tau];
                m_yinBuffer[tau] += delta * delta;
            }
        }
    }

    void cumulativeMeanNormalizedDifference(int halfBufferSize)
    {
        m_yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;

        for (int tau = 1; tau < halfBufferSize; ++tau)
        {
            runningSum += m_yinBuffer[tau];
            if (runningSum > 0.0f)
                m_yinBuffer[tau] = m_yinBuffer[tau] * tau / runningSum;
            else
                m_yinBuffer[tau] = 1.0f;
        }
    }

    int absoluteThreshold(int halfBufferSize)
    {
        int tau;
        for (tau = 2; tau < halfBufferSize; ++tau)
        {
            if (m_yinBuffer[tau] < m_yinThreshold)
            {
                while (tau + 1 < halfBufferSize && m_yinBuffer[tau + 1] < m_yinBuffer[tau])
                    tau++;
                return tau;
            }
        }

        // Search global minimum if below a relaxed threshold
        int minTau = 2;
        float minVal = m_yinBuffer[2];
        for (tau = 3; tau < halfBufferSize; ++tau)
        {
            if (m_yinBuffer[tau] < minVal)
            {
                minVal = m_yinBuffer[tau];
                minTau = tau;
            }
        }
        if (minVal < 0.35f) // relaxed confidence for guitar DI harmonics
            return minTau;

        return -1;
    }

    float parabolicInterpolation(int tau, int halfBufferSize)
    {
        if (tau < 1 || tau >= halfBufferSize - 1)
            return static_cast<float>(tau);

        float s0 = m_yinBuffer[tau - 1];
        float s1 = m_yinBuffer[tau];
        float s2 = m_yinBuffer[tau + 1];

        float denom = (2.0f * (2.0f * s1 - s0 - s2));
        if (std::abs(denom) < 1e-6f)
            return static_cast<float>(tau);

        float delta = (s2 - s0) / denom;
        return static_cast<float>(tau) + delta;
    }
};
