// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <BLECharacteristic.h>
#include <BLEService.h>

void ble_add_filter_characteristics(BLEService* service);
