#include "ble_helper.hpp"

#include <BLE2901.h>
#include <BLE2904.h>

ValueWriteCallback::ValueWriteCallback(void* val_dest, size_t val_size) noexcept
  : _val_dest(val_dest)
  , _val_size(val_size)
{
}

void ValueWriteCallback::onWrite(BLECharacteristic *pCharacteristic)
{
  if (pCharacteristic->getLength() != _val_size) {
    return;
  }

  memcpy(_val_dest, pCharacteristic->getData(), _val_size);
}

void ble_bind_characteristic_value_impl(
  BLECharacteristic *pCharacteristic,
  void* val_dest, size_t val_size)
{
  pCharacteristic->setValue(static_cast<uint8_t*>(val_dest), val_size);
  pCharacteristic->setCallbacks(new ValueWriteCallback(val_dest, val_size));
}

void ble_characteristic_add_value_u_desc(BLECharacteristic* c, String desc)
{
  auto ble_desc = new BLE2901();
  ble_desc->setDescription(std::move(desc));
  c->addDescriptor(ble_desc);
}

void ble_characteristic_add_value_format(BLECharacteristic* c, uint8_t fmt)
{
  auto ble_desc = new BLE2904();
  ble_desc->setFormat(fmt);
  c->addDescriptor(ble_desc);
}
