#include "CST9220.h"

namespace
{
constexpr uint16_t CST92XX_REG_READ = 0xD000;
constexpr uint16_t CST92XX_REG_DEBUG_MODE = 0xD101;
constexpr uint16_t CST92XX_REG_NORMAL_MODE = 0xD109;
constexpr uint16_t CST92XX_REG_CHECKCODE = 0xD1FC;
constexpr uint16_t CST92XX_REG_RESOLUTION = 0xD1F8;
constexpr uint16_t CST92XX_REG_PROJECT_ID = 0xD204;
constexpr uint8_t CST92XX_ACK = 0xAB;
constexpr size_t CST92XX_REPORT_LEN = 15;
}

CST9220::CST9220(TwoWire &wire, int sda, int scl, int rst, int interrupt,
                 uint8_t address)
    : _wire(wire), _sda(sda), _scl(scl), _rst(rst), _interrupt(interrupt),
      _address(address), _frequency(100000), _ready(false),
      _protocol(Protocol::Unknown)
{
}

bool CST9220::probe()
{
  // A reset can change a firmware variant's active address. Always include
  // both supported addresses, even after the previous probe selected 0x64.
  const uint8_t candidates[] = {_address, 0x5A, 0x64};
  for (size_t i = 0; i < sizeof(candidates); ++i)
  {
    if (i > 0 && candidates[i] == candidates[0]) continue;
    _wire.beginTransmission(candidates[i]);
    if (_wire.endTransmission() == 0)
    {
      _address = candidates[i];
      _communicationOk = true;
      return true;
    }
  }
  _communicationOk = false;
  return false;
}

bool CST9220::begin(uint32_t frequency)
{
  _frequency = frequency;
  _ready = false;
  _protocol = Protocol::Unknown;
  _releaseReported = false;
  _communicationOk = true;

  pinMode(_interrupt, INPUT); // configure once; runtime recovery must keep ISR ownership
  resetController();
  _wire.begin(_sda, _scl, _frequency);
  _ready = probe();
  if (!_ready) return false;
  return detectProtocol();
}

void CST9220::resetController()
{

  pinMode(_rst, OUTPUT);

  // CST92xx reset: high -> low -> high.  Keep the post-reset delay long
  // enough for the controller to leave its bootloader before I2C probing.
  digitalWrite(_rst, HIGH);
  delay(5);
  digitalWrite(_rst, LOW);
  delay(10);
  digitalWrite(_rst, HIGH);
  delay(50);
}

bool CST9220::recover()
{
  const Protocol previous_protocol = _protocol;
  _ready = false;
  _releaseReported = false;
  _communicationOk = true;

  // Only touch-controller GPIOs are reset. The live shared Wire instance
  // and the other peripherals' routing/clock are deliberately preserved.
  resetController();
  delay(30);
  _ready = probe();
  if (!_ready) return false;

  if (previous_protocol == Protocol::Unknown) return detectProtocol();

  bool recovered = false;
  if (previous_protocol == Protocol::Mailbox16Bit)
  {
    recovered = enterNormalMode();
  }
  else if (previous_protocol == Protocol::Legacy8Bit)
  {
    uint8_t firmware = 0;
    recovered = readRegister8(0xA9, &firmware, 1);
  }
  _ready = recovered;
  _communicationOk = recovered;
  return recovered;
}

