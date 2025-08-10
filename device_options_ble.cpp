// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "BLEDescriptor.h"
#include "BLECharacteristic.h"
#include <cstdint>
#include "device_options_ble.hpp"

extern "C" {
#include "filter.h"
#include "spectrum.h"
}

#include <BLE2901.h>
#include <BLE2904.h>

#include <Preferences.h>

#define count_of(X)     (sizeof(X)/sizeof(X[0]))

extern struct analysis_cfg acfg;
extern struct filter_opt f_options;

extern Preferences prefs;

struct ble_filter_opt_format {
  uint8_t format;
  int8_t exponent;
  void(*fmt_to)(const void*, size_t, uint8_t[2]);
  void(*fmt_from)(const uint8_t[2], void*, size_t);
};

struct ble_filter_opt {
  const char* uuid;
  const char* desc;
  const char* sect;
  const char* name;
  struct ble_filter_opt_format const* format;
  void* val_dest;
  size_t val_size;
};

static void fmt_float_to(const void* src_val, size_t val_size, uint8_t out[2])
{
  if (val_size != sizeof(float))
    return;

  auto src_val_ptr = static_cast<const float*>(src_val);

  uint16_t val = static_cast<uint16_t>(std::round(*src_val_ptr * 10'000));
  memcpy(out, &val, sizeof(val));
}

static void fmt_float_from(const uint8_t in[2], void* dst_val, size_t val_size)
{
  if (val_size != sizeof(float))
    return;

  auto dst_val_ptr = static_cast<float*>(dst_val);
  *dst_val_ptr = *reinterpret_cast<const uint16_t*>(in) * 0.0001f;
}

static void fmt_int16_to(const void* src_val, size_t val_size, uint8_t out[2])
{
  if (val_size != sizeof(uint16_t))
    return;

  memcpy(out, src_val, val_size);
}

static void fmt_int16_from(const uint8_t in[2], void* dst_val, size_t val_size)
{
  if (val_size != sizeof(float))
    return;

  memcpy(dst_val, in, val_size);
}

static struct ble_filter_opt_format const fmt_float = {
  .format = BLE2904::FORMAT_UINT16,
  .exponent = -4,
  .fmt_to = &fmt_float_to,
  .fmt_from = &fmt_float_from,
};

static struct ble_filter_opt_format const fmt_int16 = {
  .format = BLE2904::FORMAT_UINT16,
  .exponent = 0,
  .fmt_to = &fmt_int16_to,
  .fmt_from = &fmt_int16_from,
};

static struct ble_filter_opt const ble_filter_options[] = {
  {
    .uuid = "ef599dd1-35ad-4a35-a367-e4401693f02a",
    .desc = "preamp",
    .sect = "filter",
    .name = "preamp",
    .format = &fmt_float,
    .val_dest = &acfg.preamp,
    .val_size = sizeof(acfg.preamp),
  },
  {
    .uuid = "26ebeecb-c65e-4769-8bce-932e6814580e",
    .desc = "level_low",
    .sect = "filter",
    .name = "level_low",
    .format = &fmt_float,
    .val_dest = &f_options.level_low,
    .val_size = sizeof(f_options.level_low),
  },
  {
    .uuid = "b4d3b959-a0f3-4b6a-b0d9-9ca6991563a0",
    .desc = "level_mid",
    .sect = "filter",
    .name = "level_mid",
    .format = &fmt_float,
    .val_dest = &f_options.level_mid,
    .val_size = sizeof(f_options.level_mid),
  },
  {
    .uuid = "1d1750a8-9235-4f1b-890c-512f87135d31",
    .desc = "level_high",
    .sect = "filter",
    .name = "level_high",
    .format = &fmt_float,
    .val_dest = &f_options.level_high,
    .val_size = sizeof(f_options.level_high),
  },
  {
    .uuid = "f333456c-b5f0-4201-9ede-8c846b38556d",
    .desc = "thr_low",
    .sect = "filter",
    .name = "thr_low",
    .format = &fmt_int16,
    .val_dest = &f_options.thr_low,
    .val_size = sizeof(f_options.thr_low),
  },
  {
    .uuid = "a0532c1f-09b7-49aa-9131-13153d0fad75",
    .desc = "thr_ml",
    .sect = "filter",
    .name = "thr_ml",
    .format = &fmt_int16,
    .val_dest = &f_options.thr_ml,
    .val_size = sizeof(f_options.thr_ml),
  },
  {
    .uuid = "5c04fb0e-a31e-41a3-9635-1e1597729ea0",
    .desc = "thr_mh",
    .sect = "filter",
    .name = "thr_mh",
    .format = &fmt_int16,
    .val_dest = &f_options.thr_mh,
    .val_size = sizeof(f_options.thr_mh),
  },
  {
    .uuid = "84dbac92-e7b4-4f70-97bb-a9ffdaa9393e",
    .desc = "thr_high",
    .sect = "filter",
    .name = "thr_high",
    .format = &fmt_int16,
    .val_dest = &f_options.thr_high,
    .val_size = sizeof(f_options.thr_high),
  },
};


class FilterValueWriteCallback : public BLECharacteristicCallbacks
{
public:
  explicit FilterValueWriteCallback(const ble_filter_opt& opt) noexcept
    : _opt(opt)
  {}

  void onWrite(BLECharacteristic *pCharacteristic) override
  {
    // all filter options are represented as uint16_t
    if (pCharacteristic->getLength() != sizeof(uint16_t))
      return;

    _opt.format->fmt_from(pCharacteristic->getData(), _opt.val_dest, _opt.val_size);

    prefs.begin(_opt.sect, false);
    prefs.putUShort(_opt.name, *reinterpret_cast<uint16_t*>(pCharacteristic->getData()));
    prefs.end();
  }

private:
  const ble_filter_opt& _opt;
};


static void ble_characteristic_add_value_u_desc(BLECharacteristic* c, const ble_filter_opt& opt)
{
  auto ble_desc = new BLE2901();
  ble_desc->setDescription(opt.desc);
  c->addDescriptor(ble_desc);
}

static void ble_characteristic_add_value_format(BLECharacteristic* c, const ble_filter_opt& opt)
{
  auto ble_desc = new BLE2904();
  ble_desc->setFormat(opt.format->format);
  ble_desc->setExponent(opt.format->exponent);
  c->addDescriptor(ble_desc);
}

static void ble_characteristic_set_value(BLECharacteristic* c, const ble_filter_opt& opt)
{
  uint16_t def_val = 0;
  opt.format->fmt_to(opt.val_dest, opt.val_size, reinterpret_cast<uint8_t*>(&def_val));

  prefs.begin(opt.sect, true);
  auto val = prefs.getUShort(opt.name, def_val);
  prefs.end();

  c->setValue(val);
}

constexpr uint32_t rw_props = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;

void ble_add_filter_characteristics(BLEService* service)
{
  for (size_t i = 0; i < count_of(ble_filter_options); i++) {
    const auto& opt = ble_filter_options[i];
    auto characteristic = service->createCharacteristic(opt.uuid, rw_props);
    characteristic->setCallbacks(new FilterValueWriteCallback(opt));

    ble_characteristic_add_value_format(characteristic, opt);
    ble_characteristic_add_value_u_desc(characteristic, opt);
    ble_characteristic_set_value(characteristic, opt);
  }
}
