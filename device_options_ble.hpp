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
class Value
{
public:
  using value_type = T;

  virtual ~Value() = default;

  virtual T get() const = 0;
  virtual void set(T v) = 0;
};


template<typename T>
class SimpleValue final : public Value<T>
{
public:
  explicit SimpleValue(T& v) noexcept : _v(v) {}

  T get() const noexcept override { return _v; }
  void set(T v) noexcept override { _v = std::move(v); }

private:
  T& _v;
};


template<typename T>
class ValueDecorator : public Value<T>
{
public:
  explicit ValueDecorator(Value<T>& val) noexcept
    : _val(val)
  {}

  T get() const override { return _val.get(); }
  void set(T v) override { _val.set(std::move(v)); }

private:
  Value<T>& _val;
};


template<typename T>
class ConfigValue : public ValueDecorator<T>
{
  using Parent = ValueDecorator<T>;

public:
  ConfigValue(Value<T>& val, const char* sec, const char* key) noexcept
    : ValueDecorator<T>(val)
    , _sec(sec), _key(key)
  {}

  void set(T v) override
  {
    Parent::set(std::move(v));
    save();
  }

  void save()
  {
    Preferences prefs;
    prefs.begin(_sec, false);
    write(prefs, Parent::get());
    prefs.end();
  }

  void load()
  {
    Preferences prefs;
    prefs.begin(_sec, true);
    Parent::set(read(prefs, Parent::get()));
    prefs.end();
  }

protected:
  void write(Preferences& prefs, const T& val);
  T read(Preferences& prefs, const T& def);

private:
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
  ValueBinder(Value<T>& val, const ValueFormat<T>& fmt) noexcept
    : _val(val), _fmt(fmt)
  {}

  void onRead(BLECharacteristic* c) override
  {
    _fmt.to_ble(_val.get(), c);
  }

  void onWrite(BLECharacteristic* c) override
  {
    T v{};
    _fmt.from_ble(c, v);
    _val.set(std::move(v));
  }

private:
  Value<T>& _val;
  const ValueFormat<T>& _fmt;
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
void ble_characteristic_bind_value(BLECharacteristic* c, Value<T>& val, const ValueFormat<T>& fmt)
{
  c->setCallbacks(new ValueBinder<T>(val, fmt));
}


template<typename T>
void ble_add_value_impl(
  BLEService* service,
  Value<T>& value,
  uint32_t props,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  auto characteristic = service->createCharacteristic(uuid, props);
  ble_characteristic_bind_value(characteristic, value, format);
  ble_characteristic_add_format(characteristic, format.format, format.exponent);
  ble_characteristic_add_description(characteristic, description);
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
  auto c = service->createCharacteristic(uuid, props);
  ble_characteristic_add_format(c, format.format, format.exponent);
  ble_characteristic_add_description(c, description);
  c->setCallbacks(new DynamicValueBinder<T>(std::move(getter), format));
}

template<typename T>
void ble_add_ro_value(
  BLEService* service,
  Value<T>& value,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr auto props = BLECharacteristic::PROPERTY_READ;
  ble_add_value_impl(service, value, props, uuid, format, description);
}

template<typename T>
void ble_add_rw_value(
  BLEService* service,
  Value<T>& value,
  const char* uuid,
  const ValueFormat<T>& format,
  const char* description
)
{
  constexpr auto props = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
  ble_add_value_impl(service, value, props, uuid, format, description);
}
