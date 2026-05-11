#include "KissModem.h"
#include <CayenneLPP.h>

KissModem::KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
                     mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors)
  : _serial(serial), _identity(identity), _rng(rng), _radio(radio), _board(board), _sensors(sensors) {
  _rx_len = 0;
  _rx_escaped = false;
  _rx_active = false;
  _has_pending_tx = false;
  _pending_tx_len = 0;
  _txdelay = KISS_DEFAULT_TXDELAY;
  _persistence = KISS_DEFAULT_PERSISTENCE;
  _slottime = KISS_DEFAULT_SLOTTIME;
  _txtail = 0;
  _fullduplex = 0;
  _tx_state = TX_IDLE;
  _tx_timer = 0;
  _tx_started_ms = 0;
  _last_tx_duration_ms = 0;
  _setRadioCallback = nullptr;
  _setTxPowerCallback = nullptr;
  _getCurrentRssiCallback = nullptr;
  _getStatsCallback = nullptr;
  _config = {0, 0, 0, 0, 0};
  _signal_report_enabled = true;
  _last_override_reason = 0;
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    _overrides[i] = {false, 0, 0, 0};
  }
}

void KissModem::begin() {
  _rx_len = 0;
  _rx_escaped = false;
  _rx_active = false;
  _has_pending_tx = false;
  _tx_state = TX_IDLE;
  _tx_started_ms = 0;
  _last_tx_duration_ms = 0;
  purgeExpiredOverrides();
}

void KissModem::writeByte(uint8_t b) {
  if (b == KISS_FEND) {
    _serial.write(KISS_FESC);
    _serial.write(KISS_TFEND);
  } else if (b == KISS_FESC) {
    _serial.write(KISS_FESC);
    _serial.write(KISS_TFESC);
  } else {
    _serial.write(b);
  }
}

void KissModem::writeFrame(uint8_t type, const uint8_t* data, uint16_t len) {
  _serial.write(KISS_FEND);
  writeByte(type);
  for (uint16_t i = 0; i < len; i++) {
    writeByte(data[i]);
  }
  _serial.write(KISS_FEND);
}

void KissModem::writeHardwareFrame(uint8_t sub_cmd, const uint8_t* data, uint16_t len) {
  _serial.write(KISS_FEND);
  writeByte(KISS_CMD_SETHARDWARE);
  writeByte(sub_cmd);
  for (uint16_t i = 0; i < len; i++) {
    writeByte(data[i]);
  }
  _serial.write(KISS_FEND);
}

void KissModem::writeHardwareError(uint8_t error_code) {
  writeHardwareFrame(HW_RESP_ERROR, &error_code, 1);
}

void KissModem::loop() {
  purgeExpiredOverrides();

  while (_serial.available()) {
    uint8_t b = _serial.read();

    if (b == KISS_FEND) {
      if (_rx_active && _rx_len > 0) {
        processFrame();
      }
      _rx_len = 0;
      _rx_escaped = false;
      _rx_active = true;
      continue;
    }

    if (!_rx_active) continue;

    if (b == KISS_FESC) {
      _rx_escaped = true;
      continue;
    }

    if (_rx_escaped) {
      _rx_escaped = false;
      if (b == KISS_TFEND) b = KISS_FEND;
      else if (b == KISS_TFESC) b = KISS_FESC;
      else continue;
    }

    if (_rx_len < KISS_MAX_FRAME_SIZE) {
      _rx_buf[_rx_len++] = b;
    } else {
      /* Buffer full with no FEND; reset so we don't stay stuck ignoring input. */
      _rx_len = 0;
      _rx_escaped = false;
      _rx_active = false;
    }
  }

  processTx();
}

