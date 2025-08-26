// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "filter.h"

#include <tgmath.h>

#include "spectrum.h"

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

static float gamma_func(float x, float g)
{
  return pow((x + 0.055f) / (1.0f + 0.055f), g);
}

static float approx_k(float thr, float g)
{
  return thr / gamma_func(thr, g);
}

float apply_gamma(float x, float g)
{
  const float thr = 0.04045f;
  return (x > thr) ? gamma_func(x, g) : (x / approx_k(thr, g));
}
