#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include "KissModem.h"

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif
#if defined(KISS_UART_RX) && defined(KISS_UART_TX)
  #include <HardwareSerial.h>
#endif

#define NOISE_FLOOR_CALIB_INTERVAL_MS 2000
#define AGC_RESET_INTERVAL_MS 30000

StdRNG rng;
mesh::LocalIdentity identity;
KissModem* modem;
static uint32_t next_noise_floor_calib_ms = 0;
static uint32_t next_agc_reset_ms = 0;

#ifdef DISPLAY_CLASS
static bool display_ready = false;
static bool has_last_rx_meta = false;
static int8_t last_rx_snr_quarter_db = 0;
static int8_t last_rx_rssi_dbm = 0;
static uint32_t last_display_update_ms = 0;
static char short_identity[9] = "--------";

static void formatShortIdentity() {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (int i = 0; i < 4; i++) {
    short_identity[i * 2] = HEX_DIGITS[(identity.pub_key[i] >> 4) & 0x0F];
    short_identity[i * 2 + 1] = HEX_DIGITS[identity.pub_key[i] & 0x0F];
  }
  short_identity[8] = 0;
}

static const char* getModemState() {
  if (modem == nullptr) return "BOOT";
  if (modem->isActuallyTransmitting()) return "TX";
  if (modem->isTxBusy()) return "BUSY";
  return "IDLE";
}

static void renderDisplay(const char* detail_line = nullptr) {
  if (!display_ready) return;

  char line[32];
  display.startFrame();

  display.setCursor(0, 0);
  display.print("Cinder KISS");

  display.setCursor(0, 10);
  display.print(board.getManufacturerName());

  display.setCursor(0, 20);
  snprintf(line, sizeof(line), "ID %s", short_identity);
  display.print(line);

  display.setCursor(0, 30);
  snprintf(
      line,
      sizeof(line),
      "RX %lu TX %lu ER %lu",
      (unsigned long)radio_driver.getPacketsRecv(),
      (unsigned long)radio_driver.getPacketsSent(),
      (unsigned long)radio_driver.getPacketsRecvErrors());
  display.print(line);

  display.setCursor(0, 40);
  if (detail_line != nullptr) {
    display.print(detail_line);
  } else if (has_last_rx_meta) {
    int snr_q = last_rx_snr_quarter_db;
    int snr_abs_q = snr_q < 0 ? -snr_q : snr_q;
    snprintf(
        line,
        sizeof(line),
        "SNR %s%d.%02u R %d",
        snr_q < 0 ? "-" : "",
        snr_abs_q / 4,
        (unsigned int)(snr_abs_q % 4) * 25U,
        last_rx_rssi_dbm);
    display.print(line);
  } else {
    display.print("SNR -- R --");
  }

  display.setCursor(0, 50);
  if (modem != nullptr && modem->getOverrideFlags() != 0) {
    snprintf(
        line,
        sizeof(line),
        "OVR %s Q%u X%u",
        modem->getEffectiveRoleLabel(),
        modem->isQuietMode() ? 1U : 0U,
        modem->isTxInhibited() ? 1U : 0U);
  } else if (modem != nullptr) {
    uint32_t next_tx_delay_ms = modem->getSchedulerDelayMs();
    if (next_tx_delay_ms > 0 || modem->getLastDeferReason() != SCHED_DEFER_NONE) {
      snprintf(
          line,
          sizeof(line),
          "SCH Q%u/%u D%lu %s",
          modem->getTxQueueLen(),
          modem->getTxQueueCapacity(),
          (unsigned long)next_tx_delay_ms,
          modem->getLastDeferReasonLabel());
    } else if (modem->getTxQueueLen() > 0) {
      snprintf(
          line,
          sizeof(line),
          "SCH Q%u/%u A%lu/%lu",
          modem->getTxQueueLen(),
          modem->getTxQueueCapacity(),
          (unsigned long)modem->getQueuedAirtimeMs(),
          (unsigned long)modem->getQueueAirtimeBudgetMs());
    } else {
      snprintf(
          line,
          sizeof(line),
          "STATE %s Q%u/%u",
          getModemState(),
          modem->getTxQueueLen(),
          modem->getTxQueueCapacity());
    }
  } else {
    snprintf(line, sizeof(line), "STATE %s", getModemState());
  }
  display.print(line);

  display.endFrame();
}
#endif

void halt() {
  while (1) ;
}

void loadOrCreateIdentity() {
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "Filesystem not defined"
#endif

  if (!store.load("_main", identity)) {
    identity = radio_new_identity();
    while (identity.pub_key[0] == 0x00 || identity.pub_key[0] == 0xFF) {
      identity = radio_new_identity();
    }
    store.save("_main", identity);
  }
}

void onSetRadio(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio_set_params(freq, bw, sf, cr);
}