void KissModem::processFrame() {
  if (_rx_len < 1) return;

  uint8_t type_byte = _rx_buf[0];

  if (type_byte == KISS_CMD_RETURN) return;

  uint8_t port = (type_byte >> 4) & 0x0F;
  uint8_t cmd = type_byte & 0x0F;

  if (port != 0) return;

  const uint8_t* data = &_rx_buf[1];
  uint16_t data_len = _rx_len - 1;

  switch (cmd) {
    case KISS_CMD_DATA:
      purgeExpiredOverrides();
      if (isOverrideActive(OVERRIDE_KIND_TX_INHIBIT)) {
        writeHardwareError(HW_ERR_TX_INHIBITED);
        break;
      }
      if (data_len > 0 && data_len <= KISS_MAX_PACKET_SIZE && !_has_pending_tx) {
        memcpy(_pending_tx, data, data_len);
        _pending_tx_len = data_len;
        _has_pending_tx = true;
      }
      break;

    case KISS_CMD_TXDELAY:
      if (data_len >= 1) _txdelay = data[0];
      break;

    case KISS_CMD_PERSISTENCE:
      if (data_len >= 1) _persistence = data[0];
      break;

    case KISS_CMD_SLOTTIME:
      if (data_len >= 1) _slottime = data[0];
      break;

    case KISS_CMD_TXTAIL:
      if (data_len >= 1) _txtail = data[0];
      break;

    case KISS_CMD_FULLDUPLEX:
      if (data_len >= 1) _fullduplex = data[0];
      break;

    case KISS_CMD_SETHARDWARE:
      if (data_len >= 1) {
        handleHardwareCommand(data[0], data + 1, data_len - 1);
      }
      break;

    default:
      break;
  }
}

void KissModem::handleHardwareCommand(uint8_t sub_cmd, const uint8_t* data, uint16_t len) {
  switch (sub_cmd) {
    case HW_CMD_GET_IDENTITY:
      handleGetIdentity();
      break;
    case HW_CMD_GET_RANDOM:
      handleGetRandom(data, len);
      break;
    case HW_CMD_VERIFY_SIGNATURE:
      handleVerifySignature(data, len);
      break;
    case HW_CMD_SIGN_DATA:
      handleSignData(data, len);
      break;
    case HW_CMD_ENCRYPT_DATA:
      handleEncryptData(data, len);
      break;
    case HW_CMD_DECRYPT_DATA:
      handleDecryptData(data, len);
      break;
    case HW_CMD_KEY_EXCHANGE:
      handleKeyExchange(data, len);
      break;
    case HW_CMD_HASH:
      handleHash(data, len);
      break;
    case HW_CMD_SET_RADIO:
      handleSetRadio(data, len);
      break;
    case HW_CMD_SET_TX_POWER:
      handleSetTxPower(data, len);
      break;
    case HW_CMD_GET_RADIO:
      handleGetRadio();
      break;
    case HW_CMD_GET_TX_POWER:
      handleGetTxPower();
      break;
    case HW_CMD_GET_VERSION:
      handleGetVersion();
      break;
    case HW_CMD_GET_CURRENT_RSSI:
      handleGetCurrentRssi();
      break;
    case HW_CMD_IS_CHANNEL_BUSY:
      handleIsChannelBusy();
      break;
    case HW_CMD_GET_AIRTIME:
      handleGetAirtime(data, len);
      break;
    case HW_CMD_GET_NOISE_FLOOR:
      handleGetNoiseFloor();
      break;
    case HW_CMD_GET_STATS:
      handleGetStats();
      break;
    case HW_CMD_GET_BATTERY:
      handleGetBattery();
      break;
    case HW_CMD_PING:
      handlePing();
      break;
    case HW_CMD_GET_SENSORS:
      handleGetSensors(data, len);
      break;
    case HW_CMD_GET_MCU_TEMP:
      handleGetMCUTemp();
      break;
    case HW_CMD_REBOOT:
      handleReboot();
      break;
    case HW_CMD_GET_DEVICE_NAME:
      handleGetDeviceName();
      break;
    case HW_CMD_SET_SIGNAL_REPORT:
      handleSetSignalReport(data, len);
      break;
    case HW_CMD_GET_SIGNAL_REPORT:
      handleGetSignalReport();
      break;
    case HW_CMD_GET_OVERRIDE_STATUS:
      handleGetOverrideStatus();
      break;
    case HW_CMD_SET_OVERRIDE:
      handleSetOverride(data, len);
      break;
    case HW_CMD_CLEAR_OVERRIDE:
      handleClearOverride(data, len);
      break;
    case HW_CMD_CLEAR_VOLATILE_OVERRIDES:
      handleClearVolatileOverrides();
      break;
    case HW_CMD_GET_EFFECTIVE_POLICY:
      handleGetEffectivePolicy();
      break;
    case HW_CMD_GET_CAPABILITY_STATUS:
      handleGetCapabilityStatus();
      break;
    default:
      writeHardwareError(HW_ERR_UNKNOWN_CMD);
      break;
  }
}

