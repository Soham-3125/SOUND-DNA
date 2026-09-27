#pragma once

#include <cmath>
#include <algorithm>

/*
  NoiseGateAndOnset.h
  ===================
  Live-sound transient onset detector designed specifically for guitar.
  
  Key principles for live sound:
  1. The output signal is NEVER muted or hard-gated. Clean live audio always passes through.
  2. Onset is detected by sudden energy jumps (transients / pick strikes) using a dual-envelope
     follower (fast attack vs. slow baseline).
  3. Does NOT require silence between notes! Works with background amp hum, string sustain,
     and continuous playing.
  4. 160ms debounce prevents double-triggering on pick release or string wobble.
*/

class NoiseGateAndOnset
{
public:
    explicit NoiseGateAndOnset(float sampleRate = 44100.0f)
        : m_sampleRate(sampleRate),
          m_fastEnvelope(0.0f),
          m_slowEnvelope(0.0f),
          // Fast envelope: 2ms attack, 35ms decay
          m_fastAttackAlpha(std::exp(-1.0f / (sampleRate * 0.002f))),
          m_fastReleaseAlpha(std::exp(-1.0f / (sampleRate * 0.035f))),
          // Slow envelope: 30ms attack, 180ms decay (tracks background sustain/hum)
          m_slowAttackAlpha(std::exp(-1.0f / (sampleRate * 0.030f))),
          m_slowReleaseAlpha(std::exp(-1.0f / (sampleRate * 0.180f))),
          m_minNoiseFloor(std::pow(10.0f, -50.0f / 20.0f)), // -50 dB minimum signal level
          m_onsetSensitivity(0.015f),
          m_samplesSinceLastOnset(100000),
          m_minSamplesBetweenOnsets(static_cast<int>(sampleRate * 0.160f)) // 160ms debounce
    {
    }

    void setThresholdDb(float thresholdDb)
    {
        // Higher thresholdDb means less sensitive (requires harder pick attack)
        // -55 dB = very sensitive, -30 dB = requires strong strum
        m_minNoiseFloor = std::pow(10.0f, std::clamp(thresholdDb, -70.0f, -20.0f) / 20.0f);
        m_onsetSensitivity = std::clamp(m_minNoiseFloor * 0.5f, 0.008f, 0.06f);
    }

    float getThresholdDb() const { return 20.0f * std::log10(std::max(1e-5f, m_minNoiseFloor)); }
    bool  isGateOpen()    const { return m_fastEnvelope > m_minNoiseFloor; }
    float getEnvelope()   const { return m_fastEnvelope; }

    // Process a single audio sample.
    // Live audio passes through 100% cleanly (never cut off).
    // Sets outOnsetTriggered = true on pick strikes / attack transients.
    float processSample(float input, bool& outOnsetTriggered, float& outVelocity)
    {
        outOnsetTriggered = false;
        outVelocity       = 0.0f;

        float absVal = std::abs(input);

        // Fast envelope
        if (absVal > m_fastEnvelope)
            m_fastEnvelope = m_fastAttackAlpha * m_fastEnvelope + (1.0f - m_fastAttackAlpha) * absVal;
        else
            m_fastEnvelope = m_fastReleaseAlpha * m_fastEnvelope + (1.0f - m_fastReleaseAlpha) * absVal;

        // Slow envelope
        if (absVal > m_slowEnvelope)
            m_slowEnvelope = m_slowAttackAlpha * m_slowEnvelope + (1.0f - m_slowAttackAlpha) * absVal;
        else
            m_slowEnvelope = m_slowReleaseAlpha * m_slowEnvelope + (1.0f - m_slowReleaseAlpha) * absVal;

        m_samplesSinceLastOnset++;

        // Transient jump: how fast energy spiked above the baseline
        float energyJump = m_fastEnvelope - m_slowEnvelope;

        // Trigger on pick attack:
        // 1. Above minimum signal noise floor
        // 2. Significant transient jump
        // 3. Past refractory debounce window (160ms)
        if (m_fastEnvelope > m_minNoiseFloor &&
            energyJump > m_onsetSensitivity &&
            m_samplesSinceLastOnset > m_minSamplesBetweenOnsets)
        {
            outOnsetTriggered       = true;
            m_samplesSinceLastOnset = 0;
            // Velocity dynamically mapped to attack strength
            outVelocity = std::clamp(m_fastEnvelope * 3.5f, 0.25f, 1.0f);
        }

        // Live sound: NEVER cut or mute the audio signal!
        return input;
    }

    void reset()
    {
        m_fastEnvelope          = 0.0f;
        m_slowEnvelope          = 0.0f;
        m_samplesSinceLastOnset = 100000;
    }

private:
    float m_sampleRate;
    float m_fastEnvelope;
    float m_slowEnvelope;
    float m_fastAttackAlpha;
    float m_fastReleaseAlpha;
    float m_slowAttackAlpha;
    float m_slowReleaseAlpha;
    float m_minNoiseFloor;
    float m_onsetSensitivity;
    int   m_samplesSinceLastOnset;
    int   m_minSamplesBetweenOnsets;
};
