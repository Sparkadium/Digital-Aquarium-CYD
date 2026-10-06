# Glowtank for the Cheap Yellow Display

**Version 1.3 — rendering performance pass.** Version 1.2 now works on the user's CYD, at roughly 11–12 FPS. This update speeds up lossless background decoding, pixel shading and display byte conversion while preserving the native resolution, geometry, colors and controls. The working display initialization and backlight setup are retained. The startup message identifies this build as `CYD 1.3 render optimization`. **30 FPS is a target, not a measured result for this release.**

Port for the **ESP32-2432S028R**, with the original ESP32, **ILI9341 240 × 320 display** and XPT2046 resistive touchscreen. All five modes run in **240 × 320 portrait** and **320 × 240 landscape**: Realistic, Fantasy, Comb Jellies, Abyss and Pokémon.

Seven Pokémon are included: Tentacool, Chinchou, Magikarp, Horsea, Goldeen, Wooper and Staryu. Their geometry, depth sorting, smooth turns and feeding behavior are retained. Plants and scenery use mathematical geometry. There are no image assets, generated-art textures or video tools.

## Install the precompiled firmware

The ready-to-flash file is **`firmware/Glowtank-CYD-full.bin`**. It includes the bootloader, partition table and application for a 4 MB ESP32. Flash the complete file at **address `0x0`**.

With Python and esptool 5 installed, run this from the extracted project folder, replacing `COM5` with your CYD's serial port:

```sh
python -m esptool --chip esp32 --port COM5 --baud 460800 write-flash 0x0 firmware/Glowtank-CYD-full.bin
```

On macOS/Linux, use `python3` and your `/dev/cu.*` or `/dev/ttyUSB*` port. If the board does not connect automatically, hold BOOT, press and release RESET, and release BOOT when the uploader connects. Press RESET after flashing if it does not restart automatically. Use a USB data cable.

A full-image flash replaces the existing firmware and clears saved settings. A fresh installation starts in portrait Pokémon mode. **This binary is for the original ILI9341 ESP32-2432S028R CYD.** A board revision with an ST7789 display needs a different panel driver. It is not the Waveshare firmware.

`firmware/Glowtank-CYD-app.bin` is also included for an existing installation using the same default partition layout; its application address is **`0x10000`**. Use the full image for a first installation. Checksums are in `firmware/SHA256SUMS.txt`.

## Trying the faster display clock

Start with the normal sketch or `Glowtank-CYD-full.bin`: all rendering optimizations are enabled at the existing **40 MHz** display clock. Let the aquarium run for ten seconds, then inspect the `fps ... | ms: ...` line in Serial Monitor at 115200 baud.

The optional **`firmware/Glowtank-CYD-fast-full.bin`** uses **80 MHz display SPI**. It sends the same pixels, with a minimum transfer time of 15.36 ms instead of 30.72 ms. That leaves more room for rendering within a 33.3 ms / 30 FPS frame. Flash this complete image at **0x0**, just like the normal full image. A matching `Glowtank-CYD-fast-app.bin` is included for application-only updates at **0x10000**.

For an Arduino source upload, change the default `LCD_SPI_HZ` value in `board_config.h` from `40000000UL` to `80000000UL` and upload the entire sketch. This setting is separate from the board's flash-memory frequency.

80 MHz operation depends on the particular display. If it produces corruption or a blank screen, restore the 40 MHz build/value; that build retains all the CPU optimizations. Neither the faster build's physical compatibility nor a sustained 30 FPS rate has been verified here. The slower 20 MHz compatibility build remains included for display troubleshooting.

## If the screen is blank

After RESET, the screen should show **A: PANEL** color bars for 1.5 seconds, then **B: DMA FRAME** color bars for 1.5 seconds, then the aquarium. The first check draws directly through the panel library; the second sends the same framebuffer banks used by the aquarium. The second check follows your saved orientation. Serial output announces each check, and the first aquarium frame reports its nonzero pixel count.

If the default build is still blank, flash **`firmware/Glowtank-CYD-safe-full.bin`** at **`0x0`**. This compatibility build runs all five tanks using the standard Arduino SPI driver at **20 MHz**, with no custom DMA. Its second check says **B: SPI FRAME**. It is intended to isolate the display problem and will be slower. A matching `Glowtank-CYD-safe-app.bin` is also included for application-only updates at `0x10000`.

| Observation | What to report / try next |
|---|---|
| A appears, B does not | Try the compatibility build; report which checks appear with each build |
| Both checks appear, aquarium does not | Send the `Aquarium frame:` line and the first startup log |
| Neither check appears in either build | Report whether the backlight lights up; send a photo of the board's back and its exact printed model |

Neither a successful SPI transfer nor a nonzero pixel count confirms that the physical display accepted the pixels. The tests distinguish likely causes; they cannot identify a different panel controller from a serial log alone. Both included builds target **ILI9341**, not ST7789.

Once the display is working, set `LCD_STARTUP_CHECK_MS` to `0` in `board_config.h` and rebuild to skip the color bars. Full brightness is the default (`BACKLIGHT_LEVEL=255`); a lower value enables PWM dimming with a checked full-brightness fallback.

## Controls

| Input | Action |
|---|---|
| Tap the touchscreen anywhere | Drop one pinch of food from the surface |
| Tap BOOT; release before 0.55 seconds | Feed |
| Hold BOOT 0.55–2 seconds, then release | Drop a wafer; normal sinking food in Abyss and Pokémon |
| Hold BOOT 2–5 seconds, then release | Next tank |
| Hold BOOT for 5 seconds | Toggle portrait/landscape once |
| RESET | Restart the device |

