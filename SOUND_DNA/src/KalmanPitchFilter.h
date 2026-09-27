#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// 1D Kalman Filter for Pitch Smoothing
// Smooths out pitch jitter and rejects false detections from YIN.
// State: pitch frequency in Hz
// ---------------------------------------------------------------------------
class KalmanPitchFilter
{
public:
    explicit KalmanPitchFilter(float processNoise = 2.0f, float measurementNoise = 15.0f)
        : m_Q(processNoise),       // how much we expect pitch to change between frames
          m_R(measurementNoise),    // how noisy the YIN measurements are
          m_P(1000.0f),             // initial uncertainty (high = we don't know yet)
          m_x(0.0f),               // initial state estimate
          m_initialized(false)
    {
    }

    void reset()
    {
        m_P = 1000.0f;
        m_x = 0.0f;
        m_initialized = false;
        m_consecutiveHits = 0;
    }

    // Hard-set the filter to a known pitch after a confirmed onset transient.
    // This bypasses outlier rejection so large interval jumps register instantly.
    void resetTo(float pitch)
    {
        m_x = pitch;
        m_P = m_R * 0.5f;  // Low uncertainty — we trust the onset measurement
        m_initialized = true;
        m_missCount = 0;
        m_consecutiveHits = 1;
    }

    // Feed a new raw pitch measurement. Returns the filtered (smoothed) pitch.
    // If rawPitch <= 0 (unpitched), returns the last good estimate with decaying confidence.
    float update(float rawPitch)
    {
        if (rawPitch <= 0.0f)
        {
            // No valid measurement — increase uncertainty, return last estimate
            m_P += m_Q * 5.0f;
            m_missCount++;
            if (m_missCount > 8) // too many misses, reset
            {
                m_initialized = false;
                return -1.0f;
            }
            return m_initialized ? m_x : -1.0f;
        }

        m_missCount = 0;

        if (!m_initialized)
        {
            m_x = rawPitch;
            m_P = m_R;
            m_initialized = true;
            return m_x;
        }

        // Predict step
        // x_predicted = x (constant-velocity model, pitch doesn't drift fast)
        // P_predicted = P + Q
        float pPredicted = m_P + m_Q;

        // Update step
        // K = P_predicted / (P_predicted + R)
        float K = pPredicted / (pPredicted + m_R);

        // Reject wild outliers: if measurement is more than an octave away, reduce gain
        float ratio = rawPitch / m_x;
        if (ratio > 2.2f || ratio < 0.45f)
        {
            K *= 0.1f; // strongly attenuate the wild jump
        }
        else if (ratio > 1.5f || ratio < 0.67f)
        {
            K *= 0.4f; // moderate attenuation for large jumps
        }

        m_x = m_x + K * (rawPitch - m_x);
        m_P = (1.0f - K) * pPredicted;
        m_consecutiveHits++;

        return m_x;
    }

    float getEstimate() const { return m_x; }
    float getUncertainty() const { return m_P; }
    bool  isInitialized() const { return m_initialized; }
    bool  isStable() const { return m_initialized && m_P < 10.0f && m_consecutiveHits >= 2; }

private:
    float m_Q;  // process noise covariance
    float m_R;  // measurement noise covariance
    float m_P;  // estimation error covariance
    float m_x;  // state estimate (pitch Hz)
    bool m_initialized;
    int m_missCount = 0;
    int m_consecutiveHits = 0;
};

// ---------------------------------------------------------------------------
// Attack / Release Envelope Analyzer
// Extracts per-note attack time and release decay rate from input audio
// ---------------------------------------------------------------------------
struct NoteEnvelopeInfo
{
    float attackTimeMs = 5.0f;    // how fast the note reaches peak (ms)
    float releaseTimeMs = 200.0f; // how fast the note decays after peak (ms)
    float peakAmplitude = 1.0f;
    float sustainLevel = 0.5f;    // ratio of sustain to peak
};