void KissModem::processTx() {
  purgeExpiredOverrides();
  if (_has_pending_tx && _tx_state != TX_SENDING && isOverrideActive(OVERRIDE_KIND_TX_INHIBIT)) {
    _has_pending_tx = false;
    _pending_tx_len = 0;
    _tx_state = TX_IDLE;
    writeHardwareError(HW_ERR_TX_INHIBITED);
    return;
  }

  switch (_tx_state) {
    case TX_IDLE:
      if (_has_pending_tx) {
        if (_fullduplex) {
          _tx_timer = millis();
          _tx_state = TX_DELAY;
        } else {
          _tx_state = TX_WAIT_CLEAR;
        }
      }
      break;

    case TX_WAIT_CLEAR:
      if (!_radio.isReceiving()) {
        uint8_t rand_val;
        _rng.random(&rand_val, 1);
        if (rand_val <= _persistence) {
          _tx_timer = millis();
          _tx_state = TX_DELAY;
        } else {
          _tx_timer = millis();
          _tx_state = TX_SLOT_WAIT;
        }
      }
      break;

    case TX_SLOT_WAIT:
      if (millis() - _tx_timer >= (uint32_t)_slottime * 10) {
        _tx_state = TX_WAIT_CLEAR;
      }
      break;

    case TX_DELAY:
      if (millis() - _tx_timer >= (uint32_t)_txdelay * 10) {
        _tx_started_ms = millis();
        _radio.startSendRaw(_pending_tx, _pending_tx_len);
        _tx_state = TX_SENDING;
      }
      break;

    case TX_SENDING:
      if (_radio.isSendComplete()) {
        _radio.onSendFinished();
        _last_tx_duration_ms = millis() - _tx_started_ms;
        uint8_t tx_done[9];
        tx_done[0] = 0x01;
        memcpy(tx_done + 1, &_last_tx_duration_ms, 4);
        uint16_t queue_len = 0;
        uint16_t queue_capacity = 1;
        memcpy(tx_done + 5, &queue_len, 2);
        memcpy(tx_done + 7, &queue_capacity, 2);
        writeHardwareFrame(HW_RESP_TX_DONE, tx_done, sizeof(tx_done));
        _has_pending_tx = false;
        _tx_state = TX_IDLE;
      }
      break;
  }
}

void KissModem::onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len) {
  writeFrame(KISS_CMD_DATA, packet, len);
  if (_signal_report_enabled) {
    uint8_t meta[2] = { (uint8_t)snr, (uint8_t)rssi };
    writeHardwareFrame(HW_RESP_RX_META, meta, 2);
  }
}

void KissModem::handleGetIdentity() {
  writeHardwareFrame(HW_RESP(HW_CMD_GET_IDENTITY), _identity.pub_key, PUB_KEY_SIZE);
}

void KissModem::handleGetRandom(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t requested = data[0];
  if (requested < 1 || requested > 64) {
    writeHardwareError(HW_ERR_INVALID_PARAM);
    return;
  }

  uint8_t buf[64];
  _rng.random(buf, requested);
  writeHardwareFrame(HW_RESP(HW_CMD_GET_RANDOM), buf, requested);
}

