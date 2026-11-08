#pragma once

#include <Arduino.h>
#include <Wire.h>

/*
 * Minimal CST9220 driver. Calls belong to the application's I2C owner;
 * display/input callbacks can consume its cached samples instead.
 *
 * The tested module uses the documented 0xD000 report mailbox and 0xD109
 * normal-mode command. A legacy 0x00/7-byte path remains for older firmware;
 * only that path requires a STOP and delay before the read transaction.
 */
class CST9220
{
public:
  CST9220(TwoWire &wire, int sda, int scl, int rst, int interrupt,
          uint8_t address = 0x5A);

  bool begin(uint32_t frequency = 100000);
  // Reset/reprobe the touch controller without reinitializing the shared bus.
  bool recover();
  bool readTouch(uint16_t *x, uint16_t *y);
  // A false readTouch() can also mean an empty/release/invalid frame. This
  // flag only becomes false when that attempt cannot communicate reliably.
  bool communicationOk() const { return _communicationOk; }
  bool releaseReported() const { return _releaseReported; }
  bool isReady() const { return _ready; }
  uint8_t address() const { return _address; }
  int interruptPin() const { return _interrupt; }
  const char *protocolName() const;

private:
  enum class Protocol : uint8_t
  {
    Unknown,
    Legacy8Bit,
    Mailbox16Bit,
  };

  bool readRegister8(uint8_t reg, uint8_t *data, size_t length);
  bool readRegister16(uint16_t reg, uint8_t *data, size_t length);
  bool writeRegister16(uint16_t reg);
  bool enterNormalMode();
  bool writeMailboxAck();
  bool readLegacyTouch(uint16_t *x, uint16_t *y);
  bool readMailboxTouch(uint16_t *x, uint16_t *y);
  bool probe();
  void resetController();
  bool detectProtocol();

  TwoWire &_wire;
  int _sda;
  int _scl;
  int _rst;
  int _interrupt;
  uint8_t _address;
  uint32_t _frequency;
  bool _ready;
  Protocol _protocol;
  bool _releaseReported = false;
  bool _communicationOk = true;
};
