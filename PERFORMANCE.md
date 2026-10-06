# CYD port: build and verification

## Version 1.3: performance without changing the visuals

The user confirmed that version 1.2 displays correctly, but runs at roughly 11–12 FPS. The earlier detailed Pokémon log spent about 28 ms restoring the background, 10 ms drawing creatures/glows, 6 ms on foreground geometry, 6 ms packing pixels and 31 ms sending them. At 40 MHz, a full 240×320 RGB565 frame alone occupies 30.72 ms of the SPI bus; this sequential full-frame pipeline therefore cannot deliver sustained 30 FPS merely by eliminating a few drawing operations.

Confirmed costs in the version 1.2 ESP32 disassembly:

- Palette decoding called the bank-address helper for each packed index and palette lookup, with volatile IRAM loads and memory barriers at pixel frequency.
- Byte packing made 38,400 calls to the ROM byte-swap helper per frame.
- Small pixel shading functions remained out-of-line under the size-oriented compiler setting.

Version 1.3 copies each encoded row once using aligned 32-bit IRAM-safe accesses, then decodes from local DRAM. Palette widths 1–8 have specialized expansion paths; suitable paths write two RGB565 pixels per aligned store. Pixel shading helpers are inlined. On Xtensa, panel byte ordering uses five register shift/mask/OR instructions instead of the ROM call. The ESP32 disassembly confirms both the removal of per-pixel bank helper calls and the call-free conversion loop. No additional persistent framebuffer, resolution reduction, palette quantization or visual effect removal is involved. The decode call chain temporarily uses 704 + 560 bytes of stack; the source's largest single-function frame remains 1,344 bytes. Runtime stack margin should still be checked in the device log.

All ten mode/orientation previews match version 1.2 byte-for-byte. The full native renderer suite passed AddressSanitizer and UndefinedBehaviorSanitizer again. A dedicated decoder test exercises every palette bit width, both screen widths, partial final groups, 116 encoded rows crossing allocation-bank boundaries, and output-buffer guards. The fragmented allocation/reserve test also passed again. These are correctness checks and inspection of target code, **not measured ESP32 FPS**.

The default build retains 40 MHz display SPI. An optional 80 MHz build halves the theoretical full-frame transfer time to 15.36 ms; this gives the optimized renderer a plausible route toward 30 FPS, subject to actual rendering time and the panel tolerating the higher clock. The 20 MHz standard-SPI compatibility build is also retained. No version 1.3 frame-rate claim is made before a physical retest.

## Version 1.2: blank-screen investigation

The follow-up device log reports 11.9–12.2 FPS, `cache on 68344/77824 bytes`, one cache build, stable reported heap of 16,412 bytes, and uptime increasing from 8 to 16 seconds. The screen was still blank. These observations confirm that the background-cache problem improved; they do not prove that any display transfer reached a working panel or that longer-term reset behavior is resolved.

The installed Arduino GFX 1.5.9 ILI9341 initialization table issues Sleep Out and Display On back-to-back, with no intervening wake delay. The panel wrapper now waits 150 ms after that initialization, reissues Display On, and waits another 20 ms before the library applies rotation/address-window settings. This corrects an initialization omission; whether it explains this particular blank screen requires a device test.

The backlight defaults to GPIO21 held HIGH rather than an unchecked PWM attachment. Choosing a lower brightness still enables PWM; its return values are now checked, with full-on GPIO as the fallback.

Before starting the aquarium, check A draws color bars through ordinary library drawing; check B sends color bars through the aquarium's sixteen frame banks. Each remains visible for 1.5 seconds. The first complete aquarium frame also reports its nonzero pixel count. No extra framebuffer is allocated. The diagnostic bars are replaced by the aquarium, and `LCD_STARTUP_CHECK_MS=0` skips them.

Two complete firmware builds are included: the default 40 MHz custom-DMA path, and a compatibility build using standard Arduino SPI at 20 MHz. Both retain all five modes and both orientations. The compatibility build is a diagnostic alternative, not a performance improvement. Neither version 1.2 build has been physically tested here.

## Version 1.1: fix based on the first device log

The original device report showed **2.9 FPS**, `sim 270.0 ms`, `cache fallback 0/49152 bytes`, and 47,792 bytes of free heap. The `sim` field included background generation/compression, so it did not represent creature simulation alone. The cache was too small for the Pokémon backdrop and retried the failed full compression every frame. That explains the repeated large background cost; the excerpt did not identify the cause of the reported reboot.