class EnvelopeAnalyzer
{
public:
    // Analyze a segment of audio around a note onset to extract attack/release
    static NoteEnvelopeInfo analyzeNoteEnvelope(
        const float* audio,
        int length,
        float sampleRate = 44100.0f)
    {
        NoteEnvelopeInfo info;
        if (length < 64) return info;

        // Find peak amplitude and its position
        float peak = 0.0f;
        int peakPos = 0;
        for (int i = 0; i < length; ++i)
        {
            float absVal = std::abs(audio[i]);
            if (absVal > peak)
            {
                peak = absVal;
                peakPos = i;
            }
        }

        if (peak < 1e-5f) return info;
        info.peakAmplitude = peak;

        // Attack time: from onset to peak
        // Walk backward from peak to find where amplitude first rises above 10% of peak
        float threshold10 = peak * 0.10f;
        int attackStart = 0;
        for (int i = peakPos; i >= 0; --i)
        {
            if (std::abs(audio[i]) < threshold10)
            {
                attackStart = i;
                break;
            }
        }
        info.attackTimeMs = (peakPos - attackStart) * 1000.0f / sampleRate;
        info.attackTimeMs = (std::max)(0.5f, (std::min)(info.attackTimeMs, 100.0f));

        // Release / decay: measure how long it takes to drop to 37% (1/e) of peak after peak
        float threshold37 = peak * 0.37f;
        int releaseEnd = length - 1;
        for (int i = peakPos; i < length; ++i)
        {
            if (std::abs(audio[i]) < threshold37)
            {
                releaseEnd = i;
                break;
            }
        }
        info.releaseTimeMs = (releaseEnd - peakPos) * 1000.0f / sampleRate;
        info.releaseTimeMs = (std::max)(10.0f, (std::min)(info.releaseTimeMs, 3000.0f));

        // Sustain level: average amplitude in the middle third after peak
        int sustainStart = peakPos + (releaseEnd - peakPos) / 4;
        int sustainEnd = peakPos + (releaseEnd - peakPos) * 3 / 4;
        if (sustainEnd > sustainStart && sustainEnd < length)
        {
            float sumAbs = 0.0f;
            for (int i = sustainStart; i < sustainEnd; ++i)
                sumAbs += std::abs(audio[i]);
            info.sustainLevel = (sumAbs / (sustainEnd - sustainStart)) / peak;
        }

        return info;
    }

    // Analyze the global envelope characteristics of the entire input audio
    static NoteEnvelopeInfo analyzeGlobalEnvelope(
        const std::vector<float>& audio,
        float sampleRate = 44100.0f)
    {
        if (audio.size() < 1024) return NoteEnvelopeInfo{};

        // Use RMS envelope in 5ms windows
        int windowSize = static_cast<int>(sampleRate * 0.005f);
        int numWindows = static_cast<int>(audio.size()) / windowSize;

        std::vector<float> envelope(numWindows, 0.0f);
        float maxRms = 0.0f;

        for (int w = 0; w < numWindows; ++w)
        {
            float sum = 0.0f;
            for (int i = 0; i < windowSize; ++i)
            {
                float s = audio[w * windowSize + i];
                sum += s * s;
            }
            envelope[w] = std::sqrt(sum / windowSize);
            if (envelope[w] > maxRms) maxRms = envelope[w];
        }

        if (maxRms < 1e-5f) return NoteEnvelopeInfo{};

        // Find average attack time across detected transients
        float totalAttackMs = 0.0f;
        float totalReleaseMs = 0.0f;
        int transientCount = 0;

        for (int w = 1; w < numWindows - 1; ++w)
        {
            // Detect transient: sudden rise > 2x previous
            if (envelope[w] > envelope[w - 1] * 2.0f && envelope[w] > maxRms * 0.15f)
            {
                // Measure attack: how many windows to reach local peak
                int peakW = w;
                for (int p = w; p < (std::min)(w + 40, numWindows); ++p)
                {
                    if (envelope[p] >= envelope[peakW]) peakW = p;
                    else break;
                }
                float attackMs = (peakW - w + 1) * windowSize * 1000.0f / sampleRate;

                // Measure release: how many windows to drop to 37% of local peak
                float localPeak = envelope[peakW];
                float thresh = localPeak * 0.37f;
                int releaseW = peakW;
                for (int r = peakW; r < (std::min)(peakW + 200, numWindows); ++r)
                {
                    if (envelope[r] < thresh)
                    {
                        releaseW = r;
                        break;
                    }
                }
                float releaseMs = (releaseW - peakW + 1) * windowSize * 1000.0f / sampleRate;

                totalAttackMs += attackMs;
                totalReleaseMs += releaseMs;
                transientCount++;

                w = peakW + 10; // skip ahead past this note
            }
        }

        NoteEnvelopeInfo globalInfo;
        if (transientCount > 0)
        {
            globalInfo.attackTimeMs = (std::max)(0.5f, (std::min)(totalAttackMs / transientCount, 80.0f));
            globalInfo.releaseTimeMs = (std::max)(20.0f, (std::min)(totalReleaseMs / transientCount, 2000.0f));
        }
        globalInfo.peakAmplitude = maxRms;

        return globalInfo;
    }
};
