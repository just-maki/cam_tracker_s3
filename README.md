# cam_tracker_s3

Camera pipeline measurements and a starting point for an onboard object tracker on an ESP32-S3 camera board.

**Status: work in progress.** The firmware currently opens the camera, pulls raw frames, extracts a grayscale image, and logs the frame rate it actually achieves. Object detection and control are planned, not implemented.

## Hardware

- Waveshare ESP32-S3-CAM-OV3660 board (ESP32-S3R8: 8 MB octal PSRAM in the package, 16 MB external quad flash)
- OV3660 camera module on the board's 24-pin DVP connector
- USB-C for power, flashing, and the serial monitor

## Results so far

Measured on my board with the timing loop in `main/main.c` (100-frame phases):

| Camera mode (YUV422) | Driver's nominal rate | Measured rate |
|---|---|---|
| 640x480 (driver default) | 10 fps | 6.7 fps |
| 240x240 | 24 fps | 15.0 fps |

- The 15.0 fps figure was identical across repeated runs, and adding the grayscale step did not change it.
- Grayscale extraction at 240x240 (every second pixel and row, giving a 120x120 image) takes about 1.14 ms per frame, roughly 2% of the 66.7 ms frame interval.
- Not yet tested: behaviour across different lighting (exposure may change the frame rate), and end-to-end latency.

## What I found about the camera

- **Frame layout:** the frames are packed 4:2:2 (YUYV-style), 2 bytes per pixel, with the brightness byte at every even offset. I checked this by rendering a captured 640x480 frame both ways. The driver logs the format as `422P`, which does not match the bytes. I have not repeated the visual check in 240x240 mode.
- **240x240 mode:** by my reading of the sensor's register table it keeps the full sensor height and roughly three quarters of the width, then scales to a square. I read the register values against the usual OmniVision layout from memory and have not verified this with a picture.
- **Camera pins** (from the vendor example's configuration):

| Signal | GPIO |
|---|---|
| SCCB SCL / SDA | 7 / 8 |
| XCLK (20 MHz) | 38 |
| PCLK | 41 |
| VSYNC | 17 |
| DE (line valid) | 18 |
| D0 to D7 | 45, 47, 48, 46, 42, 40, 39, 21 |
| Reset / power-down | not driven (-1) |

## Toolchain

- **ESP-IDF v5.5.1.** ESP-IDF v6.1 did not build this project: `espressif/esp_cam_sensor` 1.7.0 (pulled in by the vendor example's dependencies) failed to compile against v6.1's SPI driver internals.
- Component versions in my working build: `esp_cam_sensor` 1.7.0, `esp_video` 1.4.1. They are pinned by `dependencies.lock`.

## Build and flash

Install ESP-IDF v5.5.1 (for example with Espressif's installer, EIM), activate it in your terminal, then:

```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

`set-target` generates `sdkconfig` from `sdkconfig.defaults`, which holds the board-specific settings (custom partition table, octal PSRAM, 16 MB flash, camera pins, and the 240x240 camera mode). The first build downloads the managed components and needs internet access. On macOS the serial port looks like `/dev/cu.usbmodem*` and its number can change after a reset.

The monitor prints a `[capture only]` line, then repeating `[capture + gray ...]` lines. The label on the second one still says "320x240" from an earlier version; at the 240x240 camera mode the output is actually 120x120.

## Repository layout and credits

- `main/main.c`: the measurement firmware (my code; the vendor's original `main.c` was a web server and is not included here)
- `components/example_video_common/`: board bring-up code from the vendor example, unmodified
- `frontend/`: the vendor example's web UI. This firmware does not use it, but `main/CMakeLists.txt` still embeds its files, so it stays for now.
- `sdkconfig.defaults`: board settings recorded with `idf.py save-defconfig`. Wi-Fi credentials were removed and Wi-Fi is not used.

This project started from Waveshare's ESP32-S3-CAM-OVxxxx ESP-IDF example `01_simple_video_server` (https://github.com/waveshareteam/ESP32-S3-CAM-OVxxxx), which is Apache-2.0 licensed. Files copied from it keep their original headers and license terms. My own code (`main/main.c`, this README) is licensed under the PolyForm Noncommercial License 1.0.0 (see `LICENSE`): free to use for noncommercial purposes, not for commercial use. Files taken from Waveshare and Espressif keep their own licenses, see `NOTICE`.

## Notes

- `dependencies.lock` is committed so builds resolve the same component versions. It was generated with ESP-IDF v5.5.1; other ESP-IDF versions may fail (v6.1 does).
- The camera flex cable must be fully seated. When it was not, every sensor probe failed with I2C NACK errors at boot.
- Expected boot noise: several `failed to detect DVP camera` errors appear while the driver tries each supported sensor before it finds the OV3660.

## Planned

- Detect a brightly coloured target in the low-resolution grayscale or colour data
- Split capture and detection into FreeRTOS tasks connected by a queue
