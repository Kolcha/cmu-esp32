// SPDX-FileCopyrightText: 2025 Nick Korotysh <nick.korotysh@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <deque>
#include <vector>

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
#include "led_strip_encoder.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"

#include "driver/rmt_tx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define INDICATOR_LED_PIN                 2

#define SAMPLES_COUNT       1024
#define FFT_SIZE        (SAMPLES_COUNT/2)

#define RGB_PWM_FREQ        75000
#define RGB_PWM_BITS        10

#define RMT_LED_STRIP_RESOLUTION_HZ 20000000 // 20MHz resolution, 1 tick = 0.05us
#define RMT_LED_STRIP_GPIO_NUM      GPIO_NUM_15
#define RMT_LED_STRIP_LEDS_COUNT    300

#define DEVICE_SERVICE_UUID     "8af2e1aa-6cfa-4cd8-a9f9-54243e04d9c7"
#define FILTER_SERVICE_UUID     "fc8bd000-4814-4031-bff0-fbca1b99ee44"

/* log tags */
#define BT_AV_TAG           "BT_AV"
/* Application layer causes delay value */
#define APP_DELAY_VALUE                   50  // 5ms

#define count_of(X)     (sizeof(X)/sizeof(X[0]))

// ----------------------------------------------------------
//                    device configuration
// ----------------------------------------------------------
struct device_opt d_options = {
  .swap_r_b_channels = false,
  .enable_rmt_history = false,
  .gamma_value = 2.8,
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
  .level_mid = 1.25,
  .level_high = 1.85,
  .thr_low = 2,
  .thr_ml = 3,
  .thr_mh = 18,
  .thr_high = 19,
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

struct rgb_data_t {
  uint8_t g;
  uint8_t r;
  uint8_t b;
};

std::deque<rgb_data_t> rmt_history;
std::vector<rgb_data_t> rmt_pixels;

static rmt_channel_handle_t led_chan = nullptr;
static rmt_encoder_handle_t led_encoder = nullptr;

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
  rbytes = prefs.getBytes("last_dev", bt_addr, ESP_BD_ADDR_LEN);
  prefs.end();
  return rbytes == ESP_BD_ADDR_LEN;
}

static bool save_bt_peer_addr(const uint8_t* bt_addr)
{
  size_t wbytes = 0;
  prefs.begin("device", false);
  wbytes = prefs.putBytes("last_dev", bt_addr, ESP_BD_ADDR_LEN);
  prefs.end();
  return wbytes == ESP_BD_ADDR_LEN;
}

static void maybe_save_bt_peer_addr(const uint8_t* bt_addr)
{
  uint8_t last_addr[ESP_BD_ADDR_LEN];
  bool has_last_address = read_bt_peer_addr(last_addr);
  bool last_is_the_same = has_last_address && memcmp(last_addr, bt_addr, ESP_BD_ADDR_LEN) == 0;
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
}

// ----------------------------------------------------------
//                        RMT RGB out
// ----------------------------------------------------------
static void rmt_rgb_init()
{
  rmt_history.resize(RMT_LED_STRIP_LEDS_COUNT);
  rmt_pixels.resize(RMT_LED_STRIP_LEDS_COUNT);

  rmt_tx_channel_config_t tx_chan_config = {
    .gpio_num = RMT_LED_STRIP_GPIO_NUM,
    .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
    .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
    .mem_block_symbols = 64, // increase the block size can make the LED less flickering
    .trans_queue_depth = 2,  // set the number of transactions that can be pending in the background
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

  led_strip_encoder_config_t encoder_config = {
    .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
  };
  ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

  ESP_ERROR_CHECK(rmt_enable(led_chan));
}

static void rmt_rgb_write_pixels()
{
  const rmt_transmit_config_t tx_config = {
    .loop_count = 0, // no transfer loop
  };
  const auto data = rmt_pixels.data();
  const auto size = rmt_pixels.size() * sizeof(rgb_data_t);
  ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, data, size, &tx_config));
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, 12));
}

static void rmt_rgb_set(float r, float g, float b)
{
  rgb_data_t rgb;
  rgb.r = static_cast<uint8_t>(std::lround(r*255));
  rgb.g = static_cast<uint8_t>(std::lround(g*255));
  rgb.b = static_cast<uint8_t>(std::lround(b*255));

  if (d_options.enable_rmt_history) {
    rmt_history.pop_back();
    rmt_history.push_front(rgb);
    std::copy(rmt_history.begin(), rmt_history.end(), rmt_pixels.begin());
  } else {
    std::fill(rmt_pixels.begin(), rmt_pixels.end(), rgb);
  }

  rmt_rgb_write_pixels();
}

static void rmt_rgb_clear()
{
  constexpr const rgb_data_t rgb{0, 0, 0};
  std::fill(rmt_history.begin(), rmt_history.end(), rgb);
  std::fill(rmt_pixels.begin(), rmt_pixels.end(), rgb);
  rmt_rgb_write_pixels();
}
// ----------------------------------------------------------
static void spectrum_rgb_out(const float* spectrum)
{
  float bars[3];
  spectrum_lmh_out(spectrum, FFT_SIZE, bars, &f_options);

  for (int i = 0; i < count_of(bars); i++)
    bars[i] = std::clamp(bars[i], 0.f, 1.f);

  for (int i = 0; i < count_of(bars); i++)
    bars[i] = std::pow(bars[i], d_options.gamma_value);

  if (d_options.swap_r_b_channels)
    std::swap(bars[0], bars[2]);

  pwm_rgb_set(bars[0], bars[1], bars[2]);
  rmt_rgb_set(bars[0], bars[1], bars[2]);
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
        ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
        ESP_LOG_BUFFER_HEX(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
      } else {
        ESP_LOGE(BT_AV_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
      }
      ESP_LOGI(BT_AV_TAG, "link key type of current link is: %d", param->auth_cmpl.lk_type);
      break;
    }

    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06" PRIu32, param->cfm_req.num_val);
      esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06" PRIu32, param->key_notif.passkey);
      break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
      break;

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d, interval: %.2f ms",
               param->mode_chg.mode, param->mode_chg.interval * 0.625);
      break;
    /* others */
    default: {
      ESP_LOGI(BT_AV_TAG, "event: %d", event);
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
      rmt_rgb_clear();
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
      esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
      start_led_blinking();
      maybe_save_bt_peer_addr(param->conn_stat.remote_bda);
      rmt_rgb_clear();
      break;
  }
}

