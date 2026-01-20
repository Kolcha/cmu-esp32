# ESP32 Audio Spectrum RGB Controller

A Bluetooth audio sink device that analyzes incoming audio in real-time and drives RGB LED strips to create dynamic visualizations synchronized to the music. The device connects to your phone or computer via Bluetooth and lights up in response to different frequency ranges in the audio.

## What It Does

This ESP32-based device acts as a **Bluetooth speaker** (A2DP audio sink) that processes audio through real-time **FFT spectrum analysis** and outputs a synchronized RGB light show. The device:

- Connects to your phone, tablet, or computer as a Bluetooth audio device
- Analyzes audio in real-time using 512-point FFT with Hann windowing
- Splits the audio spectrum into three frequency bands: Low (bass), Mid, and High (treble)
- Drives RGB LED strips with PWM outputs that respond to each frequency band
- Can drive "smart" RGB LED strips (with individually addressable LEDs)
- Configurable via Bluetooth Low Energy (BLE) for fine-tuning the visualization

Perfect for creating ambient lighting that responds to your music, podcasts, or any audio playback!

**Note:** Use some software to send audio to multiple devices (this device and your speaker / headphones), as this device is basically Bluetooth speaker, but it doesn't have audio output.

## Features

- **Bluetooth Audio Receiver**: Standard A2DP sink supporting common sample rates (44.1kHz, 48kHz)
- **Real-Time Spectrum Analysis**: 1024-sample FFT with configurable windowing
- **Triple-Channel RGB Output**: Separate PWM channels for bass, mids, and treble
- **Smart LED support**: Can control LEDs strips made of WS2812b or similar LEDs
- **BLE Configuration**: Adjust settings wirelessly without reprogramming
- **Automatic Reconnection**: Remembers last connected device
- **Visual Indicators**: Built-in LED shows connection status

## Visual Effects

The RGB outputs are divided by frequency:

- **Red channel (Low)**: Bass frequencies - thumps and kicks
- **Green channel (Mid)**: Mid-range - vocals and melody
- **Blue channel (High)**: Treble frequencies - cymbals and high-hats

Each channel's intensity varies based on the amplitude of its respective frequency range.

## Hardware Requirements

### Core Components

- **ESP32 Development Board** (any variant with sufficient GPIO pins)
- **RGB LED Strips** (3 strips or 1 RGB strip)
- 3 n-channel MOSFETs to drive LED strips, for example FQP30N06L
- Power supply appropriate for your LED strips

### Pin Configuration

#### RGB Output Pins (PWM)

The device supports **three independent RGB LED strips** or channels:

| Channel | 1       | 2       | 3       | Purpose |
|---------|---------|---------|---------|---------|
| Red     | GPIO 12 | GPIO 25 | GPIO 4  | Low frequencies (Bass) |
| Green   | GPIO 13 | GPIO 26 | GPIO 16 | Mid frequencies |
| Blue    | GPIO 14 | GPIO 27 | GPIO 17 | High frequencies (Treble) |

#### RGB Output Pins (RMT)

**GPIO 15**: Control data signal for LED strips with individually addressable LEDs

#### Status Indicator

**GPIO 2**: Built-in LED for connection status

- Solid ON: Disconnected/discoverable
- Blinking (every 3 seconds): Device is active

## Installation

Build and flash as any other Arduino project. Official [ESP32 board support package](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) at least version 3.3.0 is required.

## Configuration

### BLE Configuration Interface

The device exposes two BLE services for wireless configuration. Use any BLE explorer app (e.g., nRF Connect, LightBlue) to adjust the values. Changes take effect immediately. Settings are automatically saved to non-volatile storage.

#### Device Service

- **Device name**: Bluetooth device name
- **Swap R/B Channels**: Swap red and blue outputs
- **Enable color history**: Show history instead of solid color
- **Gamma Correction**: Adjust brightness curve (default: 2.8)

#### Filter Service

Configure the frequency band separation and amplification:

| Parameter | Description | Default |
|-----------|-------------|---------|
| `preamp` | Input preamplification | 1.0 |
| `level_low` | Bass amplification | 0.8 |
| `level_mid` | Mid-range amplification | 1.25 |
| `level_high` | Treble amplification | 1.85 |
| `thr_low` | Low band end index | 2 |
| `thr_ml` | Mid-low transition index | 3 |
| `thr_mh` | Mid-high transition index | 18 |
| `thr_high` | High band start index | 19 |

**Note:** Thresholds are indices into the 512-point FFT output.

## Technical Details

### Frequency Band Defaults

Based on 512-point FFT at typical sample rates:

- **Low (Bass)**: ~0-135 Hz
- **Mid**: ~135-850 Hz
- **High (Treble)**: ~850-20 kHz

Actual frequencies depend on sample rate and threshold settings.

### PWM Specifications

- Frequency: 75 kHz
- Resolution: 10-bit (0-1023 levels)

### RMT Specifications

- Channels layout: GRB
- LEDs count: 300
- T0H: 0.4 us
- T0L: 0.85 us
- T1H: 0.8 us
- T1L: 0.45 us
- Treset: 60 us

Configured for WS2812b, but should be compatible with many other.

## License

MIT License - See [LICENSE.txt](LICENSE.txt) for details.

Copyright © 2025 Nick Korotysh

## Credits

- FFT implementation: [custom fixed-point optimized for embedded](https://github.com/Kolcha/simple-fft)
- BLE integration: [ESP32 Arduino BLE library](https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE)
- A2DP support: [ESP-IDF Bluetooth stack](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_a2dp.html)
- Smart LEDs support: [ESP-IDF RMT stack](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/led_strip)
