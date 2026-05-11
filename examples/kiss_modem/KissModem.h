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

#define KISS_FIRMWARE_VERSION 4

#define CAPABILITY_STATUS_VERSION 1
#define CINDER_NATIVE_PROTOCOL_VERSION 1
#define CINDER_BEARER_KISS_BENCH 0x02
#define CINDER_TRANSPORT_LORA   0x0001
#define CINDER_TRANSPORT_SERIAL 0x0010
#define CINDER_FEATURE_OVERRIDE_CONTROL    0x00000001UL
#define CINDER_FEATURE_FIRMWARE_DIAGNOSTICS 0x00000040UL
#define CINDER_MAX_LOW_RATE_PAYLOAD_BYTES 192

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

enum TxState {
  TX_IDLE,
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

  uint8_t _pending_tx[KISS_MAX_PACKET_SIZE];
  uint16_t _pending_tx_len;
  bool _has_pending_tx;

  uint8_t _txdelay;
  uint8_t _persistence;
  uint8_t _slottime;
  uint8_t _txtail;
  uint8_t _fullduplex;

  TxState _tx_state;
  uint32_t _tx_timer;
  uint32_t _tx_started_ms;
  uint32_t _last_tx_duration_ms;

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
  bool isTxBusy() const { return _tx_state != TX_IDLE; }
  /** True only when radio is actually transmitting; use to skip recvRaw in main loop. */
  bool isActuallyTransmitting() const { return _tx_state == TX_SENDING; }
  bool isTxInhibited() const { return isOverrideActive(OVERRIDE_KIND_TX_INHIBIT); }
  bool isQuietMode() const { return isOverrideActive(OVERRIDE_KIND_QUIET_MODE); }
  uint8_t getOverrideFlags() const { return getOverrideFlagMask(); }
  const char* getEffectiveRoleLabel() const;
};