bool CST9220::detectProtocol()
{
  /*
   * The earlier 31-pin test image used the legacy 8-bit 0xA9/0x00 packet.
   * Newer CST9220 firmware exposes the CST92xx D000 mailbox.  Detect the
   * mailbox first, then retain the legacy path as a real fallback.
   */
  uint8_t checkcode[4] = {0};
  uint8_t resolution[4] = {0};
  uint8_t project_info[4] = {0};
  bool mailbox_probe = writeRegister16(CST92XX_REG_DEBUG_MODE);
  delay(10);
  if (mailbox_probe &&
      readRegister16(CST92XX_REG_CHECKCODE, checkcode, sizeof(checkcode)) &&
      readRegister16(CST92XX_REG_RESOLUTION, resolution, sizeof(resolution)) &&
      readRegister16(CST92XX_REG_PROJECT_ID, project_info, sizeof(project_info)))
  {
    const uint16_t chip_id = (uint16_t)project_info[2] |
                             ((uint16_t)project_info[3] << 8);
    if (chip_id == 0x9217U || chip_id == 0x9220U)
    {
      _protocol = Protocol::Mailbox16Bit;
    }

    if (_protocol == Protocol::Mailbox16Bit)
    {
      // Leave command mode, then use the documented D109 normal scan command.
      // Do not restore the D11E handshake removed after hardware validation.
      digitalWrite(_rst, LOW);
      delay(10);
      digitalWrite(_rst, HIGH);
      delay(50);
      delay(30);
      if (!enterNormalMode())
      {
        _protocol = Protocol::Unknown;
      }
    }
  }

  if (_protocol != Protocol::Mailbox16Bit)
  {
    // A legacy controller may have accepted the first byte as a register
    // pointer; reset it before probing the 8-bit register map.
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(50);
    uint8_t firmware = 0;
    _protocol = readRegister8(0xA9, &firmware, 1)
                    ? Protocol::Legacy8Bit
                    : Protocol::Unknown;
  }

  _ready = _protocol != Protocol::Unknown;
  _communicationOk = _ready;
  return _ready;
}

const char *CST9220::protocolName() const
{
  switch (_protocol)
  {
  case Protocol::Legacy8Bit:
    return "legacy8";
  case Protocol::Mailbox16Bit:
    return "mailbox16";
  default:
    return "unknown";
  }
}