The RGB LED flashes purple at two seconds: release to change tanks, or keep holding until five seconds to rotate. Rotation flashes cyan and does not also feed or change tanks. Release before starting another gesture.

Mode order: **Realistic → Fantasy → Comb Jellies → Abyss → Pokémon**. Both the selected tank and orientation are remembered across power cycles. A rotation preserves the inhabitants, hunger, animation state, remaining food and feeding totals. A reset restarts the simulation in the remembered mode and orientation.

Touch feeding needs no calibration because it does not use contact coordinates; the food position is chosen at the water surface. Holding a finger down produces only one pinch. No additional wiring is required for these onboard controls.

## Build and upload the source

1. Keep `Glowtank/Glowtank.ino` and **all accompanying headers** in the `Glowtank` folder.
2. Install **esp32 by Espressif Systems 3.3.0** in Arduino Boards Manager.
3. Install **GFX Library for Arduino 1.5.9** in Library Manager. No separate touch library is needed.
4. Select **ESP32 Dev Module**, CPU **240 MHz**, flash size **4 MB**, flash mode **DIO**, flash frequency **80 MHz**, default 4 MB partitions, **PSRAM disabled**.
5. Open `Glowtank/Glowtank.ino`, select the CYD's serial port, and upload. Ordinary sketch uploads preserve saved mode/orientation.

The target is the original ESP32, not ESP32-C6. The built-in USB-to-serial connection does not need a USB CDC setting.

Arduino CLI equivalent:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 Glowtank
arduino-cli upload --fqbn esp32:esp32:esp32 --port COM5 Glowtank
```

## Performance

Rendering is uncapped. The default display SPI clock is **40 MHz**, separately from the 80 MHz flash-memory clock. Native resolution is retained; animal shapes are not stretched. The larger habitat uses the extra screen area.

The renderer uses sixteen small DMA framebuffer allocations, a lossless compressed backdrop, and mathematical glow tables stored in flash. Version 1.3 reads each compressed row once into a small working buffer, then expands its palette locally. It removes repeated per-pixel bank lookups and uses paired pixel stores where possible. The ESP32 byte-conversion loop now uses register operations instead of 38,400 ROM helper calls per frame. Frequently used pixel shading helpers are inlined. No visual effects or animals are removed.

Backdrop memory is allocated in **2 KiB blocks** so fragmented free memory can be used. A failed compression fit is attempted once per scene instead of every frame. Bluetooth memory is returned to the heap because this is an offline aquarium. No PSRAM, SD card, Wi-Fi or network connection is required. Simulation and caustic preparation run while display DMA sends the previous frame.

The earlier device log reports **11.9–12.2 FPS**, `cache on 68344/77824`, one cache build, and stable heap through 16 seconds of uptime. The user subsequently confirmed that version 1.2 displays correctly at around the same frame rate. **Version 1.3 has not been measured on a physical CYD here.** A full frame alone takes at least 30.72 ms to transfer at 40 MHz, so 30 FPS cannot be promised with this pipeline. Serial Monitor at **115200** reports measured FPS, separate `sim`/`bg`/`pack`/`spi` timings, worst frame time, cache use, free heap, uptime and minimum free task stack every two seconds. Compare `base`, `glow`, `front`, `pack` and `spi` with the previous log to see the actual gains. There is no timing overlay on the tank by default.

## Checking the fix and any remaining reboot

On startup, check for `CYD 1.3 render optimization` and a `cache allocated` line. During Pokémon mode, the status should say **`cache on`**, and `cache builds` should stop increasing while the mode and orientation stay unchanged. If the available memory is still too fragmented, the fallback continues drawing but no longer spends every frame attempting compression.

Startup now prints **`reset: ...`**. If it still reboots, capture that line and any panic/backtrace immediately before it. `BROWNOUT` indicates a supply-voltage reset, `TASK_WDT`/`INT_WDT` indicates a watchdog, and `PANIC` indicates a software panic. The original FPS excerpt did not contain a reset reason, so the reboot cause is not yet confirmed. Background builds and the main loop now give system tasks time to run; watchdog protections remain enabled.

Keep the display at the supplied clock while diagnosing the blank screen. To reproduce the compatibility build from source, set `LCD_DMA` to `0` near the top of the sketch and `LCD_SPI_HZ` to `20000000UL` in `board_config.h`. To disable touch feeding, set `CYD_TOUCH_FEED` to `0`.

`PERFORMANCE.md` gives the build and test results. `previews/` contains native renders from the actual C++ code. The render tests and glow-table generator are in `extras/`.

## Hardware references

- [CYD pin mapping](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md)
- [Original CYD display configuration](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/DisplayConfig/User_Setup.h)
- [Arduino GFX](https://github.com/moononournation/Arduino_GFX)
- [Adafruit ILI9341 initialization reference](https://github.com/adafruit/Adafruit_ILI9341/blob/master/Adafruit_ILI9341.cpp)
- [ESP32 heap and memory capabilities](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html)
- [ESP32 Bluetooth memory release](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/bluetooth/controller_vhci.html)

| Function | GPIO |
|---|---|
| TFT SCK / MOSI / MISO | 14 / 13 / 12 |
| TFT CS / DC / backlight | 15 / 2 / 21 |
| BOOT | 0 |
| Touch SCK / MOSI / MISO | 25 / 32 / 39 |
| Touch CS / IRQ | 33 / 36 |
| RGB LED red / green / blue, active low | 4 / 16 / 17 |

LCD DMA uses HSPI/SPI2; touch initialization uses the separate VSPI/SPI3 controller. TFT reset is not assigned to a separate GPIO. RESET retains its hardware function.
