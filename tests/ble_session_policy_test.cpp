#include "ble_session_policy.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

int main() {
  BleSessionPolicy policy;
  policy.connected(true, 10);
  policy.activity(100);
  assert(!policy.expired(999999, false, 120000, 120000));
  policy.start(BleSessionMode::Phone, 1000);
  assert(!policy.expired(120999, false, 120000, 120000));
  assert(policy.expired(121000, false, 120000, 120000));
  policy.connected(true, 10000);
  policy.activity(20000);
  assert(!policy.expired(139999, false, 120000, 120000));
  assert(policy.expired(140000, false, 120000, 120000));
  policy.connected(false, 140000);
  assert(!policy.expired(259999, false, 120000, 120000));
  assert(policy.expired(260000, false, 120000, 120000));
  policy.stop();
  policy.connected(true, 270000);
  assert(!policy.expired(999999, false, 120000, 120000));
  policy.start(BleSessionMode::Phone, 0);
  policy.connected(true, 1);
  assert(!policy.expired(200000, true, 120000, 120000));
  assert(!policy.expired(600000, true, 120000, 120000));
  assert(!policy.expired(600001, false, 120000, 120000));
  assert(!policy.expired(720000, false, 120000, 120000));
  assert(policy.expired(720001, false, 120000, 120000));
  for (auto mode : {BleSessionMode::Phone, BleSessionMode::Bridge}) {
    policy.start(mode, UINT32_MAX - 99);
    assert(!policy.expired(19, false, 120, 120));
    assert(policy.expired(20, false, 120, 120));
    policy.connected(true, UINT32_MAX - 99);
    policy.activity(UINT32_MAX - 49);
    assert(!policy.expired(69, false, 120, 120));
    assert(policy.expired(70, false, 120, 120));
  }
  puts("BLE session policy: default Off, timeouts, rollover, transfer holds passed");
}
