#include "ble_update_service.h"
#include "device_ota_sink.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <string.h>

namespace exlink {
static constexpr char Service[] = "7c9e0001-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Control[] = "7c9e0002-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Data[] = "7c9e0003-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Status[] = "7c9e0004-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Info[] = "7c9e0005-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Telemetry[] = "7c9e0006-7b3f-4f6d-9e1a-71b4c3a22100";
static constexpr char Logs[] = "7c9e0007-7b3f-4f6d-9e1a-71b4c3a22100";
struct Packet { uint32_t generation; uint16_t size; uint8_t bytes[495]; };
static QueueHandle_t queue;
static volatile bool authenticated;
static volatile bool connected;
static volatile bool overflow;
static volatile bool disconnected;
static volatile bool enabled;
static bool bonded;
static bool connectionChanged;
static uint32_t connectionGeneration;
static portMUX_TYPE connectionMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t ownerPeer[6];
static bool ownerBound;
static bool readActivity;
static portMUX_TYPE activityMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t currentPeer[6];
static BLECharacteristic *statusCharacteristic;
static EspOtaSink firmwareSink;
static UpdateReceiver *receiver;
static UpdateReceiver firmwareReceiver(firmwareSink);
static UpdateKind kind;
static uint32_t lastPacketMs;
static bool outputHeld;
static bool paused;
static uint32_t pausedAtMs;
static constexpr uint32_t ResumeTimeoutMs = 60000U;
static UpdateResult lastResult;
static char telemetrySnapshot[480];
static size_t telemetryLength;
static portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastTelemetryMs;
static uint8_t statusSnapshot[16];
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

struct LinkSnapshot {
  bool enabled, connected, authenticated, bonded, ownerAllowed;
  uint32_t generation;
  uint8_t peer[6];
};
static LinkSnapshot linkSnapshot() {
  LinkSnapshot link;
  portENTER_CRITICAL(&connectionMux);
  link = {enabled, connected, authenticated, bonded,
      !ownerBound || (bonded && memcmp(currentPeer, ownerPeer, sizeof(ownerPeer)) == 0),
      connectionGeneration, {0}};
  memcpy(link.peer, currentPeer, sizeof(link.peer));
  portEXIT_CRITICAL(&connectionMux);
  return link;
}
static void releaseOwner() {
  portENTER_CRITICAL(&connectionMux);
  ownerBound = false;
  memset(ownerPeer, 0, sizeof(ownerPeer));
  portEXIT_CRITICAL(&connectionMux);
  paused = false;
}

static void noteActivity() {
  portENTER_CRITICAL(&activityMux);
  readActivity = true;
  portEXIT_CRITICAL(&activityMux);
}

bool bleUpdateTakeActivity() {
  portENTER_CRITICAL(&activityMux);
  bool value = readActivity;
  readActivity = false;
  portEXIT_CRITICAL(&activityMux);
  return value;
}

static void refreshTelemetry() {
  // Arduino's BTC_TASK has only 3072 bytes. In particular, printf's floating
  // point conversion must run in setup/loop, never underneath a GATT read.
  static char rendered[sizeof(telemetrySnapshot)];
  size_t length = updateReadTelemetry(rendered, sizeof(rendered));
  if (length >= sizeof(rendered)) length = sizeof(rendered) - 1;
  portENTER_CRITICAL(&telemetryMux);
  memcpy(telemetrySnapshot, rendered, length);
  telemetryLength = length;
  portEXIT_CRITICAL(&telemetryMux);
  lastTelemetryMs = millis();
}
static uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static void write32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
UpdateSink *__attribute__((weak)) screenAssetUpdateSink() { return nullptr; }
bool __attribute__((weak)) screenAssetRestoreDefault() { return false; }
size_t __attribute__((weak)) updateReadLog(char *, size_t) { return 0; }
void __attribute__((weak)) updateReportProgress(const UpdateProgressSnapshot &) {}

class StatusCallbacks : public BLECharacteristicCallbacks {
  uint8_t bytes_[16];
public:
  void onRead(BLECharacteristic *characteristic) override {
    const auto link = linkSnapshot();
    if (!link.enabled || !link.connected || !link.authenticated || !link.ownerAllowed) {
      characteristic->setValue(bytes_, 0);
      return;
    }
    noteActivity();
    portENTER_CRITICAL(&statusMux);
    memcpy(bytes_, statusSnapshot, sizeof(bytes_));
    portEXIT_CRITICAL(&statusMux);
    characteristic->setValue(bytes_, sizeof(bytes_));
  }
};
static StatusCallbacks statusCallbacks;

class ReadCallbacks : public BLECharacteristicCallbacks {
  bool logs_;
  uint8_t buffer_[480]; // GATT callbacks are serialized by BTC_TASK.
public:
  explicit ReadCallbacks(bool logs) : logs_(logs) {}
  void onRead(BLECharacteristic *characteristic) override {
    if (!enabled || !authenticated) { characteristic->setValue(buffer_, 0); return; }
    noteActivity();
    size_t length;
    if (logs_) {
      length = updateReadLog(reinterpret_cast<char *>(buffer_), sizeof(buffer_));
      if (length > sizeof(buffer_)) length = sizeof(buffer_);
    } else {
      portENTER_CRITICAL(&telemetryMux);
      length = telemetryLength;
      memcpy(buffer_, telemetrySnapshot, length);
      portEXIT_CRITICAL(&telemetryMux);
    }
    // Only update the value at the beginning of a read. Arduino keeps it for
    // continuation packets when the peer negotiates a small MTU.
    characteristic->setValue(buffer_, length);
  }
};
static ReadCallbacks telemetryCallbacks(false), logCallbacks(true);

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override {
    portENTER_CRITICAL(&connectionMux);
    const bool allowed = enabled && connected &&
        (!ownerBound || memcmp(currentPeer, ownerPeer, sizeof(ownerPeer)) == 0);
    portEXIT_CRITICAL(&connectionMux);
    return allowed;
  }
  bool onConfirmPIN(uint32_t) override { return false; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    portENTER_CRITICAL(&connectionMux);
    if (connected && memcmp(result.bd_addr, currentPeer, sizeof(currentPeer)) == 0) {
      authenticated = result.success;
      bonded = result.success && (result.auth_mode & ESP_LE_AUTH_BOND);
    }
    portEXIT_CRITICAL(&connectionMux);
  }
};
static SecurityCallbacks securityCallbacks;
class PacketCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const std::string value = characteristic->getValue();
    const auto link = linkSnapshot();
    if (!link.enabled || !link.connected || !link.authenticated || !link.ownerAllowed || !queue) return;
    noteActivity();
    if (value.empty() || value.size() > sizeof(Packet::bytes)) { overflow = true; return; }
    Packet packet;
    packet.generation = link.generation;
    packet.size = value.size();
    memcpy(packet.bytes, value.data(), value.size());
    if (xQueueSend(queue, &packet, 0) != pdTRUE) overflow = true;
  }
};
static PacketCallbacks packetCallbacks;

