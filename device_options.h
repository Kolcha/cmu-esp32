// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#ifndef _DEVICE_OPTIONS_H_
#define _DEVICE_OPTIONS_H_

#include <stdbool.h>
#include <stdint.h>

struct device_opt {
  bool swap_r_b_channels;
  bool enable_rmt_history;
  float gamma_value;
};

struct rmt_cfg {
  uint16_t leds_count;
  uint16_t Treset;
  float T0H, T0L, T1H, T1L;
};

#endif /* _DEVICE_OPTIONS_H_ */
