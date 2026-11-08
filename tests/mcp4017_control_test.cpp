#include "mcp4017_control.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <deque>
#include <vector>

using exlink::Mcp4017Controller;
using exlink::Mcp4017Status;

// Model an independent hardware wiper: a successful write can reach the chip
// even when the following read fails, so software must then report unknown.
struct FakeBus {
  int hardware = 86;
  size_t queue_capacity = 1;
  uint8_t tx_code = 0;
  size_t response_count = 1;
  bool response_available = true;
  int read_override = -2;
  bool transmitting = false;
  unsigned end_count = 0;
  unsigned request_count = 0;
  unsigned hardware_writes = 0;
  std::vector<uint8_t> pending;
  std::vector<std::vector<uint8_t> > transactions;
  std::deque<int> receive;

  void beginTransmission(uint8_t address) {
    assert(address == 0x2F);
    assert(!transmitting);
    transmitting = true;
    pending.clear();
  }

  size_t write(uint8_t value) {
    assert(transmitting);
    if (pending.size() >= queue_capacity) return 0;
    pending.push_back(value);
    return 1;
  }

  uint8_t endTransmission() {
    assert(transmitting);
    transmitting = false;
    ++end_count;
    transactions.push_back(pending);
    if (tx_code == 0 && pending.size() == 1) {
      hardware = pending[0];
      ++hardware_writes;
    }
    return tx_code;
  }

  size_t requestFrom(int address, int count) {
    assert(address == 0x2F);
    assert(count == 1);
    assert(!transmitting);
    ++request_count;
    receive.clear();
    if (response_count && response_available)
      receive.push_back(read_override == -2 ? hardware : read_override);
    return response_count;
  }

  int available() { return static_cast<int>(receive.size()); }

  int read() {
    if (receive.empty()) return -1;
    int value = receive.front();
    receive.pop_front();
    return value;
  }
};

static void test_protocol_and_probe() {
  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  assert(!controller.known());
  const int previous = controller.value();
  assert(controller.probe() == 0);
  assert(bus.end_count == 1 && bus.transactions.back().empty());
  assert(bus.hardware == 86 && bus.request_count == 0);
  assert(!controller.known() && controller.value() == previous);

  auto result = controller.write(44);
  assert(result.status == Mcp4017Status::Ok);
  assert(result.target == 44 && result.readback == 44);
  assert(result.tx_code == 0 && result.queued_bytes == 1);
  assert(result.received_bytes == 1);
  assert(bus.transactions.back().size() == 1);
  assert(bus.transactions.back()[0] == 44);  // No register-address byte.
  assert(bus.request_count == 1 && bus.hardware == 44);
  assert(controller.known() && controller.value() == 44);

  bus.tx_code = 2;
  assert(controller.probe() == 2);
  assert(controller.known() && controller.value() == 44);
  assert(bus.hardware == 44 && bus.transactions.back().empty());
}

static void test_read_and_adjust_from_real_hardware() {
  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  auto result = controller.read();
  assert(result.status == Mcp4017Status::Ok && result.readback == 86);
  assert(controller.known() && controller.value() == 86);
  assert(bus.end_count == 0 && bus.hardware_writes == 0);

  // An external change must supersede the stale cached value before +/-.
  bus.hardware = 20;
  result = controller.adjust(1);
  assert(result.status == Mcp4017Status::Ok);
  assert(result.target == 21 && result.readback == 21);
  assert(bus.hardware == 21 && controller.value() == 21);
  assert(bus.request_count == 3);  // Initial read, pre-adjust read, verify.
}

static void test_nack_and_queue_failure() {
  for (uint8_t code = 1; code <= 5; ++code) {
    FakeBus bus;
    Mcp4017Controller<FakeBus> controller(bus);
    assert(controller.write(44).status == Mcp4017Status::Ok);
    const unsigned reads = bus.request_count;
    bus.tx_code = code;
    auto result = controller.write(16);
    assert(result.status == Mcp4017Status::TxFailed);
    assert(result.tx_code == code && result.target == 16);
    assert(!controller.known() && controller.value() == 44);
    assert(bus.hardware == 44 && bus.request_count == reads);
  }

  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  assert(controller.write(44).status == Mcp4017Status::Ok);
  bus.queue_capacity = 0;
  const unsigned ends = bus.end_count;
  const unsigned reads = bus.request_count;
  auto result = controller.write(16);
  assert(result.status == Mcp4017Status::QueueFailed);
  assert(result.queued_bytes == 0);
  assert(bus.end_count == ends + 1 && !bus.transmitting);
  assert(bus.request_count == reads && bus.hardware == 44);
  assert(!controller.known() && controller.value() == 44);
}