UpdateProgressSnapshot bleUpdateProgress() {
  const auto status = receiver ? receiver->status() : UpdateStatus{UpdateState::Idle, UpdateResult::Ok, 0, 0, 0};
  return {kind, status.state, lastResult, status.session, status.accepted, status.total, outputHeld, paused};
}

static void publish() {
  UpdateStatus status = receiver ? receiver->status() : UpdateStatus{UpdateState::Idle, UpdateResult::Ok, 0, 0, 0};
  uint8_t bytes[16] = {1, uint8_t(status.state), uint8_t(lastResult), uint8_t(kind)};
  write32(bytes + 4, status.session);
  write32(bytes + 8, status.accepted);
  write32(bytes + 12, status.total);
  portENTER_CRITICAL(&statusMux);
  memcpy(statusSnapshot, bytes, sizeof(bytes));
  portEXIT_CRITICAL(&statusMux);
  if (statusCharacteristic) {
    statusCharacteristic->setValue(bytes, sizeof(bytes));
    const auto link = linkSnapshot();
    if (link.enabled && link.connected && link.authenticated && link.ownerAllowed)
      statusCharacteristic->notify();
  }
  updateReportProgress(bleUpdateProgress());
}

void bleUpdateInit(BLEServer *server) {
  if (!queue) queue = xQueueCreate(4, sizeof(Packet));
  authenticated = false;
  connected = false;
  refreshTelemetry();
  BLEDevice::setMTU(517);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);
  // LE Secure Connections + bond. Just Works encrypts transport but does not
  // establish owner identity or MITM protection; server/account policy is separate.
  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEService *service = server->createService(BLEUUID(std::string(Service)), 24);
  auto control = service->createCharacteristic(Control, BLECharacteristic::PROPERTY_WRITE);
  auto data = service->createCharacteristic(Data, BLECharacteristic::PROPERTY_WRITE);
  control->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  data->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  control->setCallbacks(&packetCallbacks);
  data->setCallbacks(&packetCallbacks);
  statusCharacteristic = service->createCharacteristic(Status, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  statusCharacteristic->setCallbacks(&statusCallbacks);
  statusCharacteristic->addDescriptor(new BLE2902());
  auto info = service->createCharacteristic(Info, BLECharacteristic::PROPERTY_READ);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char json[480];
  const esp_app_desc_t *description = esp_ota_get_app_description();
  snprintf(json, sizeof(json), "{\"protocol\":1,\"model\":\"exlink-co5300-31pin\",\"id\":\"%02X%02X%02X%02X%02X%02X\",\"version\":\"%s\",\"build\":\"%s %s\",\"display\":[480,480],\"features\":[\"power\",\"pwm\",\"servo\",\"uart\",\"i2c\",\"spi\",\"measurement\",\"screen_asset\"],\"max_chunk\":480,\"ota\":%s}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], description->version, description->date, description->time, esp_ota_get_next_update_partition(nullptr) ? "true" : "false");
  info->setValue(json);
  auto telemetry = service->createCharacteristic(Telemetry, BLECharacteristic::PROPERTY_READ);
  telemetry->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  telemetry->setCallbacks(&telemetryCallbacks);
  auto logs = service->createCharacteristic(Logs, BLECharacteristic::PROPERTY_READ);
  logs->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  logs->setCallbacks(&logCallbacks);
  publish();
  service->start();
}

void bleUpdateSetEnabled(bool value) {
  // Mode changes are handled by loop after disconnect, never BTC_TASK.
  portENTER_CRITICAL(&connectionMux);
  enabled = value;
  authenticated = bonded = false;
  if (!value) ++connectionGeneration;
  portEXIT_CRITICAL(&connectionMux);
  bleUpdateTakeActivity();
  if (!value) {
    if (receiver) receiver->abort();
    if (outputHeld) updateRestoreOutputs();
    receiver = nullptr;
    outputHeld = false;
    releaseOwner();
    portENTER_CRITICAL(&connectionMux);
    disconnected = connectionChanged = false;
    portEXIT_CRITICAL(&connectionMux);
    overflow = false;
    if (queue) xQueueReset(queue);
    lastResult = UpdateResult::NotReady;
    publish();
  }
}

void bleUpdateConnection(bool value, const uint8_t *identity) {
  portENTER_CRITICAL(&connectionMux);
  connected = enabled && value;
  authenticated = bonded = false;
  ++connectionGeneration;
  if (value && identity) memcpy(currentPeer, identity, sizeof(currentPeer));
  else memset(currentPeer, 0, sizeof(currentPeer));
  connectionChanged = true;
  if (!value) disconnected = true;
  portEXIT_CRITICAL(&connectionMux);
}
bool bleUpdateBusy() { return outputHeld; }
void bleUpdateShutdown() {
  if (receiver) receiver->abort();
  if (outputHeld) updateRestoreOutputs();
  outputHeld = false;
  releaseOwner();
  receiver = nullptr;
  statusCharacteristic = nullptr;
  portENTER_CRITICAL(&connectionMux);
  enabled = authenticated = connected = bonded = false;
  disconnected = connectionChanged = false;
  ++connectionGeneration;
  portEXIT_CRITICAL(&connectionMux);
  if (queue) xQueueReset(queue);
  lastResult = UpdateResult::NotReady;
  publish();
}

void bleUpdatePoll() {
  if (!queue) return;
  const uint32_t now = millis();
  portENTER_CRITICAL(&connectionMux);
  const bool changed = connectionChanged;
  const bool lostLink = disconnected;
  connectionChanged = disconnected = false;
  portEXIT_CRITICAL(&connectionMux);
  if (changed) updateRevokeConsent();
  if (enabled && millis() - lastTelemetryMs >= 250U) refreshTelemetry();
  const auto link = linkSnapshot();
  if (outputHeld && lostLink && !paused) {
    // Keep the sink open, outputs stopped and display awake for the original
    // encrypted/bonded peer. Packets are tagged with a connection generation,
    // so writes queued by a previous connection can never reach the sink.
    paused = true;
    pausedAtMs = now;
    publish();
  }
  if (outputHeld && paused && now - pausedAtMs >= ResumeTimeoutMs) {
    receiver->abort();
    updateRestoreOutputs();
    outputHeld = false;
    releaseOwner();
    lastResult = UpdateResult::NotReady;
    publish();
  }
  if (outputHeld && paused && link.connected && link.authenticated && link.bonded && link.ownerAllowed) {
    paused = false;
    lastPacketMs = now;
    publish();
  }
  if (overflow) { overflow = false; lastResult = UpdateResult::Busy; publish(); }
  if (!enabled) return;
  if (outputHeld && !paused && now - lastPacketMs >= ResumeTimeoutMs) {
    receiver->abort();
    updateRestoreOutputs();
    outputHeld = false;
    releaseOwner();
    lastResult = UpdateResult::NotReady;
    publish();
  }
  Packet packet;
  if (xQueueReceive(queue, &packet, 0) != pdTRUE) return;
  if (!link.authenticated || !link.connected || !link.ownerAllowed || paused ||
      packet.generation != link.generation) return;
  const uint8_t *p = packet.bytes;
  if (p[0] == 7 && packet.size == 1) {
    if (!updateLocalConsent()) lastResult = UpdateResult::Unauthorized;
    else if (outputHeld) lastResult = UpdateResult::Busy;
    else if (!screenAssetRestoreDefault()) lastResult = UpdateResult::StorageError;
    else {
      if (receiver) receiver->abort();
      receiver = nullptr;
      kind = static_cast<UpdateKind>(0);
      lastResult = UpdateResult::Ok;
    }
  } else if (p[0] == 1 && packet.size == 42) {
    UpdateManifest manifest{UpdateKind(p[1]), read32(p + 2), read32(p + 6), {0}};
    memcpy(manifest.sha256, p + 10, 32);
    if (outputHeld) {
      lastResult = receiver && receiver->matches(manifest) ? UpdateResult::Ok : UpdateResult::Busy;
    } else if (!updateLocalConsent() || !link.bonded) {
      lastResult = UpdateResult::Unauthorized;
    } else {
      if (manifest.kind == UpdateKind::Firmware) receiver = &firmwareReceiver;
      else if (manifest.kind == UpdateKind::ScreenAsset) {
        static UpdateReceiver *assetReceiver;
        if (!assetReceiver && screenAssetUpdateSink()) assetReceiver = new UpdateReceiver(*screenAssetUpdateSink());
        receiver = assetReceiver;
      } else receiver = nullptr;
      if (!receiver) { lastResult = UpdateResult::InvalidManifest; publish(); return; }
      outputHeld = updateQuiesceOutputs();
      kind = manifest.kind;
      lastResult = receiver->begin(manifest, link.authenticated, outputHeld);
      if (lastResult == UpdateResult::Ok) {
        portENTER_CRITICAL(&connectionMux);
        memcpy(ownerPeer, link.peer, sizeof(ownerPeer));
        ownerBound = true;
        portEXIT_CRITICAL(&connectionMux);
      }
    }
  } else if (p[0] == 2 && receiver) lastResult = receiveDataFrame(*receiver, p, packet.size);
  else if (packet.size == 5 && receiver) {
    uint32_t session = read32(p + 1);
    if (p[0] == 3) lastResult = receiver->finish(session);
    else if (p[0] == 4) {
      lastResult = receiver->activate(session, link.authenticated);
      publish();
      if (lastResult == UpdateResult::Ok && kind == UpdateKind::Firmware) { delay(250); ESP.restart(); }
    } else if (p[0] == 5) {
      if (session != receiver->status().session) lastResult = UpdateResult::WrongSession;
      else { receiver->abort(); lastResult = UpdateResult::Ok; }
    } else lastResult = UpdateResult::BadChunk;
  } else if (p[0] == 6 && packet.size == 1) lastResult = UpdateResult::Ok;
  else lastResult = UpdateResult::BadChunk;
  lastPacketMs = millis();
  if (outputHeld && receiver && (receiver->status().state == UpdateState::Idle || receiver->status().state == UpdateState::Failed)) {
    updateRestoreOutputs();
    outputHeld = false;
    releaseOwner();
  }
  publish();
}
} // namespace exlink
