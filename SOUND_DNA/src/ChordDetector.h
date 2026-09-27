#pragma once
/*
  ChordDetector.h
  ===============
  Guitar-focused multi-pitch chord detector with Harmonic Cancellation.
  
  Unlike naive peak-picking which mistakes harmonic overtones (3rd harmonic,
  5th harmonic, etc.) for extra chord notes, this detector:
  1. Finds candidate spectral peaks in the guitar range.
  2. Traverses from lowest fundamental upwards.
  3. Cancels all integer harmonic multiples (2f, 3f, 4f, 5f, 6f, 7f) of each accepted note.
  4. Collapses octave duplicates to avoid muddy layering.
  
  Result: A single guitar string produces EXACTLY ONE note.
          A strummed chord produces the distinct musical chord notes.
*/

#include <vector>
#include <cmath>
#include <algorithm>
#include "SimpleFFT.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class ChordDetector
{
public:
    static constexpr int   MAX_CHORD_NOTES = 5;
    static constexpr float MIN_FREQ_HZ     = 75.0f;   // low E is ~82.4 Hz
    static constexpr float MAX_FREQ_HZ     = 1200.0f; // upper guitar range

    explicit ChordDetector(float sampleRate = 44100.0f, int fftSize = 2048)
        : m_sampleRate(sampleRate), m_fftSize(fftSize)
    {
        m_window.resize(fftSize);
        for (int i = 0; i < fftSize; ++i)
            m_window[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (fftSize - 1));
    }

    std::vector<int> detect(const float* samples, int numSamples,
                            float minMagnitudeThreshold = 0.003f)
    {
        int n = m_fftSize;
        std::vector<float> windowed(n, 0.0f);

        int copyLen = std::min(numSamples, n);
        for (int i = 0; i < copyLen; ++i)
            windowed[i] = samples[i] * m_window[i];

        // Check overall RMS to avoid analyzing background silence
        float rms = 0.0f;
        for (float s : windowed) rms += s * s;
        rms = std::sqrt(rms / n);
        if (rms < minMagnitudeThreshold)
            return {};

        std::vector<float> mag = realFFTMagnitude(windowed, n);

        float binHz = m_sampleRate / static_cast<float>(n);
        int   minBin = std::max(2, static_cast<int>(MIN_FREQ_HZ / binHz));
        int   maxBin = std::min(static_cast<int>(MAX_FREQ_HZ / binHz), static_cast<int>(mag.size()) - 2);

        // Find global maximum in guitar range
        float globalMax = 0.0f;
        for (int b = minBin; b <= maxBin; ++b)
        {
            if (mag[b] > globalMax) globalMax = mag[b];
        }
        if (globalMax < 1e-5f) return {};

        // Peaks must be at least 8% of the dominant peak
        float peakThreshold = globalMax * 0.08f;

        struct Peak {
            float freqHz;
            float mag;
            bool isHarmonic = false;
        };
        std::vector<Peak> peaks;

        for (int b = minBin + 1; b < maxBin; ++b)
        {
            if (mag[b] > peakThreshold &&
                mag[b] > mag[b - 1] &&
                mag[b] > mag[b + 1])
            {
                // Parabolic interpolation for sub-bin precision
                float denom = mag[b - 1] - 2.0f * mag[b] + mag[b + 1];
                float offset = (std::abs(denom) > 1e-6f) ? 0.5f * (mag[b - 1] - mag[b + 1]) / denom : 0.0f;
                float freqHz = (static_cast<float>(b) + offset) * binHz;
                if (freqHz >= MIN_FREQ_HZ && freqHz <= MAX_FREQ_HZ)
                {
                    peaks.push_back({freqHz, mag[b], false});
                }
            }
        }

        if (peaks.empty()) return {};

        // Sort candidate peaks by FREQUENCY: LOWEST TO HIGHEST
        // In physical instruments, harmonics are always higher than fundamentals.
        std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
            return a.freqHz < b.freqHz;
        });

        std::vector<int> midiNotes;
        std::vector<float> acceptedFundamentals;

        for (size_t i = 0; i < peaks.size(); ++i)
        {
            if (peaks[i].isHarmonic)
                continue;

            float fundFreq = peaks[i].freqHz;
            int midi = static_cast<int>(std::round(69.0f + 12.0f * std::log2(fundFreq / 440.0f)));
            if (midi < 28 || midi > 88) continue; // E2 to E6

            // Check if this pitch class (or octave duplicate) is already accepted
            bool duplicate = false;
            for (int existing : midiNotes)
            {
                int diff = std::abs(existing - midi);
                // Collapse same note or octave duplicate to prevent muddy layering
                if (diff == 0 || diff == 12 || diff == 24)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                midiNotes.push_back(midi);
                acceptedFundamentals.push_back(fundFreq);
            }

            // NOW: Cancel all higher peaks that are harmonic overtones of this fundamental
            // Harmonics: 2f, 3f, 4f, 5f, 6f, 7f, 8f (tolerance ~4.5%)
            for (size_t j = i + 1; j < peaks.size(); ++j)
            {
                if (peaks[j].isHarmonic) continue;

                float ratio = peaks[j].freqHz / fundFreq;
                float nearestInt = std::round(ratio);

                if (nearestInt >= 2.0f && nearestInt <= 8.0f)
                {
                    float deviation = std::abs(ratio - nearestInt) / nearestInt;
                    if (deviation < 0.045f) // within ~4.5% of an integer harmonic
                    {
                        peaks[j].isHarmonic = true; // Mark as harmonic overtone, not a new note!
                    }
                }
            }

            if (static_cast<int>(midiNotes.size()) >= MAX_CHORD_NOTES)
                break;
        }

        std::sort(midiNotes.begin(), midiNotes.end());
        return midiNotes;
    }

private:
    float              m_sampleRate;
    int                m_fftSize;
    std::vector<float> m_window;
};
