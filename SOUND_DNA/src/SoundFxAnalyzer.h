#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

struct SoundFxProfile
{
    float reverbWet = 0.15f;       // Clean, transparent room mix (default 15%)
    float reverbRoomSize = 0.45f;  // Crisp studio room size
    float reverbDamping = 0.45f;   // High damping to cut low-mid muddiness

    float delayTimeMs = 0.0f;      // Echo delay time (ms)
    float delayFeedback = 0.0f;    // Feedback amount
    float delayWet = 0.0f;         // Delay mix

    float modRateHz = 1.2f;        // Modulation rate
    float modDepthMs = 1.5f;       // Subtle modulation depth
    float modWet = 0.0f;           // Chorus mix

    bool hasSlideOrVibrato = false;
};

class SoundFxAnalyzer
{
public:
    static SoundFxProfile profileAudio(const std::vector<float>& audio, float sampleRate = 44100.0f)
    {
        SoundFxProfile profile;
        if (audio.size() < 2048) return profile;

        int hopSize = static_cast<int>(sampleRate * 0.010f); // 10ms
        int numHops = static_cast<int>(audio.size() / hopSize);
        std::vector<float> envelope(numHops, 0.0f);

        float maxRms = 0.0f;
        for (int h = 0; h < numHops; ++h)
        {
            float sum = 0.0f;
            int start = h * hopSize;
            for (int i = 0; i < hopSize && (start + i) < static_cast<int>(audio.size()); ++i)
            {
                float s = audio[start + i];
                sum += s * s;
            }
            envelope[h] = std::sqrt(sum / hopSize);
            if (envelope[h] > maxRms) maxRms = envelope[h];
        }

        if (maxRms < 1e-5f) return profile;

        // Measure ambient decay energy ratio
        float lowEnergySum = 0.0f;
        float highEnergySum = 0.0f;

        for (int h = 0; h < numHops; ++h)
        {
            float rel = envelope[h] / maxRms;
            if (rel > 0.4f)
                highEnergySum += rel;
            else if (rel > 0.02f)
                lowEnergySum += rel;
        }

        float tailRatio = (highEnergySum > 0.0f) ? (lowEnergySum / highEnergySum) : 0.0f;

        // Controlled, non-muddy reverb scaling
        if (tailRatio > 0.35f)
        {
            profile.reverbWet = (std::min)(0.28f, 0.12f + (tailRatio * 0.18f));
            profile.reverbRoomSize = (std::min)(0.65f, 0.40f + (tailRatio * 0.2f));
            profile.reverbDamping = 0.45f;
        }
        else
        {
            profile.reverbWet = 0.12f;
            profile.reverbRoomSize = 0.40f;
            profile.reverbDamping = 0.50f;
        }

        // Delay detection: look for strong distinct echo repetitions (correlation > 0.60)
        int minLag = static_cast<int>(0.100f * sampleRate / hopSize); // 100ms
        int maxLag = static_cast<int>(0.500f * sampleRate / hopSize); // 500ms
        maxLag = (std::min)(maxLag, numHops / 2);

        float bestCorr = 0.0f;
        int bestLag = 0;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float corr = 0.0f;
            float norm1 = 0.0f, norm2 = 0.0f;
            for (int i = 0; i < numHops - lag; ++i)
            {
                corr += envelope[i] * envelope[i + lag];
                norm1 += envelope[i] * envelope[i];
                norm2 += envelope[i + lag] * envelope[i + lag];
            }
            float denom = std::sqrt(norm1 * norm2);
            if (denom > 1e-6f)
            {
                corr /= denom;
                if (corr > bestCorr)
                {
                    bestCorr = corr;
                    bestLag = lag;
                }
            }
        }

        if (bestCorr > 0.60f && bestLag > 0)
        {
            profile.delayTimeMs = bestLag * 10.0f;
            profile.delayFeedback = (std::min)(0.35f, bestCorr * 0.45f);
            profile.delayWet = (std::min)(0.20f, bestCorr * 0.25f);
        }

        // Modulation estimation
        float envVariance = 0.0f;
        for (int h = 1; h < numHops; ++h)
        {
            float diff = envelope[h] - envelope[h - 1];
            envVariance += diff * diff;
        }
        envVariance /= (numHops > 1 ? (numHops - 1) : 1);

        if (envVariance > 0.0015f)
        {
            profile.hasSlideOrVibrato = true;
            profile.modRateHz = 1.2f;
            profile.modDepthMs = 1.5f;
            profile.modWet = 0.15f; // subtle dimensional width, not detuned smear
        }

        return profile;
    }
};
