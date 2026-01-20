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
void ConfigValue<uint8_t>::write(Preferences& prefs, const uint8_t& val)
{
  prefs.putUChar(_key, val);
}

template<>
uint8_t ConfigValue<uint8_t>::read(Preferences& prefs, const uint8_t& def)
{
  return prefs.getUChar(_key, def);
}

template<>
void ConfigValue<float>::write(Preferences& prefs, const float& val)
{
  prefs.putUShort(_key, float_to_u16(val));
}

template<>
float ConfigValue<float>::read(Preferences& prefs, const float& def)
{
  return float_from_u16(prefs.getUShort(_key, float_to_u16(def)));
}

template<>
void ConfigValue<String>::write(Preferences& prefs, const String& val)
{
  prefs.putString(_key, val);
}

template<>
String ConfigValue<String>::read(Preferences& prefs, const String& def)
{
  return prefs.getString(_key, def);
}

template<>
void ConfigValue<bool>::write(Preferences& prefs, const bool& val)
{
  prefs.putBool(_key, val);
}

template<>
bool ConfigValue<bool>::read(Preferences& prefs, const bool& def)
{
  return prefs.getBool(_key, def);
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
  c->setValue(float_to_int<Float, Int>(val, e));
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


static auto val_device_name = SimpleValue(device_name);
static auto val_swap_channels = SimpleValue(d_options.swap_r_b_channels);
static auto val_enable_history = SimpleValue(d_options.enable_rmt_history);
static auto val_gamma_value = SimpleValue(d_options.gamma_value);

static auto val_preamp = SimpleValue(acfg.preamp);
static auto val_level_low = SimpleValue(f_options.level_low);
static auto val_level_mid = SimpleValue(f_options.level_mid);
static auto val_level_high = SimpleValue(f_options.level_high);

static auto val_thr_low = SimpleValue(f_options.thr_low);
static auto val_thr_ml = SimpleValue(f_options.thr_ml);
static auto val_thr_mh = SimpleValue(f_options.thr_mh);
static auto val_thr_high = SimpleValue(f_options.thr_high);

static auto opt_device_name = ConfigValue(val_device_name, "device", "dev_name");
static auto opt_swap_channels = ConfigValue(val_swap_channels, "device", "swap_r_b");
static auto opt_enable_history = ConfigValue(val_enable_history, "device", "rmt_history_en");
static auto opt_gamma_value = ConfigValue(val_gamma_value, "device", "gamma_value");

static auto opt_preamp = ConfigValue(val_preamp, "filter", "preamp");
static auto opt_level_low = ConfigValue(val_level_low, "filter", "level_low");
static auto opt_level_mid = ConfigValue(val_level_mid, "filter", "level_mid");
static auto opt_level_high = ConfigValue(val_level_high, "filter", "level_high");

static auto opt_thr_low = ConfigValue(val_thr_low, "filter", "thr_low");
static auto opt_thr_ml = ConfigValue(val_thr_ml, "filter", "thr_ml");
static auto opt_thr_mh = ConfigValue(val_thr_mh, "filter", "thr_mh");
static auto opt_thr_high = ConfigValue(val_thr_high, "filter", "thr_high");

void load_values_from_config()
{
  opt_device_name.load();
  opt_swap_channels.load();
  opt_enable_history.load();
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

static uint32_t get_minimum_free_mem()
{
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

void ble_add_device_characteristics(BLEService* service)
{
  ble_add_rw_value(service, opt_device_name,
                   "101588e6-7fb1-4992-963b-b2ef597fa49d",
                   fmt_string,
                   "Device name");
  ble_add_rw_value(service, opt_swap_channels,
                   "5a8b2bba-6319-46a6-b37e-520744f35bfe",
                   fmt_bool,
                   "Swap red and blue channels");
  ble_add_rw_value(service, opt_enable_history,
                   "b3da21ab-cdcf-47eb-b216-357b374d0a27",
                   fmt_bool,
                   "Enable color history");

  ble_add_rw_value(service, opt_gamma_value,
                   "47f5321d-27af-4ec4-b44f-49b082cf0505",
                   fmt_float_u16,
                   "Gamma value");

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

void ble_add_filter_characteristics(BLEService* service)
{
  ble_add_rw_value(service, opt_preamp,
                   "ef599dd1-35ad-4a35-a367-e4401693f02a",
                   fmt_float_u16,
                   "Input preamplifier gain");
  ble_add_rw_value(service, opt_level_low,
                   "26ebeecb-c65e-4769-8bce-932e6814580e",
                   fmt_float_u16,
                   "Low frequencies amplification level");
  ble_add_rw_value(service, opt_level_mid,
                   "b4d3b959-a0f3-4b6a-b0d9-9ca6991563a0",
                   fmt_float_u16,
                   "Mid frequencies amplification level");
  ble_add_rw_value(service, opt_level_high,
                   "1d1750a8-9235-4f1b-890c-512f87135d31",
                   fmt_float_u16,
                   "High frequencies amplification level");

  ble_add_rw_value(service, opt_thr_low,
                   "f333456c-b5f0-4201-9ede-8c846b38556d",
                   fmt_u8_raw,
                   "Low frequency filter threshold");
  ble_add_rw_value(service, opt_thr_ml,
                   "a0532c1f-09b7-49aa-9131-13153d0fad75",
                   fmt_u8_raw,
                   "Mid frequency filter lower bound");
  ble_add_rw_value(service, opt_thr_mh,
                   "5c04fb0e-a31e-41a3-9635-1e1597729ea0",
                   fmt_u8_raw,
                   "Mid frequency filter upper bound");
  ble_add_rw_value(service, opt_thr_high,
                   "84dbac92-e7b4-4f70-97bb-a9ffdaa9393e",
                   fmt_u8_raw,
                   "High frequency filter threshold");
}
