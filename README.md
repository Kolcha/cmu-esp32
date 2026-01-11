# ESP32 Audio Spectrum RGB Controller

A Bluetooth audio sink device that analyzes incoming audio in real-time and drives RGB LED strips to create dynamic visualizations synchronized to the music. The device connects to your phone or computer via Bluetooth and lights up in response to different frequency ranges in the audio.

## What It Does

This ESP32-based device acts as a **Bluetooth speaker** (A2DP audio sink) that processes audio through real-time **FFT spectrum analysis** and outputs a synchronized RGB light show. The device:

- Connects to your phone, tablet, or computer as a Bluetooth audio device
- Analyzes audio in real-time using 512-point FFT with Hann windowing
- Splits the audio spectrum into three frequency bands: Low (bass), Mid, and High (treble)
- Drives RGB LED strips with PWM outputs that respond to each frequency band
- Configurable via Bluetooth Low Energy (BLE) for fine-tuning the visualization

Perfect for creating ambient lighting that responds to your music, podcasts, or any audio playback!

**Note:** Use some software to send audio to multiple devices (this device and your speaker / headphones), as this device is basically Bluetooth speaker, but it doesn't have audio output.

## Features

- **Bluetooth Audio Receiver**: Standard A2DP sink supporting common sample rates (44.1kHz, 48kHz)
- **Real-Time Spectrum Analysis**: 1024-sample FFT with configurable windowing
- **Triple-Channel RGB Output**: Separate PWM channels for bass, mids, and treble
- **BLE Configuration**: Adjust settings wirelessly without reprogramming
- **Automatic Reconnection**: Remembers last connected device
- **Visual Indicators**: Built-in LED shows connection status

## Hardware Requirements

### Core Components

- **ESP32 Development Board** (any variant with sufficient GPIO pins)
- **RGB LED Strips** (3 strips or 1 RGB strip)
- 3 n-channel MOSFETs to drive LED strips, for example FQP30N06L
- Power supply appropriate for your LED strips

### Pin Configuration

#### RGB Output Pins (PWM)

The device supports **three independent RGB LED strips** or channels:

| Channel | Red Pin | Green Pin | Blue Pin | Purpose |
|---------|---------|-----------|----------|---------|
| 1       | GPIO 12 | GPIO 13   | GPIO 14  | Low frequencies (Bass) |
| 2       | GPIO 25 | GPIO 26   | GPIO 27  | Mid frequencies |
| 3       | GPIO 4  | GPIO 16   | GPIO 17  | High frequencies (Treble) |

PWM Specifications:

- Frequency: 75 kHz
- Resolution: 10-bit (0-1023 levels)

#### Status Indicator

- **GPIO 2**: Built-in LED for connection status
  - Solid ON: Disconnected/discoverable
  - Blinking (every 3 seconds): Device is active

## Installation

### Prerequisites

- Arduino IDE with [ESP32 board support](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html), at least BSP version 3.3.0 is required
- Required libraries:
  - ESP32 BLE Arduino (comes with board support package)
  - Built-in ESP32 A2DP libraries (part of board support package)

### Upload Instructions

1. Clone or download this repository
2. Open `cmu_esp32.ino` in Arduino IDE
3. Select your ESP32 board from Tools → Board
4. Select the correct COM port
5. Upload the sketch

### First Boot

On first boot, the device will:

1. Initialize with name: **"ESP_Speaker_K"**
2. Become discoverable as a Bluetooth audio device
3. Start BLE advertising for configuration
4. Show solid LED when waiting for connection

## Usage

### Connecting Audio

1. **On your phone/computer:**
   - Enable Bluetooth
   - Look for device named "ESP_Speaker_K"
   - Connect (PIN: 0000 if prompted)

2. **Play audio:**

   - Start playing music or any audio
   - RGB LEDs will respond to the audio frequencies

3. **Connection behavior:**

   - Device remembers last connected device
   - Automatically attempts reconnection on power-up
   - LED blinks every 3 seconds when connected and active

### Visual Effects

The RGB outputs are divided by frequency:

