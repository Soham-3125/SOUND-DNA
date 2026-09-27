#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <algorithm>
#include "WavIO.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AudioSampleBuffer
{
    int midiNote = 60;
    float rootFreq = 261.63f;
    std::vector<float> samples;
};

// Interface for all instrument sound banks
class IInstrumentBank
{
public:
    virtual ~IInstrumentBank() = default;
    virtual std::string getName() const = 0;
    virtual const AudioSampleBuffer* getSampleForMidi(int midiNote) const = 0;
};

// ---------------------------------------------------------------------------
// 1. Crystal Clear 64-Key Concert Grand Piano (A1 - C7 / MIDI 33 - 96)
// ---------------------------------------------------------------------------
class PianoBank : public IInstrumentBank
{
public:
    static constexpr int LOWEST_MIDI = 33;  // A1
    static constexpr int HIGHEST_MIDI = 96; // C7

    explicit PianoBank(float sampleRate = 44100.0f)
    {
        for (int midi = LOWEST_MIDI; midi <= HIGHEST_MIDI; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            float duration = std::clamp(2.0f - ((midi - LOWEST_MIDI) / 64.0f) * 1.3f, 0.7f, 2.0f);
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            // Clean harmonic distribution (fundamental emphasis to eliminate mud)
            int numHarmonics = (std::min)(static_cast<int>(sampleRate * 0.4f / f0), 14);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;
                float sampleVal = 0.0f;

                // Crisp, clean hammer click (very short 6ms transient)
                if (t < 0.008f)
                {
                    float strikeEnvelope = std::exp(-t * 600.0f);
                    float noise = ((std::rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                    sampleVal += noise * strikeEnvelope * 0.12f;
                }

                // Pure harmonic series with clean natural acoustic damping
                for (int h = 1; h <= numHarmonics; ++h)
                {
                    float fh = h * f0;
                    if (fh >= sampleRate * 0.48f) break;

                    // Faster decay for low midrange to keep chords pristine and punchy
                    float decayRate = (2.2f + h * 2.5f) * (1.0f + (f0 / 400.0f));
                    float amp = (1.0f / std::pow(static_cast<float>(h), 1.35f)) * std::exp(-t * decayRate);

                    // High-clarity single/coupled string phase
                    sampleVal += amp * std::sin(2.0f * (float)M_PI * fh * t);
                }

                // 2ms anti-click attack
                float attack = (std::min)(1.0f, t / 0.002f);
                buffer.samples[i] = sampleVal * attack * 0.42f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }

    std::string getName() const override { return "Crystal Clear Concert Grand Piano"; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        int clamped = std::clamp(midiNote, LOWEST_MIDI, HIGHEST_MIDI);
        auto it = m_keyBuffers.find(clamped);
        if (it != m_keyBuffers.end()) return &it->second;
        return nullptr;
    }

private:
    std::map<int, AudioSampleBuffer> m_keyBuffers;
};

// ---------------------------------------------------------------------------
// 2. Warm Polyphonic Analog Synth Lead / Pad
// ---------------------------------------------------------------------------
class SynthBank : public IInstrumentBank
{
public:
    explicit SynthBank(float sampleRate = 44100.0f)
    {
        for (int midi = 24; midi <= 96; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            float duration = 1.8f;
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;

                float phase1 = std::fmod(t * f0, 1.0f);
                float phase2 = std::fmod(t * (f0 * 1.004f), 1.0f);
                float saw1 = (2.0f * phase1) - 1.0f;
                float saw2 = (2.0f * phase2) - 1.0f;
                float subSquare = (std::sin(2.0f * (float)M_PI * (f0 * 0.5f) * t) > 0.0f) ? 0.25f : -0.25f;

                float raw = (saw1 * 0.4f + saw2 * 0.4f + subSquare * 0.2f);
                float env = std::exp(-t * 2.2f);
                float attack = (std::min)(1.0f, t / 0.010f);

                buffer.samples[i] = raw * env * attack * 0.38f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }

    std::string getName() const override { return "Warm Polyphonic Analog Synth"; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        int clamped = std::clamp(midiNote, 24, 96);
        auto it = m_keyBuffers.find(clamped);
        if (it != m_keyBuffers.end()) return &it->second;
        return nullptr;
    }

private:
    std::map<int, AudioSampleBuffer> m_keyBuffers;
};

// ---------------------------------------------------------------------------
// 3. Deep 808 / Electric Bass
// ---------------------------------------------------------------------------
class BassBank : public IInstrumentBank
{
public:
    explicit BassBank(float sampleRate = 44100.0f)
    {
        for (int midi = 24; midi <= 72; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            float duration = 2.0f;
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;

                float pitchMod = 1.0f + 0.25f * std::exp(-t * 70.0f);
                float curFreq = f0 * pitchMod;

                float fund = std::sin(2.0f * (float)M_PI * curFreq * t);
                float harm2 = 0.28f * std::sin(4.0f * (float)M_PI * curFreq * t);
                float driven = std::tanh((fund + harm2) * 1.3f);

                float env = std::exp(-t * 2.0f);
                float attack = (std::min)(1.0f, t / 0.004f);

                buffer.samples[i] = driven * env * attack * 0.42f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }

    std::string getName() const override { return "Deep 808 / Electric Bass"; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        int clamped = std::clamp(midiNote, 24, 72);
        auto it = m_keyBuffers.find(clamped);
        if (it != m_keyBuffers.end()) return &it->second;
        return nullptr;
    }

private:
    std::map<int, AudioSampleBuffer> m_keyBuffers;
};

// ---------------------------------------------------------------------------
// 4. Indian Sitar (Plucked String with Jawari Buzz & Sympathetic Resonance)
//    Range: C2 (MIDI 36) to C5 (MIDI 72)
// ---------------------------------------------------------------------------
class SitarBank : public IInstrumentBank
{
public:
    static constexpr int LOWEST_MIDI = 36;   // C2
    static constexpr int HIGHEST_MIDI = 72;  // C5

    explicit SitarBank(float sampleRate = 44100.0f)
    {
        for (int midi = LOWEST_MIDI; midi <= HIGHEST_MIDI; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            float duration = std::clamp(3.0f - ((midi - LOWEST_MIDI) / 36.0f) * 1.5f, 1.2f, 3.0f);
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            // Sitar: rich harmonics with buzzy jawari bridge character
            int numHarmonics = (std::min)(static_cast<int>(sampleRate * 0.45f / f0), 22);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;
                float sampleVal = 0.0f;

                // Sharp metallic pluck transient (jawari buzz attack)
                if (t < 0.015f)
                {
                    float strikeEnv = std::exp(-t * 350.0f);
                    // Buzzy noise burst simulating the jawari bridge contact
                    float noise = ((std::rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                    float buzzTone = std::sin(2.0f * (float)M_PI * f0 * 3.0f * t); // high harmonic buzz
                    sampleVal += (noise * 0.15f + buzzTone * 0.20f) * strikeEnv;
                }

                // Rich harmonic series with jawari buzz character
                // Sitar harmonics are unusually strong and evenly distributed
                for (int h = 1; h <= numHarmonics; ++h)
                {
                    float fh = h * f0;
                    if (fh >= sampleRate * 0.48f) break;

                    // Sitar has strong even AND odd harmonics (unlike most strings)
                    // The jawari bridge makes higher harmonics decay slower than normal
                    float harmonicDecay = (1.8f + h * 0.8f) * (1.0f + (f0 / 500.0f));
                    float amp = (1.0f / std::pow(static_cast<float>(h), 0.85f)) * std::exp(-t * harmonicDecay);

                    // Add slight inharmonicity (characteristic of real sitar strings)
                    float inharmonicity = 1.0f + 0.0003f * h * h;
                    sampleVal += amp * std::sin(2.0f * (float)M_PI * fh * inharmonicity * t);
                }

                // Sympathetic string resonance (taraf) — adds shimmering overtones
                // Sympathetic strings resonate at sa, pa, and upper octave frequencies
                float sympatheticAmp = 0.06f * std::exp(-t * 1.2f);
                sampleVal += sympatheticAmp * std::sin(2.0f * (float)M_PI * f0 * 2.0f * t) * 0.5f; // octave
                sampleVal += sympatheticAmp * std::sin(2.0f * (float)M_PI * f0 * 1.5f * t) * 0.35f; // fifth (pa)
                sampleVal += sympatheticAmp * std::sin(2.0f * (float)M_PI * f0 * 3.0f * t) * 0.25f; // upper octave+fifth

                // Jawari buzz sustain — periodic amplitude modulation simulating bridge buzz
                float buzzMod = 1.0f + 0.08f * std::sin(2.0f * (float)M_PI * f0 * 0.5f * t) * std::exp(-t * 2.5f);
                sampleVal *= buzzMod;

                // Attack shaping
                float attack = (std::min)(1.0f, t / 0.001f); // very fast pluck attack
                buffer.samples[i] = sampleVal * attack * 0.35f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }

    std::string getName() const override { return "Indian Sitar (Jawari Buzz + Taraf Resonance)"; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        int clamped = std::clamp(midiNote, LOWEST_MIDI, HIGHEST_MIDI);
        auto it = m_keyBuffers.find(clamped);
        if (it != m_keyBuffers.end()) return &it->second;
        return nullptr;
    }

private:
    std::map<int, AudioSampleBuffer> m_keyBuffers;
};

// ---------------------------------------------------------------------------
// 5. Indian Sarangi (Bowed String with Vocal/Nasal Timbre)
//    Range: C3 (MIDI 48) to C6 (MIDI 84)
// ---------------------------------------------------------------------------
class SarangiBank : public IInstrumentBank
{
public:
    static constexpr int LOWEST_MIDI = 48;   // C3
    static constexpr int HIGHEST_MIDI = 84;  // C6

    explicit SarangiBank(float sampleRate = 44100.0f)
    {
        for (int midi = LOWEST_MIDI; midi <= HIGHEST_MIDI; ++midi)
        {
            float f0 = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
            float duration = std::clamp(2.5f - ((midi - LOWEST_MIDI) / 36.0f) * 1.0f, 1.0f, 2.5f);
            size_t numSamples = static_cast<size_t>(sampleRate * duration);

            AudioSampleBuffer buffer;
            buffer.midiNote = midi;
            buffer.rootFreq = f0;
            buffer.samples.resize(numSamples, 0.0f);

            // Sarangi: bowed string, vocal/nasal quality, rich odd harmonics
            int numHarmonics = (std::min)(static_cast<int>(sampleRate * 0.40f / f0), 18);

            for (size_t i = 0; i < numSamples; ++i)
            {
                float t = static_cast<float>(i) / sampleRate;
                float sampleVal = 0.0f;

                // Bow noise transient (soft scratchy bow contact)
                if (t < 0.030f)
                {
                    float bowNoiseEnv = std::exp(-t * 120.0f) * (1.0f - std::exp(-t * 800.0f));
                    float noise = ((std::rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                    sampleVal += noise * bowNoiseEnv * 0.08f;
                }

                // Sarangi harmonic series: strong odd harmonics give nasal/vocal quality
                // Similar to a human voice — odd harmonics dominate
                for (int h = 1; h <= numHarmonics; ++h)
                {
                    float fh = h * f0;
                    if (fh >= sampleRate * 0.48f) break;

                    // Odd harmonics are much stronger (vocal/nasal character)
                    float harmonicWeight;
                    if (h % 2 == 1) // odd
                        harmonicWeight = 1.0f / std::pow(static_cast<float>(h), 0.9f);
                    else // even — much weaker
                        harmonicWeight = 0.3f / std::pow(static_cast<float>(h), 1.2f);

                    // Bowed string sustains longer than plucked
                    float decayRate = (0.8f + h * 1.2f) * (1.0f + (f0 / 600.0f));
                    float amp = harmonicWeight * std::exp(-t * decayRate);

                    // Slight vibrato (natural bow vibrato, 5-6 Hz)
                    float vibrato = 1.0f + 0.004f * std::sin(2.0f * (float)M_PI * 5.5f * t);

                    sampleVal += amp * std::sin(2.0f * (float)M_PI * fh * vibrato * t);
                }

                // Nasal formant emphasis: boost around 800-1200 Hz range
                // This gives sarangi its human-voice-like crying quality
                float formantFreq = 950.0f;
                float formantBandwidth = 300.0f;
                float formantGain = 0.12f * std::exp(-t * 2.0f);
                float formantDistance = std::abs(f0 - formantFreq) / formantBandwidth;
                if (formantDistance < 2.0f)
                {
                    float formantBoost = formantGain * std::exp(-formantDistance * formantDistance);
                    sampleVal += formantBoost * std::sin(2.0f * (float)M_PI * formantFreq * t);
                }

                // Second formant around 2500 Hz (adds brightness/presence)
                float formant2Freq = 2500.0f;
                float formant2Gain = 0.05f * std::exp(-t * 3.0f);
                sampleVal += formant2Gain * std::sin(2.0f * (float)M_PI * formant2Freq * t)
                           * std::exp(-(std::abs(f0 * 3.0f - formant2Freq) / 500.0f));

                // Sympathetic string resonance
                float sympatheticAmp = 0.04f * std::exp(-t * 1.5f);
                sampleVal += sympatheticAmp * std::sin(2.0f * (float)M_PI * f0 * 2.0f * t) * 0.4f;
                sampleVal += sympatheticAmp * std::sin(2.0f * (float)M_PI * f0 * 1.5f * t) * 0.3f;

                // Bowed string amplitude modulation (subtle bow pressure variation)
                float bowMod = 1.0f + 0.03f * std::sin(2.0f * (float)M_PI * 3.2f * t);
                sampleVal *= bowMod;

                // Slow bowed attack (not plucked — gradual onset)
                float attack = (std::min)(1.0f, t / 0.025f); // 25ms bow attack
                buffer.samples[i] = sampleVal * attack * 0.38f;
            }

            m_keyBuffers[midi] = std::move(buffer);
        }
    }

    std::string getName() const override { return "Indian Sarangi (Bowed Vocal Strings + Sympathetic Resonance)"; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        int clamped = std::clamp(midiNote, LOWEST_MIDI, HIGHEST_MIDI);
        auto it = m_keyBuffers.find(clamped);
        if (it != m_keyBuffers.end()) return &it->second;
        return nullptr;
    }

private:
    std::map<int, AudioSampleBuffer> m_keyBuffers;
};

// ---------------------------------------------------------------------------
// 6. Custom User WAV File Preset
// ---------------------------------------------------------------------------
class CustomWavBank : public IInstrumentBank
{
public:
    CustomWavBank(const std::string& wavPath, int rootMidi = 60, float sampleRate = 44100.0f)
        : m_rootMidi(rootMidi), m_name("Custom WAV: " + wavPath)
    {
        m_rootBuffer.midiNote = rootMidi;
        m_rootBuffer.rootFreq = 440.0f * std::pow(2.0f, (rootMidi - 69.0f) / 12.0f);
        WavIO::loadAudioFile(wavPath, m_rootBuffer.samples, static_cast<ma_uint32>(sampleRate));
    }

    std::string getName() const override { return m_name; }

    const AudioSampleBuffer* getSampleForMidi(int midiNote) const override
    {
        (void)midiNote;
        if (m_rootBuffer.samples.empty()) return nullptr;
        return &m_rootBuffer;
    }

    bool isValid() const { return !m_rootBuffer.samples.empty(); }

private:
    int m_rootMidi;
    std::string m_name;
    AudioSampleBuffer m_rootBuffer;
};
