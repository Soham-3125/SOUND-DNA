#include "SoundDNA.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

static const IColor COLOR_CYAN = IColor(255, 0, 220, 255);

SoundDNA::SoundDNA(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
, mNoiseGate(44100.0f)
, mPitchDetector(2048, 44100.0f, 0.18f)
, mKalmanFilter(2.0f, 8.0f)
, mChebyshevFilter(44100.0f, 2200.0f, 70.0f)
{
  GetParam(kParamPreset)->InitEnum("Preset", 0, 5, "", IParam::kFlagsNone, "Piano", "Synth", "Bass", "Sitar", "Sarangi");
  GetParam(kParamInputGain)->InitDouble("Input Gain", 1.0, 0.0, 2.0, 0.01);
  GetParam(kParamGateThresh)->InitDouble("Gate Thresh", -45.0, -80.0, 0.0, 0.1, "dB");
  GetParam(kParamDryMix)->InitDouble("Dry Mix", 0.0, 0.0, 1.0, 0.01);
  GetParam(kParamWetMix)->InitDouble("Wet Mix", 1.0, 0.0, 1.0, 0.01);
  mCurrentInstrument = std::make_unique<PianoBank>(44100.0f);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };
  
  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(IColor(255, 18, 22, 28)); // Dark modern slate
    pGraphics->LoadFont("Roboto-Regular", "Roboto-Regular.ttf");
    
    // Header Title
    pGraphics->AttachControl(new ITextControl(IRECT(20, 12, 620, 42), "SOUND DNA ENGINE", IText(24, COLOR_CYAN, "Roboto-Regular", EAlign::Center)));
    
    // Preset Caption Box
    pGraphics->AttachControl(new ICaptionControl(IRECT(170, 48, 470, 78), kParamPreset, IText(16, COLOR_WHITE), IColor(255, 35, 42, 54), true));
    
    // Knobs (Row 1)
    int knobY = 90;
    pGraphics->AttachControl(new IVKnobControl(IRECT(50,  knobY, 130, knobY+65), kParamInputGain));
    pGraphics->AttachControl(new IVKnobControl(IRECT(190, knobY, 270, knobY+65), kParamGateThresh));
    pGraphics->AttachControl(new IVKnobControl(IRECT(330, knobY, 410, knobY+65), kParamDryMix));
    pGraphics->AttachControl(new IVKnobControl(IRECT(470, knobY, 550, knobY+65), kParamWetMix));
    
    // Sound DNA Live Metrics Dashboard (Custom Drawn Panel)
    IRECT dnaBox(30, 175, 610, 465);
    pGraphics->AttachControl(new ILambdaControl(dnaBox, [this](ILambdaControl* pCtrl, IGraphics& g, IRECT& r) {
      // Background card with neon border
      g.FillRoundRect(IColor(255, 24, 30, 40), r, 8.0f);
      g.DrawRoundRect(COLOR_CYAN.WithOpacity(0.35f), r, 8.0f, nullptr, 1.5f);

      // Section Title
      g.DrawText(IText(14, COLOR_CYAN, "Roboto-Regular", EAlign::Near), "SOUND DNA -- CAPTURED PROPERTIES", r.GetPadded(-15).GetFromTop(20));

      int midi = mCurrentMidiNote.load();
      float f0 = mCurrentPitchHz.load();
      float lambda = mCurrentWavelengthM.load();
      float centroid = mCurrentTimbreCentroidHz.load();
      float spread = mCurrentTimbreSpreadHz.load();
      float flatness = mCurrentTimbreFlatness.load();
      float attack = mCurrentAttackTimeMs.load();
      float vel = mCurrentVelocity.load();
      float peakDb = mCurrentPeakDb.load();
      float rmsDb = mCurrentRmsDb.load();

      char line1[128], line2[128], line3[128];

      if (midi > 0 && f0 > 30.0f) {
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int oct = (midi / 12) - 1;
        const char* name = noteNames[midi % 12];
        snprintf(line1, sizeof(line1), "Pitch: %s%d (%.1f Hz)  |  Wavelength: %.2f m  |  Speed: 343.4 m/s", name, oct, f0, lambda);
      } else {
        snprintf(line1, sizeof(line1), "Pitch: Waiting for input...  |  Wavelength: ---  |  Speed: 343.4 m/s");
      }

      snprintf(line2, sizeof(line2), "Timbre Centroid: %.0f Hz (%s)  |  Spread: %.0f Hz  |  Flatness: %.4f",
               centroid, (centroid > 1800.0f ? "Crisp/Bright" : (centroid > 1000.0f ? "Balanced" : "Warm/Dark")), spread, flatness);

      snprintf(line3, sizeof(line3), "Velocity: %.2f  |  Attack Time: %.1f ms  |  Peak: %.1f dB  |  RMS: %.1f dB",
               vel, attack, peakDb, rmsDb);

      IRECT textR = r.GetPadded(-15).GetVShifted(28);
      g.DrawText(IText(15, COLOR_WHITE, "Roboto-Regular", EAlign::Near), line1, textR.GetFromTop(22));
      g.DrawText(IText(13, IColor(255, 180, 205, 230), "Roboto-Regular", EAlign::Near), line2, textR.GetVShifted(28).GetFromTop(20));
      g.DrawText(IText(13, IColor(255, 160, 220, 180), "Roboto-Regular", EAlign::Near), line3, textR.GetVShifted(52).GetFromTop(20));

      // Timbre Spectrum Visualizer Bar
      IRECT barR(r.L + 15, r.T + 125, r.R - 15, r.T + 145);
      g.FillRoundRect(IColor(255, 15, 18, 24), barR, 4.0f);
      float barNorm = std::clamp((centroid - 300.0f) / 3200.0f, 0.05f, 1.0f);
      IRECT fillBar = barR.GetFromLeft(barR.W() * barNorm);
      IColor barColor = (centroid > 1800.0f) ? COLOR_YELLOW : ((centroid > 1000.0f) ? COLOR_GREEN : COLOR_BLUE);
      g.FillRoundRect(barColor, fillBar, 4.0f);
      g.DrawText(IText(11, COLOR_WHITE, "Roboto-Regular", EAlign::Center), "Timbre Centroid Warm-to-Bright Spectrum", barR);
    }, 1000/30)); // 30 FPS update

    // Live Input VU Meter Bar (Bottom)
    IRECT meterR(30, 475, 610, 498);
    pGraphics->AttachControl(new ILambdaControl(meterR, [this](ILambdaControl* pCtrl, IGraphics& g, IRECT& r) {
      g.FillRoundRect(IColor(255, 15, 18, 24), r, 4.0f);
      float peakDb = 20.0f * std::log10(std::max(0.0001f, mCurrentInputPeak.load()));
      float meterFill = std::clamp((peakDb + 60.0f) / 60.0f, 0.0f, 1.0f);
      IRECT fillR = r.GetFromLeft(r.W() * meterFill);
      g.FillRoundRect(mGateIsOpen.load() ? COLOR_GREEN : COLOR_ORANGE, fillR, 4.0f);
      g.DrawText(IText(11, COLOR_WHITE, "Roboto-Regular", EAlign::Center), "Input Audio Level (VU)", r);
    }, 1000/30));
  };
