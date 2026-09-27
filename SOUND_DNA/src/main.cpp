/*
 main.cpp
 ========
 Sound DNA — FX-Aware Note-Triggered Instrument Synthesizer & Audio Converter
 
 Features:
 1. Input Source Selection:
    - Real-Time Duplex (Guitar DI / Mic)
    - Live Record Take
    - Import Pre-Recorded Audio File (.wav, .mp3, etc.)
 2. Target Instrument Preset Selection:
    - 64-Key Acoustic Grand Piano (A1 - C7)
    - Warm Polyphonic Analog Synth Lead / Pad
    - Deep 808 / Electric Bass
    - Custom User WAV Sample
 3. Sound DNA FX Extraction:
    - Analyzes Reverb RT60 / room ambiance from original wave
    - Extracts rhythmic echo / delay taps and feedback
    - Detects pitch slides, vibrato, and modulation
    - Automatically replicates the acoustic space & modulation on target instruments
 4. Output & Export:
    - Stereo Audio Playback
    - Direct 32-bit Stereo WAV File Export
*/

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "YinPitchDetector.h"
#include "NoiseGateAndOnset.h"
#include "InstrumentPresets.h"
#include "SamplerVoice.h"
#include "WavIO.h"
#include "FxEngine.h"
#include "SoundFxAnalyzer.h"
#include "KalmanPitchFilter.h"
#include "ChebyshevFilter.h"
#include "SoundAnalyzer.h"

#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <iomanip>
#include <string>

// ---------------------------------------------------------------------------
// Preset Manager Helper
// ---------------------------------------------------------------------------

std::unique_ptr<IInstrumentBank> selectInstrumentPreset()
{
    std::cout << "\n=======================================================\n";
    std::cout << "  SELECT TARGET INSTRUMENT PRESET\n";
    std::cout << "=======================================================\n";
    std::cout << "1. 64-Key Acoustic Grand Piano (A1 to C7 / 64 Keys)\n";
    std::cout << "2. Warm Polyphonic Analog Synth (Dual-Saw + Sub)\n";
    std::cout << "3. Deep 808 / Electric Bass (Punchy Sub + Saturation)\n";
    std::cout << "4. Indian Sitar (Jawari Buzz + Taraf Resonance)\n";
    std::cout << "5. Indian Sarangi (Bowed Vocal Strings + Sympathetic)\n";
    std::cout << "6. Custom User WAV Sample (Loaded from disk)\n";
    std::cout << "Choice [1]: ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "2")
    {
        std::cout << "Generating Warm Polyphonic Analog Synth bank...\n";
        return std::make_unique<SynthBank>(44100.0f);
    }
    else if (choice == "3")
    {
        std::cout << "Generating Deep 808 / Electric Bass bank...\n";
        return std::make_unique<BassBank>(44100.0f);
    }
    else if (choice == "4")
    {
        std::cout << "Generating Indian Sitar bank (C2-C5, 37 keys)...\n";
        return std::make_unique<SitarBank>(44100.0f);
    }
    else if (choice == "5")
    {
        std::cout << "Generating Indian Sarangi bank (C3-C6, 37 keys)...\n";
        return std::make_unique<SarangiBank>(44100.0f);
    }
    else if (choice == "6")
    {
        std::cout << "Enter path to WAV file: ";
        std::string path;
        std::getline(std::cin, path);
        
        if (!path.empty() && (path.front() == '"' || path.front() == '\''))
            path = path.substr(1, path.length() - 2);

        auto custom = std::make_unique<CustomWavBank>(path, 60, 44100.0f);
        if (custom->isValid())
        {
            std::cout << "Successfully loaded custom sample: " << path << "\n";
            return custom;
        }
        else
        {
            std::cout << "Could not load file. Falling back to 64-Key Piano.\n";
            return std::make_unique<PianoBank>(44100.0f);
        }
    }

    std::cout << "Generating 64-Key Acoustic Grand Piano bank...\n";
    return std::make_unique<PianoBank>(44100.0f);
}

// ---------------------------------------------------------------------------
// Duplex Real-Time Context for Note-Triggered Playback
// ---------------------------------------------------------------------------

