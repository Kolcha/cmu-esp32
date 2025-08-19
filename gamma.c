// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "gamma.h"

#include <tgmath.h>

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
