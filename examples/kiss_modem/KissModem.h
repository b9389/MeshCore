#pragma once

#include <Arduino.h>
#include <Identity.h>
#include <Utils.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>

#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MAX_FRAME_SIZE  512
#define KISS_MAX_PACKET_SIZE 255
#define KISS_TX_QUEUE_CAPACITY 4

#define KISS_CMD_DATA        0x00
#define KISS_CMD_TXDELAY     0x01
#define KISS_CMD_PERSISTENCE 0x02
#define KISS_CMD_SLOTTIME    0x03
#define KISS_CMD_TXTAIL      0x04
#define KISS_CMD_FULLDUPLEX  0x05
#define KISS_CMD_SETHARDWARE 0x06
#define KISS_CMD_RETURN      0xFF

#define KISS_DEFAULT_TXDELAY     50
#define KISS_DEFAULT_PERSISTENCE 63
#define KISS_DEFAULT_SLOTTIME    10
#define KISS_TX_CHANNEL_GUARD_MS 1000UL
#define KISS_TX_QUEUE_AIRTIME_BUDGET_MS 8000UL
#define KISS_TX_DATA_QUEUE_HIGH_WATERMARK 3
#define KISS_TX_DATA_AIRTIME_HIGH_WATERMARK_MS 4000UL
#define KISS_TX_CONTROL_WAIT_DATA_QUEUE_HIGH_WATERMARK 2
#define KISS_TX_CONTROL_WAIT_DATA_AIRTIME_HIGH_WATERMARK_MS 2600UL
#define KISS_TX_CONTROL_PRIORITY_MAX 1
#define KISS_TX_DATA_PRIORITY 2
#define KISS_TX_DATA_ADMISSION_BACKOFF_MIN_MS 250UL
#define KISS_TX_DATA_ADMISSION_BACKOFF_MAX_MS 4200UL
#define KISS_TX_DATA_BUSY_BACKOFF_MIN_MS 250UL
#define KISS_TX_DATA_BUSY_BACKOFF_MAX_MS 2000UL
#define KISS_TX_DATA_CONGESTION_SCORE_MAX 8
#define KISS_TX_DATA_ADMISSION_BACKOFF_CAP_MS 8000UL
#define KISS_TX_DATA_BUSY_BACKOFF_CAP_MS 6000UL
#define KISS_TX_ADMISSION_WINDOW_REFERENCE_BYTES 229

#define HW_CMD_GET_IDENTITY      0x01
#define HW_CMD_GET_RANDOM        0x02
#define HW_CMD_VERIFY_SIGNATURE  0x03
#define HW_CMD_SIGN_DATA         0x04
#define HW_CMD_ENCRYPT_DATA      0x05
#define HW_CMD_DECRYPT_DATA      0x06
#define HW_CMD_KEY_EXCHANGE      0x07
#define HW_CMD_HASH              0x08
#define HW_CMD_SET_RADIO         0x09
#define HW_CMD_SET_TX_POWER      0x0A
#define HW_CMD_GET_RADIO         0x0B
#define HW_CMD_GET_TX_POWER      0x0C
#define HW_CMD_GET_CURRENT_RSSI  0x0D
#define HW_CMD_IS_CHANNEL_BUSY   0x0E
#define HW_CMD_GET_AIRTIME       0x0F
#define HW_CMD_GET_NOISE_FLOOR   0x10
#define HW_CMD_GET_VERSION       0x11
#define HW_CMD_GET_STATS         0x12
#define HW_CMD_GET_BATTERY       0x13
#define HW_CMD_GET_MCU_TEMP      0x14
#define HW_CMD_GET_SENSORS       0x15
#define HW_CMD_GET_DEVICE_NAME   0x16
#define HW_CMD_PING              0x17
#define HW_CMD_REBOOT            0x18
#define HW_CMD_SET_SIGNAL_REPORT 0x19
#define HW_CMD_GET_SIGNAL_REPORT 0x1A
#define HW_CMD_GET_OVERRIDE_STATUS       0x40
#define HW_CMD_SET_OVERRIDE              0x41
#define HW_CMD_CLEAR_OVERRIDE            0x42
#define HW_CMD_CLEAR_VOLATILE_OVERRIDES  0x43
#define HW_CMD_GET_EFFECTIVE_POLICY      0x45
#define HW_CMD_GET_CAPABILITY_STATUS     0x46
#define HW_CMD_REPORT_ADMISSION_FEEDBACK 0x47
#define HW_CMD_RESET_ADMISSION_STATE     0x48