#endif
}

void SoundDNA::OnParamChange(int paramIdx)
{
  if (paramIdx == kParamPreset) {
    int preset = GetParam(kParamPreset)->Int();
    float sr = GetSampleRate();
    if (sr <= 0) sr = 44100.0f;
    
    switch(preset) {
      case 1: mCurrentInstrument = std::make_unique<SynthBank>(sr); break;
      case 2: mCurrentInstrument = std::make_unique<BassBank>(sr); break;
      case 3: mCurrentInstrument = std::make_unique<SitarBank>(sr); break;
      case 4: mCurrentInstrument = std::make_unique<SarangiBank>(sr); break;
      default: mCurrentInstrument = std::make_unique<PianoBank>(sr); break;
    }
  }
}

void SoundDNA::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  float inGain = GetParam(kParamInputGain)->Value();
  float gateThresh = GetParam(kParamGateThresh)->Value();
  float dryMix = GetParam(kParamDryMix)->Value();
  float wetMix = GetParam(kParamWetMix)->Value();
  
  mNoiseGate.setThresholdDb(gateThresh);

  sample* in1 = inputs[0];
  sample* out1 = outputs[0];
  sample* out2 = outputs[1];

  float peak = 0.0f;

  for (int s = 0; s < nFrames; ++s) {
    float rawIn = in1[s] * inGain;
    if (std::abs(rawIn) > peak) peak = std::abs(rawIn);
    
    // We run the noise gate just to get the onset triggers.
    // The gated output is ignored; live audio flows through uninterrupted.
    bool onsetTriggered = false;
    float onsetVelocity = 0.0f;
    mNoiseGate.processSample(rawIn, onsetTriggered, onsetVelocity);
    
    if (mAnalysisBuffer.size() < ANALYSIS_SIZE) mAnalysisBuffer.resize(ANALYSIS_SIZE, 0.0f);
    mAnalysisBuffer[mWriteHead] = rawIn; // Feed raw, ungated audio into analysis buffer
    mWriteHead = (mWriteHead + 1) % ANALYSIS_SIZE;
    
    mSamplesSinceLastNoteTrigger++;

    if (onsetTriggered) {
      // Wait ~25ms (1100 samples) for the pick transient to pass and the string to oscillate cleanly
      mSamplesUntilAnalysis = 1100;
      mLastOnsetVelocity = onsetVelocity;
    }

    if (mSamplesUntilAnalysis > 0) {
      mSamplesUntilAnalysis--;
      if (mSamplesUntilAnalysis == 0) {
        // Collect past ANALYSIS_SIZE samples into a linear buffer
        std::vector<float> linearBuf(ANALYSIS_SIZE);
        for (size_t k = 0; k < ANALYSIS_SIZE; ++k)
            linearBuf[k] = mAnalysisBuffer[(mWriteHead + k) % ANALYSIS_SIZE];

        // Apply 4th-Order Chebyshev Filter (steep lowpass at 1100 Hz + 70 Hz highpass)
        // This eliminates pick scrape, high harmonic hash, and sub-bass rumble from analysis
        std::vector<float> filteredBuf(ANALYSIS_SIZE);
        mChebyshevFilter.processBuffer(linearBuf.data(), filteredBuf.data(), static_cast<int>(ANALYSIS_SIZE));

        std::vector<int> notesToPlay;

        // Step 1: Run Yin Pitch Detector on the Chebyshev-conditioned audio.
        float confidence = 0.0f;
        float rawPitch = mPitchDetector.detectPitch(filteredBuf.data(), static_cast<int>(ANALYSIS_SIZE), &confidence);

        if (confidence >= 0.55f && rawPitch > 60.0f && rawPitch < 1400.0f) {
          // Reset Kalman on onset so large interval jumps register instantly
          mKalmanFilter.resetTo(rawPitch);
          float smoothedPitch = rawPitch;
          int midi = YinPitchDetector::freqToMidi(smoothedPitch);

          // Prevent fluttering/rapid retriggering of the same note within a short window (~150ms)
          bool isRapidDuplicate = (midi == mLastTriggeredMidi && mSamplesSinceLastNoteTrigger < static_cast<int>(44100.0f * 0.15f));

          if (!isRapidDuplicate) {
            notesToPlay.push_back(midi);
            mLastTriggeredMidi = midi;
            mSamplesSinceLastNoteTrigger = 0;
            mCurrentPitchHz.store(smoothedPitch);
            mCurrentMidiNote.store(midi);
          }
        } else {
          // Multi-note / Chord mode: Yin confidence is low because multiple strings are ringing.
          // Reset Kalman filter so it's fresh for the next single note
          mKalmanFilter.reset();

          // Detect chord notes using Chebyshev-filtered audio and harmonic cancellation
          notesToPlay = mChordDetector.detect(filteredBuf.data(), static_cast<int>(ANALYSIS_SIZE));

          if (!notesToPlay.empty()) {
            int rootMidi = notesToPlay.front();
            mLastTriggeredMidi = rootMidi;
            mSamplesSinceLastNoteTrigger = 0;
            mCurrentPitchHz.store(440.0f * std::pow(2.0f, (rootMidi - 69.0f) / 12.0f));
            mCurrentMidiNote.store(rootMidi);
          }
        }

        // Extract full Sound DNA and trigger notes with mapped timbre, velocity, and attack
        if (!notesToPlay.empty() && mCurrentInstrument) {
          float leadPitch = mCurrentPitchHz.load();
          if (leadPitch <= 0.0f) {
            leadPitch = 440.0f * std::pow(2.0f, (notesToPlay.front() - 69.0f) / 12.0f);
          }

          // Extract Sound DNA: Timbre Centroid, Spread, Flatness, Wavelength, Attack Time, Velocity
          float sr = GetSampleRate() > 0 ? GetSampleRate() : 44100.0f;
          NoteSoundDNA dna = SoundAnalyzer::extractNoteDNA(linearBuf.data(), static_cast<int>(ANALYSIS_SIZE),
                                                          sr, leadPitch, mLastOnsetVelocity);

          // Update live Sound DNA metrics for GUI visualization
          mCurrentPitchHz.store(dna.pitchHz);
          mCurrentMidiNote.store(dna.midiNote);
          mCurrentWavelengthM.store(dna.wavelengthM);
          mCurrentTimbreCentroidHz.store(dna.timbreCentroidHz);
          mCurrentTimbreSpreadHz.store(dna.timbreSpreadHz);
          mCurrentTimbreFlatness.store(dna.timbreFlatness);
          mCurrentAttackTimeMs.store(dna.attackTimeMs);
          mCurrentVelocity.store(dna.velocity);
          mCurrentBrightness.store(dna.brightness);
          mCurrentPeakDb.store(dna.amplitudePeakDb);
          mCurrentRmsDb.store(dna.amplitudeRmsDb);

          // Trigger voice playback: dynamically shapes attack slope and voice timbre filter
          mVoiceManager.chordOn(notesToPlay, dna.velocity, [this](int midi) {
              return mCurrentInstrument->getSampleForMidi(midi);
          }, dna.attackTimeMs, 250.0f, dna.timbreCentroidHz);
        }
      }
    }
    
    float synthAudio = mVoiceManager.renderSample();
    float mixedMono = (rawIn * dryMix) + (synthAudio * wetMix);
    
    float l = mixedMono, r = mixedMono;
    mChorus.process(l, r, l, r);
    mDelay.process(l, r, l, r);
    mReverb.process(l, r, l, r);
    
    out1[s] = l;
    out2[s] = r;
  }
  
  mCurrentInputPeak.store(peak);
  mGateIsOpen.store(mNoiseGate.isGateOpen());
}