struct RealTimeEngineContext
{
    float sampleRate = 44100.0f;
    std::atomic<float> inputGain{1.0f};
    std::atomic<float> dryMix{0.0f};
    std::atomic<float> wetMix{0.9f};
    std::atomic<float> masterGain{1.0f};

    NoiseGateAndOnset noiseGate;
    YinPitchDetector pitchDetector;
    PolyVoiceManager voiceManager;
    StereoReverb reverb;
    const IInstrumentBank* instrument = nullptr;

    std::vector<float> analysisBuffer;
    size_t writeHead = 0;
    static constexpr size_t ANALYSIS_SIZE = 1024;

    std::atomic<int> lastMidiNote{-1};
    std::atomic<float> lastPitchHz{0.0f};
    std::atomic<float> lastPeakLevel{0.0f};
    std::atomic<bool> gateOpenState{false};

    RealTimeEngineContext()
        : noiseGate(44100.0f),
          pitchDetector(1024, 44100.0f, 0.18f),
          analysisBuffer(ANALYSIS_SIZE, 0.0f)
    {
        reverb.init(44100.0f, 0.6f, 0.35f, 0.25f);
    }
};

void duplexCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    auto* ctx = static_cast<RealTimeEngineContext*>(pDevice->pUserData);
    const float* in = static_cast<const float*>(pInput);
    float* out = static_cast<float*>(pOutput);

    float gain = ctx->inputGain.load();
    float dry = ctx->dryMix.load();
    float wet = ctx->wetMix.load();
    float master = ctx->masterGain.load();

    float peak = 0.0f;

    for (ma_uint32 i = 0; i < frameCount; ++i)
    {
        float rawIn = (in ? in[i] : 0.0f) * gain;
        if (std::abs(rawIn) > peak) peak = std::abs(rawIn);

        bool onsetTriggered = false;
        float onsetVelocity = 0.0f;
        float gatedIn = ctx->noiseGate.processSample(rawIn, onsetTriggered, onsetVelocity);

        ctx->analysisBuffer[ctx->writeHead] = gatedIn;
        ctx->writeHead = (ctx->writeHead + 1) % ctx->ANALYSIS_SIZE;

        if (onsetTriggered)
        {
            std::vector<float> linearBuf(ctx->ANALYSIS_SIZE);
            for (size_t k = 0; k < ctx->ANALYSIS_SIZE; ++k)
                linearBuf[k] = ctx->analysisBuffer[(ctx->writeHead + k) % ctx->ANALYSIS_SIZE];

            float confidence = 0.0f;
            float pitch = ctx->pitchDetector.detectPitch(linearBuf.data(), static_cast<int>(ctx->ANALYSIS_SIZE), &confidence);

            if (pitch > 0.0f)
            {
                int midi = YinPitchDetector::freqToMidi(pitch);
                ctx->lastMidiNote.store(midi);
                ctx->lastPitchHz.store(pitch);

                if (ctx->instrument)
                {
                    const AudioSampleBuffer* sample = ctx->instrument->getSampleForMidi(midi);
                    if (sample)
                        ctx->voiceManager.noteOn(midi, onsetVelocity, sample);
                }
            }
        }

        float sampleAudio = ctx->voiceManager.renderSample();
        float mixed = (rawIn * dry) + (sampleAudio * wet);
        
        float outL = 0.0f, outR = 0.0f;
        ctx->reverb.process(mixed, mixed, outL, outR);

        out[i] = ((outL + outR) * 0.5f) * master;
    }

    ctx->lastPeakLevel.store(peak);
    ctx->gateOpenState.store(ctx->noiseGate.isGateOpen());
}

// ---------------------------------------------------------------------------
// Playback Context for Stereo buffers
// ---------------------------------------------------------------------------

struct StereoPlaybackContext
{
    const std::vector<float>* buffer = nullptr; // interleaved L/R
    size_t pos = 0;
    std::atomic<bool> finished{true};
};

void stereoPlaybackCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;
    auto* ctx = static_cast<StereoPlaybackContext*>(pDevice->pUserData);
    float* out = static_cast<float*>(pOutput);

    if (ctx->finished.load() || ctx->buffer == nullptr)
    {
        std::fill(out, out + (frameCount * 2), 0.0f);
        return;
    }

    for (ma_uint32 i = 0; i < frameCount * 2; ++i)
    {
        if (ctx->pos < ctx->buffer->size())
            out[i] = (*ctx->buffer)[ctx->pos++];
        else
        {
            out[i] = 0.0f;
            ctx->finished.store(true);
        }
    }
}