/* Response code = command code | 0x80.  Generic / unsolicited use 0xF0+. */
#define HW_RESP(cmd)             ((cmd) | 0x80)

/* Generic responses (shared by multiple commands) */
#define HW_RESP_OK               0xF0
#define HW_RESP_ERROR            0xF1

/* Unsolicited notifications (no corresponding request) */
#define HW_RESP_TX_DONE          0xF8
#define HW_RESP_RX_META          0xF9

/* Cinder experimental override responses. */
#define HW_RESP_OVERRIDE_STATUS  0xA0
#define HW_RESP_EFFECTIVE_POLICY 0xA2
#define HW_RESP_CAPABILITY_STATUS 0xA3

#define HW_ERR_INVALID_LENGTH    0x01
#define HW_ERR_INVALID_PARAM     0x02
#define HW_ERR_NO_CALLBACK       0x03
#define HW_ERR_MAC_FAILED        0x04
#define HW_ERR_UNKNOWN_CMD       0x05
#define HW_ERR_ENCRYPT_FAILED    0x06
#define HW_ERR_TX_INHIBITED      0x07
#define HW_ERR_TX_QUEUE_FULL     0x08
#define HW_ERR_TX_BACKPRESSURE   0x09
#define HW_ERR_BUSY              0x0A

#define KISS_FIRMWARE_VERSION 15

#define SCHED_DEFER_NONE          0x00
#define SCHED_DEFER_CHANNEL_GUARD 0x01
#define SCHED_DEFER_CHANNEL_BUSY  0x02
#define SCHED_DEFER_PERSISTENCE   0x03
#define SCHED_DEFER_TX_INHIBIT    0x04
#define SCHED_DEFER_QUEUE_FULL    0x05
#define SCHED_DEFER_AIRTIME_FULL  0x06
#define SCHED_DEFER_BACKPRESSURE  0x07
#define SCHED_DEFER_RANDOM_BACKOFF 0x08

#define CAPABILITY_STATUS_VERSION 1
#define CINDER_NATIVE_PROTOCOL_VERSION 1
#define CINDER_BEARER_KISS_BENCH 0x02
#define CINDER_TRANSPORT_LORA   0x0001
#define CINDER_TRANSPORT_SERIAL 0x0010
#define CINDER_FEATURE_OVERRIDE_CONTROL    0x00000001UL
#define CINDER_FEATURE_FIRMWARE_DIAGNOSTICS 0x00000040UL
#define CINDER_FEATURE_ADMISSION_FEEDBACK  0x00000400UL
#define CINDER_FEATURE_ADMISSION_RESET     0x00000800UL
#define CINDER_MAX_LOW_RATE_PAYLOAD_BYTES 192

#define ADMISSION_FEEDBACK_DELIVERED   0x01
#define ADMISSION_FEEDBACK_ACKED       0x02
#define ADMISSION_FEEDBACK_LOST        0x03
#define ADMISSION_FEEDBACK_ACK_TIMEOUT 0x04

#define KISS_MAX_OVERRIDES 8

#define OVERRIDE_TLV_KIND        0x01
#define OVERRIDE_TLV_SCOPE       0x02
#define OVERRIDE_TLV_TARGET      0x03
#define OVERRIDE_TLV_VALUE       0x04
#define OVERRIDE_TLV_TTL_MS      0x05
#define OVERRIDE_TLV_REASON      0x06

