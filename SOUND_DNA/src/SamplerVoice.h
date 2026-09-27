#pragma once

#include <vector>
#include <cmath>
#include <functional>
#include "InstrumentPresets.h"

struct Voice
{
    bool active = false;
    int midiNote = -1;
    float velocity = 1.0f;
    double samplePosition = 0.0;
    double playbackRate = 1.0;
    const AudioSampleBuffer* currentSample = nullptr;

    // Attack shaping (matches input transient)
    float attackGain = 0.0f;       // current attack envelope level (0 -> 1)
    float attackStep = 0.005f;     // how fast we ramp up per sample
    bool attackComplete = false;

    // Release envelope
    bool releasing = false;
    float releaseGain = 1.0f;
    float releaseStep = 0.005f;    // click-free fade (adjusted per-note from input analysis)

    // Dynamic Timbre Filter (maps input spectral centroid / brightness to voice tone)
    float filterZ = 0.0f;
    float filterCoeff = 0.85f;
};

class PolyVoiceManager
{
public:
    static constexpr int MAX_VOICES = 6; // Up to 6-note chords (guitar has 6 strings)

    PolyVoiceManager()
    {
        m_voices.resize(MAX_VOICES);
    }

    // Set global attack/release characteristics extracted from input audio
    void setEnvelopeShaping(float attackTimeMs, float releaseTimeMs, float sampleRate = 44100.0f)
    {
        float attackSamples  = (std::max)(1.0f, attackTimeMs  * 0.001f * sampleRate);
        float releaseSamples = (std::max)(1.0f, releaseTimeMs * 0.001f * sampleRate);
        m_attackStep  = 1.0f / attackSamples;
        m_releaseStep = 1.0f / releaseSamples;
        m_useShaping  = true;
    }

    // Stop all current voices and start all chord notes simultaneously with Sound DNA characteristics
    void chordOn(const std::vector<int>& midiNotes, float velocity,
                 std::function<const AudioSampleBuffer*(int)> getSample,
                 float attackTimeMs = 8.0f, float releaseTimeMs = 250.0f, float timbreCentroidHz = 1200.0f)
    {
        stopAll(); // clear previous chord cleanly
        for (int midi : midiNotes)
        {
            const AudioSampleBuffer* buf = getSample(midi);
            if (buf) noteOn(midi, velocity, buf, attackTimeMs, releaseTimeMs, timbreCentroidHz);
        }
    }

    void noteOn(int midiNote, float velocity, const AudioSampleBuffer* sampleBuffer,
                float attackTimeMs = 8.0f, float releaseTimeMs = 250.0f, float timbreCentroidHz = 1200.0f, float pitchOffsetRatio = 1.0f)
    {
        if (!sampleBuffer || sampleBuffer->samples.empty())
            return;

        // Find free slot, or steal the one furthest through its playback
        int  useIdx     = -1;
        double maxPos   = -1.0;
        int  oldestIdx  = 0;

        for (int i = 0; i < MAX_VOICES; ++i)
        {
            if (!m_voices[i].active) { useIdx = i; break; }
            if (m_voices[i].samplePosition > maxPos)
            {
                maxPos     = m_voices[i].samplePosition;
                oldestIdx  = i;
            }
        }
        if (useIdx == -1) useIdx = oldestIdx;

        Voice& v = m_voices[useIdx];
        v.active = true;
        v.midiNote = midiNote;
        v.velocity = std::clamp(velocity, 0.15f, 1.0f);
        v.samplePosition = 0.0;
        v.currentSample = sampleBuffer;
        v.releasing = false;
        v.releaseGain = 1.0f;

        // Dynamic Attack Envelope Shaping from input Sound DNA
        float effAttackMs = m_useShaping ? (1.0f / (m_attackStep * 44.1f)) : attackTimeMs;
        float attackSamples = (std::max)(1.0f, effAttackMs * 0.001f * 44100.0f);
        v.attackGain = 0.0f;
        v.attackStep = 1.0f / attackSamples;
        v.attackComplete = false;

        // Dynamic Release Envelope Shaping from input Sound DNA
        float effReleaseMs = m_useShaping ? (1.0f / (m_releaseStep * 44.1f)) : releaseTimeMs;
        float releaseSamples = (std::max)(1.0f, effReleaseMs * 0.001f * 44100.0f);
        v.releaseStep = 1.0f / releaseSamples;

        // Dynamic Timbre Cutoff: map input spectral centroid (400 Hz - 4000 Hz) to filter
        float normCentroid = std::clamp(timbreCentroidHz / 3500.0f, 0.15f, 1.0f);
        v.filterCoeff = normCentroid;
        v.filterZ = 0.0f;

        float targetFreq = 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
        v.playbackRate = (targetFreq / sampleBuffer->rootFreq) * pitchOffsetRatio;
    }

    void noteOff(int midiNote)
    {
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            if (m_voices[i].active && m_voices[i].midiNote == midiNote && !m_voices[i].releasing)
            {
                m_voices[i].releasing = true;
            }
        }
    }

    void stopAll()
    {
        for (auto& v : m_voices)
        {
            v.active = false;
            v.currentSample = nullptr;
        }
    }

    // Render one audio sample across all active voices
    float renderSample()
    {
        float output = 0.0f;

        for (auto& v : m_voices)
        {
            if (!v.active || !v.currentSample)
                continue;

            const auto& buf = v.currentSample->samples;
            size_t size = buf.size();

            size_t idx0 = static_cast<size_t>(v.samplePosition);
            size_t idx1 = idx0 + 1;

            if (idx1 >= size)
            {
                v.active = false;
                continue;
            }

            // Linear interpolation
            float frac = static_cast<float>(v.samplePosition - idx0);
            float sampleVal = buf[idx0] + frac * (buf[idx1] - buf[idx0]);

            // Attack envelope shaping (ramp up to match input's attack characteristic)
            if (!v.attackComplete)
            {
                v.attackGain += v.attackStep;
                if (v.attackGain >= 1.0f)
                {
                    v.attackGain = 1.0f;
                    v.attackComplete = true;
                }
                sampleVal *= v.attackGain;
            }

            // Release envelope (matches input's decay/release characteristic)
            if (v.releasing)
            {
                v.releaseGain -= v.releaseStep;
                if (v.releaseGain <= 0.0f)
                {
                    v.active = false;
                    continue;
                }
                sampleVal *= v.releaseGain;
            }

            // Apply Dynamic Timbre Lowpass Filter: shapes voice harmonics to mirror input timbre
            v.filterZ += v.filterCoeff * (sampleVal - v.filterZ);
            sampleVal = v.filterZ;

            output += sampleVal * v.velocity;
            v.samplePosition += v.playbackRate;
        }

        return output;
    }

    int getActiveVoiceCount() const
    {
        int count = 0;
        for (const auto& v : m_voices)
            if (v.active) count++;
        return count;
    }

private:
    std::vector<Voice> m_voices;
    bool m_useShaping = false;
    float m_attackStep = 0.005f;
    float m_releaseStep = 0.005f;
};
