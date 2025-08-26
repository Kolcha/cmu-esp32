// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include <Preferences.h>

#include <BLEDevice.h>
#include <BLEServer.h>

extern "C" {
#include "device_options.h"
#include "fft_hann_1024.h"
#include "fft_twiddles_512.h"
#include "filter.h"
#include "spectrum.h"
}
#include "device_options_ble.hpp"

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

#define DEVICE_SERVICE_UUID     "8af2e1aa-6cfa-4cd8-a9f9-54243e04d9c7"
#define FILTER_SERVICE_UUID     "fc8bd000-4814-4031-bff0-fbca1b99ee44"

#define count_of(X)     (sizeof(X)/sizeof(X[0]))

// ----------------------------------------------------------
//                    device configuration
// ----------------------------------------------------------
struct device_opt d_options = {
  .swap_r_b_channels = true,
  .enable_log_log_f_ks = true,
  .enable_gamma_corr = false,
  .gamma_value = 2.4,
};
String device_name = "ESP_Speaker_K";

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

  if (d_options.enable_gamma_corr)
    for (int i = 0; i < count_of(bars); i++)
      bars[i] = apply_gamma(bars[i], d_options.gamma_value);

  if (d_options.swap_r_b_channels)
    std::swap(bars[0], bars[2]);

  pwm_rgb_set(bars[0], bars[1], bars[2]);
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//                  ESP32 BT stack callbacks
// ----------------------------------------------------------
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
  switch (event) {
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
      if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
        Serial.printf("authentication success: %s\n", param->auth_cmpl.device_name);
      } else {
        Serial.printf("authentication failed, status: %d\n", param->auth_cmpl.stat);
      }
      break;
    }

    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
      Serial.printf("ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06"PRIu32"\n", param->cfm_req.num_val);
      esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      Serial.printf("ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06"PRIu32"\n", param->key_notif.passkey);
      break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
      Serial.printf("ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!\n");
      break;

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
      Serial.printf("ESP_BT_GAP_MODE_CHG_EVT mode: %d\n", param->mode_chg.mode);
      break;
    /* others */
    default: {
      Serial.printf("event: %d\n", event);
      break;
    }
  }
}

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
    uint8_t samp_freq = param->audio_cfg.mcc.cie.sbc_info.samp_freq;
    if (samp_freq & ESP_A2D_SBC_CIE_SF_32K) {
      sample_rate = 32000;
    } else if (samp_freq & ESP_A2D_SBC_CIE_SF_44K) {
      sample_rate = 44100;
    } else if (samp_freq & ESP_A2D_SBC_CIE_SF_48K) {
      sample_rate = 48000;
    }
    Serial.printf("sample rate: %lu\n", sample_rate);
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

  /* set default parameters for Secure Simple Pairing */
  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
  esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

  /* set default parameters for Legacy Pairing (use fixed pin code 0000) */
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  pin_code[0] = '0';
  pin_code[1] = '0';
  pin_code[2] = '0';
  pin_code[3] = '0';
  esp_bt_gap_set_pin(pin_type, 4, pin_code);

  esp_bt_gap_set_device_name(dev_name);
  esp_bt_gap_register_callback(bt_app_gap_cb);

  esp_avrc_ct_init();
  esp_avrc_ct_register_callback(avrc_cb);

  esp_a2d_sink_init();
  esp_a2d_register_callback(a2d_cb);
  esp_a2d_sink_register_data_callback(bt_data_cb);

  esp_a2d_sink_get_delay_value();

  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

static void reconnect_to_last_device()
{
  uint8_t last_addr[6];
  if (read_bt_peer_addr(last_addr)) {
    esp_a2d_sink_connect(last_addr);
  }
}

class MyServerCallbacks: public BLEServerCallbacks
{
  void onConnect(BLEServer* pServer)
  {
  }

  void onDisconnect(BLEServer* pServer)
  {
    // pServer->startAdvertising();
    BLEDevice::startAdvertising();
  }
};

static void ble_server_init(const char* dev_name)
{
  BLEDevice::init(dev_name);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks);

  auto i_service = pServer->createService(BLEUUID(static_cast<uint16_t>(0x180A)));
  auto dev_man_c = i_service->createCharacteristic(BLEUUID(static_cast<uint16_t>(0x2A29)), BLECharacteristic::PROPERTY_READ);
  dev_man_c->setValue(String("Nick Korotysh"));
  i_service->start();

  auto d_service = pServer->createService(BLEUUID(DEVICE_SERVICE_UUID), 32);
  ble_add_device_characteristics(d_service);
  d_service->start();

  auto f_service = pServer->createService(BLEUUID(FILTER_SERVICE_UUID), 64);
  ble_add_filter_characteristics(f_service);
  f_service->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID(static_cast<uint16_t>(0x180A)));
  pAdvertising->addServiceUUID(DEVICE_SERVICE_UUID);
  pAdvertising->addServiceUUID(FILTER_SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter

  BLEDevice::startAdvertising();
}
// ----------------------------------------------------------

// ----------------------------------------------------------
//                       Arduino hooks
// ----------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  while (!Serial) {}
  delay(500);
  Serial.println("serial ready!");

  // use double buffering: 2 buffers x 2 16bit channels
  raw_audio_buffer = xRingbufferCreate(2*2*SAMPLES_COUNT*sizeof(int16_t), RINGBUF_TYPE_BYTEBUF);

  pwm_rgb_init();

  pinMode(INDICATOR_LED_PIN, OUTPUT);
  digitalWrite(INDICATOR_LED_PIN, HIGH);

  load_values_from_config();

  bt_audio_sink_init(device_name.c_str());
  ble_server_init(device_name.c_str());
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

  if (d_options.enable_log_log_f_ks) {
    for (int i = 0; i < FFT_SIZE; i++) {
      fft_io_buffer[2*i + 1] *= 2 * log_log_f_ks[i];
    }
  }

  spectrum_rgb_out(fft_io_buffer);
}
