// SPDX-FileCopyrightText: 2026 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "rgb_channel_switcher.hpp"

#include <stdio.h>
#include <string.h>

void ChannelSwitcher::setLayout(rgb_layout_t l) noexcept
{
  _layout = l;
  auto lv = static_cast<uint8_t>(l);
  _ri = lv / 100;
  lv -= _ri * 100;
  _gi = lv / 10;
  lv -= _gi * 10;
  _bi = lv;
}

bool rgb_layout_to_str(char* str, size_t sz, rgb_layout_t l)
{
  if (sz < 3) return false;

  switch (l) {
    case LAYOUT_RGB: return snprintf(str, sz, "RGB") > 0;
    case LAYOUT_RBG: return snprintf(str, sz, "RBG") > 0;
    case LAYOUT_GRB: return snprintf(str, sz, "GRB") > 0;
    case LAYOUT_GBR: return snprintf(str, sz, "GBR") > 0;
    case LAYOUT_BRG: return snprintf(str, sz, "BRG") > 0;
    case LAYOUT_BGR: return snprintf(str, sz, "BGR") > 0;
  }

  return false;
}

bool str_to_rgb_layout(const char* str, size_t sz, rgb_layout_t& l)
{
  if (sz < 3) return false;

  if (strncmp(str, "RGB", 3) == 0) {
    l = LAYOUT_RGB;
    return true;
  }
  if (strncmp(str, "RBG", 3) == 0) {
    l = LAYOUT_RBG;
    return true;
  }
  if (strncmp(str, "GRB", 3) == 0) {
    l = LAYOUT_GRB;
    return true;
  }
  if (strncmp(str, "GBR", 3) == 0) {
    l = LAYOUT_GBR;
    return true;
  }
  if (strncmp(str, "BRG", 3) == 0) {
    l = LAYOUT_BRG;
    return true;
  }
  if (strncmp(str, "BGR", 3) == 0) {
    l = LAYOUT_BGR;
    return true;
  }

  return false;
}
