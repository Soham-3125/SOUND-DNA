/*
 SoundAnalyzer.h / .cpp (combined in one file for this simpler build)
 =====================================================================
 Same algorithm as the JUCE version, with every JUCE type replaced by
 plain C++: std::vector<float> instead of juce::AudioBuffer, std::string
 instead of juce::String, and our own SimpleFFT instead of juce::dsp::FFT.
*/

#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "SimpleFFT.h"

constexpr int kFrameSize = 2048;
constexpr int kHopSize = 512;
constexpr int kNumHarmonics = 16;

struct SoundProperties
{
    float pitchHz = 0.0f;
    std::string pitchNote = "unvoiced";
    float wavelengthM = 0.0f;
    float frequencyHz = 0.0f;
    float timbreCentroidHz = 0.0f;
    float timbreSpreadHz = 0.0f;
    float timbreFlatness = 0.0f;
    float amplitudePeakDb = 0.0f;
    float amplitudeRmsDb = 0.0f;
    float waveSpeedMs = 343.42f;
    float attackTimeS = 0.0f;
    float pitchStabilityHz = 0.0f;
    float harmonicToNoiseProxy = 0.0f;
    float durationS = 0.0f;
};

struct NoteSoundDNA
{
    float pitchHz = 0.0f;
    int   midiNote = -1;
    std::string noteName = "unvoiced";
    float wavelengthM = 0.0f;
    float waveSpeedMs = 343.42f;
    float timbreCentroidHz = 1200.0f;
    float timbreSpreadHz = 600.0f;
    float timbreFlatness = 0.02f;
    float amplitudePeakDb = -60.0f;
    float amplitudeRmsDb = -60.0f;
    float attackTimeMs = 8.0f;
    float velocity = 0.8f;
    float brightness = 1.0f;
};

class SoundAnalyzer
{
public:
    void analyze(const std::vector<float>& audio, double sampleRate, float temperatureC = 20.0f);

    const SoundProperties& getProperties() const { return properties; }
    const std::vector<float>& getF0Contour() const { return f0Contour; }
    const std::vector<std::vector<float>>& getHarmonicAmps() const { return harmonicAmps; }
    const std::vector<float>& getAmplitudeEnvelope() const { return amplitudeEnvelope; }
    int getNumFrames() const { return numFrames; }
    int getHopSize() const { return kHopSize; }
    double getSampleRate() const { return sr; }
    int getNumSamples() const { return numSamples; }

    static std::string freqToNote(float freqHz)
    {
        if (freqHz <= 0.0f) return "unvoiced";
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        float semitones = 12.0f * std::log2(freqHz / 440.0f);
        int midi = static_cast<int>(std::round(semitones)) + 69;
        int octave = midi / 12 - 1;
        int noteIdx = ((midi % 12) + 12) % 12;
        return std::string(names[noteIdx]) + std::to_string(octave);
    }

    static NoteSoundDNA extractNoteDNA(const float* buffer, int length, float sampleRate, float f0, float velocity = 0.8f)
    {
        NoteSoundDNA dna;
        dna.pitchHz = f0;
        dna.velocity = velocity;
        dna.waveSpeedMs = 343.42f;

        if (f0 > 0.0f)
        {
            dna.wavelengthM = 343.42f / f0;
            dna.midiNote = static_cast<int>(std::round(69.0f + 12.0f * std::log2(f0 / 440.0f)));
            dna.noteName = freqToNote(f0);
        }

        if (!buffer || length < 256) return dna;

        int fftSize = 1024;
        if (length < fftSize) fftSize = 512;

        std::vector<float> win(fftSize);
        for (int i = 0; i < fftSize; ++i)
            win[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (fftSize - 1));

        std::vector<float> windowed(fftSize);
        float peak = 0.0f, sumSq = 0.0f;
        int peakPos = 0;

        for (int i = 0; i < fftSize; ++i)
        {
            float s = buffer[i];
            float absS = std::abs(s);
            if (absS > peak) { peak = absS; peakPos = i; }
            sumSq += s * s;
            windowed[i] = s * win[i];
        }

        dna.amplitudePeakDb = 20.0f * std::log10(std::max(1e-5f, peak));
        float rms = std::sqrt(sumSq / fftSize);
        dna.amplitudeRmsDb = 20.0f * std::log10(std::max(1e-5f, rms));

        // Attack time estimation: samples from 10% peak to peak
        float thresh10 = peak * 0.10f;
        int attackStart = 0;
        for (int i = peakPos; i >= 0; --i)
        {
            if (std::abs(buffer[i]) < thresh10) { attackStart = i; break; }
        }
        dna.attackTimeMs = (peakPos - attackStart) * 1000.0f / sampleRate;
        dna.attackTimeMs = std::clamp(dna.attackTimeMs, 1.0f, 150.0f);

        // FFT spectral analysis for Timbre
        auto mags = realFFTMagnitude(windowed, fftSize);
        float binHz = sampleRate / static_cast<float>(fftSize);

        float specSum = 0.0f, weightedFreqSum = 0.0f;
        for (size_t b = 0; b < mags.size(); ++b)
        {
            specSum += mags[b];
            weightedFreqSum += b * binHz * mags[b];
        }

        if (specSum > 1e-6f)
        {
            dna.timbreCentroidHz = weightedFreqSum / specSum;

            float spreadSum = 0.0f;
            for (size_t b = 0; b < mags.size(); ++b)
            {
                float diff = (b * binHz) - dna.timbreCentroidHz;
                spreadSum += diff * diff * mags[b];
            }
            dna.timbreSpreadHz = std::sqrt(spreadSum / specSum);

            double logSum = 0.0;
            for (float m : mags) logSum += std::log(static_cast<double>(m) + 1e-12);
            float geoMean = static_cast<float>(std::exp(logSum / mags.size()));
            dna.timbreFlatness = geoMean / (specSum / mags.size() + 1e-12f);
            dna.brightness = std::clamp(dna.timbreCentroidHz / 1200.0f, 0.2f, 2.5f);
        }

        return dna;
    }

private:
    static std::vector<float> hannWindow(int size)
    {
        std::vector<float> w(size);
        for (int i = 0; i < size; ++i)
            w[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (size - 1));
        return w;
    }