void playStereoBuffer(const std::vector<float>& interleavedStereo)
{
    StereoPlaybackContext playCtx;
    playCtx.buffer = &interleavedStereo;

    ma_device_config playConfig = ma_device_config_init(ma_device_type_playback);
    playConfig.playback.format   = ma_format_f32;
    playConfig.playback.channels = 2; // Stereo
    playConfig.sampleRate        = 44100;
    playConfig.dataCallback      = stereoPlaybackCallback;
    playConfig.pUserData         = &playCtx;

    ma_device playDevice;
    if (ma_device_init(nullptr, &playConfig, &playDevice) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to initialize playback device.\n";
        return;
    }

    std::cout << "Playing converted audio with FX space... (Press ENTER to stop early)\n";
    playCtx.pos = 0;
    playCtx.finished.store(false);
    ma_device_start(&playDevice);

    while (!playCtx.finished.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ma_device_stop(&playDevice);
    ma_device_uninit(&playDevice);
}

// ---------------------------------------------------------------------------
// FX-Aware Audio to Instrument Converter
// ---------------------------------------------------------------------------

std::vector<float> convertAudioToInstrumentWithFx(
    const std::vector<float>& inputAudio,
    const IInstrumentBank& instrument,
    const SoundFxProfile& fxProfile)
{
    std::cout << "\nConverting audio to instrument: [" << instrument.getName() << "] with Sound DNA FX...\n";

    if (inputAudio.empty()) return {};

    // Auto-normalize input gain
    float maxVal = 0.0f;
    for (float s : inputAudio)
        if (std::abs(s) > maxVal) maxVal = std::abs(s);

    float gainBoost = 1.0f;
    if (maxVal > 1e-5f && maxVal < 0.5f)
    {
        gainBoost = 0.8f / maxVal;
        std::cout << "Input level was low (peak: " << maxVal << "). Auto-normalized with x"
                  << std::fixed << std::setprecision(2) << gainBoost << " gain boost.\n";
    }

    std::vector<float> normalizedInput(inputAudio.size());
    for (size_t i = 0; i < inputAudio.size(); ++i)
        normalizedInput[i] = inputAudio[i] * gainBoost;

    const float sampleRate = 44100.0f;
    const int hopSize = 256;      // analyze every 5.8 ms
    const int frameSize = 2048;   // 46.4 ms analysis window (needs ≥2 periods of low E2 = 82 Hz)

    // --- Envelope Analysis: Extract attack/release from the input ---
    std::cout << "Analyzing input envelope (attack/release characteristics)...\n";
    auto envelopeInfo = EnvelopeAnalyzer::analyzeGlobalEnvelope(normalizedInput, sampleRate);
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  * Detected Attack:  " << envelopeInfo.attackTimeMs << " ms\n";
    std::cout << "  * Detected Release: " << envelopeInfo.releaseTimeMs << " ms\n";

    // --- Dynamic Range Analysis for velocity mapping ---
    std::vector<float> rmsEnvelope;
    {
        int rmsWindowSize = static_cast<int>(sampleRate * 0.010f); // 10ms
        int numWindows = static_cast<int>(normalizedInput.size()) / rmsWindowSize;
        rmsEnvelope.resize(numWindows, 0.0f);
        for (int w = 0; w < numWindows; ++w)
        {
            float sum = 0.0f;
            for (int i = 0; i < rmsWindowSize; ++i)
            {
                float s = normalizedInput[w * rmsWindowSize + i];
                sum += s * s;
            }
            rmsEnvelope[w] = std::sqrt(sum / rmsWindowSize);
        }
    }
    // Find the dynamic range (min/max RMS of active segments)
    float minActiveRms = 1e10f, maxActiveRms = 0.0f;
    for (float rms : rmsEnvelope)
    {
        if (rms > 0.005f) // only count active segments
        {
            if (rms < minActiveRms) minActiveRms = rms;
            if (rms > maxActiveRms) maxActiveRms = rms;
        }
    }
    float dynamicRange = (maxActiveRms > minActiveRms) ? (maxActiveRms - minActiveRms) : 0.3f;
    std::cout << "  * Dynamic Range:    " << std::setprecision(3) << minActiveRms
              << " - " << maxActiveRms << " RMS\n";

    // Pre-condition audio through 4th-Order Chebyshev Filter (1100 Hz lowpass + 70 Hz highpass)
    // Eliminates pick noise, high harmonic hash, and sub-bass rumble
    ChebyshevFilter chebFilter(sampleRate, 2200.0f, 70.0f);
    std::vector<float> filteredInput(normalizedInput.size());
    chebFilter.processBuffer(normalizedInput.data(), filteredInput.data(), static_cast<int>(normalizedInput.size()));

    YinPitchDetector pitchDetector(frameSize, sampleRate, 0.18f);
    KalmanPitchFilter kalmanFilter(2.0f, 8.0f); // smooth pitch, reject outliers
    PolyVoiceManager voiceManager;

    // Apply envelope shaping from input analysis
    voiceManager.setEnvelopeShaping(envelopeInfo.attackTimeMs, envelopeInfo.releaseTimeMs, sampleRate);

    // Initialize FX Processors based on extracted Sound DNA
    StereoChorus chorus;
    chorus.init(sampleRate, fxProfile.modRateHz, fxProfile.modDepthMs, fxProfile.modWet);

    FeedbackDelay delay;
    delay.init(sampleRate, fxProfile.delayTimeMs, fxProfile.delayFeedback, fxProfile.delayWet);

    StereoReverb reverb;
    reverb.init(sampleRate, fxProfile.reverbRoomSize, fxProfile.reverbDamping, fxProfile.reverbWet);

    std::vector<float> dryMonoRender;
    dryMonoRender.reserve(inputAudio.size() + static_cast<size_t>(sampleRate * 2.5f));

    int currentPlayingMidi = -1;
    int noteHoldFrames = 0;
    int notesDetected = 0;
    float currentVelocity = 0.8f;
    float prevRms = 0.0f;
    int candidateMidi = -1;
    int candidateStableFrames = 0;
    const int minHoldFramesBeforeRetrigger = static_cast<int>(sampleRate * 0.150f / hopSize); // 150ms debounce

    // 1. Render Dry Note Performances with Chebyshev + Kalman pitch tracking
    for (size_t hopStart = 0; hopStart + frameSize <= normalizedInput.size(); hopStart += hopSize)
    {
        float sumSq = 0.0f;
        for (int j = 0; j < frameSize; ++j)
        {
            float val = normalizedInput[hopStart + j];
            sumSq += val * val;
        }
        float rms = std::sqrt(sumSq / frameSize);

        // Track transient envelope jump for note re-triggering (true pick attack)
        bool isOnset = (rms > prevRms * 1.6f && rms > 0.010f && noteHoldFrames > minHoldFramesBeforeRetrigger);
        prevRms = rms;

        int detectedMidi = -1;
        float detectedPitch = 0.0f;
        float confidence = 0.0f;

        if (rms > 0.005f)
        {
            // Analyze Chebyshev-filtered audio to avoid pick noise & harmonic confusion
            float rawPitch = pitchDetector.detectPitch(&filteredInput[hopStart], frameSize, &confidence);

            if (confidence >= 0.55f && rawPitch > 60.0f && rawPitch < 1400.0f)
            {
                // On confirmed onset transients, reset Kalman to accept the new pitch instantly
                if (isOnset && rawPitch > 0.0f)
                {
                    kalmanFilter.resetTo(rawPitch);
                    detectedPitch = rawPitch;
                }
                else
                {
                    detectedPitch = kalmanFilter.update(rawPitch);
                }
                detectedMidi = YinPitchDetector::freqToMidi(detectedPitch);
            }
            else
            {
                kalmanFilter.update(-1.0f);
            }
        }
        else
        {
            kalmanFilter.update(-1.0f);
        }

        if (detectedMidi > 0)
        {
            // Track pitch stability across consecutive frames
            if (detectedMidi == candidateMidi)
            {
                candidateStableFrames++;
            }
            else
            {
                candidateMidi = detectedMidi;
                candidateStableFrames = 1;
            }

            // Decide whether to trigger:
            // 1. New note onset (pick attack after debounce window)
            // 2. Legato pitch change (≥2 semitones stable for 3 frames, or 1 semitone stable for 4 frames)
            // 3. Initial note strike from silence
            int semitoneDiff = std::abs(detectedMidi - currentPlayingMidi);
            bool isLegatoJump = (semitoneDiff >= 2 && candidateStableFrames >= 3) ||
                                (semitoneDiff == 1 && candidateStableFrames >= 4);
            bool isInitialStrike = (currentPlayingMidi == -1 && candidateStableFrames >= 2);
            bool isNewPickOnset = (isOnset && candidateStableFrames >= 1);

            bool shouldTrigger = (isInitialStrike || isNewPickOnset || isLegatoJump);

            if (shouldTrigger)
            {
                if (currentPlayingMidi != -1 && currentPlayingMidi != detectedMidi)
                {
                    voiceManager.noteOff(currentPlayingMidi);
                }

                currentPlayingMidi = detectedMidi;
                noteHoldFrames = 0;

                // Dynamic velocity mapping: map RMS to 0.25-1.0 based on input's dynamic range
                float normalizedRms = (rms - minActiveRms) / (std::max)(0.001f, dynamicRange);
                normalizedRms = (std::max)(0.0f, (std::min)(1.0f, normalizedRms));
                currentVelocity = 0.25f + 0.75f * std::sqrt(normalizedRms);

                const AudioSampleBuffer* sampleBuf = instrument.getSampleForMidi(detectedMidi);
                if (sampleBuf)
                {
                    // Extract full Sound DNA for this note (timbre, attack, spectral characteristics)
                    NoteSoundDNA dna = SoundAnalyzer::extractNoteDNA(
                        &normalizedInput[hopStart], std::min(frameSize, static_cast<int>(normalizedInput.size() - hopStart)),
                        sampleRate, detectedPitch, currentVelocity);

                    voiceManager.noteOn(detectedMidi, dna.velocity, sampleBuf,
                                        dna.attackTimeMs, envelopeInfo.releaseTimeMs, dna.timbreCentroidHz);
                    notesDetected++;
                    std::cout << "  Note #" << std::setw(3) << notesDetected << ": "
                              << std::setw(4) << YinPitchDetector::midiToNoteName(detectedMidi)
                              << " (" << std::fixed << std::setprecision(1) << detectedPitch << " Hz) at "
                              << std::setprecision(2) << (hopStart / sampleRate) << "s"
                              << "  vel=" << std::setprecision(2) << dna.velocity
                              << "  atk=" << std::setprecision(1) << dna.attackTimeMs << "ms"
                              << "  centroid=" << std::setprecision(0) << dna.timbreCentroidHz << "Hz"
                              << "  spread=" << dna.timbreSpreadHz << "Hz"
                              << "  flat=" << std::setprecision(4) << dna.timbreFlatness
                              << "  bright=" << std::setprecision(2) << dna.brightness
                              << "  wl=" << std::setprecision(2) << dna.wavelengthM << "m\n";
                }
            }
            else
            {
                noteHoldFrames++;
            }
        }
        else
        {
            candidateStableFrames = 0;
            candidateMidi = -1;

            if (currentPlayingMidi != -1 && rms < 0.003f)
            {
                voiceManager.noteOff(currentPlayingMidi);
                currentPlayingMidi = -1;
            }
            noteHoldFrames = 0;
        }

        for (int h = 0; h < hopSize; ++h)
            dryMonoRender.push_back(voiceManager.renderSample());
    }

    // Render instrument decay tail (2.5 seconds)
    for (int t = 0; t < static_cast<int>(sampleRate * 2.5f); ++t)
        dryMonoRender.push_back(voiceManager.renderSample());

    std::cout << "Detected " << notesDetected << " notes (Kalman-smoothed). Now processing FX chain (Modulation -> Delay -> Reverb)...\n";

    // 2. Process FX Chain (Chorus -> Delay -> Reverb) into Stereo Master
    std::vector<float> stereoOutput;
    stereoOutput.reserve(dryMonoRender.size() * 2);

    for (float drySample : dryMonoRender)
    {
        float l = drySample, r = drySample;

        // Stage 1: Modulation / Chorus
        chorus.process(l, r, l, r);

        // Stage 2: Feedback Delay / Echo
        delay.process(l, r, l, r);

        // Stage 3: Schroeder Stereo Reverb
        reverb.process(l, r, l, r);

        stereoOutput.push_back(l);
        stereoOutput.push_back(r);
    }

    std::cout << "Conversion complete with FX! Produced " << (stereoOutput.size() / 2.0 / 44100.0) << "s stereo master.\n";
    return stereoOutput;
}

// ---------------------------------------------------------------------------
// Recording context for taking live takes
// ---------------------------------------------------------------------------

struct RecordContext
{
    std::vector<float> buffer;
    std::atomic<bool> recording{false};
};

void recordCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pOutput;
    auto* ctx = static_cast<RecordContext*>(pDevice->pUserData);
    if (!ctx->recording.load()) return;

    const float* in = static_cast<const float*>(pInput);
    for (ma_uint32 i = 0; i < frameCount; ++i)
        ctx->buffer.push_back(in[i]);
}

