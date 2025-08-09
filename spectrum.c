// SPDX-FileCopyrightText: 2024 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "spectrum.h"

#include <tgmath.h>

void frequencies_data(float* freq, size_t sample_rate, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    freq[i] = sample_rate / 2.f * (i + 1) / n;
  }
}

// converts raw_input into the form expected by FFT algorithm
// implementation depends on FFT algorithm input format
// raw_input - 16bit stereo input, size must be 2*ns
// input - output buffer where prepared data should be written
// ns - samples count (i.e. number of *pairs* in raw_input)
// raw_input size is 2*ns, window and input size is ns
static void prepare_fft_input(const struct analysis_cfg* cfg,
                              const int16_t* raw_input, float* input)
{
  const size_t ns = 2*cfg->fft_cfg->n;
  const float* window = cfg->kwnd;
  const int16_t* raw_input_end = raw_input + 2*ns;        // 2 channels
  for (; raw_input != raw_input_end; raw_input += 2) {
    int16_t ch1 = *(raw_input+0);
    int16_t ch2 = *(raw_input+1);
    *input++ = (ch1 + ch2) / 2.f / 32768.f * cfg->preamp * *window++;
  }
}

// process FFT output buffer and calculates spectrum
// implementation depends on FFT algorithm output format
// expected format the same as KISSFFT C++ produces for real data input
// spectrum data **overwrites** FFT data, amplitudes are at odd indexes
// fft_buffer - FFT algorithm output buffer, size must be 2*nfft
static void calculate_spectrum(const struct analysis_cfg* cfg,
                               float* fft_buffer)
{
  // first item in the output is special, its real part is not used,
  // imaginary part (the second array element) is the last magnitude
  // so, save it for the later use, as it will be overwritten first
  float last_magnitude = fabs(fft_buffer[1]);

  float* buf_end = fft_buffer + 2*cfg->fft_cfg->n;
  const float* freq = cfg->freq;

  float* fft_in = fft_buffer + 2;
  float* sp_out = fft_buffer;
  while (sp_out != buf_end) {
    float m = fft_in == buf_end ?
              last_magnitude : hypot(*fft_in, *(fft_in+1));
    // scale the magnitude of FFT by window and factor of 2,
    // because we are using half of FFT spectrum
    m = m * 2 / cfg->kwnd_sum;
    fft_in += 2;
    *sp_out++ = freq ? *freq++ : 0;     // frequency axis
    *sp_out++ = m;                      // raw magnitude
  }
}

void analyze_input(const struct analysis_cfg* cfg,
                   const int16_t* raw_input, float* spectrum)
{
  prepare_fft_input(cfg, raw_input, spectrum);
  fft_real(cfg->fft_cfg, spectrum);
  calculate_spectrum(cfg, spectrum);
}

void magnitudes_to_decibels(float* spectrum, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    float* m = spectrum + 2*i + 1;
    // ref == 1.0 because of float [-1, 1] FFT input
    *m = 20 * log10(*m / 1.f);    // convert to dBFS
  }
}

static float clamp_range(float v, float lo, float hi)
{
  return v < lo ? lo : hi < v ? hi : v;
}

void clamp_spectrum_range(float lo, float hi, float* spectrum, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    float* v = spectrum + 2*i + 1;
    *v = clamp_range(*v, lo, hi);
  }
}

// finds maximum value in range [b, e)
// values in range can be grouped, only one of the values is considered
// b - range begin, points to the first value in the first group
// e - range end, points to the value after the last value in the last group
// d - distance from the group beginning, 0 for the first value in the group
// s - step size (number of values in the group)
static float max_in_range(const float* b, const float* e, size_t d, size_t s)
{
  float m = *(b+d);
  for (; b != e; b += s)
    if (*(b+d) > m)
      m = *(b+d);
  return m;
}

void spectrum_bars(uint8_t n, float* bars, const uint16_t* bands,
                   const float* spectrum, size_t nfft)
{
  for (uint8_t i = 0; i < n; i++) {
    uint16_t bf = bands[2*i];
    uint16_t bl = bands[2*i + 1];

    if (bf >= nfft || bl >= nfft)
      continue;

    *(bars + i) = max_in_range(spectrum + 2*bf, spectrum + 2*(bl + 1), 1, 2);
  }
}
