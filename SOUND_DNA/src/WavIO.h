#pragma once

#include "miniaudio.h"
#include <vector>
#include <string>
#include <iostream>

class WavIO
{
public:
    // Load a WAV/MP3 (or any miniaudio supported format) file into a mono float buffer
    static bool loadAudioFile(const std::string& filePath, std::vector<float>& outBuffer, ma_uint32 targetSampleRate = 44100)
    {
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 1, targetSampleRate);
        ma_decoder decoder;

        ma_result result = ma_decoder_init_file(filePath.c_str(), &decoderConfig, &decoder);
        if (result != MA_SUCCESS)
        {
            std::cerr << "Error: Could not open audio file: " << filePath << " (error code: " << result << ")\n";
            return false;
        }

        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);

        if (totalFrames == 0)
        {
            std::vector<float> chunk(4096);
            ma_uint64 framesRead = 0;
            while (ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunk.size(), &framesRead) == MA_SUCCESS && framesRead > 0)
            {
                outBuffer.insert(outBuffer.end(), chunk.begin(), chunk.begin() + framesRead);
            }
        }
        else
        {
            outBuffer.resize(static_cast<size_t>(totalFrames));
            ma_uint64 framesRead = 0;
            ma_decoder_read_pcm_frames(&decoder, outBuffer.data(), totalFrames, &framesRead);
            outBuffer.resize(static_cast<size_t>(framesRead));
        }

        ma_decoder_uninit(&decoder);
        return !outBuffer.empty();
    }

    // Save a mono or stereo float buffer into a WAV file
    static bool saveWavFileStereo(const std::string& filePath, const std::vector<float>& inInterleavedStereo, ma_uint32 sampleRate = 44100)
    {
        if (inInterleavedStereo.empty())
        {
            std::cerr << "Error: Buffer is empty, nothing to write to " << filePath << "\n";
            return false;
        }

        ma_encoder_config encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sampleRate);
        ma_encoder encoder;

        ma_result result = ma_encoder_init_file(filePath.c_str(), &encoderConfig, &encoder);
        if (result != MA_SUCCESS)
        {
            std::cerr << "Error: Failed to create output WAV file: " << filePath << " (error code: " << result << ")\n";
            return false;
        }

        ma_uint64 framesCount = inInterleavedStereo.size() / 2;
        ma_uint64 framesWritten = 0;
        result = ma_encoder_write_pcm_frames(&encoder, inInterleavedStereo.data(), framesCount, &framesWritten);
        ma_encoder_uninit(&encoder);

        return (result == MA_SUCCESS);
    }
};
