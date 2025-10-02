// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "filter.h"

#include "spectrum.h"

void spectrum_lmh_out(const float* spectrum, size_t n, float out[3],
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