void onSetTxPower(uint8_t power) {
  radio_set_tx_power(power);
}

float onGetCurrentRssi() {
  return radio_driver.getCurrentRSSI();
}

void onGetStats(uint32_t* rx, uint32_t* tx, uint32_t* errors,
                int16_t* last_recv_error_code, uint16_t* last_recv_error_len,
                int16_t* last_start_recv_error_code, uint32_t* start_recv_error_count,
                uint32_t* recv_crc_error_count, uint32_t* recv_header_error_count,
                uint32_t* recv_other_error_count) {
  *rx = radio_driver.getPacketsRecv();
  *tx = radio_driver.getPacketsSent();
  *errors = radio_driver.getPacketsRecvErrors();
  *last_recv_error_code = radio_driver.getLastRecvErrorCode();
  *last_recv_error_len = radio_driver.getLastRecvErrorLen();
  *last_start_recv_error_code = radio_driver.getLastStartRecvErrorCode();
  *start_recv_error_count = radio_driver.getStartRecvErrorCount();
  *recv_crc_error_count = radio_driver.getRecvCrcErrorCount();
  *recv_header_error_count = radio_driver.getRecvHeaderErrorCount();
  *recv_other_error_count = radio_driver.getRecvOtherErrorCount();
}

void setup() {
  board.begin();

#ifdef DISPLAY_CLASS
  display_ready = display.begin();
  if (display_ready) {
    renderDisplay("Booting...");
  }
#endif

  if (!radio_init()) {
#ifdef DISPLAY_CLASS
    renderDisplay("Radio init failed");
#endif
    halt();
  }

  radio_driver.begin();

  rng.begin(radio_get_rng_seed());
  loadOrCreateIdentity();

#ifdef DISPLAY_CLASS
  formatShortIdentity();
#endif

  sensors.begin();

#if defined(KISS_UART_RX) && defined(KISS_UART_TX)
#if defined(ESP32)
  Serial1.setPins(KISS_UART_RX, KISS_UART_TX);
  Serial1.begin(115200);
#elif defined(NRF52_PLATFORM)
  ((Uart *)&Serial1)->setPins(KISS_UART_RX, KISS_UART_TX);
  Serial1.begin(115200);
#elif defined(RP2040_PLATFORM)
  ((SerialUART *)&Serial1)->setRX(KISS_UART_RX);
  ((SerialUART *)&Serial1)->setTX(KISS_UART_TX);
  Serial1.begin(115200);
#elif defined(STM32_PLATFORM)
  ((HardwareSerial *)&Serial1)->setRx(KISS_UART_RX);
  ((HardwareSerial *)&Serial1)->setTx(KISS_UART_TX);
  Serial1.begin(115200);
#else
  #error "KISS UART not supported on this platform"
#endif
  modem = new KissModem(Serial1, identity, rng, radio_driver, board, sensors);
#else
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(100);
  modem = new KissModem(Serial, identity, rng, radio_driver, board, sensors);
#endif

  modem->setRadioCallback(onSetRadio);
  modem->setTxPowerCallback(onSetTxPower);
  modem->setGetCurrentRssiCallback(onGetCurrentRssi);
  modem->setGetStatsCallback(onGetStats);
  modem->begin();

#ifdef DISPLAY_CLASS
  renderDisplay();
  last_display_update_ms = millis();
#endif
}

void loop() {
  modem->loop();

  if (!modem->isActuallyTransmitting()) {
    if (!modem->isTxBusy()) {
      if ((uint32_t)(millis() - next_agc_reset_ms) >= AGC_RESET_INTERVAL_MS) {
        radio_driver.resetAGC();
        next_agc_reset_ms = millis();
      }
    }

    uint8_t rx_buf[256];
    int rx_len = radio_driver.recvRaw(rx_buf, sizeof(rx_buf));
    if (rx_len > 0) {
      int8_t snr = (int8_t)(radio_driver.getLastSNR() * 4);
      int8_t rssi = (int8_t)radio_driver.getLastRSSI();
#ifdef DISPLAY_CLASS
      has_last_rx_meta = true;
      last_rx_snr_quarter_db = snr;
      last_rx_rssi_dbm = rssi;
#endif
      modem->onPacketReceived(snr, rssi, rx_buf, rx_len);
    }
  }

  if ((uint32_t)(millis() - next_noise_floor_calib_ms) >= NOISE_FLOOR_CALIB_INTERVAL_MS) {
    radio_driver.triggerNoiseFloorCalibrate(0);
    next_noise_floor_calib_ms = millis();
  }
  radio_driver.loop();

#ifdef DISPLAY_CLASS
  if ((uint32_t)(millis() - last_display_update_ms) >= 750) {
    renderDisplay();
    last_display_update_ms = millis();
  }
#endif
}
