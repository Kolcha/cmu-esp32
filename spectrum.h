// SPDX-FileCopyrightText: 2024 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#ifndef _SPECTRUM_H_
#define _SPECTRUM_H_

#include <stddef.h>
#include <stdint.h>

#include "simple_fft.h"

// calculate spectrum frequencies
// freq - frequencies output buffer, size is n
// sample_rate - input sample rate
// n - spectrum elements count
void frequencies_data(float* freq, size_t sample_rate, size_t n);

// spectrum analysis configuration and data
struct analysis_cfg {
  const simple_fft_cfg* fft_cfg;  // FFT algorithm configuration and data
  const float* kwnd;  // window function coefficients, e.g. Hann window
  const float* freq;  // spectrum frequencies, FFTs count, optional
  float kwnd_sum;     // window function coefficients sum
  float preamp;       // input amplification, [0...2]
};

// analyze input and calculate the spectrum
// calculations are done according to given FFT configuration
// implementation depends on used FFT algorithm
// returned amplitude values are normalized magnitudes
// cfg - spectrum analysis configuration and data
// raw_input - 16bit stereo input, number of samples must be 2*nfft
// spectrum - output array of (0,amplitude) pairs, nfft in total
void analyze_input(const struct analysis_cfg* cfg,
                   const int16_t* raw_input, float* spectrum);

// convert magnitudes to amplitudes in decibels in-place
// spectrum - input array of (freq,magnitude) pairs, n in total
// n - spectrum elements (i.e. pairs) count, array size / 2
void magnitudes_to_decibels(float* spectrum, size_t n);

// create spectrum "bars" representation
// n - desired bars count
// bars - output buffer, size must be n
// bands - list of n [first index, last index] *pairs* for each bar
// spectrum - source spectrum, it should be large enough to contain max index
// nfft - number of (freq,amplitude) pairs in spectrum, FFTs count
void spectrum_bars(uint8_t n, float* bars, const uint16_t* bands,
                   const float* spectrum, size_t nfft);

#endif  // _SPECTRUM_H_
