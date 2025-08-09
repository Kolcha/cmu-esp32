// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#ifndef _FILTER_H_
#define _FILTER_H_

#include <stddef.h>
#include <stdint.h>

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

#endif /* _FILTER_H_ */