#define OVERRIDE_KIND_FORCE_LEAF           0x01
#define OVERRIDE_KIND_FORCE_RELAY          0x02
#define OVERRIDE_KIND_QUIET_MODE           0x03
#define OVERRIDE_KIND_TX_INHIBIT           0x04
#define OVERRIDE_KIND_ADVERT_CADENCE       0x05
#define OVERRIDE_KIND_RADIO_PROFILE        0x06
#define OVERRIDE_KIND_PIN_NEXT_HOP         0x07
#define OVERRIDE_KIND_DENY_PEER            0x08
#define OVERRIDE_KIND_CLEAR_ROUTE_CACHE    0x09
#define OVERRIDE_KIND_CLEAR_NEIGHBOR_CACHE 0x0A
#define OVERRIDE_KIND_EMERGENCY_PROFILE    0x0B
#define OVERRIDE_KIND_DISPLAY_PAGE         0x0C

#define OVERRIDE_FLAG_FORCE_LEAF   0x01
#define OVERRIDE_FLAG_FORCE_RELAY  0x02
#define OVERRIDE_FLAG_QUIET_MODE   0x04
#define OVERRIDE_FLAG_TX_INHIBIT   0x08
#define OVERRIDE_FLAG_DISPLAY_PAGE 0x10

#define OVERRIDE_ROLE_AUTO  0x00
#define OVERRIDE_ROLE_LEAF  0x01
#define OVERRIDE_ROLE_RELAY 0x02
#define OVERRIDE_ROLE_QUIET 0x03

#define OVERRIDE_STATUS_VERSION 1
#define OVERRIDE_DEFAULT_TTL_MS 300000UL

typedef void (*SetRadioCallback)(float freq, float bw, uint8_t sf, uint8_t cr);
typedef void (*SetTxPowerCallback)(uint8_t power);
typedef float (*GetCurrentRssiCallback)();
typedef void (*GetStatsCallback)(uint32_t* rx, uint32_t* tx, uint32_t* errors);

struct RadioConfig {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};

struct OverrideEntry {
  bool active;
  uint8_t kind;
  uint8_t value;
  uint32_t expires_at_ms;
};

struct TxQueueEntry {
  uint8_t data[KISS_MAX_PACKET_SIZE];
  uint16_t len;
  uint8_t priority;
  uint32_t estimated_airtime_ms;
  uint32_t release_at_ms;
};

enum TxState {
  TX_IDLE,
  TX_ADMISSION_WAIT,
  TX_WAIT_CLEAR,
  TX_SLOT_WAIT,
  TX_DELAY,
  TX_SENDING
};

class KissModem {
  Stream& _serial;
  mesh::LocalIdentity& _identity;
  mesh::RNG& _rng;
  mesh::Radio& _radio;
  mesh::MainBoard& _board;
  SensorManager& _sensors;

  uint8_t _rx_buf[KISS_MAX_FRAME_SIZE];
  uint16_t _rx_len;
  bool _rx_escaped;
  bool _rx_active;

  TxQueueEntry _tx_queue[KISS_TX_QUEUE_CAPACITY];
  uint8_t _tx_queue_len;
  uint32_t _queued_airtime_ms;
  uint32_t _tx_drop_count;
  uint8_t _last_drop_reason;
  uint32_t _admission_accept_count;
  uint32_t _admission_defer_count;
  uint32_t _admission_backoff_count;
  uint32_t _admission_busy_count;
  uint32_t _admission_reject_count;
  uint32_t _last_admission_delay_ms;
  uint8_t _last_admission_reason;
  uint8_t _data_congestion_score;
  uint32_t _admission_feedback_success_count;
  uint32_t _admission_feedback_failure_count;
  uint8_t _last_admission_feedback;

