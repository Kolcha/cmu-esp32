// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "device_options_ble.hpp"

extern "C" {
#include "device_options.h"
#include "filter.h"
#include "spectrum.h"
}
#include <esp_heap_caps.h>

#include <BLE2901.h>
#include <BLE2904.h>

#include <type_traits>

extern String device_name;

extern struct device_opt d_options;
extern struct analysis_cfg acfg;
extern struct filter_opt f_options;


template<typename T>
struct ble_format_for_type;

template<typename T>
constexpr uint8_t ble_format_for_type_v = ble_format_for_type<T>::value;

template<>
struct ble_format_for_type<bool> : std::integral_constant<uint8_t, BLE2904::FORMAT_BOOLEAN> {};
template<>
struct ble_format_for_type<uint8_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_UINT8> {};
template<>
struct ble_format_for_type<uint16_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_UINT16> {};
template<>
struct ble_format_for_type<uint32_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_UINT32> {};
template<>
struct ble_format_for_type<uint64_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_UINT64> {};
template<>
struct ble_format_for_type<int8_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_SINT8> {};
template<>
struct ble_format_for_type<int16_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_SINT16> {};
template<>
struct ble_format_for_type<int32_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_SINT32> {};
template<>
struct ble_format_for_type<int64_t> : std::integral_constant<uint8_t, BLE2904::FORMAT_SINT64> {};
template<>
struct ble_format_for_type<float> : std::integral_constant<uint8_t, BLE2904::FORMAT_FLOAT32> {};
template<>
struct ble_format_for_type<double> : std::integral_constant<uint8_t, BLE2904::FORMAT_FLOAT64> {};


template<typename Float>
constexpr Float pow10_int(int8_t n) noexcept
{
  Float res = 1;

  if (n < 0) {
    while (n++ < 0)
      res /= 10;
    return res;
  }

  if (n > 0) {
    while (n-- > 0)
      res *= 10;
    return res;
  }

  return res;
}

template<typename Float, typename Int>
constexpr Int float_to_int(Float x, int8_t e) noexcept
{
  return static_cast<Int>(std::round(x * pow10_int<Float>(-e)));
}

template<typename Float, typename Int>
constexpr Float float_from_int(Int x, int8_t e) noexcept
{
  return x * pow10_int<Float>(e);
}

constexpr uint16_t float_to_u16(float x) noexcept
{
  return float_to_int<float, uint16_t>(x, -4);
}

constexpr float float_from_u16(uint16_t x) noexcept
{
  return float_from_int<float, uint16_t>(x, -4);
}

template<>
void ConfigValue<uint8_t>::write(Preferences& prefs)
{
  prefs.putUChar(_key, _val);
}

template<>
void ConfigValue<uint8_t>::read(Preferences& prefs)
{
  _val = prefs.getUChar(_key, _val);
}

template<>
void ConfigValue<float>::write(Preferences& prefs)
{
  prefs.putUShort(_key, float_to_u16(_val));
}

template<>
void ConfigValue<float>::read(Preferences& prefs)
{
  _val = float_from_u16(prefs.getUShort(_key, float_to_u16(_val)));
}

template<>
void ConfigValue<String>::write(Preferences& prefs)
{
  prefs.putString(_key, _val);
}

template<>
void ConfigValue<String>::read(Preferences& prefs)
{
  _val = prefs.getString(_key, _val);
}

template<>
void ConfigValue<bool>::write(Preferences& prefs)
{
  prefs.putBool(_key, _val);
}

template<>
void ConfigValue<bool>::read(Preferences& prefs)
{
  _val = prefs.getBool(_key, _val);
}


template<typename T>
void fmt_raw_to_ble(const T& val, BLECharacteristic* c)
{
  T v = val;  // ugly interface requires non-const pointer
  c->setValue(reinterpret_cast<uint8_t*>(&v), sizeof(v));
}

template<typename T>
void fmt_raw_from_ble(BLECharacteristic* c, T& val)
{
  memcpy(&val, c->getData(), c->getLength());
}


template<typename T>
struct RawValueFormat : ValueFormat<T> {
  constexpr RawValueFormat() noexcept : ValueFormat<T>
  {
    .format = ble_format_for_type_v<T>,
    .exponent = 0,
    .to_ble = &fmt_raw_to_ble<T>,
    .from_ble = &fmt_raw_from_ble<T>
  } {}
};