This update:

- Replaces 16 KiB cache allocation requests with **2 KiB blocks**, preserving at least 16 KiB of byte-addressable internal RAM before allocator overhead.
- Attempts a failed cache fit once per scene. Mode changes and rotations allow another attempt; ordinary fallback frames only redraw the background.
- Uses aligned, alias-safe 32-bit pixel-pair accesses for display byte packing instead of small repeated memory copies.
- Gives system tasks a scheduling opportunity during long background builds and once per completed frame. Watchdogs are not disabled or extended.
- Prints the release version, ESP-IDF version, CPU frequency, reset reason, cache allocation split, largest free DRAM block, uptime and minimum free task stack. Background and packing costs now have separate timing fields.

A fragmented-heap test reproduces the original **49,152-byte cache / 47,792-byte combined free heap** outcome with the old allocation policy. The test accounts for `ESP.getFreeHeap()` including instruction RAM: only 32,432 bytes of its reported free memory are byte-addressable DRAM. Under that same fixture, the new allocation policy obtains a **77,824-byte cache**, enough for the tested backgrounds, while preserving the DRAM reserve. This is a controlled native test, not a measurement of the user's physical heap layout. Separate tests force a 48 KiB cache and verify that failed compression is not retried every frame.

## Build

Built for `esp32:esp32:esp32`, Arduino ESP32 core **3.3.0**, GFX Library for Arduino **1.5.9**, default 4 MB partition layout. CPU 240 MHz; flash DIO/80 MHz; TFT SPI 40 MHz; no PSRAM. Build date: 2026-10-03.

| Compiler result | Default DMA | Compatibility SPI |
|---|---:|---:|
| Program storage, bytes / 1,310,720 | 527,931 | 505,047 |
| Static global data, bytes / 327,680 | 97,352 | 97,144 |
| Compile-time remainder, bytes | 230,328 | 230,536 |
| RGB565 framebuffer, dynamically allocated, bytes | 153,600 | 153,600 |
| Requested compressed-backdrop capacity, bytes | Up to 81,920 | Up to 81,920 |
| Mathematical glow tables in flash, bytes | 21,832 | 21,832 |

The compiler's remainder is **not** the final available heap. Runtime libraries, framebuffer and any DRAM-backed cache banks also consume memory. Startup and periodic serial output report the actual device heap. Version 1.1 allocated a 77,824-byte backdrop cache on the user's device; version 1.3 allocation remains to be checked. The compatibility build uses the same core/library versions and board settings, with `-DLCD_DMA=0 -DLCD_SPI_HZ=20000000UL` supplied to the C++ compiler. The fast build uses `-DLCD_SPI_HZ=80000000UL` and has the same program/static-data sizes as the default build.

## Memory and display pipeline

- Sixteen independent, word-aligned **9,600-byte** DMA allocations hold all 76,800 native pixels. Each bank contains 20 portrait rows or 15 landscape rows, so scanlines never cross an allocation boundary.
- The backdrop cache chooses exact RGB565 palettes or repeating four-pixel runs per row. No color quantization or resolution reduction is used.
- Cache banks preferentially use otherwise available instruction RAM, accessed exclusively as aligned, volatile 32-bit words. Spare internal RAM may supply additional 2 KiB banks while preserving a heap reserve. If a scene cannot fit, background rendering falls back to recomputing it without repeatedly retrying compression.
- Immutable glow tables live in flash. `extras/generate_kernels.cpp` reproduces them from the original equations; its output was checked byte-for-byte against the included header.
- All framebuffer rendering and background-cache updates finish **before** DMA starts. Only simulation and caustic preparation overlap DMA. The queued buffers remain untouched until all sixteen transfers finish.
- Rotation uses ILI9341 address traversal and updates habitat layout. No second framebuffer, image transpose, or rendering-time heap allocation is required. A mode change or rotation rebuilds the backdrop.
- Wi-Fi and Bluetooth remain unused; Bluetooth's reserved memory is released during startup.