std::vector<float> recordInputTake()
{
    RecordContext recCtx;
    ma_device_config recConfig = ma_device_config_init(ma_device_type_capture);
    recConfig.capture.format   = ma_format_f32;
    recConfig.capture.channels = 1;
    recConfig.sampleRate       = 44100;
    recConfig.dataCallback     = recordCallback;
    recConfig.pUserData        = &recCtx;

    ma_device recDevice;
    if (ma_device_init(nullptr, &recConfig, &recDevice) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to initialize recording device.\n";
        return {};
    }

    std::cout << "\nPress ENTER to start recording your take...";
    std::cin.get();

    recCtx.buffer.clear();
    recCtx.recording.store(true);
    ma_device_start(&recDevice);
    std::cout << "Recording... Play your instrument! Press ENTER to stop.\n";
    std::cin.get();

    recCtx.recording.store(false);
    ma_device_stop(&recDevice);
    ma_device_uninit(&recDevice);

    std::cout << "Recorded " << (recCtx.buffer.size() / 44100.0) << " seconds of audio.\n";
    return recCtx.buffer;
}

// ---------------------------------------------------------------------------
// Audio Device Selection Helper
// ---------------------------------------------------------------------------

struct SelectedDevices
{
    ma_device_id captureId;
    ma_device_id playbackId;
    bool hasCaptureId = false;
    bool hasPlaybackId = false;
};