void KissModem::handleVerifySignature(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + SIGNATURE_SIZE + 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  mesh::Identity signer(data);
  const uint8_t* signature = data + PUB_KEY_SIZE;
  const uint8_t* msg = data + PUB_KEY_SIZE + SIGNATURE_SIZE;
  uint16_t msg_len = len - PUB_KEY_SIZE - SIGNATURE_SIZE;

  uint8_t result = signer.verify(signature, msg, msg_len) ? 0x01 : 0x00;
  writeHardwareFrame(HW_RESP(HW_CMD_VERIFY_SIGNATURE), &result, 1);
}

void KissModem::handleSignData(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t signature[SIGNATURE_SIZE];
  _identity.sign(signature, data, len);
  writeHardwareFrame(HW_RESP(HW_CMD_SIGN_DATA), signature, SIGNATURE_SIZE);
}

void KissModem::handleEncryptData(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  const uint8_t* key = data;
  const uint8_t* plaintext = data + PUB_KEY_SIZE;
  uint16_t plaintext_len = len - PUB_KEY_SIZE;

  uint8_t buf[KISS_MAX_FRAME_SIZE];
  int encrypted_len = mesh::Utils::encryptThenMAC(key, buf, plaintext, plaintext_len);

  if (encrypted_len > 0) {
    writeHardwareFrame(HW_RESP(HW_CMD_ENCRYPT_DATA), buf, encrypted_len);
  } else {
    writeHardwareError(HW_ERR_ENCRYPT_FAILED);
  }
}

void KissModem::handleDecryptData(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + CIPHER_MAC_SIZE + 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  const uint8_t* key = data;
  const uint8_t* ciphertext = data + PUB_KEY_SIZE;
  uint16_t ciphertext_len = len - PUB_KEY_SIZE;

  uint8_t buf[KISS_MAX_FRAME_SIZE];
  int decrypted_len = mesh::Utils::MACThenDecrypt(key, buf, ciphertext, ciphertext_len);

  if (decrypted_len > 0) {
    writeHardwareFrame(HW_RESP(HW_CMD_DECRYPT_DATA), buf, decrypted_len);
  } else {
    writeHardwareError(HW_ERR_MAC_FAILED);
  }
}

void KissModem::handleKeyExchange(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t shared_secret[PUB_KEY_SIZE];
  _identity.calcSharedSecret(shared_secret, data);
  writeHardwareFrame(HW_RESP(HW_CMD_KEY_EXCHANGE), shared_secret, PUB_KEY_SIZE);
}

void KissModem::handleHash(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t hash[32];
  mesh::Utils::sha256(hash, 32, data, len);
  writeHardwareFrame(HW_RESP(HW_CMD_HASH), hash, 32);
}

void KissModem::handleSetRadio(const uint8_t* data, uint16_t len) {
  if (len < 10) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }
  if (!_setRadioCallback) {
    writeHardwareError(HW_ERR_NO_CALLBACK);
    return;
  }

  memcpy(&_config.freq_hz, data, 4);
  memcpy(&_config.bw_hz, data + 4, 4);
  _config.sf = data[8];
  _config.cr = data[9];

  _setRadioCallback(_config.freq_hz / 1000000.0f, _config.bw_hz / 1000.0f, _config.sf, _config.cr);
  writeHardwareFrame(HW_RESP_OK, nullptr, 0);
}

void KissModem::handleSetTxPower(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }
  if (!_setTxPowerCallback) {
    writeHardwareError(HW_ERR_NO_CALLBACK);
    return;
  }

  _config.tx_power = data[0];
  _setTxPowerCallback(data[0]);
  writeHardwareFrame(HW_RESP_OK, nullptr, 0);
}

void KissModem::handleGetRadio() {
  uint8_t buf[10];
  memcpy(buf, &_config.freq_hz, 4);
  memcpy(buf + 4, &_config.bw_hz, 4);
  buf[8] = _config.sf;
  buf[9] = _config.cr;
  writeHardwareFrame(HW_RESP(HW_CMD_GET_RADIO), buf, 10);
}

