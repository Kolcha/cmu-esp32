// SPDX-FileCopyrightText: 2026 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

union rgb_data_t {
  struct {
    uint8_t ch1;
    uint8_t ch2;
    uint8_t ch3;
  };
  uint8_t data[3];
};


enum rgb_layout_t : uint8_t {
  LAYOUT_RGB =  12,
  LAYOUT_RBG =  21,
  LAYOUT_GRB = 102,
  LAYOUT_GBR = 201,
  LAYOUT_BRG = 120,
  LAYOUT_BGR = 210,
};

bool rgb_layout_to_str(char* str, size_t sz, rgb_layout_t l);
bool str_to_rgb_layout(const char* str, size_t sz, rgb_layout_t& l);


class ChannelSwitcher
{
public:
  explicit ChannelSwitcher(rgb_layout_t l = LAYOUT_GRB) noexcept
  {
    setLayout(l);
  }

  void setLayout(rgb_layout_t l) noexcept;

  rgb_layout_t layout() const noexcept { return _layout; }

  void fillRGB(rgb_data_t& rgb, uint8_t r, uint8_t g, uint8_t b) const noexcept
  {
    fillR(rgb, r);
    fillG(rgb, g);
    fillB(rgb, b);
  }

  void fillR(rgb_data_t& rgb, uint8_t r) const noexcept { rgb.data[_ri] = r; }
  void fillG(rgb_data_t& rgb, uint8_t g) const noexcept { rgb.data[_gi] = g; }
  void fillB(rgb_data_t& rgb, uint8_t b) const noexcept { rgb.data[_bi] = b; }

private:
  rgb_layout_t _layout;
  uint8_t _ri;
  uint8_t _gi;
  uint8_t _bi;
};