template<typename Float, typename Int, int8_t e>
void fmt_float_to_ble_int(const Float& val, BLECharacteristic* c)
{
  // ugly interface requires non-const reference
  auto v = float_to_int<Float, Int>(val, e);
  c->setValue(v);
}

template<typename Float, typename Int, int8_t e>
void fmt_float_from_ble_int(BLECharacteristic* c, Float& val)
{
  val = float_from_int<Float, Int>(*reinterpret_cast<Int*>(c->getData()), e);
}


template<typename Float, typename Int, int8_t e>
struct FloatValueFormat : ValueFormat<Float> {
  constexpr FloatValueFormat() noexcept : ValueFormat<Float>
  {
    .format = ble_format_for_type_v<Int>,
    .exponent = e,
    .to_ble = &fmt_float_to_ble_int<Float, Int, e>,
    .from_ble = &fmt_float_from_ble_int<Float, Int, e>
  } {}
};


void fmt_string_to_ble(const String& val, BLECharacteristic* c)
{
  c->setValue(val);
}

void fmt_string_from_ble(BLECharacteristic* c, String& val)
{
  val = c->getValue();
}

static const RawValueFormat<uint8_t> fmt_u8_raw;
static const RawValueFormat<uint16_t> fmt_u16_raw;
static const RawValueFormat<uint32_t> fmt_u32_raw;
static const RawValueFormat<bool> fmt_bool;

static const FloatValueFormat<float, uint16_t, -4> fmt_float_u16;

static const ValueFormat<String> fmt_string = {
  .format = BLE2904::FORMAT_UTF8,
  .exponent = 0,
  .to_ble = &fmt_string_to_ble,
  .from_ble = &fmt_string_from_ble,
};


void ble_characteristic_add_format(BLECharacteristic* c, uint8_t fmt, int8_t exp)
{
  auto ble_desc = new BLE2904();
  ble_desc->setFormat(fmt);
  ble_desc->setExponent(exp);
  c->addDescriptor(ble_desc);
}

void ble_characteristic_add_description(BLECharacteristic* c, const char* desc)
{
  auto ble_desc = new BLE2901();
  ble_desc->setDescription(desc);
  c->addDescriptor(ble_desc);
}


static auto opt_device_name = ConfigValue(device_name, "device", "dev_name");
static auto opt_swap_channels = ConfigValue(d_options.swap_r_b_channels, "device", "swap_r_b");
static auto opt_en_log_log_f = ConfigValue(d_options.enable_log_log_f_ks, "device", "en_log_log_f");
static auto opt_en_gamma_corr = ConfigValue(d_options.enable_gamma_corr, "device", "en_gamma_corr");
static auto opt_gamma_value = ConfigValue(d_options.gamma_value, "device", "gamma_value");

static auto opt_preamp = ConfigValue(acfg.preamp, "filter", "preamp");
static auto opt_level_low = ConfigValue(f_options.level_low, "filter", "level_low");
static auto opt_level_mid = ConfigValue(f_options.level_mid, "filter", "level_mid");
static auto opt_level_high = ConfigValue(f_options.level_high, "filter", "level_high");

static auto opt_thr_low = ConfigValue(f_options.thr_low, "filter", "thr_low");
static auto opt_thr_ml = ConfigValue(f_options.thr_ml, "filter", "thr_ml");
static auto opt_thr_mh = ConfigValue(f_options.thr_mh, "filter", "thr_mh");
static auto opt_thr_high = ConfigValue(f_options.thr_high, "filter", "thr_high");

void load_values_from_config()
{
  opt_device_name.load();
  opt_swap_channels.load();
  opt_en_log_log_f.load();
  opt_en_gamma_corr.load();
  opt_gamma_value.load();

  opt_preamp.load();
  opt_level_low.load();
  opt_level_mid.load();
  opt_level_high.load();

  opt_thr_low.load();
  opt_thr_ml.load();
  opt_thr_mh.load();
  opt_thr_high.load();
}

static uint16_t get_prefs_free_entries_count()
{
  Preferences prefs;
  return static_cast<uint16_t>(prefs.freeEntries());
}