void KissModem::handleGetTxPower() {
  writeHardwareFrame(HW_RESP(HW_CMD_GET_TX_POWER), &_config.tx_power, 1);
}

void KissModem::handleGetVersion() {
  uint8_t buf[2];
  buf[0] = KISS_FIRMWARE_VERSION;
  buf[1] = 0;
  writeHardwareFrame(HW_RESP(HW_CMD_GET_VERSION), buf, 2);
}

void KissModem::handleGetCurrentRssi() {
  if (!_getCurrentRssiCallback) {
    writeHardwareError(HW_ERR_NO_CALLBACK);
    return;
  }

  float rssi = _getCurrentRssiCallback();
  int8_t rssi_byte = (int8_t)rssi;
  writeHardwareFrame(HW_RESP(HW_CMD_GET_CURRENT_RSSI), (uint8_t*)&rssi_byte, 1);
}

void KissModem::handleIsChannelBusy() {
  uint8_t busy = _radio.isReceiving() ? 0x01 : 0x00;
  writeHardwareFrame(HW_RESP(HW_CMD_IS_CHANNEL_BUSY), &busy, 1);
}

void KissModem::handleGetAirtime(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t packet_len = data[0];
  uint32_t airtime = _radio.getEstAirtimeFor(packet_len);
  writeHardwareFrame(HW_RESP(HW_CMD_GET_AIRTIME), (uint8_t*)&airtime, 4);
}

void KissModem::handleGetNoiseFloor() {
  int16_t noise_floor = _radio.getNoiseFloor();
  writeHardwareFrame(HW_RESP(HW_CMD_GET_NOISE_FLOOR), (uint8_t*)&noise_floor, 2);
}

void KissModem::handleGetStats() {
  if (!_getStatsCallback) {
    writeHardwareError(HW_ERR_NO_CALLBACK);
    return;
  }

  uint32_t rx, tx, errors;
  _getStatsCallback(&rx, &tx, &errors);
  uint8_t buf[16];
  uint16_t queue_len = _has_pending_tx ? 1 : 0;
  uint16_t queue_capacity = 1;
  memcpy(buf, &rx, 4);
  memcpy(buf + 4, &tx, 4);
  memcpy(buf + 8, &errors, 4);
  memcpy(buf + 12, &queue_len, 2);
  memcpy(buf + 14, &queue_capacity, 2);
  writeHardwareFrame(HW_RESP(HW_CMD_GET_STATS), buf, sizeof(buf));
}

void KissModem::handleGetBattery() {
  uint16_t mv = _board.getBattMilliVolts();
  writeHardwareFrame(HW_RESP(HW_CMD_GET_BATTERY), (uint8_t*)&mv, 2);
}

void KissModem::handlePing() {
  writeHardwareFrame(HW_RESP(HW_CMD_PING), nullptr, 0);
}

void KissModem::handleGetSensors(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  uint8_t permissions = data[0];
  CayenneLPP telemetry(255);
  if (_sensors.querySensors(permissions, telemetry)) {
    writeHardwareFrame(HW_RESP(HW_CMD_GET_SENSORS), telemetry.getBuffer(), telemetry.getSize());
  } else {
    writeHardwareFrame(HW_RESP(HW_CMD_GET_SENSORS), nullptr, 0);
  }
}

void KissModem::handleGetMCUTemp() {
  float temp = _board.getMCUTemperature();
  if (isnan(temp)) {
    writeHardwareError(HW_ERR_NO_CALLBACK);
    return;
  }
  int16_t temp_tenths = (int16_t)(temp * 10.0f);
  writeHardwareFrame(HW_RESP(HW_CMD_GET_MCU_TEMP), (uint8_t*)&temp_tenths, 2);
}

void KissModem::handleReboot() {
  writeHardwareFrame(HW_RESP_OK, nullptr, 0);
  _serial.flush();
  delay(50);
  _board.reboot();
}

void KissModem::handleGetDeviceName() {
  const char* name = _board.getManufacturerName();
  writeHardwareFrame(HW_RESP(HW_CMD_GET_DEVICE_NAME), (const uint8_t*)name, strlen(name));
}

