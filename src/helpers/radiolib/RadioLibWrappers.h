#pragma once

#include <Mesh.h>
#include <RadioLib.h>

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent, n_recv_errors;
  uint32_t _recv_crc_error_count;
  uint32_t _recv_header_error_count;
  uint32_t _recv_other_error_count;
  int16_t _last_recv_error_code;
  uint16_t _last_recv_error_len;
  int16_t _last_start_recv_error_code;
  uint32_t _start_recv_error_count;
  int16_t _noise_floor, _threshold;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;

  void idle();
  void startRecv();
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : _radio(&radio), _board(&board) { resetStats(); }

  void begin() override;
  virtual void powerOff() { _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool isChannelActive();

  bool isReceiving() override { 
    if (isReceivingPacket()) return true;

    return isChannelActive();
  }

  virtual float getCurrentRSSI() =0;

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  uint32_t getRecvCrcErrorCount() const { return _recv_crc_error_count; }
  uint32_t getRecvHeaderErrorCount() const { return _recv_header_error_count; }
  uint32_t getRecvOtherErrorCount() const { return _recv_other_error_count; }
  int16_t getLastRecvErrorCode() const { return _last_recv_error_code; }
  uint16_t getLastRecvErrorLen() const { return _last_recv_error_len; }
  int16_t getLastStartRecvErrorCode() const { return _last_start_recv_error_code; }
  uint32_t getStartRecvErrorCount() const { return _start_recv_error_count; }
  void resetStats() {
    n_recv = n_sent = n_recv_errors = 0;
    _recv_crc_error_count = 0;
    _recv_header_error_count = 0;
    _recv_other_error_count = 0;
    _last_recv_error_code = 0;
    _last_recv_error_len = 0;
    _last_start_recv_error_code = 0;
    _start_recv_error_count = 0;
  }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual void setRxBoostedGainMode(bool) { }
  virtual bool getRxBoostedGainMode() const { return false; }
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is VERY SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseListener : public mesh::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseListener(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
  }
};
