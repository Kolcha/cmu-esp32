// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <BLECharacteristic.h>
#include <BLEService.h>

#include <Preferences.h>

void load_values_from_config();

void ble_add_device_characteristics(BLEService* service);
void ble_add_filter_characteristics(BLEService* service);


template<typename T>
class ConfigValue
{
public:
  ConfigValue(T& val, const char* sec, const char* key) noexcept
    : _val(val), _sec(sec), _key(key)
  {}

  void save()
  {
    Preferences prefs;
    prefs.begin(_sec, false);
    write(prefs);
    prefs.end();
  }

  void load()
  {
    Preferences prefs;
    prefs.begin(_sec, true);
    read(prefs);
    prefs.end();
  }

  T& value() noexcept { return _val; }
  const T& value() const noexcept { return _val; }

protected:
  void write(Preferences& prefs);
  void read(Preferences& prefs);

private:
  T& _val;
  const char* const _sec;
  const char* const _key;
};


template<typename T>
struct ValueFormat {
  uint8_t format;
  int8_t exponent;
  void(*to_ble)(const T& val, BLECharacteristic* c);
  void(*from_ble)(BLECharacteristic* c, T& val);
};


template<typename T>
class ValueWriteCallback : public BLECharacteristicCallbacks
{
public:
  ValueWriteCallback(ConfigValue<T>& val, const ValueFormat<T>& fmt) noexcept
    : _val(val), _fmt(fmt)
  {}

  void onWrite(BLECharacteristic* c) override
  {
    _fmt.from_ble(c, _val.value());
    _val.save();
  }

private:
  ConfigValue<T>& _val;
  const ValueFormat<T>& _fmt;
};


void ble_characteristic_add_format(BLECharacteristic* c, uint8_t fmt, int8_t exp);
void ble_characteristic_add_description(BLECharacteristic* c, const char* desc);

template<typename T>
void ble_characteristic_bind_value(BLECharacteristic* c, ConfigValue<T>& val, const ValueFormat<T>& fmt)
{
  c->setCallbacks(new ValueWriteCallback<T>(val, fmt));
  fmt.to_ble(val.value(), c);
}

template<typename T>
void ble_add_option(
  BLEService* service,
  ConfigValue<T>& value,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr uint32_t rw_props = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
  auto characteristic = service->createCharacteristic(uuid, rw_props);
  ble_characteristic_add_format(characteristic, format.format, format.exponent);
  ble_characteristic_add_description(characteristic, description);
  ble_characteristic_bind_value(characteristic, value, format);
}