void KissModem::handleSetSignalReport(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }
  _signal_report_enabled = (data[0] != 0x00);
  uint8_t val = _signal_report_enabled ? 0x01 : 0x00;
  writeHardwareFrame(HW_RESP(HW_CMD_GET_SIGNAL_REPORT), &val, 1);
}

void KissModem::handleGetSignalReport() {
  uint8_t val = _signal_report_enabled ? 0x01 : 0x00;
  writeHardwareFrame(HW_RESP(HW_CMD_GET_SIGNAL_REPORT), &val, 1);
}

void KissModem::handleGetOverrideStatus() {
  writeOverrideStatus(HW_RESP_OVERRIDE_STATUS);
}

void KissModem::handleSetOverride(const uint8_t* data, uint16_t len) {
  uint8_t kind = 0;
  uint8_t value = 1;
  uint32_t ttl_ms = OVERRIDE_DEFAULT_TTL_MS;

  if (!parseOverrideTlv(data, len, &kind, &value, &ttl_ms)) {
    _last_override_reason = HW_ERR_INVALID_LENGTH;
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  if (!isOverrideKindSupported(kind)) {
    _last_override_reason = HW_ERR_INVALID_PARAM;
    writeHardwareError(HW_ERR_INVALID_PARAM);
    return;
  }

  if (!applyOverride(kind, value, ttl_ms)) {
    _last_override_reason = HW_ERR_INVALID_PARAM;
    writeHardwareError(HW_ERR_INVALID_PARAM);
    return;
  }

  _last_override_reason = 0;
  writeOverrideStatus(HW_RESP_OVERRIDE_STATUS);
}

void KissModem::handleClearOverride(const uint8_t* data, uint16_t len) {
  uint8_t kind = 0;
  uint8_t value = 0;
  uint32_t ttl_ms = 0;

  if (len == 1) {
    kind = data[0];
  } else if (!parseOverrideTlv(data, len, &kind, &value, &ttl_ms)) {
    _last_override_reason = HW_ERR_INVALID_LENGTH;
    writeHardwareError(HW_ERR_INVALID_LENGTH);
    return;
  }

  if (kind == 0 || !clearOverrideKind(kind)) {
    _last_override_reason = HW_ERR_INVALID_PARAM;
    writeHardwareError(HW_ERR_INVALID_PARAM);
    return;
  }

  _last_override_reason = 0;
  writeOverrideStatus(HW_RESP_OVERRIDE_STATUS);
}

void KissModem::handleClearVolatileOverrides() {
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    _overrides[i].active = false;
    _overrides[i].kind = 0;
    _overrides[i].value = 0;
    _overrides[i].expires_at_ms = 0;
  }
  _last_override_reason = 0;
  writeOverrideStatus(HW_RESP_OVERRIDE_STATUS);
}

void KissModem::handleGetEffectivePolicy() {
  writeOverrideStatus(HW_RESP_EFFECTIVE_POLICY);
}

void KissModem::handleGetCapabilityStatus() {
  writeCapabilityStatus();
}

void KissModem::purgeExpiredOverrides() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (_overrides[i].active &&
        _overrides[i].expires_at_ms != 0 &&
        (int32_t)(now - _overrides[i].expires_at_ms) >= 0) {
      _overrides[i].active = false;
      _overrides[i].kind = 0;
      _overrides[i].value = 0;
      _overrides[i].expires_at_ms = 0;
    }
  }
}

