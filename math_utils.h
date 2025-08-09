// SPDX-FileCopyrightText: None
// SPDX-License-Identifier: Unlicense

#ifndef _MATH_UTILS_H_
#define _MATH_UTILS_H_

float clamp_range(float v, float lo, float hi);

float map_range(float x, float imin, float imax, float omin, float omax);

float normalize(float x, float xmin, float xmax);

#endif  // _MATH_UTILS_H_
