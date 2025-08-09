// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "spectrum_utils.h"

#include "spectrum.h"

static const uint16_t bands_8[] = {
  0, 0,
  1, 1,
  2, 4,
  5, 12,
  13, 31,
  32, 75,
  76, 180,
  181, 429,
};

static const uint16_t bands_16[] = {
  0, 0,
  1, 1,
  2, 2,
  3, 3,
  4, 4,
  5, 5,
  6, 7,
  8, 12,
  13, 20,
  21, 31,
  32, 48,
  49, 75,
  76, 116,
  117, 180,
  181, 278,
  279, 429,
};

void spectrum_lmh_out(const float* spectrum, size_t n, float out[],
                      const struct filter_opt* opt)
{
  const uint16_t bands[] = {
    0, opt->thr_low,
    opt->thr_ml, opt->thr_mh,
    opt->thr_high, n-1,
  };

  spectrum_bars(3, out, bands, spectrum, n);

  out[0] *= opt->level_low;
  out[1] *= opt->level_mid;
  out[2] *= opt->level_high;
}

void spectrum_bars_8(const float* spectrum, size_t n, float out[])
{
  spectrum_bars(8, out, bands_8, spectrum, n);
}

void spectrum_bars_16(const float* spectrum, size_t n, float out[])
{
  spectrum_bars(16, out, bands_16, spectrum, n);
}