  uint8_t _txdelay;
  uint8_t _persistence;
  uint8_t _slottime;
  uint8_t _txtail;
  uint8_t _fullduplex;

  TxState _tx_state;
  uint32_t _tx_timer;
  uint32_t _tx_started_ms;
  uint32_t _next_tx_allowed_ms;
  uint32_t _last_tx_duration_ms;
  uint32_t _last_defer_delay_ms;
  uint8_t _last_defer_reason;

  SetRadioCallback _setRadioCallback;
  SetTxPowerCallback _setTxPowerCallback;
  GetCurrentRssiCallback _getCurrentRssiCallback;
  GetStatsCallback _getStatsCallback;

  RadioConfig _config;
  bool _signal_report_enabled;
  OverrideEntry _overrides[KISS_MAX_OVERRIDES];
  uint8_t _last_override_reason;

  void writeByte(uint8_t b);
  void writeFrame(uint8_t type, const uint8_t* data, uint16_t len);
  void writeHardwareFrame(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void writeHardwareError(uint8_t error_code);
  void processFrame();
  void handleHardwareCommand(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void processTx();
  uint8_t getTxPriority(const uint8_t* data, uint16_t len) const;
  bool enqueueTx(const uint8_t* data, uint16_t len);
  void dropQueuedTxAt(uint8_t index);
  void popQueuedTx();
  void clearTxQueue();
  bool evictLowerPriorityFor(uint8_t priority, uint8_t first_reorderable);
  bool queuedAirtimeWouldFit(uint32_t additional_airtime_ms) const;
  bool hasQueuedControlFrame(uint8_t first_reorderable) const;
  bool shouldBackpressureTx(uint8_t priority, uint32_t additional_airtime_ms,
      uint8_t first_reorderable) const;
  uint8_t getTxRejectErrorCode() const;
  uint8_t getTxDoneDeferReason(uint32_t next_tx_delay_ms) const;
  bool headTxIsData() const;
  uint32_t randomDelayMs(uint32_t min_ms, uint32_t max_ms);
  uint32_t randomDataAdmissionBackoffMs(uint32_t estimated_airtime_ms);
  uint32_t randomDataBusyBackoffMs(uint32_t estimated_airtime_ms);
  uint32_t getAdaptiveDataAdmissionBackoffMinMs(uint32_t estimated_airtime_ms) const;
  uint32_t getAdaptiveDataAdmissionBackoffMaxMs(uint32_t estimated_airtime_ms) const;
  uint32_t getAdaptiveDataBusyBackoffMaxMs(uint32_t estimated_airtime_ms) const;
  void increaseDataCongestionScore(uint8_t amount);
  void decreaseDataCongestionScore();
  uint32_t getRemainingChannelGuardDelayMs(uint32_t now_ms) const;
  uint32_t getRemainingHeadReleaseDelayMs(uint32_t now_ms) const;
  uint8_t getTxAdmissionDelayReason(uint32_t now_ms) const;
  uint32_t getRemainingTxAdmissionDelayMs(uint32_t now_ms) const;
  uint32_t getSchedulerDelayMs(uint32_t now_ms) const;
  void recordAdmissionEvent(uint8_t reason, uint32_t delay_ms);
  uint32_t applyBusyBackoffToHead(uint32_t now_ms);
  void setLastDefer(uint8_t reason, uint32_t delay_ms);
  void recordTxDrop(uint8_t reason);
  void resetAdmissionState();

  void handleGetIdentity();
  void handleGetRandom(const uint8_t* data, uint16_t len);
  void handleVerifySignature(const uint8_t* data, uint16_t len);
  void handleSignData(const uint8_t* data, uint16_t len);
  void handleEncryptData(const uint8_t* data, uint16_t len);
  void handleDecryptData(const uint8_t* data, uint16_t len);
  void handleKeyExchange(const uint8_t* data, uint16_t len);
  void handleHash(const uint8_t* data, uint16_t len);
  void handleSetRadio(const uint8_t* data, uint16_t len);
  void handleSetTxPower(const uint8_t* data, uint16_t len);
  void handleGetRadio();
  void handleGetTxPower();
  void handleGetVersion();
  void handleGetCurrentRssi();
  void handleIsChannelBusy();
  void handleGetAirtime(const uint8_t* data, uint16_t len);
  void handleGetNoiseFloor();
  void handleGetStats();
  void handleGetBattery();
  void handlePing();
  void handleGetSensors(const uint8_t* data, uint16_t len);
  void handleGetMCUTemp();
  void handleReboot();
  void handleGetDeviceName();
  void handleSetSignalReport(const uint8_t* data, uint16_t len);
  void handleGetSignalReport();
  void handleGetOverrideStatus();
  void handleSetOverride(const uint8_t* data, uint16_t len);
  void handleClearOverride(const uint8_t* data, uint16_t len);
  void handleClearVolatileOverrides();
  void handleGetEffectivePolicy();
  void handleGetCapabilityStatus();
  void handleAdmissionFeedback(const uint8_t* data, uint16_t len);
  void handleResetAdmissionState(const uint8_t* data, uint16_t len);

  void purgeExpiredOverrides();
  bool parseOverrideTlv(const uint8_t* data, uint16_t len, uint8_t* kind, uint8_t* value, uint32_t* ttl_ms);
  bool isOverrideKindSupported(uint8_t kind) const;
  bool applyOverride(uint8_t kind, uint8_t value, uint32_t ttl_ms);
  bool clearOverrideKind(uint8_t kind);
  bool isOverrideActive(uint8_t kind) const;
  uint8_t getOverrideFlagMask() const;
  uint8_t getActiveOverrideCount() const;
  uint8_t getEffectiveRoleCode() const;
  uint32_t getNextOverrideTtlMs() const;
  void writeOverrideStatus(uint8_t response_subcommand);
  void writeCapabilityStatus();

public:
  KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
            mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors);

  void begin();
  void loop();

  void setRadioCallback(SetRadioCallback cb) { _setRadioCallback = cb; }
  void setTxPowerCallback(SetTxPowerCallback cb) { _setTxPowerCallback = cb; }
  void setGetCurrentRssiCallback(GetCurrentRssiCallback cb) { _getCurrentRssiCallback = cb; }
  void setGetStatsCallback(GetStatsCallback cb) { _getStatsCallback = cb; }

  void onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len);
  bool isTxBusy() const { return _tx_state != TX_IDLE || _tx_queue_len > 0; }
  /** True only when radio is actually transmitting; use to skip recvRaw in main loop. */
  bool isActuallyTransmitting() const { return _tx_state == TX_SENDING; }
  uint8_t getTxQueueLen() const { return _tx_queue_len; }
  uint8_t getTxQueueCapacity() const { return KISS_TX_QUEUE_CAPACITY; }
  uint32_t getQueuedAirtimeMs() const { return _queued_airtime_ms; }
  uint32_t getQueueAirtimeBudgetMs() const { return KISS_TX_QUEUE_AIRTIME_BUDGET_MS; }
  uint32_t getNextTxDelayMs() const { return getRemainingTxAdmissionDelayMs(millis()); }
  uint32_t getSchedulerDelayMs() const { return getSchedulerDelayMs(millis()); }
  uint8_t getLastDeferReason() const { return _last_defer_reason; }
  const char* getLastDeferReasonLabel() const;
  bool isTxInhibited() const { return isOverrideActive(OVERRIDE_KIND_TX_INHIBIT); }
  bool isQuietMode() const { return isOverrideActive(OVERRIDE_KIND_QUIET_MODE); }
  uint8_t getOverrideFlags() const { return getOverrideFlagMask(); }
  const char* getEffectiveRoleLabel() const;
};
