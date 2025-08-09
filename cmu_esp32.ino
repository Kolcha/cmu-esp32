// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include <Preferences.h>

extern "C" {
#include "fft_hann_1024.h"
#include "fft_twiddles_512.h"
#include "spectrum.h"
#include "spectrum_utils.h"
}

#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_gap_bt_api.h"

#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define INDICATOR_LED_PIN                 2

#define SAMPLES_COUNT       1024
#define FFT_SIZE        (SAMPLES_COUNT/2)

#define RGB_PWM_FREQ        50000
#define RGB_PWM_BITS        10

#define count_of(X)     (sizeof(X)/sizeof(X[0]))

// ----------------------------------------------------------
//           FFT & spectrum analysis configuration
// ----------------------------------------------------------
static const simple_fft_cfg fft_cfg = {
  .n = FFT_SIZE,
  .tw = fft_twiddles_512,
  .tw_mul_re = FFT_TWIDDLE_MUL_512_RE,
  .tw_mul_im = FFT_TWIDDLE_MUL_512_IM,
};

static float fft_io_buffer[SAMPLES_COUNT];      // 4k
// reuse fft_io_buffer for spectrum: freq - amp pairs
static float spectrum_frs[FFT_SIZE];            // 2k
static float log_log_f_ks[FFT_SIZE];            // 2k

struct analysis_cfg acfg = {
  .fft_cfg = &fft_cfg,
  .kwnd = fft_window_ks_1024,
  .freq = spectrum_frs,
  .kwnd_sum = FFT_WINDOW_KS_1024_SUM,
  .preamp = 1.0,
};

// TODO: load values from "prefs"
struct filter_opt f_options = {
  .level_low = 0.8,
  .level_mid = 1.5,
  .level_high = 2.5,
  .thr_low = 3,
  .thr_ml = 4,
  .thr_mh = 17,
  .thr_high = 18,
};

// calculate by-frequency amplification coefficients
// amp_k - amplification coefficients output buffer, size is n
// freq - frequencies buffer, size is n
// n - spectrum elements count
static void amplification_coefficients(float* amp_k, const float* freq, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    amp_k[i] = log(log(freq[i]));
  }
}

static void frequencies_data_init(size_t sample_rate)
{
  frequencies_data(spectrum_frs, sample_rate, FFT_SIZE);
  amplification_coefficients(log_log_f_ks, spectrum_frs, FFT_SIZE);
}
// ----------------------------------------------------------

static RingbufHandle_t raw_audio_buffer;
static TaskHandle_t led_blink_task;

Preferences prefs;

// ----------------------------------------------------------
//                   indicator LED blinking
// ----------------------------------------------------------
static void update_led_proc(void* data)
{
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    digitalWrite(INDICATOR_LED_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(60));
    digitalWrite(INDICATOR_LED_PIN, LOW);
  }
}

static void start_led_blinking()
{
  digitalWrite(INDICATOR_LED_PIN, LOW);
  xTaskCreatePinnedToCore(
    update_led_proc,    // Function that should be called
    "blink",            // Name of the task (for debugging)
    1024,               // Stack size (bytes)
    NULL,               // Parameter to pass
    0,                  // Task priority
    &led_blink_task,    // Task handle
    1                   // Core you want to run the task on (0 or 1)
  );
}

static void stop_led_blinking()
{
  if (led_blink_task) {
    vTaskDelete(led_blink_task);
    led_blink_task = nullptr;
  }
  digitalWrite(INDICATOR_LED_PIN, HIGH);
}

// ----------------------------------------------------------
//                 BT load/save last address
// ----------------------------------------------------------
static bool read_bt_peer_addr(uint8_t* bt_addr)
{
  size_t rbytes = 0;
  prefs.begin("device", true);
  rbytes = prefs.getBytes("last_dev", bt_addr, 6);
  prefs.end();
  return rbytes == 6;
}

static bool save_bt_peer_addr(const uint8_t* bt_addr)
{
  size_t wbytes = 0;
  prefs.begin("device", false);
  wbytes = prefs.putBytes("last_dev", bt_addr, 6);
  prefs.end();
  return wbytes == 6;
}