SelectedDevices selectAudioDevices()
{
    SelectedDevices sel;

    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to initialize audio context for device enumeration.\n";
        return sel;
    }

    ma_device_info* pCaptureDevices;
    ma_uint32 captureCount;
    ma_device_info* pPlaybackDevices;
    ma_uint32 playbackCount;

    if (ma_context_get_devices(&context, &pPlaybackDevices, &playbackCount,
                               &pCaptureDevices, &captureCount) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to enumerate audio devices.\n";
        ma_context_uninit(&context);
        return sel;
    }

    // --- Show Input (Capture) Devices ---
    std::cout << "\n=======================================================\n";
    std::cout << "  SELECT INPUT DEVICE (Your Instrument / Mic / Interface)\n";
    std::cout << "=======================================================\n";
    for (ma_uint32 i = 0; i < captureCount; ++i)
    {
        std::cout << "  " << (i + 1) << ". " << pCaptureDevices[i].name;
        if (pCaptureDevices[i].isDefault)
            std::cout << "  [DEFAULT]";
        std::cout << "\n";
    }
    std::cout << "Choice [1]: ";

    std::string inputChoice;
    std::getline(std::cin, inputChoice);

    int inputIdx = 0;
    if (!inputChoice.empty())
    {
        try { inputIdx = std::stoi(inputChoice) - 1; } catch (...) { inputIdx = 0; }
    }
    if (inputIdx < 0 || inputIdx >= static_cast<int>(captureCount))
        inputIdx = 0;

    sel.captureId = pCaptureDevices[inputIdx].id;
    sel.hasCaptureId = true;
    std::cout << "Selected INPUT: " << pCaptureDevices[inputIdx].name << "\n";

    // --- Show Output (Playback) Devices ---
    std::cout << "\n=======================================================\n";
    std::cout << "  SELECT OUTPUT DEVICE (Your Speakers / Headphones)\n";
    std::cout << "=======================================================\n";
    for (ma_uint32 i = 0; i < playbackCount; ++i)
    {
        std::cout << "  " << (i + 1) << ". " << pPlaybackDevices[i].name;
        if (pPlaybackDevices[i].isDefault)
            std::cout << "  [DEFAULT]";
        std::cout << "\n";
    }
    std::cout << "Choice [1]: ";

    std::string outputChoice;
    std::getline(std::cin, outputChoice);

    int outputIdx = 0;
    if (!outputChoice.empty())
    {
        try { outputIdx = std::stoi(outputChoice) - 1; } catch (...) { outputIdx = 0; }
    }
    if (outputIdx < 0 || outputIdx >= static_cast<int>(playbackCount))
        outputIdx = 0;

    sel.playbackId = pPlaybackDevices[outputIdx].id;
    sel.hasPlaybackId = true;
    std::cout << "Selected OUTPUT: " << pPlaybackDevices[outputIdx].name << "\n";

    ma_context_uninit(&context);
    return sel;
}