    float estimateF0Frame(const std::vector<float>& frame, double sampleRate, float fmin = 60.0f, float fmax = 1000.0f)
    {
        int size = static_cast<int>(frame.size());
        static std::vector<float> window = hannWindow(kFrameSize);

        std::vector<float> windowed(size);
        for (int i = 0; i < size; ++i)
            windowed[i] = frame[i] * window[i];

        std::vector<float> corr(size, 0.0f);
        for (int lag = 0; lag < size; ++lag)
        {
            float sum = 0.0f;
            for (int i = 0; i < size - lag; ++i)
                sum += windowed[i] * windowed[i + lag];
            corr[lag] = sum;
        }

        int minLag = static_cast<int>(sampleRate / fmax);
        int maxLag = std::min(static_cast<int>(sampleRate / fmin), size - 1);
        if (maxLag <= minLag) return 0.0f;

        float peakVal = 0.0f;
        int peakLag = minLag;
        for (int lag = minLag; lag < maxLag; ++lag)
            if (corr[lag] > peakVal) { peakVal = corr[lag]; peakLag = lag; }

        if (peakVal < 0.05f * corr[0]) return 0.0f;
        return static_cast<float>(sampleRate) / static_cast<float>(peakLag);
    }

    void harmonicAmplitudesForFrame(const std::vector<float>& frame, double sampleRate, float f0, std::vector<float>& outAmps)
    {
        outAmps.assign(kNumHarmonics, 0.0f);
        if (f0 <= 0.0f) return;

        static std::vector<float> window = hannWindow(kFrameSize);
        std::vector<float> windowed(kFrameSize);
        for (int i = 0; i < kFrameSize; ++i)
            windowed[i] = frame[i] * window[i];

        auto mags = realFFTMagnitude(windowed, kFrameSize);
        float binHz = static_cast<float>(sampleRate) / kFrameSize;

        for (int h = 1; h <= kNumHarmonics; ++h)
        {
            float targetFreq = f0 * h;
            if (targetFreq >= static_cast<float>(sampleRate) / 2.0f) break;

            int binIdx = static_cast<int>(std::round(targetFreq / binHz));
            float maxMag = 0.0f;
            for (int b = std::max(0, binIdx - 2); b < std::min(static_cast<int>(mags.size()), binIdx + 3); ++b)
                maxMag = std::max(maxMag, mags[b]);
            outAmps[h - 1] = maxMag;
        }
    }


    std::vector<float> f0Contour;
    std::vector<std::vector<float>> harmonicAmps;
    std::vector<float> amplitudeEnvelope;
    int numFrames = 0;
    int numSamples = 0;
    double sr = 44100.0;
    SoundProperties properties;
};