bool KissModem::parseOverrideTlv(const uint8_t* data, uint16_t len, uint8_t* kind, uint8_t* value, uint32_t* ttl_ms) {
  bool saw_kind = false;
  uint16_t offset = 0;

  while (offset < len) {
    if (offset + 2 > len) return false;
    uint8_t field = data[offset++];
    uint8_t field_len = data[offset++];
    if (offset + field_len > len) return false;

    const uint8_t* field_value = data + offset;
    switch (field) {
      case OVERRIDE_TLV_KIND:
        if (field_len != 1) return false;
        *kind = field_value[0];
        saw_kind = true;
        break;
      case OVERRIDE_TLV_VALUE:
        if (field_len < 1) return false;
        *value = field_value[0];
        break;
      case OVERRIDE_TLV_TTL_MS:
        if (field_len != 4) return false;
        *ttl_ms = ((uint32_t)field_value[0]) |
                  ((uint32_t)field_value[1] << 8) |
                  ((uint32_t)field_value[2] << 16) |
                  ((uint32_t)field_value[3] << 24);
        break;
      case OVERRIDE_TLV_SCOPE:
      case OVERRIDE_TLV_TARGET:
      case OVERRIDE_TLV_REASON:
        break;
      default:
        break;
    }

    offset += field_len;
  }

  return saw_kind;
}

bool KissModem::isOverrideKindSupported(uint8_t kind) const {
  switch (kind) {
    case OVERRIDE_KIND_FORCE_LEAF:
    case OVERRIDE_KIND_FORCE_RELAY:
    case OVERRIDE_KIND_QUIET_MODE:
    case OVERRIDE_KIND_TX_INHIBIT:
    case OVERRIDE_KIND_DISPLAY_PAGE:
      return true;
    default:
      return false;
  }
}

bool KissModem::applyOverride(uint8_t kind, uint8_t value, uint32_t ttl_ms) {
  if (kind == 0) return false;
  if (value == 0 || ttl_ms == 0) return clearOverrideKind(kind);

  int8_t free_slot = -1;
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (_overrides[i].active && _overrides[i].kind == kind) {
      free_slot = i;
      break;
    }
    if (!_overrides[i].active && free_slot < 0) {
      free_slot = i;
    }
  }

  if (free_slot < 0) return false;

  _overrides[free_slot].active = true;
  _overrides[free_slot].kind = kind;
  _overrides[free_slot].value = value;
  _overrides[free_slot].expires_at_ms = millis() + ttl_ms;
  return true;
}

bool KissModem::clearOverrideKind(uint8_t kind) {
  bool cleared = false;
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (_overrides[i].active && _overrides[i].kind == kind) {
      _overrides[i].active = false;
      _overrides[i].kind = 0;
      _overrides[i].value = 0;
      _overrides[i].expires_at_ms = 0;
      cleared = true;
    }
  }
  return cleared || isOverrideKindSupported(kind);
}

bool KissModem::isOverrideActive(uint8_t kind) const {
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (_overrides[i].active && _overrides[i].kind == kind && _overrides[i].value != 0) {
      return true;
    }
  }
  return false;
}

uint8_t KissModem::getOverrideFlagMask() const {
  uint8_t flags = 0;
  if (isOverrideActive(OVERRIDE_KIND_FORCE_LEAF)) flags |= OVERRIDE_FLAG_FORCE_LEAF;
  if (isOverrideActive(OVERRIDE_KIND_FORCE_RELAY)) flags |= OVERRIDE_FLAG_FORCE_RELAY;
  if (isOverrideActive(OVERRIDE_KIND_QUIET_MODE)) flags |= OVERRIDE_FLAG_QUIET_MODE;
  if (isOverrideActive(OVERRIDE_KIND_TX_INHIBIT)) flags |= OVERRIDE_FLAG_TX_INHIBIT;
  if (isOverrideActive(OVERRIDE_KIND_DISPLAY_PAGE)) flags |= OVERRIDE_FLAG_DISPLAY_PAGE;
  return flags;
}

uint8_t KissModem::getActiveOverrideCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (_overrides[i].active) count++;
  }
  return count;
}

uint8_t KissModem::getEffectiveRoleCode() const {
  if (isOverrideActive(OVERRIDE_KIND_QUIET_MODE)) return OVERRIDE_ROLE_QUIET;
  if (isOverrideActive(OVERRIDE_KIND_FORCE_LEAF)) return OVERRIDE_ROLE_LEAF;
  if (isOverrideActive(OVERRIDE_KIND_FORCE_RELAY)) return OVERRIDE_ROLE_RELAY;
  return OVERRIDE_ROLE_AUTO;
}