static void test_read_failures_do_not_commit() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    FakeBus bus;
    Mcp4017Controller<FakeBus> controller(bus);
    assert(controller.write(44).status == Mcp4017Status::Ok);
    if (mode == 0) bus.response_count = 0;
    if (mode == 1) bus.response_available = false;
    if (mode == 2) bus.read_override = -1;
    auto result = controller.write(16);
    assert(result.status == Mcp4017Status::ReadFailed);
    // The physical wiper changed, but the write could not be verified.
    assert(bus.hardware == 16);
    assert(!controller.known() && controller.value() == 44);
  }

  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  assert(controller.write(44).status == Mcp4017Status::Ok);
  bus.read_override = 17;
  auto result = controller.write(16);
  assert(result.status == Mcp4017Status::ReadbackMismatch);
  assert(result.readback == 17 && bus.hardware == 16);
  assert(!controller.known() && controller.value() == 44);
}

static void test_invalid_readback_and_failed_adjust() {
  for (int invalid : {0x80, 0xFF}) {
    FakeBus bus;
    Mcp4017Controller<FakeBus> controller(bus);
    assert(controller.write(44).status == Mcp4017Status::Ok);
    bus.read_override = invalid;
    auto result = controller.read();
    assert(result.status == Mcp4017Status::InvalidReadback);
    assert(!controller.known() && controller.value() == 44);
    const unsigned writes = bus.hardware_writes;
    result = controller.adjust(-1);
    assert(result.status == Mcp4017Status::InvalidReadback);
    assert(bus.hardware_writes == writes && bus.hardware == 44);
  }

  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  bus.response_count = 0;
  const int previous = controller.value();
  auto result = controller.adjust(1);
  assert(result.status == Mcp4017Status::ReadFailed);
  assert(bus.hardware_writes == 0 && bus.end_count == 0);
  assert(!controller.known() && controller.value() == previous);
  // A later successful read allows adjustment without inventing a base value.
  bus.response_count = 1;
  bus.hardware = 30;
  result = controller.adjust(-1);
  assert(result.status == Mcp4017Status::Ok && bus.hardware == 29);
  assert(controller.known() && controller.value() == 29);
}

static void test_clamping() {
  FakeBus bus;
  Mcp4017Controller<FakeBus> controller(bus);
  auto result = controller.write(-100);
  assert(result.status == Mcp4017Status::Ok);
  assert(result.target == 1 && bus.hardware == 1);
  result = controller.adjust(-1);
  assert(result.status == Mcp4017Status::Ok && bus.hardware == 1);
  result = controller.write(1000);
  assert(result.status == Mcp4017Status::Ok);
  assert(result.target == 126 && bus.hardware == 126);
  result = controller.adjust(1);
  assert(result.status == Mcp4017Status::Ok && bus.hardware == 126);
}

static void test_adjust_at_hardware_endpoints() {
  struct Boundary {
    int raw;
    int delta;
  };
  for (const Boundary boundary : {Boundary{0, -1}, Boundary{1, -1},
                                  Boundary{126, 1}, Boundary{127, 1},
                                  Boundary{44, 0}}) {
    FakeBus bus;
    bus.hardware = boundary.raw;
    Mcp4017Controller<FakeBus> controller(bus);
    auto result = controller.adjust(boundary.delta);
    assert(result.status == Mcp4017Status::Ok);
    assert(result.target == boundary.raw && result.readback == boundary.raw);
    assert(result.tx_code == 255 && result.queued_bytes == 0);
    assert(bus.end_count == 0 && bus.hardware_writes == 0);
    assert(bus.request_count == 1 && bus.hardware == boundary.raw);
    assert(controller.known() && controller.value() == boundary.raw);
  }

  for (const Boundary boundary : {Boundary{0, 1}, Boundary{127, -1}}) {
    FakeBus bus;
    bus.hardware = boundary.raw;
    Mcp4017Controller<FakeBus> controller(bus);
    const int expected = boundary.raw + boundary.delta;
    auto result = controller.adjust(boundary.delta);
    assert(result.status == Mcp4017Status::Ok);
    assert(result.target == expected && result.readback == expected);
    assert(result.tx_code == 0 && result.queued_bytes == 1);
    assert(bus.end_count == 1 && bus.hardware_writes == 1);
    assert(bus.request_count == 2 && bus.hardware == expected);
    assert(controller.known() && controller.value() == expected);
  }
}

int main() {
  test_protocol_and_probe();
  test_read_and_adjust_from_real_hardware();
  test_nack_and_queue_failure();
  test_read_failures_do_not_commit();
  test_invalid_readback_and_failed_adjust();
  test_clamping();
  test_adjust_at_hardware_endpoints();
  puts("MCP4017 control: protocol, probe, readback, ACK/queue/read failures, safe adjust and limits passed");
}
