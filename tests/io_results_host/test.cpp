#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "mcp4017_control.h"
#include "production_io_types.h"

static uint32_t now, io_request_id, power_pending_id, power_diag_pending_id, ina_pending_id;
static int power_pending_action, ina266_flag;
static bool internal_io_started = true, ina266_sample_valid, ina_sample_ready;
static bool power_diag_sample_ready;
static uint64_t vol, cur, wat;
static double v, a, w;
static volatile int64_t mAH;
static char voltageStr[20], currentStr[20], powerStr[20], mAHStr[20];
static exlink::IoResult power_diag_sample, ina_sample;
static std::vector<std::string> finished_power;
static uint32_t millis() { return now; }
static void power_finish_request(const char *action, const exlink::Mcp4017Result &) {
  finished_power.push_back(action);
}
struct Io {
  std::deque<exlink::IoResult> results;
  std::vector<uint32_t> submitted;
  bool takeResult(exlink::IoResult &result) {
    if (results.empty()) return false;
    result = results.front(); results.pop_front(); return true;
  }
  bool submit(exlink::IoRequestKind kind, int, uint32_t id) {
    assert(kind == exlink::IoRequestKind::Measurement);
    submitted.push_back(id); return true;
  }
} internal_io;
#include "production_io_results.inc"

static void reset() {
  now += 10000;
  power_pending_id = power_diag_pending_id = 0;
  ina266_flag = 1;
  mAH = 0;
  finished_power.clear(); internal_io.results.clear();
  power_diag_sample_ready = false;
  ina266_reset_measurement();
}
static uint32_t request() {
  ina266_task(); assert(ina_pending_id != 0); return ina_pending_id;
}
static void complete(uint32_t id, bool cancelled, uint64_t millivolts = 5000) {
  exlink::IoResult result;
  result.kind = exlink::IoRequestKind::Measurement;
  result.id = id; result.cancelled = cancelled;
  result.millivolts = millivolts;
  result.microamps = 5000000;
  result.microwatts = 5000000;
  internal_io.results.push_back(result);
  internal_io_results_task();
}
static bool invalidated() {
  return !ina266_sample_valid && !ina_sample_ready && !ina_pending_id &&
      v == 0 && a == 0 && w == 0 &&
      !voltageStr[0] && !currentStr[0] && !powerStr[0];
}
static bool failedMeasurementThenRecovery() {
  reset(); complete(request(), false); ina266_task();
  assert(ina266_sample_valid && v == 5 && voltageStr[0]);
  now += 100;
  complete(request(), true);
  if (!invalidated()) return false;
  // Fault time must not be integrated as valid charge on the next good read.
  const auto before = mAH;
  now += 100;
  complete(request(), false, 3300); ina266_task();
  assert(ina266_sample_valid && std::abs(v - 3.3) < .001 && mAH == before);
  now += 100;
  complete(request(), false, 3300); ina266_task();
  assert(mAH > before);
  return true;
}
static void staleSessionResult() {
  reset(); const auto old = request();
  ina266_reset_measurement();
  now += 100; const auto current = request();
  complete(old, false, 9000);
  assert(!ina266_sample_valid && !ina_sample_ready && ina_pending_id == current && v == 0);
  complete(current, false, 4800); ina266_task();
  assert(ina266_sample_valid && std::abs(v - 4.8) < .001);
}
static void unrelatedCompletions() {
  reset(); const auto measurement = request();
  power_pending_id = ++io_request_id;
  power_diag_pending_id = ++io_request_id;
  power_pending_action = 1;
  complete(power_pending_id, true);
  assert(!power_pending_id && finished_power.empty() && ina_pending_id == measurement);
  complete(power_diag_pending_id, false);
  assert(!power_diag_pending_id && power_diag_sample_ready && ina_pending_id == measurement);
  complete(measurement, false); ina266_task();
  assert(ina266_sample_valid && v == 5);
  power_pending_id = ++io_request_id;
  complete(power_pending_id, false);
  assert(finished_power.size() == 1 && finished_power.front() == "write" && ina266_sample_valid);
}
int main(int argc, char **argv) {
  const bool fixed = failedMeasurementThenRecovery();
  if (argc == 2 && std::strcmp(argv[1], "--legacy") == 0) {
    assert(!fixed);
    std::puts("PASS legacy mutation reproduces stale voltage after transport failure");
    return 0;
  }
  assert(fixed);
  staleSessionResult(); unrelatedCompletions();
  std::puts("PASS production result cancellation clears stale readings and recovers without fault-gap integration");
  std::puts("PASS production session IDs reject late results and keep power/diagnostic completions independent");
}
