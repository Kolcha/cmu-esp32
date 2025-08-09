// SPDX-FileCopyrightText: None
// SPDX-License-Identifier: Unlicense

#include "math_utils.h"

float clamp_range(float v, float lo, float hi)
{
  return v < lo ? lo : hi < v ? hi : v;
}

float map_range(float x, float imin, float imax, float omin, float omax)
{
  return (x - imin) * (omax - omin) / (imax - imin) + omin;
}

float normalize(float x, float xmin, float xmax)
{
  return (x - xmin) / (xmax - xmin);
}
