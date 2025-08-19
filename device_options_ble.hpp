// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <BLECharacteristic.h>
#include <BLEDescriptor.h>
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
class DynamicValueBinder : public BLECharacteristicCallbacks
{
public:
  using getter_type = std::function<T()>;

  DynamicValueBinder(getter_type getter, const ValueFormat<T>& format) noexcept
    : _getter(std::move(getter)), _format(format)
  {}

  void onRead(BLECharacteristic* c) override { _format.to_ble(_getter(), c); }

private:
  getter_type _getter;
  const ValueFormat<T>& _format;
};


template<typename T>
class ValueBinder : public BLECharacteristicCallbacks
{
public:
  ValueBinder(T& val, const ValueFormat<T>& fmt) noexcept
    : _val(val), _fmt(fmt)
  {}

  void onRead(BLECharacteristic* c) override { _fmt.to_ble(_val, c); }
  void onWrite(BLECharacteristic* c) override { _fmt.from_ble(c, _val); }

private:
  T& _val;
  const ValueFormat<T>& _fmt;
};


template<typename T>
class ValueWriteCallback : public ValueBinder<T>
{
public:
  ValueWriteCallback(ConfigValue<T>& val, const ValueFormat<T>& fmt) noexcept
    : ValueBinder<T>(val.value(), fmt), _val(val)
  {}

  void onWrite(BLECharacteristic* c) override
  {
    ValueBinder<T>::onWrite(c);
    _val.save();
  }

private:
  ConfigValue<T>& _val;
};


void ble_characteristic_add_format(BLECharacteristic* c, uint8_t fmt, int8_t exp);
void ble_characteristic_add_description(BLECharacteristic* c, const char* desc);

template<typename T>
void ble_characteristic_add_value_range(BLECharacteristic* c, T vmin, T vmax)
{
  T data[] = {vmin, vmax};
  auto desc = new BLEDescriptor(BLEUUID(static_cast<uint16_t>(0x2906)));
  desc->setValue(reinterpret_cast<uint8_t*>(data), sizeof(data));
  c->addDescriptor(desc);
}

template<typename T>
void ble_characteristic_bind_value(BLECharacteristic* c, T& val, const ValueFormat<T>& fmt)
{
  c->setCallbacks(new ValueBinder<T>(val, fmt));
}


template<typename T>
BLECharacteristic* ble_add_value_impl(
  BLEService* service,
  uint32_t props,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  auto characteristic = service->createCharacteristic(uuid, props);
  ble_characteristic_add_format(characteristic, format.format, format.exponent);
  ble_characteristic_add_description(characteristic, description);
  return characteristic;
}


template<typename T>
void ble_add_ro_value(
  BLEService* service,
  typename DynamicValueBinder<T>::getter_type getter,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr auto props = BLECharacteristic::PROPERTY_READ;
  auto c = ble_add_value_impl(service, props, uuid, format, description);
  c->setCallbacks(new DynamicValueBinder<T>(std::move(getter), format));
}


template<typename T>
void ble_add_ro_value(
  BLEService* service,
  T& value,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr auto props = BLECharacteristic::PROPERTY_READ;
  auto c = ble_add_value_impl(service, props, uuid, format, description);
  ble_characteristic_bind_value(c, value, format);
}

template<typename T>
void ble_add_rw_value(
  BLEService* service,
  T& value,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr auto props = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
  auto c = ble_add_value_impl(service, props, uuid, format, description);
  ble_characteristic_bind_value(c, value, format);
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
  auto characteristic = ble_add_value_impl(service, rw_props, uuid, format, description);
  characteristic->setCallbacks(new ValueWriteCallback<T>(value, format));
}
