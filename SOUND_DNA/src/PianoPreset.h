#pragma once

#include <vector>
#include <cmath>
#include <map>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AudioSampleBuffer
{
    int midiNote = 60;
    float rootFreq = 261.63f;
    std::vector<float> samples;
};

class PianoPreset
{
public:
    // 64 keys: A1 (MIDI 33, 55Hz) to C7 (MIDI 96, 2093Hz)
    static constexpr int LOWEST_MIDI = 33;  // A1
    static constexpr int HIGHEST_MIDI = 96; // C7
    static constexpr int NUM_KEYS = HIGHEST_MIDI - LOWEST_MIDI + 1; // 64 keys

    static PianoPreset& getInstance(float sampleRate = 44100.0f)
    {
        static PianoPreset instance(sampleRate);
        return instance;
    }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const
    {
        int clampedMidi = std::clamp(midiNote, LOWEST_MIDI, HIGHEST_MIDI);
        auto it = m_keyBuffers.find(clampedMidi);
        if (it != m_keyBuffers.end())
            return &it->second;
        return nullptr;
    }

private:
    explicit PianoPreset(float sampleRate = 44100.0f)
    {
        generate64KeyPianoBank(sampleRate);
    }

    std::map<int, AudioSampleBuffer> m_keyBuffers;

    // High fidelity harmonic model of acoustic piano keys
    void generate64KeyPianoBank(float sampleRate)
    {
        for (int midi = LOWEST_MIDI; midi <= HIGHEST_MIDI; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            
            // Duration scales with pitch: bass notes ring longer (2.5s), treble notes decay faster (0.8s)
            float duration = std::clamp(2.5f - ((midi - LOWEST_MIDI) / 64.0f) * 1.7f, 0.8f, 2.8f);
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            // Piano string inharmonicity coefficient
            float B = 0.0001f * std::pow(f0 / 100.0f, 1.2f);
            int numHarmonics = std::min(static_cast<int>(sampleRate * 0.45f / f0), 20);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;
                float sampleVal = 0.0f;

                // Strike transient (felt hammer noise burst at onset)
                if (t < 0.025f)
                {
                    float strikeEnvelope = std::exp(-t * 220.0f);
                    float noise = ((std::rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                    sampleVal += noise * strikeEnvelope * 0.25f;
                }

                // Sum harmonics with natural acoustic decay rates and string coupling
                for (int h = 1; h <= numHarmonics; ++h)
                {
                    // Inharmonic partial frequency: f_h = h * f0 * sqrt(1 + B * h^2)
                    float fh = h * f0 * std::sqrt(1.0f + B * h * h);
                    if (fh >= sampleRate * 0.49f) break;

                    // Higher harmonics decay faster; bass harmonics sustain longer
                    float decayRate = (1.5f + h * 1.8f) * (1.0f + (f0 / 300.0f));
                    float amp = (1.0f / std::pow(static_cast<float>(h), 1.15f)) * std::exp(-t * decayRate);

                    // Dual-string detuning / beating characteristic of pianos
                    float beat = 0.5f * (std::sin(2.0f * (float)M_PI * (fh - 0.25f) * t) + std::sin(2.0f * (float)M_PI * (fh + 0.25f) * t));
                    sampleVal += amp * beat;
                }

                // Overall master envelope (fast attack curve to prevent pop)
                float attack = std::min(1.0f, t / 0.003f); // 3ms strike attack
                buffer.samples[i] = sampleVal * attack * 0.35f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }
};