static uint32_t get_minimum_free_mem()
{
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

void ble_add_device_characteristics(BLEService* service)
{
  ble_add_option(service, opt_device_name,
                 "101588e6-7fb1-4992-963b-b2ef597fa49d",
                 fmt_string,
                 "Device name");
  ble_add_option(service, opt_swap_channels,
                 "5a8b2bba-6319-46a6-b37e-520744f35bfe",
                 fmt_bool,
                 "Swap red and blue channels");
  ble_add_option(service, opt_en_log_log_f,
                 "b59c3a95-41e1-474c-a2e6-75a296a43024",
                 fmt_bool,
                 "Enable non-uniform amplification");

  ble_add_option(service, opt_en_gamma_corr,
                 "bc5878ff-bf6f-460a-8db0-9d280ad9be3d",
                 fmt_bool,
                 "Enable gamma correction");
  ble_add_option(service, opt_gamma_value,
                 "47f5321d-27af-4ec4-b44f-49b082cf0505",
                 fmt_float_u16,
                 "Gamma value");
  ble_characteristic_add_value_range(
    service->getCharacteristic("47f5321d-27af-4ec4-b44f-49b082cf0505"),
    float_to_u16(2.0f), float_to_u16(2.8f));

  ble_add_ro_value(service, get_prefs_free_entries_count,
                   "61d35402-cf8a-4267-a896-673bb74ebf3f",
                   fmt_u16_raw,
                   "Number of free config entries"
                  );
  ble_add_ro_value(service, get_minimum_free_mem,
                   "32a34428-4456-4d62-a2f5-2fc7eaadeb97",
                   fmt_u32_raw,
                   "Total minimum free memory since boot");
}

template<typename R, typename T>
void ble_bulk_add_range(BLEService* service, R&& uuids, T vmin, T vmax)
{
  for (const auto& uuid : uuids)
    if (auto c = service->getCharacteristic(uuid))
      ble_characteristic_add_value_range(c, vmin, vmax);
}

static void ble_add_levels_range(BLEService* service, float vmin, float vmax)
{
  auto ble_vmin = float_to_u16(vmin);
  auto ble_vmax = float_to_u16(vmax);

  const auto levels_uuids = {
    "ef599dd1-35ad-4a35-a367-e4401693f02a",
    "26ebeecb-c65e-4769-8bce-932e6814580e",
    "b4d3b959-a0f3-4b6a-b0d9-9ca6991563a0",
    "1d1750a8-9235-4f1b-890c-512f87135d31",
  };

  ble_bulk_add_range(service, levels_uuids, ble_vmin, ble_vmax);
}

static void ble_add_thresholds_range(BLEService* service, uint8_t vmin, uint8_t vmax)
{
  const auto thresholds_uuids = {
    "f333456c-b5f0-4201-9ede-8c846b38556d",
    "a0532c1f-09b7-49aa-9131-13153d0fad75",
    "5c04fb0e-a31e-41a3-9635-1e1597729ea0",
    "84dbac92-e7b4-4f70-97bb-a9ffdaa9393e",
  };

  ble_bulk_add_range(service, thresholds_uuids, vmin, vmax);
}

void ble_add_filter_characteristics(BLEService* service)
{
  ble_add_option(service, opt_preamp,
                 "ef599dd1-35ad-4a35-a367-e4401693f02a",
                 fmt_float_u16,
                 "Input preamplifier gain");
  ble_add_option(service, opt_level_low,
                 "26ebeecb-c65e-4769-8bce-932e6814580e",
                 fmt_float_u16,
                 "Amplification level for low frequencies");
  ble_add_option(service, opt_level_mid,
                 "b4d3b959-a0f3-4b6a-b0d9-9ca6991563a0",
                 fmt_float_u16,
                 "Amplification level for mid frequencies");
  ble_add_option(service, opt_level_high,
                 "1d1750a8-9235-4f1b-890c-512f87135d31",
                 fmt_float_u16,
                 "Amplification level for high frequencies");
  ble_add_levels_range(service, 0.f, 3.f);

  ble_add_option(service, opt_thr_low,
                 "f333456c-b5f0-4201-9ede-8c846b38556d",
                 fmt_u8_raw,
                 "Threshold for low-frequency filter");
  ble_add_option(service, opt_thr_ml,
                 "a0532c1f-09b7-49aa-9131-13153d0fad75",
                 fmt_u8_raw,
                 "Lower bound threshold for mid-frequency filter");
  ble_add_option(service, opt_thr_mh,
                 "5c04fb0e-a31e-41a3-9635-1e1597729ea0",
                 fmt_u8_raw,
                 "Upper bound threshold for mid-frequency filter");
  ble_add_option(service, opt_thr_high,
                 "84dbac92-e7b4-4f70-97bb-a9ffdaa9393e",
                 fmt_u8_raw,
                 "Threshold for high-frequency filter");
  ble_add_thresholds_range(service, 0, 255);
}