static void maybe_save_bt_peer_addr(const uint8_t* bt_addr)
{
  uint8_t last_addr[6];
  bool has_last_address = read_bt_peer_addr(last_addr);
  bool last_is_the_same = has_last_address && memcmp(last_addr, bt_addr, 6) == 0;
  if (!has_last_address || !last_is_the_same) {
    save_bt_peer_addr(bt_addr);
  }
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//                        PWM RGB out
// ----------------------------------------------------------
static void pwm_rgb_init()
{
  ledcAttachChannel(12, RGB_PWM_FREQ, RGB_PWM_BITS, 0);
  ledcAttachChannel(13, RGB_PWM_FREQ, RGB_PWM_BITS, 1);
  ledcAttachChannel(14, RGB_PWM_FREQ, RGB_PWM_BITS, 2);

  ledcAttachChannel(25, RGB_PWM_FREQ, RGB_PWM_BITS, 0);
  ledcAttachChannel(26, RGB_PWM_FREQ, RGB_PWM_BITS, 1);
  ledcAttachChannel(27, RGB_PWM_FREQ, RGB_PWM_BITS, 2);

  ledcAttachChannel( 4, RGB_PWM_FREQ, RGB_PWM_BITS, 0);
  ledcAttachChannel(16, RGB_PWM_FREQ, RGB_PWM_BITS, 1);
  ledcAttachChannel(17, RGB_PWM_FREQ, RGB_PWM_BITS, 2);
}

static void pwm_rgb_set(float r, float g, float b)
{
  constexpr uint32_t max_value = (1 << RGB_PWM_BITS) - 1;
  ledcWriteChannel(0, static_cast<uint32_t>(std::lround(r*max_value)));
  ledcWriteChannel(1, static_cast<uint32_t>(std::lround(g*max_value)));
  ledcWriteChannel(2, static_cast<uint32_t>(std::lround(b*max_value)));

  static int ticks_to_wait = 100;
  static int ticks_counter = 0;
  if (++ticks_counter == ticks_to_wait) {
    ticks_counter = 0;
    Serial.printf("r: %.2f, g: %.2f, b: %.2f\n", r, g, b);
  }
}

static void spectrum_rgb_out(const float* spectrum)
{
  float bars[3];
  spectrum_lmh_out(spectrum, FFT_SIZE, bars, &f_options);

  for (int i = 0; i < count_of(bars); i++)
    bars[i] = std::clamp(bars[i], 0.f, 1.f);

  // TODO: implement option for swapping channels
  pwm_rgb_set(bars[2], bars[1], bars[0]);
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//                  ESP32 BT stack callbacks
// ----------------------------------------------------------
static void handle_a2d_connection_state(const esp_a2d_cb_param_t* param)
{
  switch (param->conn_stat.state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
      stop_led_blinking();
      pwm_rgb_set(0, 0, 0);
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
      esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
      start_led_blinking();
      maybe_save_bt_peer_addr(param->conn_stat.remote_bda);
      break;
  }
}

static void handle_a2d_audio_cfg(const esp_a2d_cb_param_t* param)
{
  /* for now only SBC stream is supported */
  if (param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
    uint32_t sample_rate = 16000;
    char oct0 = param->audio_cfg.mcc.cie.sbc[0];
    if (oct0 & (0x01 << 6)) {
      sample_rate = 32000;
    } else if (oct0 & (0x01 << 5)) {
      sample_rate = 44100;
    } else if (oct0 & (0x01 << 4)) {
      sample_rate = 48000;
    }
    frequencies_data_init(sample_rate);
  }
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param)
{
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
      Serial.printf("ESP_A2D_CONNECTION_STATE_EVT: %d\n", param->conn_stat.state);
      handle_a2d_connection_state(param);
      break;
    case ESP_A2D_AUDIO_STATE_EVT:
      Serial.printf("ESP_A2D_AUDIO_STATE_EVT: %d\n", param->audio_stat.state);
      break;
    case ESP_A2D_AUDIO_CFG_EVT:
      Serial.println("ESP_A2D_AUDIO_CFG_EVT");
      handle_a2d_audio_cfg(param);
      break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
      Serial.println("ESP_A2D_MEDIA_CTRL_ACK_EVT");
      break;
    case ESP_A2D_PROF_STATE_EVT:
      Serial.println("ESP_A2D_PROF_STATE_EVT");
      break;
  }
}

static void bt_data_cb(const uint8_t* data, uint32_t len)
{
  BaseType_t high_prio_task_woken = pdFALSE;
  xRingbufferSendFromISR(raw_audio_buffer, data, len, &high_prio_task_woken);
}

static void avrc_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* param)
{
  // does nothing
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//        application-specific functions (main logic)
// ----------------------------------------------------------
static void bt_audio_sink_init(const char* dev_name)
{
  btStart();
  esp_bluedroid_init();
  esp_bluedroid_enable();

  esp_bt_dev_set_device_name(dev_name);

  esp_avrc_ct_init();
  esp_avrc_ct_register_callback(avrc_cb);

  esp_a2d_sink_init();
  esp_a2d_register_callback(a2d_cb);
  esp_a2d_sink_register_data_callback(bt_data_cb);

  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

static void reconnect_to_last_device()
{
  uint8_t last_addr[6];
  if (read_bt_peer_addr(last_addr)) {
    esp_a2d_sink_connect(last_addr);
  }
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//                       Arduino hooks
// ----------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  while (!Serial) {}

  // use double buffering: 2 buffers x 2 16bit channels
  raw_audio_buffer = xRingbufferCreate(2*2*SAMPLES_COUNT*sizeof(int16_t), RINGBUF_TYPE_BYTEBUF);

  pwm_rgb_init();

  pinMode(INDICATOR_LED_PIN, OUTPUT);
  digitalWrite(INDICATOR_LED_PIN, HIGH);

  // TODO: read name from "prefs", use "dev_name" key
  bt_audio_sink_init("ESP_Speaker_N");
  reconnect_to_last_device();
}

static int16_t input_buffer[2*SAMPLES_COUNT];   // 2 channels

void loop()
{
  size_t bytes_left = sizeof(input_buffer);
  size_t dst_offset = 0;

  while (bytes_left > 0) {
    size_t bytes_read = 0;
    void* buffer = xRingbufferReceiveUpTo(raw_audio_buffer, &bytes_read, pdMS_TO_TICKS(10), bytes_left);
    if (buffer && bytes_read > 0) {
      bytes_left -= bytes_read;
      memcpy((uint8_t*)input_buffer + dst_offset, buffer, bytes_read);
      dst_offset += bytes_read;
      vRingbufferReturnItem(raw_audio_buffer, buffer);
    }
  }

  analyze_input(&acfg, input_buffer, fft_io_buffer);

  for (int i = 0; i < FFT_SIZE; i++) {
    fft_io_buffer[2*i + 1] *= 2 * log_log_f_ks[i];
  }

  spectrum_rgb_out(fft_io_buffer);
}