- **Red channel (Low)**: Bass frequencies - thumps and kicks
- **Green channel (Mid)**: Mid-range - vocals and melody
- **Blue channel (High)**: Treble frequencies - cymbals and high-hats

Each channel's intensity varies based on the amplitude of its respective frequency range.

## Configuration

### BLE Configuration Interface

The device exposes two BLE services for wireless configuration:

#### Device Service

**UUID:** `8af2e1aa-6cfa-4cd8-a9f9-54243e04d9c7`

- **Device name**: Bluetooth device name
- **Swap R/B Channels**: Swap red and blue outputs
- **Gamma Correction**: Adjust brightness curve (default: 2.8)

#### Filter Service

**UUID:** `fc8bd000-4814-4031-bff0-fbca1b99ee44`

Configure the frequency band separation and amplification:

| Parameter | Description | Default |
|-----------|-------------|---------|
| `level_low` | Bass amplification | 0.8 |
| `level_mid` | Mid-range amplification | 1.25 |
| `level_high` | Treble amplification | 1.85 |
| `thr_low` | Low band end index | 2 |
| `thr_ml` | Mid-low transition index | 3 |
| `thr_mh` | Mid-high transition index | 18 |
| `thr_high` | High band start index | 19 |

**Note:** Thresholds are indices into the 512-point FFT output. Settings are automatically saved to non-volatile storage.

### Using a BLE App

Use any BLE explorer app (e.g., nRF Connect, LightBlue) to:

1. Connect to "ESP_Speaker_K" via BLE
2. Navigate to the Device or Filter service
3. Read/write characteristic values
4. Changes take effect immediately

## Technical Details

### Audio Processing Pipeline

1. **Audio Reception**: A2DP sink receives 16-bit stereo audio
2. **Buffering**: FreeRTOS ring buffer stores incoming samples
3. **Windowing**: Hann window applied to 1024 samples
4. **FFT**: 512-point FFT calculates frequency spectrum
5. **Frequency Weighting**: Logarithmic amplification compensation
6. **Band Filtering**: Spectrum split into low/mid/high bands
7. **Gamma Correction**: Non-linear brightness scaling
8. **PWM Output**: 10-bit PWM drives RGB channels

### Frequency Band Defaults

Based on 512-point FFT at typical sample rates:

- **Low (Bass)**: ~0-180 Hz
- **Mid**: ~180-1.5 kHz
- **High (Treble)**: ~1.5-20 kHz

Actual frequencies depend on sample rate and threshold settings.

## Customization

### Changing Device Name

Edit line 47 in [cmu_esp32.ino](cmu_esp32.ino#L47):
```cpp
String device_name = "ESP_Speaker_K";  // Change to your preferred name
```

### Adjusting Frequency Bands

Modify the filter options in [cmu_esp32.ino](cmu_esp32.ino#L70-L77) or configure via BLE.

### PWM Frequency/Resolution

Adjust PWM settings in [cmu_esp32.ino](cmu_esp32.ino#L31-L32):
```cpp
#define RGB_PWM_FREQ        75000  // PWM frequency in Hz
#define RGB_PWM_BITS        10     // PWM resolution in bits
```

## Troubleshooting

**Device not discoverable:**

- Verify Bluetooth is enabled and device is powered
- Check serial output for initialization errors

**No light output:**

- Verify LED strip connections and power
- Check if PWM pins match your wiring
- Test individual channels with a multimeter

**Poor frequency response:**

- Adjust filter levels via BLE
- Modify preamp value (default: 1.0) in [cmu_esp32.ino](cmu_esp32.ino#L66)
- Experiment with gamma correction value

**Connection drops:**

- Ensure sufficient power supply
- Reduce distance between devices
- Check for Bluetooth interference

## License

MIT License - See [LICENSE.txt](LICENSE.txt) for details.

Copyright © 2025 Nick Korotysh

## Credits

- FFT implementation: [custom fixed-point optimized for embedded](https://github.com/Kolcha/simple-fft)
- BLE integration: [ESP32 Arduino BLE library](https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE)
- A2DP support: [ESP-IDF Bluetooth stack](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_a2dp.html)
