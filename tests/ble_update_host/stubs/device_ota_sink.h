#pragma once
#include "device_update.h"
#include "ble_platform.h"
namespace exlink {
extern unsigned testSinkBegins, testSinkWrites, testSinkFinishes, testSinkActivations;
class EspOtaSink : public UpdateSink {
public:
  bool begin(const UpdateManifest &) override { ++testSinkBegins; return true; }
  bool write(const uint8_t *, size_t) override { ++testSinkWrites; return true; }
  UpdateResult finish(const uint8_t *) override { ++testSinkFinishes; return UpdateResult::Ok; }
  bool activate() override { ++testSinkActivations; return true; }
  void abort() override {}
};
}
