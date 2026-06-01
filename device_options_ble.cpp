// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include "device_options_ble.hpp"

extern "C" {
#include "device_options.h"
#include "filter.h"
#include "spectrum.h"
}
#include "rgb_channel_switcher.hpp"

#include <BLE2901.h>
#include <BLE2904.h>

#include <type_traits>

extern String device_name;

extern struct device_opt d_options;
extern struct filter_opt f_options;

extern ChannelSwitcher rmt_rgb_ch_swither;
extern struct rmt_cfg rmt_options;

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

constexpr uint8_t float_to_u8(float x) noexcept
{
  return float_to_int<float, uint8_t>(x, -2);
}

constexpr float float_from_u8(uint8_t x) noexcept
{
  return float_from_int<float, uint8_t>(x, -2);
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
void default_config_encode<uint8_t>(Preferences& prefs, const char* key, const uint8_t& val)
{
  prefs.putUChar(key, val);
}

template<>
uint8_t default_config_decode<uint8_t>(Preferences& prefs, const char* key, const uint8_t& def)
{
  return prefs.getUChar(key, def);
}

template<>
void default_config_encode<uint16_t>(Preferences& prefs, const char* key, const uint16_t& val)
{
  prefs.putUShort(key, val);
}

template<>
uint16_t default_config_decode<uint16_t>(Preferences& prefs, const char* key, const uint16_t& def)
{
  return prefs.getUShort(key, def);
}

template<>
void default_config_encode<String>(Preferences& prefs, const char* key, const String& val)
{
  prefs.putString(key, val);
}

template<>
String default_config_decode<String>(Preferences& prefs, const char* key, const String& def)
{
  return prefs.getString(key, def);
}

template<>
void default_config_encode<bool>(Preferences& prefs, const char* key, const bool& val)
{
  prefs.putBool(key, val);
}

template<>
bool default_config_decode<bool>(Preferences& prefs, const char* key, const bool& def)
{
  return prefs.getBool(key, def);
}


void config_encode_float_u8(Preferences& prefs, const char* key, const float& val)
{
  prefs.putUChar(key, float_to_u8(val));
}

float config_decode_float_u8(Preferences& prefs, const char* key, const float& def)
{
  return float_from_u8(prefs.getUChar(key, float_to_u8(def)));
}

void config_encode_float_u16(Preferences& prefs, const char* key, const float& val)
{
  prefs.putUShort(key, float_to_u16(val));
}

float config_decode_float_u16(Preferences& prefs, const char* key, const float& def)
{
  return float_from_u16(prefs.getUShort(key, float_to_u16(def)));
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

void fmt_rgb_layout_to_ble(const uint8_t& val, BLECharacteristic* c)
{
  char str[4];
  rgb_layout_to_str(str, sizeof(str), static_cast<rgb_layout_t>(val));
  c->setValue(reinterpret_cast<uint8_t*>(&str[0]), sizeof(str));
}

void fmt_rgb_layout_from_ble(BLECharacteristic* c, uint8_t& val)
{
  auto str = c->getValue();
  rgb_layout_t l = LAYOUT_GRB;
  if (!str_to_rgb_layout(str.c_str(), str.length(), l)) return;
  val = static_cast<uint8_t>(l);
}


constexpr ConfigEncoder<float> enc_float_u8 = {
  .write = &config_encode_float_u8,
  .read  = &config_decode_float_u8,
};

constexpr ConfigEncoder<float> enc_float_u16 = {
  .write = &config_encode_float_u16,
  .read  = &config_decode_float_u16,
};

static const RawValueFormat<uint8_t> fmt_u8_raw;
static const RawValueFormat<uint16_t> fmt_u16_raw;
static const RawValueFormat<uint32_t> fmt_u32_raw;
static const RawValueFormat<bool> fmt_bool;

static const FloatValueFormat<float, uint8_t, -2> fmt_float_u8;
static const FloatValueFormat<float, uint16_t, -4> fmt_float_u16;

static const ValueFormat<String> fmt_string = {
  .format = BLE2904::FORMAT_UTF8,
  .exponent = 0,
  .to_ble = &fmt_string_to_ble,
  .from_ble = &fmt_string_from_ble,
};

static const ValueFormat<uint8_t> fmt_rgb_layout = {
  .format = BLE2904::FORMAT_UTF8,
  .exponent = 0,
  .to_ble = &fmt_rgb_layout_to_ble,
  .from_ble = &fmt_rgb_layout_from_ble,
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


class ChannelsLayoutValue final : public Value<uint8_t>
{
public:
  explicit ChannelsLayoutValue(ChannelSwitcher& sw) noexcept
    : _sw(sw)
  {}

  uint8_t get() const override { return static_cast<uint8_t>(_sw.layout()); }
  void set(uint8_t v) override { _sw.setLayout(static_cast<rgb_layout_t>(v)); }

private:
  ChannelSwitcher& _sw;
};


static auto val_device_name = SimpleValue(device_name);
static auto val_swap_channels = SimpleValue(d_options.swap_r_b_channels);
static auto val_enable_history = SimpleValue(d_options.enable_rmt_history);
static auto val_gamma_value = SimpleValue(d_options.gamma_value);

static auto val_level_low = SimpleValue(f_options.level_low);
static auto val_level_mid = SimpleValue(f_options.level_mid);
static auto val_level_high = SimpleValue(f_options.level_high);

static auto val_thr_low = SimpleValue(f_options.thr_low);
static auto val_thr_ml = SimpleValue(f_options.thr_ml);
static auto val_thr_mh = SimpleValue(f_options.thr_mh);
static auto val_thr_high = SimpleValue(f_options.thr_high);

static auto val_rmt_ch_layout = ChannelsLayoutValue(rmt_rgb_ch_swither);
static auto val_rmt_leds_count = SimpleValue(rmt_options.leds_count);
static auto val_rmt_treset = SimpleValue(rmt_options.Treset);
static auto val_rmt_t0h = SimpleValue(rmt_options.T0H);
static auto val_rmt_t0l = SimpleValue(rmt_options.T0L);
static auto val_rmt_t1h = SimpleValue(rmt_options.T1H);
static auto val_rmt_t1l = SimpleValue(rmt_options.T1L);

static auto opt_device_name = ConfigValue(val_device_name, "device", "dev_name");
static auto opt_swap_channels = ConfigValue(val_swap_channels, "device", "swap_r_b");
static auto opt_enable_history = ConfigValue(val_enable_history, "device", "rmt_history_en");
static auto opt_gamma_value = ConfigValue(val_gamma_value, "device", "gamma_value", enc_float_u16);

static auto opt_level_low = ConfigValue(val_level_low, "filter", "level_low", enc_float_u16);
static auto opt_level_mid = ConfigValue(val_level_mid, "filter", "level_mid", enc_float_u16);
static auto opt_level_high = ConfigValue(val_level_high, "filter", "level_high", enc_float_u16);

static auto opt_thr_low = ConfigValue(val_thr_low, "filter", "thr_low");
static auto opt_thr_ml = ConfigValue(val_thr_ml, "filter", "thr_ml");
static auto opt_thr_mh = ConfigValue(val_thr_mh, "filter", "thr_mh");
static auto opt_thr_high = ConfigValue(val_thr_high, "filter", "thr_high");

static auto opt_rmt_ch_layout = ConfigValue(val_rmt_ch_layout, "rmt", "ch_layout");
static auto opt_rmt_leds_count = ConfigValue(val_rmt_leds_count, "rmt", "leds_count");
static auto opt_rmt_treset = ConfigValue(val_rmt_treset, "rmt", "Treset");
static auto opt_rmt_t0h = ConfigValue(val_rmt_t0h, "rmt", "T0H", enc_float_u8);
static auto opt_rmt_t0l = ConfigValue(val_rmt_t0l, "rmt", "T0L", enc_float_u8);
static auto opt_rmt_t1h = ConfigValue(val_rmt_t1h, "rmt", "T1H", enc_float_u8);
static auto opt_rmt_t1l = ConfigValue(val_rmt_t1l, "rmt", "T1L", enc_float_u8);

void load_values_from_config()
{
  opt_device_name.load();
  opt_swap_channels.load();
  opt_enable_history.load();
  opt_gamma_value.load();

  opt_level_low.load();
  opt_level_mid.load();
  opt_level_high.load();

  opt_thr_low.load();
  opt_thr_ml.load();
  opt_thr_mh.load();
  opt_thr_high.load();

  opt_rmt_ch_layout.load();
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

void ble_add_rmtcfg_characteristics(BLEService* service)
{
  ble_add_rw_value(service, opt_rmt_ch_layout,
                   "ccb17409-4414-4b42-9257-59631ad6c30a",
                   fmt_rgb_layout,
                   "Channels layout");
  ble_add_rw_value(service, opt_rmt_leds_count,
                   "81ddbfb5-53c2-47d4-9f65-4fc6ec0ea870",
                   fmt_u16_raw,
                   "(*) LEDs count");
  ble_add_rw_value(service, opt_rmt_treset,
                   "ed3dcafd-fb4f-4e1f-bd49-0f52ab1debf0",
                   fmt_u16_raw,
                   "(*) Treset, us");
  ble_add_rw_value(service, opt_rmt_t0h,
                   "0a1b14b4-2128-4662-bf98-77d59b2a6c05",
                   fmt_float_u8,
                   "(*) T0H, us");
  ble_add_rw_value(service, opt_rmt_t0l,
                   "952c5e4a-f828-49d6-9b5d-741cced228d0",
                   fmt_float_u8,
                   "(*) T0L, us");
  ble_add_rw_value(service, opt_rmt_t1h,
                   "18647cf2-a109-4368-9284-00fd6188ee01",
                   fmt_float_u8,
                   "(*) T1H, us");
  ble_add_rw_value(service, opt_rmt_t1l,
                   "3c1753db-8ded-4827-9f20-32c7b1f016f1",
                   fmt_float_u8,
                   "(*) T1L, us");
}
