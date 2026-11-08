#pragma once

#include "device_update.h"
#include "exa_format.h"
#include <stdint.h>

namespace exlink {

// The app sends EXA1 media directly over the encrypted BLE update service.
// These helpers keep the active resource in flash and decode one frame using
// the existing four-row display transfer buffer.
UpdateSink *screenAssetUpdateSink();
bool screenAssetRestoreDefault();
bool screenAssetAvailable();
uint32_t screenAssetDurationMs();
uint16_t screenAssetFrameCount();
uint16_t screenAssetFrameForElapsed(uint32_t elapsedMs);
bool screenAssetDecodeFrameRows(uint16_t frameIndex, uint16_t *row, size_t capacity,
                               exa::RowCallback callback, void *context);

} // namespace exlink
