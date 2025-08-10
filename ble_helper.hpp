#pragma once

#include <BLECharacteristic.h>
#include <BLEService.h>

class ValueWriteCallback : public BLECharacteristicCallbacks
{
public:
  ValueWriteCallback(void* val_dest, size_t val_size) noexcept;

  void onWrite(BLECharacteristic *pCharacteristic) override;

private:
  void* _val_dest;
  size_t _val_size;
};


void ble_bind_characteristic_value_impl(
  BLECharacteristic *pCharacteristic,
  void* val_dest, size_t val_size);

template<typename T>
void ble_bind_characteristic_value(BLECharacteristic *pCharacteristic, T* value)
{
  ble_bind_characteristic_value_impl(pCharacteristic, value, sizeof(*value));
}

void ble_characteristic_add_value_u_desc(BLECharacteristic* c, String desc);
void ble_characteristic_add_value_format(BLECharacteristic* c, uint8_t fmt);

template<typename T>
void ble_characteristic_configure(BLECharacteristic* c, T* val, String desc, uint8_t fmt)
{
  ble_bind_characteristic_value(c, val);
  ble_characteristic_add_value_u_desc(c, desc);
  ble_characteristic_add_value_format(c, fmt);
}

template<typename T>
void ble_add_rw_characteristic(BLEService* service, const char* uuid, T* value, uint8_t format, String desc)
{
  constexpr uint32_t rw_props = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
  ble_characteristic_configure(service->createCharacteristic(uuid, rw_props), value, desc, format);
}