// ---------------------------------------------------------------------------
// Live Duplex Runner (with Device Selection)
// ---------------------------------------------------------------------------

void runLiveDuplex()
{
    // Step 1: Select audio devices
    auto devices = selectAudioDevices();

    // Step 2: Select instrument preset
    auto instrument = selectInstrumentPreset();

    RealTimeEngineContext engineCtx;
    engineCtx.instrument = instrument.get();

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format    = ma_format_f32;
    config.capture.channels  = 1;
    config.playback.format   = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate        = 44100;
    config.dataCallback      = duplexCallback;
    config.pUserData         = &engineCtx;

    // Assign selected device IDs
    if (devices.hasCaptureId)
        config.capture.pDeviceID = &devices.captureId;
    if (devices.hasPlaybackId)
        config.playback.pDeviceID = &devices.playbackId;

    ma_device device;
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to initialize duplex audio device.\n";
        std::cerr << "       Make sure your selected input/output devices support 44100 Hz mono.\n";
        std::cerr << "       Try selecting different devices.\n";
        return;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        std::cerr << "ERROR: Failed to start duplex audio device.\n";
        ma_device_uninit(&device);
        return;
    }

    std::cout << "\n[Audio Engine Running]\n";
    std::cout << "  - Active Preset: " << instrument->getName() << "\n";
    std::cout << "  - Polyphony:     8 Voices with Dynamic Stealing\n";
    std::cout << "  - Pitch:         YIN Real-Time + Kalman Smoothing\n";
    std::cout << "\nPlay your instrument! Press ENTER anytime to stop.\n\n";

    std::atomic<bool> running{true};
    std::thread uiThread([&]() {
        while (running.load())
        {
            float peak = engineCtx.lastPeakLevel.load();
            bool gate = engineCtx.gateOpenState.load();
            int midi = engineCtx.lastMidiNote.load();
            float pitch = engineCtx.lastPitchHz.load();
            int activeVoices = engineCtx.voiceManager.getActiveVoiceCount();

            std::string vuMeter = "";
            int bars = static_cast<int>(peak * 25.0f);
            if (bars > 25) bars = 25;
            for (int b = 0; b < bars; ++b) vuMeter += "=";
            while (vuMeter.length() < 25) vuMeter += " ";

            std::cout << "\r[In: " << (gate ? "GATE OPEN " : "GATE SHUT ") << "] "
                      << "[" << vuMeter << "] "
                      << "Note: " << std::setw(4) << (midi > 0 ? YinPitchDetector::midiToNoteName(midi) : "---")
                      << " (" << std::fixed << std::setprecision(1) << std::setw(6) << pitch << " Hz) | "
                      << "Voices: " << activeVoices << "   " << std::flush;

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    std::cin.get();
    running.store(false);
    if (uiThread.joinable()) uiThread.join();

    ma_device_stop(&device);
    ma_device_uninit(&device);
    std::cout << "\nStopped Real-Time Engine.\n";
}

// ---------------------------------------------------------------------------
// File Import / Record Conversion Workflow with FX Extraction
// ---------------------------------------------------------------------------

void runFileOrRecordConversion(bool isImport)
{
    std::vector<float> inputBuffer;

    if (isImport)
    {
        std::cout << "\nEnter path to audio file (.wav, .mp3, etc.): ";
        std::string path;
        std::getline(std::cin, path);

        if (!path.empty() && (path.front() == '"' || path.front() == '\''))
            path = path.substr(1, path.length() - 2);

        if (!WavIO::loadAudioFile(path, inputBuffer, 44100))
        {
            std::cerr << "Failed to import audio file.\n";
            return;
        }
        std::cout << "Imported audio file: " << (inputBuffer.size() / 44100.0) << " seconds.\n";
    }
    else
    {
        inputBuffer = recordInputTake();
        if (inputBuffer.empty()) return;
    }

    // Step 1: Analyze Sound FX DNA from the input wave
    std::cout << "\n=======================================================\n";
    std::cout << "  ANALYZING INPUT SOUND DNA & ACOUSTIC FX...\n";
    std::cout << "=======================================================\n";
    auto fxProfile = SoundFxAnalyzer::profileAudio(inputBuffer, 44100.0f);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  * Reverb Ambiance:  " << (fxProfile.reverbWet * 100.0f) << "% wet (Room size: " << fxProfile.reverbRoomSize << ")\n";
    if (fxProfile.delayTimeMs > 0.0f)
        std::cout << "  * Echo / Delay:     " << fxProfile.delayTimeMs << " ms (Feedback: " << (fxProfile.delayFeedback * 100.0f) << "%)\n";
    else
        std::cout << "  * Echo / Delay:     None detected\n";
    if (fxProfile.hasSlideOrVibrato)
        std::cout << "  * Modulation/Slide: Detected (Applying " << fxProfile.modDepthMs << "ms stereo chorus/vibrato)\n";
    else
        std::cout << "  * Modulation/Slide: Subtle/None\n";
    std::cout << "=======================================================\n";

    // Step 2: Target Preset Selection
    auto instrument = selectInstrumentPreset();

    // Step 3: Render FX-Enhanced Instrument
    auto convertedStereoBuffer = convertAudioToInstrumentWithFx(inputBuffer, *instrument, fxProfile);

    // Step 4: Prompt for Playback / Save
    std::cout << "\n=== OUTPUT OPTIONS ===\n";
    std::cout << "1. Play Converted Instrument Audio (Stereo)\n";
    std::cout << "2. Export Converted Audio to Stereo WAV File\n";
    std::cout << "3. Both (Play and Export)\n";
    std::cout << "Choice [3]: ";

    std::string outChoice;
    std::getline(std::cin, outChoice);

    if (outChoice == "1" || outChoice == "3" || outChoice.empty())
    {
        playStereoBuffer(convertedStereoBuffer);
    }

    if (outChoice == "2" || outChoice == "3" || outChoice.empty())
    {
        std::cout << "Enter output WAV path [converted_instrument.wav]: ";
        std::string outPath;
        std::getline(std::cin, outPath);
        if (outPath.empty()) outPath = "converted_instrument.wav";

        if (!outPath.empty() && (outPath.front() == '"' || outPath.front() == '\''))
            outPath = outPath.substr(1, outPath.length() - 2);

        if (WavIO::saveWavFileStereo(outPath, convertedStereoBuffer, 44100))
        {
            std::cout << "Successfully exported converted stereo master to: " << outPath << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Main Menu
// ---------------------------------------------------------------------------

int main()
{
    while (true)
    {
        std::cout << "\n=======================================================\n";
        std::cout << "  SOUND DNA -- FX-Aware Note & Instrument Synthesizer  \n";
        std::cout << "=======================================================\n";
        std::cout << "1. Live Real-Time Duplex (Guitar DI / Mic -> Instrument)\n";
        std::cout << "2. Record a Take & Convert to Instrument Preset\n";
        std::cout << "3. Import Pre-Recorded Audio File & Convert to Instrument\n";
        std::cout << "4. Exit\n";
        std::cout << "Select mode (1-4) [1]: ";

        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "4" || choice == "exit" || choice == "q")
        {
            std::cout << "Goodbye!\n";
            break;
        }
        else if (choice == "2")
        {
            runFileOrRecordConversion(false);
        }
        else if (choice == "3")
        {
            runFileOrRecordConversion(true);
        }
        else
        {
            runLiveDuplex();
        }
    }

    return 0;
}
