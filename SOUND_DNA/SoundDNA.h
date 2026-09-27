#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "src/NoiseGateAndOnset.h"
#include "src/YinPitchDetector.h"
#include "src/KalmanPitchFilter.h"
#include "src/ChebyshevFilter.h"
#include "src/ChordDetector.h"
#include "src/SamplerVoice.h"
#include "src/FxEngine.h"
#include "src/InstrumentPresets.h"
#include "src/SoundAnalyzer.h"

const int kNumPresets = 1;

enum EParams
{
  kParamPreset = 0,
  kParamInputGain,
  kParamGateThresh,
  kParamDryMix,
  kParamWetMix,
  kNumParams
};

using namespace iplug;
using namespace igraphics;

class SoundDNA final : public Plugin
{
public:
  SoundDNA(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnParamChange(int paramIdx) override;

private:
  NoiseGateAndOnset mNoiseGate;
  YinPitchDetector mPitchDetector;
  KalmanPitchFilter mKalmanFilter;
  ChebyshevFilter mChebyshevFilter;
  ChordDetector mChordDetector;
  PolyVoiceManager mVoiceManager;
  std::unique_ptr<IInstrumentBank> mCurrentInstrument;

  StereoChorus mChorus;
  FeedbackDelay mDelay;
  StereoReverb mReverb;

  // Analysis Ring Buffer
  static constexpr size_t ANALYSIS_SIZE = 2048;
  std::vector<float> mAnalysisBuffer;
  size_t mWriteHead = 0;

  // Complete Sound DNA Live Metrics (Frequency, Wavelength, Timbre, Velocity, Attack, etc.)
  std::atomic<float> mCurrentInputPeak{0.0f};
  std::atomic<float> mCurrentPitchHz{0.0f};
  std::atomic<int>   mCurrentMidiNote{-1};
  std::atomic<float> mCurrentWavelengthM{0.0f};
  std::atomic<float> mCurrentTimbreCentroidHz{1200.0f};
  std::atomic<float> mCurrentTimbreSpreadHz{600.0f};
  std::atomic<float> mCurrentTimbreFlatness{0.02f};
  std::atomic<float> mCurrentAttackTimeMs{8.0f};
  std::atomic<float> mCurrentVelocity{0.8f};
  std::atomic<float> mCurrentBrightness{1.0f};
  std::atomic<float> mCurrentPeakDb{-60.0f};
  std::atomic<float> mCurrentRmsDb{-60.0f};
  std::atomic<bool>  mGateIsOpen{false};

  // Delayed analysis state
  int mSamplesUntilAnalysis = -1;
  float mLastOnsetVelocity = 0.0f;
  int mLastTriggeredMidi = -1;
  int mSamplesSinceLastNoteTrigger = 10000;
};
