#ifndef _SPECTRUM_UTILS_H_
#define _SPECTRUM_UTILS_H_

#include <stddef.h>
#include <stdint.h>

// calculate by-frequency amplification coefficients
// amp_k - amplification coefficients output buffer, size is n
// freq - frequencies buffer, size is n
// n - spectrum elements count
void amplification_coefficients(float* amp_k, const float* freq, size_t n);

struct proc_opt {
  float min_db;         // lowest dB value, e.g. -50
  float max_db;         // highest dB value, e.g. 0
  const float* ampm;    // amplification level per frequency
};

// post-process the spectrum
// normalizes amplitudes, output is in range [0...1]
// n - spectrum elements (i.e. pairs) count, spectrum array size / 2
void process_spectrum(const struct proc_opt* opt, float* spectrum, size_t n);

struct filter_opt {
  float level_low;
  float level_mid;
  float level_high;

  uint16_t thr_low;
  uint16_t thr_ml;
  uint16_t thr_mh;
  uint16_t thr_high;
};

void spectrum_lmh_out(const float* spectrum, size_t n, float out[3],
                      const struct filter_opt* opt);

void spectrum_bars_8(const float* spectrum, size_t n, float out[8]);
void spectrum_bars_16(const float* spectrum, size_t n, float out[16]);

#endif /* _SPECTRUM_UTILS_H_ */