uint32_t KissModem::getNextOverrideTtlMs() const {
  uint32_t now = millis();
  uint32_t next_ttl_ms = 0;

  for (uint8_t i = 0; i < KISS_MAX_OVERRIDES; i++) {
    if (!_overrides[i].active || _overrides[i].expires_at_ms == 0) continue;
    if ((int32_t)(now - _overrides[i].expires_at_ms) >= 0) continue;

    uint32_t remaining = _overrides[i].expires_at_ms - now;
    if (next_ttl_ms == 0 || remaining < next_ttl_ms) {
      next_ttl_ms = remaining;
    }
  }

  return next_ttl_ms;
}

void KissModem::writeOverrideStatus(uint8_t response_subcommand) {
  purgeExpiredOverrides();

  uint32_t next_ttl_ms = getNextOverrideTtlMs();
  uint8_t buf[9];
  buf[0] = OVERRIDE_STATUS_VERSION;
  buf[1] = getOverrideFlagMask();
  buf[2] = getActiveOverrideCount();
  buf[3] = getEffectiveRoleCode();
  buf[4] = _last_override_reason;
  buf[5] = (uint8_t)(next_ttl_ms & 0xFF);
  buf[6] = (uint8_t)((next_ttl_ms >> 8) & 0xFF);
  buf[7] = (uint8_t)((next_ttl_ms >> 16) & 0xFF);
  buf[8] = (uint8_t)((next_ttl_ms >> 24) & 0xFF);

  writeHardwareFrame(response_subcommand, buf, sizeof(buf));
}

void KissModem::writeCapabilityStatus() {
  purgeExpiredOverrides();

  uint32_t feature_flags = CINDER_FEATURE_OVERRIDE_CONTROL |
                           CINDER_FEATURE_FIRMWARE_DIAGNOSTICS;
  uint16_t supported_transports = CINDER_TRANSPORT_LORA | CINDER_TRANSPORT_SERIAL;
  uint16_t max_low_rate_payload_bytes = CINDER_MAX_LOW_RATE_PAYLOAD_BYTES;
  uint16_t max_queue_frames = 1;

  uint8_t buf[16];
  buf[0] = CAPABILITY_STATUS_VERSION;
  buf[1] = CINDER_NATIVE_PROTOCOL_VERSION;
  buf[2] = CINDER_BEARER_KISS_BENCH;
  buf[3] = getEffectiveRoleCode();
  buf[4] = (uint8_t)(feature_flags & 0xFF);
  buf[5] = (uint8_t)((feature_flags >> 8) & 0xFF);
  buf[6] = (uint8_t)((feature_flags >> 16) & 0xFF);
  buf[7] = (uint8_t)((feature_flags >> 24) & 0xFF);
  buf[8] = (uint8_t)(supported_transports & 0xFF);
  buf[9] = (uint8_t)((supported_transports >> 8) & 0xFF);
  buf[10] = (uint8_t)(max_low_rate_payload_bytes & 0xFF);
  buf[11] = (uint8_t)((max_low_rate_payload_bytes >> 8) & 0xFF);
  buf[12] = (uint8_t)(max_queue_frames & 0xFF);
  buf[13] = (uint8_t)((max_queue_frames >> 8) & 0xFF);
  buf[14] = getOverrideFlagMask();
  buf[15] = 0;

  writeHardwareFrame(HW_RESP_CAPABILITY_STATUS, buf, sizeof(buf));
}

const char* KissModem::getEffectiveRoleLabel() const {
  switch (getEffectiveRoleCode()) {
    case OVERRIDE_ROLE_QUIET:
      return "QUIET";
    case OVERRIDE_ROLE_LEAF:
      return "LEAF";
    case OVERRIDE_ROLE_RELAY:
      return "RELAY";
    default:
      return "AUTO";
  }
}
