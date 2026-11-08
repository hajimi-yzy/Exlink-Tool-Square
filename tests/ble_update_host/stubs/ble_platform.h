#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t amount) { testMillis += amount; }
struct TestEsp { void restart() {} };
extern TestEsp ESP;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

struct TestQueue {
  size_t capacity, packetSize;
  std::deque<std::vector<uint8_t>> packets;
};
using QueueHandle_t = TestQueue *;
constexpr int pdTRUE = 1;
inline QueueHandle_t xQueueCreate(size_t count, size_t size) { return new TestQueue{count, size, {}}; }
inline int xQueueSend(QueueHandle_t queue, const void *packet, int) {
  if (queue->packets.size() >= queue->capacity) return 0;
  const auto *p = static_cast<const uint8_t *>(packet);
  queue->packets.emplace_back(p, p + queue->packetSize);
  return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void *packet, int) {
  if (queue->packets.empty()) return 0;
  memcpy(packet, queue->packets.front().data(), queue->packetSize);
  queue->packets.pop_front();
  return pdTRUE;
}
inline void xQueueReset(QueueHandle_t queue) { queue->packets.clear(); }

class BLECharacteristic;
class BLECharacteristicCallbacks {
public:
  virtual ~BLECharacteristicCallbacks() {}
  virtual void onRead(BLECharacteristic *) {}
  virtual void onWrite(BLECharacteristic *) {}
};
class BLE2902 {};
class BLECharacteristic {
public:
  enum { PROPERTY_WRITE = 1, PROPERTY_READ = 2, PROPERTY_NOTIFY = 4 };
  BLECharacteristicCallbacks *callbacks = nullptr;
  std::string value;
  int permissions = 0;
  unsigned notifications = 0;
  void setAccessPermissions(int p) { permissions = p; }
  void setCallbacks(BLECharacteristicCallbacks *c) { callbacks = c; }
  void addDescriptor(BLE2902 *) {}
  void setValue(const uint8_t *p, size_t count) { value.assign(reinterpret_cast<const char *>(p), count); }
  void setValue(const char *p) { value = p; }
  std::string getValue() { return value; }
  void notify() { ++notifications; }
  void write(const std::vector<uint8_t> &bytes) {
    setValue(bytes.data(), bytes.size());
    callbacks->onWrite(this);
  }
  std::string read() { callbacks->onRead(this); return value; }
};
class BLEUUID { public: explicit BLEUUID(const std::string &) {} };
class BLEService {
public:
  std::map<std::string, BLECharacteristic *> characteristics;
  BLECharacteristic *createCharacteristic(const char *id, int) {
    return characteristics[id] = new BLECharacteristic;
  }
  void start() {}
};
class BLEServer {
public:
  BLEService service;
  BLEService *createService(BLEUUID, int) { return &service; }
};
constexpr uint8_t ESP_LE_AUTH_BOND = 1;
constexpr uint8_t ESP_LE_AUTH_REQ_SC_BOND = 9;
constexpr int ESP_IO_CAP_NONE = 0;
constexpr int ESP_BLE_ENC_KEY_MASK = 1;
constexpr int ESP_BLE_ID_KEY_MASK = 2;
constexpr int ESP_GATT_PERM_READ_ENCRYPTED = 4;
constexpr int ESP_GATT_PERM_WRITE_ENCRYPTED = 8;
constexpr int ESP_MAC_BT = 0;
struct esp_ble_auth_cmpl_t { uint8_t bd_addr[6]; bool success; uint8_t auth_mode; };
class BLESecurityCallbacks {
public:
  virtual ~BLESecurityCallbacks() {}
  virtual uint32_t onPassKeyRequest() = 0;
  virtual void onPassKeyNotify(uint32_t) = 0;
  virtual bool onSecurityRequest() = 0;
  virtual bool onConfirmPIN(uint32_t) = 0;
  virtual void onAuthenticationComplete(esp_ble_auth_cmpl_t) = 0;
};
class BLEDevice {
public:
  static BLESecurityCallbacks *security;
  static void setMTU(int) {}
  static void setSecurityCallbacks(BLESecurityCallbacks *s) { security = s; }
};
class BLESecurity {
public:
  void setAuthenticationMode(int) {}
  void setCapability(int) {}
  void setInitEncryptionKey(int) {}
  void setRespEncryptionKey(int) {}
};
struct esp_app_desc_t { const char *version, *date, *time; };
inline const esp_app_desc_t *esp_ota_get_app_description() {
  static const esp_app_desc_t description{"test", "date", "time"};
  return &description;
}
inline const void *esp_ota_get_next_update_partition(const void *) { return "partition"; }
inline void esp_read_mac(uint8_t *mac, int) { memset(mac, 1, 6); }