bool CST9220::readRegister8(uint8_t reg, uint8_t *data, size_t length)
{
  if (!_ready || data == nullptr || length == 0)
  {
    _communicationOk = false;
    return false;
  }

  _wire.beginTransmission(_address);
  const size_t queued = _wire.write(reg);
  const uint8_t error = _wire.endTransmission();
  if (queued != 1 || error != 0)
  {
    _communicationOk = false;
    return false;
  }

  delayMicroseconds(250);
  const size_t received = _wire.requestFrom((int)_address, (int)length, (int)true);
  if (received != length)
  {
    _communicationOk = false;
    while (_wire.available())
    {
      (void)_wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i)
  {
    if (!_wire.available())
    {
      _communicationOk = false;
      return false;
    }
    const int value = _wire.read();
    if (value < 0)
    {
      _communicationOk = false;
      return false;
    }
    data[i] = (uint8_t)value;
  }
  return true;
}

bool CST9220::readRegister16(uint16_t reg, uint8_t *data, size_t length)
{
  if (!_ready || data == nullptr || length == 0)
  {
    _communicationOk = false;
    return false;
  }

  _wire.beginTransmission(_address);
  size_t queued = _wire.write((uint8_t)(reg >> 8));
  queued += _wire.write((uint8_t)reg);
  if (queued != 2)
  {
    // Finish with STOP on a queue failure so Wire's transaction lock is not
    // left pending for a repeated-start read that will never happen.
    (void)_wire.endTransmission();
    _communicationOk = false;
    return false;
  }
  if (_wire.endTransmission(false) != 0)
  {
    _communicationOk = false;
    return false;
  }

  const size_t received = _wire.requestFrom((int)_address, (int)length, (int)true);
  if (received != length)
  {
    _communicationOk = false;
    while (_wire.available())
    {
      (void)_wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i)
  {
    if (!_wire.available())
    {
      _communicationOk = false;
      return false;
    }
    const int value = _wire.read();
    if (value < 0)
    {
      _communicationOk = false;
      return false;
    }
    data[i] = (uint8_t)value;
  }
  return true;
}

bool CST9220::writeRegister16(uint16_t reg)
{
  _wire.beginTransmission(_address);
  size_t queued = _wire.write((uint8_t)(reg >> 8));
  queued += _wire.write((uint8_t)reg);
  const uint8_t error = _wire.endTransmission();
  if (queued != 2 || error != 0)
  {
    _communicationOk = false;
    return false;
  }
  return true;
}

bool CST9220::enterNormalMode()
{
  // The CST92XX register manual defines D109 as the normal-scan command.
  // Do not add the undocumented D11E/0x0002 exchange here: on some firmware
  // revisions it leaves the controller in a non-reporting command state.
  const bool entered = writeRegister16(CST92XX_REG_NORMAL_MODE);
  if (entered)
  {
    delay(20);
  }
  return entered;
}

bool CST9220::writeMailboxAck()
{
  _wire.beginTransmission(_address);
  size_t queued = _wire.write((uint8_t)(CST92XX_REG_READ >> 8));
  queued += _wire.write((uint8_t)CST92XX_REG_READ);
  queued += _wire.write(CST92XX_ACK);
  const uint8_t error = _wire.endTransmission();
  if (queued != 3 || error != 0)
  {
    _communicationOk = false;
    return false;
  }
  return true;
}

bool CST9220::readLegacyTouch(uint16_t *x, uint16_t *y)
{
  uint8_t packet[7] = {0};
  if (!readRegister8(0x00, packet, sizeof(packet)))
  {
    return false;
  }
  if ((packet[0] & 0x0FU) == 0) _releaseReported = true;
  if (packet[0] != 0x06) return false;

  *x = (uint16_t)(((uint16_t)packet[1] << 4) | ((packet[3] >> 4) & 0x0F));
  *y = (uint16_t)(((uint16_t)packet[2] << 4) | (packet[3] & 0x0F));
  return (*x < 480U && *y < 480U);
}

bool CST9220::readMailboxTouch(uint16_t *x, uint16_t *y)
{
  uint8_t packet[CST92XX_REPORT_LEN] = {0};
  if (!readRegister16(CST92XX_REG_READ, packet, sizeof(packet)))
  {
    return false;
  }

  bool all_zero = true;
  for (uint8_t byte : packet)
  {
    if (byte != 0)
    {
      all_zero = false;
      break;
    }
  }
  if (all_zero)
  {
    return false;
  }

  // ACK immediately after the mailbox read, before parsing the frame.
  if (!writeMailboxAck())
  {
    delay(2);
    if (!writeMailboxAck())
    {
      return false;
    }
  }

  if (packet[6] != CST92XX_ACK || packet[0] == CST92XX_ACK)
  {
    return false;
  }
  if ((packet[0] & 0x0FU) == 0 || (packet[5] & 0x7FU) == 0)
  {
    _releaseReported = true;
    return false;
  }

  // D004 bit7 is the manual's "big palm" flag; this is not a point.
  if ((packet[4] & 0x80U) != 0U)
  {
    return false;
  }

  // The manual defines event 0x06 as a valid contact and 0x00 as empty.
  // Reject other event codes rather than forwarding malformed frames.
  const uint8_t event = packet[0] & 0x0F;
  if (event != 0x06U)
  {
    return false;
  }

  *x = (uint16_t)(((uint16_t)packet[1] << 4) | ((packet[3] >> 4) & 0x0F));
  *y = (uint16_t)(((uint16_t)packet[2] << 4) | (packet[3] & 0x0F));
  return (*x < 480U && *y < 480U);
}

bool CST9220::readTouch(uint16_t *x, uint16_t *y)
{
  _releaseReported = false;
  _communicationOk = true;
  if (x == nullptr || y == nullptr || !_ready)
  {
    _communicationOk = false;
    return false;
  }

  if (_protocol == Protocol::Mailbox16Bit)
  {
    return readMailboxTouch(x, y);
  }
  if (_protocol == Protocol::Legacy8Bit)
  {
    return readLegacyTouch(x, y);
  }
  _communicationOk = false;
  return false;
}