static void handle_a2d_audio_cfg(const esp_a2d_cb_param_t* param)
{
  const esp_a2d_mcc_t* p_mcc = &param->audio_cfg.mcc;
  ESP_LOGI(BT_AV_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);
  /* for now only SBC stream is supported */
  if (p_mcc->type == ESP_A2D_MCT_SBC) {
    int sample_rate = 16000;
    int ch_count = 2;
    if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) {
      sample_rate = 32000;
    } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) {
      sample_rate = 44100;
    } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) {
      sample_rate = 48000;
    }

    if (p_mcc->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) {
      ch_count = 1;
    }

    ESP_LOGI(BT_AV_TAG, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-%d-%d",
             p_mcc->cie.sbc_info.samp_freq,
             p_mcc->cie.sbc_info.ch_mode,
             p_mcc->cie.sbc_info.block_len,
             p_mcc->cie.sbc_info.num_subbands,
             p_mcc->cie.sbc_info.alloc_mthd,
             p_mcc->cie.sbc_info.min_bitpool,
             p_mcc->cie.sbc_info.max_bitpool);
    ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate: %d", sample_rate);

    frequencies_data_init(sample_rate);
  }
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param)
{
  ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

  switch (event) {
    /* when connection state changed, this event comes */
    case ESP_A2D_CONNECTION_STATE_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_A2D_CONNECTION_STATE_EVT: %d", param->conn_stat.state);
      handle_a2d_connection_state(param);
      break;
    /* when audio stream transmission state changed, this event comes */
    case ESP_A2D_AUDIO_STATE_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_A2D_AUDIO_STATE_EVT: %d", param->audio_stat.state);
      break;
    /* when audio codec is configured, this event comes */
    case ESP_A2D_AUDIO_CFG_EVT:
      ESP_LOGI(BT_AV_TAG, "ESP_A2D_AUDIO_CFG_EVT");
      handle_a2d_audio_cfg(param);
      break;
    /* when a2dp init or deinit completed, this event comes */
    case ESP_A2D_PROF_STATE_EVT: {
      if (ESP_A2D_INIT_SUCCESS == param->a2d_prof_stat.init_state) {
        ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Init Complete");
      } else {
        ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Deinit Complete");
      }
      break;
    }
    /* When protocol service capabilities configured, this event comes */
    case ESP_A2D_SNK_PSC_CFG_EVT: {
      ESP_LOGI(BT_AV_TAG, "protocol service capabilities configured: 0x%x ", param->a2d_psc_cfg_stat.psc_mask);
      if (param->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT) {
        ESP_LOGI(BT_AV_TAG, "Peer device support delay reporting");
      } else {
        ESP_LOGI(BT_AV_TAG, "Peer device unsupported delay reporting");
      }
      break;
    }
    /* when set delay value completed, this event comes */
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT: {
      if (ESP_A2D_SET_INVALID_PARAMS == param->a2d_set_delay_value_stat.set_state) {
        ESP_LOGI(BT_AV_TAG, "Set delay report value: fail");
      } else {
        ESP_LOGI(BT_AV_TAG, "Set delay report value: success, delay_value: %u * 1/10 ms", param->a2d_set_delay_value_stat.delay_value);
      }
      break;
    }
    /* when get delay value completed, this event comes */
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
      ESP_LOGI(BT_AV_TAG, "Get delay report value: delay_value: %u * 1/10 ms", param->a2d_get_delay_value_stat.delay_value);
      /* Default delay value plus delay caused by application layer */
      esp_a2d_sink_set_delay_value(param->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);
      break;
    }
    /* others */
    default:
      ESP_LOGW(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
      break;
  }
}

static void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len)
{
  BaseType_t high_prio_task_woken = pdFALSE;
  xRingbufferSendFromISR(raw_audio_buffer, data, len, &high_prio_task_woken);
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

  esp_a2d_register_callback(&bt_app_a2d_cb);
  esp_a2d_sink_init();
  esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

  esp_a2d_sink_get_delay_value();
  esp_bt_gap_get_device_name();

  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

static void reconnect_to_last_device()
{
  uint8_t last_addr[ESP_BD_ADDR_LEN];
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
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks);

  auto d_service = pServer->createService(BLEUUID(DEVICE_SERVICE_UUID), 32);
  ble_add_device_characteristics(d_service);
  d_service->start();

  auto f_service = pServer->createService(BLEUUID(FILTER_SERVICE_UUID), 64);
  ble_add_filter_characteristics(f_service);
  f_service->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
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
  rmt_rgb_init();

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

  for (int i = 0; i < FFT_SIZE; i++) {
    fft_io_buffer[2*i + 1] *= 2 * log_log_f_ks[i];
  }

  spectrum_rgb_out(fft_io_buffer);
}
