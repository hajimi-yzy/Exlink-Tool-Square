#include "CST9220.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned irq_pin_configurations = 0;
void pinMode(int pin, int) {
  assert(pin == 16 || pin == 17);
  if (pin == 17) ++irq_pin_configurations;
}
void digitalWrite(int pin, int) { assert(pin == 16); }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}

static void validPoint(TwoWire &bus) {
  bus.packet.assign(15, 0);
  bus.packet[0] = 6;
  bus.packet[1] = 10;
  bus.packet[2] = 15;
  bus.packet[3] = 0x34;
  bus.packet[5] = 1;
  bus.packet[6] = 0xAB;
}

int main() {
  uint16_t x = 0, y = 0;
  TwoWire bus;
  CST9220 touch(bus, 18, 21, 16, 17);
  assert(!touch.isReady());
  assert(touch.begin());
  assert(touch.isReady() && touch.communicationOk());
  assert(touch.interruptPin() == 17);
  assert(strcmp(touch.protocolName(), "mailbox16") == 0);
  assert(bus.beginCalls == 1);

  const unsigned initialAck = bus.ackWrites;
  assert(!touch.readTouch(&x, &y));
  assert(touch.communicationOk() && !touch.releaseReported());
  assert(bus.ackWrites == initialAck); // Empty report must not be ACKed.

  validPoint(bus);
  assert(touch.readTouch(&x, &y));
  assert(x == 163 && y == 244 && touch.communicationOk());
  assert(bus.lastReadStop == 1 && bus.ackWrites == initialAck + 1);

  bus.packet[0] = 0;
  bus.packet[5] = 0;
  assert(!touch.readTouch(&x, &y));
  assert(touch.communicationOk() && touch.releaseReported());

  validPoint(bus);
  bus.packet[6] = 0;
  assert(!touch.readTouch(&x, &y));
  assert(touch.communicationOk() && !touch.releaseReported());

  validPoint(bus);
  bus.failRead = true;
  assert(!touch.readTouch(&x, &y));
  assert(!touch.communicationOk() && !touch.releaseReported());
  bus.failRead = false;
  assert(touch.readTouch(&x, &y) && touch.communicationOk());

  bus.shortBuffer = true;
  assert(!touch.readTouch(&x, &y) && !touch.communicationOk());
  bus.shortBuffer = false;
  bus.queueCapacity = 1;
  const unsigned beforeQueueFail = bus.requestCalls;
  assert(!touch.readTouch(&x, &y) && !touch.communicationOk());
  assert(bus.requestCalls == beforeQueueFail && bus.lastWriteStop == 1);
  bus.queueCapacity = 32;
  assert(touch.readTouch(&x, &y) && touch.communicationOk());

  bus.ackFailures = 2;
  assert(!touch.readTouch(&x, &y) && !touch.communicationOk());
  bus.ackFailures = 1;
  assert(touch.readTouch(&x, &y) && !touch.communicationOk());
  assert(touch.readTouch(&x, &y) && touch.communicationOk());

  const unsigned beginBeforeRecovery = bus.beginCalls;
  const unsigned irqBeforeRecovery = irq_pin_configurations;
  bus.activeAddress = 0x64;
  assert(touch.recover());
  assert(touch.address() == 0x64 && touch.communicationOk());
  bus.activeAddress = 0x5A;
  assert(touch.recover());
  assert(touch.address() == 0x5A && touch.isReady());
  assert(bus.beginCalls == beginBeforeRecovery);
  assert(irq_pin_configurations == irqBeforeRecovery);
  bus.online = false;
  assert(!touch.recover() && !touch.isReady() && !touch.communicationOk());
  bus.online = true;
  assert(touch.recover() && touch.isReady() && touch.communicationOk());
  assert(bus.beginCalls == beginBeforeRecovery);
  assert(irq_pin_configurations == irqBeforeRecovery);

  bus.firmware = TwoWire::Unknown;
  assert(!touch.begin());
  assert(!touch.isReady() && !touch.communicationOk());
  assert(strcmp(touch.protocolName(), "unknown") == 0);
  const unsigned beginUnknown = bus.beginCalls;
  bus.firmware = TwoWire::Mailbox;
  assert(touch.recover() && touch.isReady());
  assert(bus.beginCalls == beginUnknown); // Unknown protocol recovery also preserves Wire.

  TwoWire legacyBus;
  legacyBus.firmware = TwoWire::Legacy;
  CST9220 legacy(legacyBus, 18, 21, 16, 17);
  assert(legacy.begin());
  assert(strcmp(legacy.protocolName(), "legacy8") == 0);
  validPoint(legacyBus);
  assert(legacy.readTouch(&x, &y) && legacy.communicationOk());
  assert(legacy.recover() && legacyBus.beginCalls == 1);

  puts("CST9220 transport/recovery host tests passed");
}
