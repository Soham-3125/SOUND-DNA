/*
 SoundResynthesizer.h
 =====================
 Same additive resynthesis + blend logic as the JUCE version, using plain
 std::vector<float> instead of juce::AudioBuffer.
*/

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "SoundAnalyzer.h"

class SoundResynthesizer
{
public:
    static std::vector<float> resynthesize(const SoundAnalyzer& analyzer, float brightness = 1.0f, float pitchOffsetSemitones = 0.0f)
    {
        double sr = analyzer.getSampleRate();
        int numSamples = analyzer.getNumSamples();
        int numFrames = analyzer.getNumFrames();
        int hopSize = analyzer.getHopSize();

        std::vector<float> out(numSamples, 0.0f);

        const auto& f0Contour = analyzer.getF0Contour();
        const auto& harmonicAmps = analyzer.getHarmonicAmps();

        float pitchMult = std::pow(2.0f, pitchOffsetSemitones / 12.0f);

        std::vector<float> frameTimes(numFrames);
        for (int i = 0; i < numFrames; ++i)
            frameTimes[i] = (i * hopSize + kFrameSize / 2.0f) / static_cast<float>(sr);

        auto interp = [&](const std::vector<float>& track, float t) -> float
        {
            if (t <= frameTimes.front()) return track.front();
            if (t >= frameTimes.back()) return track.back();
            for (size_t i = 0; i + 1 < frameTimes.size(); ++i)
                if (t >= frameTimes[i] && t <= frameTimes[i + 1])
                {
                    float alpha = (t - frameTimes[i]) / (frameTimes[i + 1] - frameTimes[i] + 1e-12f);
                    return track[i] * (1.0f - alpha) + track[i + 1] * alpha;
                }
            return track.back();
        };

        for (int h = 0; h < kNumHarmonics; ++h)
        {
            std::vector<float> harmonicTrack(numFrames);
            for (int i = 0; i < numFrames; ++i)
                harmonicTrack[i] = harmonicAmps[i][h];

            float weight = std::pow(brightness, static_cast<float>(h + 1) / kNumHarmonics);
            double phase = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                float t = static_cast<float>(n) / static_cast<float>(sr);
                float f0 = interp(f0Contour, t);
                float amp = interp(harmonicTrack, t);
                float harmonicFreq = f0 * (h + 1) * pitchMult;

                if (harmonicFreq >= static_cast<float>(sr) / 2.0f || f0 <= 0.0f) continue;

                phase += 2.0 * M_PI * harmonicFreq / sr;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;

                out[n] += std::sin(static_cast<float>(phase)) * amp * weight;
            }
        }

        const auto& ampEnv = analyzer.getAmplitudeEnvelope();
        float targetRms = 0.0f;
        for (float e : ampEnv) targetRms += e;
        targetRms = ampEnv.empty() ? 0.0f : targetRms / ampEnv.size();

        float sumSq = 0.0f;
        for (float s : out) sumSq += s * s;
        float currentRms = std::sqrt(sumSq / std::max(1, numSamples)) + 1e-12f;

        float scale = targetRms / currentRms;
        for (float& s : out) s *= scale;

        float peak = 0.0f;
        for (float s : out) peak = std::max(peak, std::abs(s));
        if (peak > 0.98f)
        {
            float limitScale = 0.98f / peak;
            for (float& s : out) s *= limitScale;
        }

        return out;
    }

    /** blend = 0 -> pure original, blend = 1 -> pure resynthesis. */
    static std::vector<float> blend(const std::vector<float>& original, const std::vector<float>& resynth, float blendAmount)
    {
        size_t n = std::min(original.size(), resynth.size());
        blendAmount = std::clamp(blendAmount, 0.0f, 1.0f);

        std::vector<float> out(n);
        for (size_t i = 0; i < n; ++i)
            out[i] = (1.0f - blendAmount) * original[i] + blendAmount * resynth[i];
        return out;
    }
};
