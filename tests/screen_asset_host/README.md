# Host-only transaction simulation

The test includes the real unchanged `src/screen_asset.cpp` in its translation unit to access the RAM-cache invalidator for reboot simulation. Real `src/exa_format.cpp` is compiled and linked. Fake headers model two flash slots and NVS writes, including commit failures. The deterministic digest stub is **not SHA-256** and tests only transaction decisions/hash mismatch plumbing, not cryptographic correctness, ESP flash/NVS power-loss atomicity or actual display/DMA hardware.

```sh
zig c++ -std=c++11 -Wall -Wextra -Werror -I tests/screen_asset_host -I include tests/screen_asset_host/test.cpp src/exa_format.cpp -o screen_asset_host_test
./screen_asset_host_test
```

Tests cover inactive-slot writes; FINISH without ACTIVATE; wrong hash and invalid EXA rejection; previous/active NVS write failures; fresh-boot fallback from corrupt active to previous; explicit built-in restore across reboot; preservation of last upload while built-in is selected; 480-row decode into a guarded four-row staging buffer and guarded one-row scratch.