At 40 MHz, sending 153,600 bytes requires **30.72 ms** before protocol/transaction overhead. The theoretical transfer-only ceiling is **32.55 FPS**; rendering adds time. At 80 MHz those transfer-only figures become 15.36 ms and 65.10 FPS, but operation at that display clock is board-dependent. Neither figure is a measured aquarium frame rate.

Native desktop timing is not reported as ESP32 timing. Use the device's 115200-baud serial statistics to measure the actual tanks. Optional full-frame bloom and trails remain off in the shipping build; the existing creature glows remain enabled.

## Completed verification

For version 1.3, all three ESP32 target builds compiled and linked. All application images pass esptool checksum/hash validation and identify chip ID 0 (original ESP32). Each complete 4 MB image was checked against its bootloader at 0x1000, partition table at 0x8000 and application at 0x10000. The complete renderer test and sanitizer run were repeated, including byte-for-byte comparison of all ten previews against version 1.2. The new decoder and allocation tests passed. The panel driver and controls are unchanged from the working version 1.2; the earlier display and gesture checks remain applicable.

- ESP32 target compilation and linkage succeeded for 40 MHz DMA, 80 MHz DMA and 20 MHz standard SPI.
- All ten mode/orientation combinations ran through 400 fully rendered warm-up frames, then 7,000 variable-timestep simulation steps with repeated feeds and periodic rendering.
- Twenty live rotation round trips preserved simulation state during feeding.
- Every mode consumed food in both orientations; all seven Pokémon ate in each orientation. Geometry stayed within native bounds and Pokémon yaw changes remained smooth.
- Lossless-cache round trips matched every RGB565 pixel. Codec edge cases, row boundaries, background fallback, framebuffer bank boundaries, byte/color packing and DMA isolation passed.
- The full native suite also passed with cache capacity limited to 64 KiB, with zero fallback frames in its ten mode/orientation runs. This checks the smaller-cache path if the device cannot allocate the full requested cache.
- AddressSanitizer and UndefinedBehaviorSanitizer passed the full renderer suite. The wider-screen vignette capacity was corrected after the sanitizer exposed an inherited size limit.
- Optional bloom and trails passed a separate sanitizer check across all ten layouts. These optional features were not used in the shipped hardware build.
- BOOT tests covered debounce, all hold thresholds, one action per press, startup holds, delayed polling and timer wraparound.
- Touch tests covered bounce, one feed per contact, release/rearm, startup contact and timer wraparound.
- Mocked DMA-driver tests covered the CYD pins, SPI mode 0, sixteen distinct buffers, the complete 153,600-byte transfer, byte order, partial queue failures, invalid buffers, alignment and cleanup.
- Target compiler stack-usage output was inspected; the largest reported single source function frame was 1,344 bytes. This is not a runtime stack high-water measurement.

Detailed results are in `extras/*checks.txt` and `extras/cyd-sanitizers.txt`. The `test_cache_alloc.cpp` and `test_cache_fallback.cpp` tests also passed AddressSanitizer/UndefinedBehaviorSanitizer for version 1.1; allocation and the new decoder test were rerun for version 1.3. Previews are still frames rendered from the C++ source, not device photographs. Sustained FPS, the 80 MHz display path and longer-term reset behavior of version 1.3 require a physical device retest.

## Reproduce native checks

Run from the extracted project root with a C++11-capable `g++`:

```sh
g++ -O2 -std=c++11 extras/test_cyd.cpp -o /tmp/test_cyd
/tmp/test_cyd
g++ -O2 -std=c++11 extras/test_boot.cpp -o /tmp/test_boot
/tmp/test_boot
g++ -O2 -std=c++11 extras/test_touch.cpp -o /tmp/test_touch
/tmp/test_touch
g++ -O2 -std=c++11 -I extras/fakes extras/test_dma.cpp -o /tmp/test_dma
/tmp/test_dma
g++ -O1 -g -std=c++11 -fsanitize=address,undefined -fno-omit-frame-pointer extras/test_cyd.cpp -o /tmp/test_cyd_sanitized
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/test_cyd_sanitized
```

Leak detection is disabled in the sanitizer command because it is unavailable in the validation container. Address and undefined-behavior checks remain enabled. `test_cyd` accepts an optional existing directory argument to export native PPM previews. For the optional effects check, compile `test_optional.cpp` with `-DGT_BLOOM=1 -DGT_TRAILS=1` and the same sanitizer flags.
