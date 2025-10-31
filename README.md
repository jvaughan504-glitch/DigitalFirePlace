# DigitalFirePlace

Wifi control upgrade for electic fireplace

## Firmware sketch

Source files for the controller firmware live in `src/`. The main sketch `DigitalFirePlace.ino`
initialises the relay, thermistor, four buttons, 14 NeoPixels, and the Adafruit Micro OLED
display. Supporting modules provide configuration constants (`config.h`) and a flame animation
(`fire_animation.cpp`).

## Required Arduino libraries

Install the following Arduino libraries via the Library Manager:

- **Adafruit GFX Library**
- **Adafruit SSD1306** (configured for the 128×64 Micro OLED)
- **Adafruit NeoPixel**

## Build and upload (Arduino IDE)

1. Open the Arduino IDE and select **File → Open...**, then choose `src/DigitalFirePlace.ino`.
2. Select the target board (ESP32 Dev Module recommended) and the appropriate serial port.
3. Ensure the I2C address in `config.h` matches your OLED (default `0x3C`). Adjust pin
   assignments and thermistor calibration constants if your wiring differs.
4. Click **Sketch → Verify/Compile** to build the firmware.
5. Click **Sketch → Upload** to flash the firmware to your board.

## Build and upload (ESP-IDF via Arduino component)

If you prefer ESP-IDF, add this repository as an Arduino component project and build with:

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The sketch relies on the Arduino core for ESP32. Confirm that the Arduino-ESP32 component is
installed and enabled within your ESP-IDF workspace.
