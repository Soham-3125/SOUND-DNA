/*
 SimpleFFT.h
 ===========
 A compact, dependency-free radix-2 FFT, used in place of juce::dsp::FFT
 now that JUCE is no longer part of this build. Works on power-of-2 sizes
 only (we always use 2048, same as before).
*/

#pragma once
#include <vector>
#include <complex>
#include <cmath>

inline void simpleFFT(std::vector<std::complex<float>>& a, bool invert = false)
{
    size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    // Iterative Cooley-Tukey
    for (size_t len = 2; len <= n; len <<= 1)
    {
        float ang = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len) * (invert ? 1.0f : -1.0f);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                std::complex<float> u = a[i + j];
                std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (invert)
        for (auto& x : a)
            x /= static_cast<float>(n);
}

/** Real-input forward FFT: returns magnitude spectrum for bins 0..n/2 (inclusive). */
inline std::vector<float> realFFTMagnitude(const std::vector<float>& real, size_t n)
{
    std::vector<std::complex<float>> buf(n);
    for (size_t i = 0; i < n; ++i)
        buf[i] = std::complex<float>(i < real.size() ? real[i] : 0.0f, 0.0f);

    simpleFFT(buf, false);

    std::vector<float> mag(n / 2 + 1);
    for (size_t i = 0; i <= n / 2; ++i)
        mag[i] = std::abs(buf[i]);
    return mag;
}

/** Real-input forward FFT: returns full complex spectrum (bins 0..n/2), for cases needing phase too. */
inline std::vector<std::complex<float>> realFFTComplex(const std::vector<float>& real, size_t n)
{
    std::vector<std::complex<float>> buf(n);
    for (size_t i = 0; i < n; ++i)
        buf[i] = std::complex<float>(i < real.size() ? real[i] : 0.0f, 0.0f);

    simpleFFT(buf, false);
    buf.resize(n / 2 + 1);
    return buf;
}