inline void SoundAnalyzer::analyze(const std::vector<float>& audio, double sampleRate, float temperatureC)
{
    sr = sampleRate;
    numSamples = static_cast<int>(audio.size());
    numFrames = std::max(1, (numSamples - kFrameSize) / kHopSize + 1);

    f0Contour.assign(numFrames, 0.0f);
    harmonicAmps.assign(numFrames, std::vector<float>(kNumHarmonics, 0.0f));
    amplitudeEnvelope.assign(numFrames, 0.0f);

    std::vector<float> frameBuf(kFrameSize, 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        int start = i * kHopSize;
        int available = std::min(kFrameSize, numSamples - start);

        std::fill(frameBuf.begin(), frameBuf.end(), 0.0f);
        for (int s = 0; s < available; ++s)
            frameBuf[s] = audio[start + s];

        float f0 = estimateF0Frame(frameBuf, sampleRate);
        f0Contour[i] = f0;
        harmonicAmplitudesForFrame(frameBuf, sampleRate, f0, harmonicAmps[i]);

        float sumSq = 0.0f;
        for (int s = 0; s < kFrameSize; ++s) sumSq += frameBuf[s] * frameBuf[s];
        amplitudeEnvelope[i] = std::sqrt(sumSq / kFrameSize);
    }

    // ---- Summary properties ----
    float sumF0 = 0.0f; int voicedCount = 0;
    for (float f : f0Contour) if (f > 0.0f) { sumF0 += f; ++voicedCount; }
    float meanF0 = voicedCount > 0 ? sumF0 / voicedCount : 0.0f;

    float v = 331.3f + 0.606f * temperatureC;
    float wavelength = meanF0 > 0.0f ? v / meanF0 : 0.0f;

    float peak = 0.0f, sumSqAll = 0.0f;
    for (int i = 0; i < numSamples; ++i) { peak = std::max(peak, std::abs(audio[i])); sumSqAll += audio[i] * audio[i]; }
    float rms = std::sqrt(sumSqAll / std::max(1, numSamples));
    float peakDb = 20.0f * std::log10(peak + 1e-12f);
    float rmsDb = 20.0f * std::log10(rms + 1e-12f);

    static std::vector<float> window = hannWindow(kFrameSize);
    int firstFrameLen = std::min(kFrameSize, numSamples);
    std::vector<float> firstFrame(kFrameSize, 0.0f);
    for (int i = 0; i < firstFrameLen; ++i) firstFrame[i] = audio[i] * window[i];

    auto mags = realFFTMagnitude(firstFrame, kFrameSize);
    float binHz = static_cast<float>(sampleRate) / kFrameSize;

    float specSum = 0.0f, weightedFreqSum = 0.0f;
    for (size_t b = 0; b < mags.size(); ++b) { specSum += mags[b]; weightedFreqSum += b * binHz * mags[b]; }
    float centroid = specSum > 0.0f ? weightedFreqSum / specSum : 0.0f;

    float spreadSum = 0.0f;
    for (size_t b = 0; b < mags.size(); ++b) { float freq = b * binHz; spreadSum += (freq - centroid) * (freq - centroid) * mags[b]; }
    float spread = specSum > 0.0f ? std::sqrt(spreadSum / specSum) : 0.0f;

    double logSum = 0.0;
    for (float m : mags) logSum += std::log(static_cast<double>(m) + 1e-12);
    float geoMean = static_cast<float>(std::exp(logSum / mags.size()));
    float flatness = geoMean / (specSum / mags.size() + 1e-12f);

    float envMax = 0.0f;
    for (float e : amplitudeEnvelope) envMax = std::max(envMax, e);
    envMax += 1e-12f;

    int onsetFrame = -1;
    for (int i = 0; i < numFrames; ++i)
        if (amplitudeEnvelope[i] / envMax > 0.05f) { onsetFrame = i; break; }

    float attackTimeS = 0.0f;
    if (onsetFrame >= 0)
    {
        std::vector<float> postOnset(amplitudeEnvelope.begin() + onsetFrame, amplitudeEnvelope.end());
        std::vector<float> sorted = postOnset;
        std::sort(sorted.begin(), sorted.end());
        float sustainLevel = sorted[static_cast<size_t>(0.75 * (sorted.size() - 1))];

        int reachFrame = -1;
        for (size_t i = 0; i < postOnset.size(); ++i)
            if (postOnset[i] >= 0.9f * sustainLevel) { reachFrame = static_cast<int>(i); break; }

        attackTimeS = (reachFrame >= 0 ? reachFrame : 0) * static_cast<float>(kHopSize) / static_cast<float>(sampleRate);
    }

    float pitchStability = 0.0f;
    if (voicedCount > 2)
    {
        float variance = 0.0f;
        for (float f : f0Contour) if (f > 0.0f) variance += (f - meanF0) * (f - meanF0);
        variance /= voicedCount;
        pitchStability = std::sqrt(variance);
    }

    float harmonicEnergy = 0.0f;
    for (int i = 0; i < numFrames; ++i)
        if (f0Contour[i] > 0.0f)
            for (float a : harmonicAmps[i]) harmonicEnergy += a;
    float hnrProxy = harmonicEnergy / (specSum * numFrames + 1e-12f);

    properties.pitchHz = meanF0;
    properties.pitchNote = freqToNote(meanF0);
    properties.wavelengthM = wavelength;
    properties.frequencyHz = meanF0;
    properties.timbreCentroidHz = centroid;
    properties.timbreSpreadHz = spread;
    properties.timbreFlatness = flatness;
    properties.amplitudePeakDb = peakDb;
    properties.amplitudeRmsDb = rmsDb;
    properties.waveSpeedMs = v;
    properties.attackTimeS = attackTimeS;
    properties.pitchStabilityHz = pitchStability;
    properties.harmonicToNoiseProxy = hnrProxy;
    properties.durationS = static_cast<float>(numSamples) / static_cast<float>(sampleRate);
}
