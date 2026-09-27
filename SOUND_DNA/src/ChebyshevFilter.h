#pragma once

#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
  ChebyshevFilter.h
  =================
  4th-Order Chebyshev Type I Lowpass Filter + Highpass DC/Hum blocker.
  
  Specifically designed for guitar pitch and chord analysis:
  1. Highpass (70 Hz, 2nd order Butterworth): Removes DC bias, palm-thumps, and 50/60 Hz amp hum.
  2. Chebyshev Type I Lowpass (1100 Hz, 4th order, 0.5 dB ripple):
     - Has an extremely steep roll-off (-24 dB/octave)
     - Aggressively crushes pick scratch, string noise, fret buzz, and upper harmonic overtones
     - Preserves the fundamental guitar range (80 Hz – 1000 Hz) with maximum clarity
     - Eliminates false harmonic triggers and muddiness in pitch tracking
*/

struct BiquadSection
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset()
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    inline float process(float in)
    {
        float out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }
};

class ChebyshevFilter
{
public:
    explicit ChebyshevFilter(float sampleRate = 44100.0f, float cutoffHz = 1100.0f, float highpassHz = 70.0f)
        : m_sampleRate(sampleRate), m_cutoffHz(cutoffHz), m_highpassHz(highpassHz)
    {
        setupFilters();
    }

    void setSampleRate(float sampleRate)
    {
        m_sampleRate = sampleRate;
        setupFilters();
    }

    void reset()
    {
        for (auto& s : m_lowpassSections) s.reset();
        m_highpassSection.reset();
    }

    // Process a single audio sample through the Chebyshev filter chain
    inline float processSample(float input)
    {
        // Step 1: Remove sub-bass rumble & amp hum (< 70 Hz)
        float s = m_highpassSection.process(input);

        // Step 2: 4th-order Chebyshev lowpass (< 1100 Hz)
        for (auto& sec : m_lowpassSections)
        {
            s = sec.process(s);
        }
        return s;
    }

    // Filter an entire buffer into an output buffer
    void processBuffer(const float* input, float* output, int length)
    {
        for (int i = 0; i < length; ++i)
        {
            output[i] = processSample(input[i]);
        }
    }

private:
    float m_sampleRate;
    float m_cutoffHz;
    float m_highpassHz;

    std::vector<BiquadSection> m_lowpassSections; // 2 biquads = 4th order
    BiquadSection              m_highpassSection;  // 2nd order highpass

    void setupFilters()
    {
        m_lowpassSections.resize(2);

        // --- 1. 2nd-Order Highpass (Butterworth, 70 Hz) ---
        float w0_hp = 2.0f * static_cast<float>(M_PI) * (m_highpassHz / m_sampleRate);
        float cos_hp = std::cos(w0_hp);
        float sin_hp = std::sin(w0_hp);
        float alpha_hp = sin_hp / (2.0f * 0.7071f);

        float a0_hp = 1.0f + alpha_hp;
        m_highpassSection.b0 = ((1.0f + cos_hp) / 2.0f) / a0_hp;
        m_highpassSection.b1 = (-(1.0f + cos_hp)) / a0_hp;
        m_highpassSection.b2 = ((1.0f + cos_hp) / 2.0f) / a0_hp;
        m_highpassSection.a1 = (-2.0f * cos_hp) / a0_hp;
        m_highpassSection.a2 = (1.0f - alpha_hp) / a0_hp;
        m_highpassSection.reset();

        // --- 2. 4th-Order Chebyshev Type I Lowpass (1100 Hz, 0.5 dB ripple) ---
        // Pre-warp cutoff frequency
        float wp = 2.0f * m_sampleRate * std::tan(static_cast<float>(M_PI) * m_cutoffHz / m_sampleRate);
        float eps = std::sqrt(std::pow(10.0f, 0.5f / 10.0f) - 1.0f); // 0.5 dB ripple -> eps ~ 0.3493
        float mu = (1.0f / 4.0f) * std::asinh(1.0f / eps);

        // Two conjugate pole pairs for N = 4:
        for (int k = 0; k < 2; ++k)
        {
            float theta = (2.0f * k + 1.0f) * static_cast<float>(M_PI) / 8.0f;
            float sigma = -std::sinh(mu) * std::sin(theta);
            float omega =  std::cosh(mu) * std::cos(theta);

            // Analog pole pair: s^2 - 2*sigma*wp*s + (sigma^2 + omega^2)*wp^2
            float p_real = sigma * wp;
            float p_imag = omega * wp;
            float p_mag_sq = p_real * p_real + p_imag * p_imag;

            // Bilinear transform: s = 2*fs * (1 - z^-1) / (1 + z^-1)
            float fs2 = 2.0f * m_sampleRate;
            float fs2_sq = fs2 * fs2;

            float denom = fs2_sq - 2.0f * p_real * fs2 + p_mag_sq;

            float b_gain = p_mag_sq / denom;
            m_lowpassSections[k].b0 = b_gain;
            m_lowpassSections[k].b1 = 2.0f * b_gain;
            m_lowpassSections[k].b2 = b_gain;

            m_lowpassSections[k].a1 = (2.0f * p_mag_sq - 2.0f * fs2_sq) / denom;
            m_lowpassSections[k].a2 = (fs2_sq + 2.0f * p_real * fs2 + p_mag_sq) / denom;
            m_lowpassSections[k].reset();
        }
    }
};
