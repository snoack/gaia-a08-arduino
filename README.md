# GAIA A08 Firmware

This is an independently maintained fork of the open-source Arduino firmware for
the [GAIA A08](https://aqicn.org/gaia/a08/) air-quality station. It is based on
[aqicn/gaia-a08-arduino](https://github.com/aqicn/gaia-a08-arduino).

[![PlatformIO CI](https://github.com/snoack/gaia-a08-arduino/actions/workflows/compile-platformio.yml/badge.svg)](https://github.com/snoack/gaia-a08-arduino/actions/workflows/compile-platformio.yml)

## Configuration

To start with, you need to update the `include/config.hpp` file with your
aqicn.org Data-Platform token and the station location:

```C
#define AQICN_TOKEN "dummy-token-for-test-purpose-only"

#define LATITUDE 48.756080
#define LONGITUDE 2.302038
```

You can get your own token from https://aqicn.org/data-platform/token/

Uploads to aqicn.org are enabled by default. To disable them,
comment out `CONF_AQICN`; the token and coordinates aren't needed then.

By default, the device will use the
[Wi-Fi Manager](https://github.com/tzapu/WiFiManager) to connect to your Wi-Fi
access point. If you want to configure a static Wi-Fi access point instead,
comment out `CONF_USE_WIFI_MANAGER` and define `WIFI_SSID` and `WIFI_PASS`.

```C
//#define CONF_USE_WIFI_MANAGER
#define WIFI_SSID "yourNetworkName" 
#define WIFI_PASS "yourNetworkPassword"
```

### Sensor.Community

Optionally, the firmware can publish air-quality measurements to
[Sensor.Community](https://sensor.community/). Create an account at
https://devices.sensor.community/ and register a new sensor. Complete the
device fields as follows:

- **Sensor ID:** Enter the MAC address without separators, exactly as reported
  by the GAIA web interface or printed after `esp32-` in the boot log.
- **Sensor Board:** Select `esp32`.
- Under **Hardware configuration**, set the first **Sensor Type** to `PMS5003`.
- Set the second **Sensor Type** to `AHT20`, or `HTU21D` if AHT20 is not available.
- If the optional CO2 sensor is present, use **add component** and select
  `SCD4x`, or `SCD30` if SCD4x is not available.

Finally, enable `CONF_SENSOR_COMMUNITY` in `include/config.hpp`.

### openSenseMap

Optionally, the firmware can publish measurements to
[openSenseMap](https://opensensemap.org/). Create an account, register a new
senseBox, and add a sensor for every measurement you want to publish. Copy the
senseBox ID, access token, and the individual sensor IDs into the corresponding
`OPENSENSEMAP_*` settings in `include/config.hpp`, then enable
`CONF_OPENSENSEMAP`. All sensor ID settings are optional; leave a setting
undefined if that measurement was not registered.

### Home Assistant

With `CONF_MQTT` and `CONF_HOME_ASSISTANT` enabled, the device announces itself to
[Home Assistant](https://www.home-assistant.io/) over MQTT and its sensors show
up automatically. The RGB LED is also exposed as a light entity you can turn on
and off, set to a color, or switch back to following the air quality via its
**AQI** effect. The setting is remembered across reboots.

## Libraries

The firmware uses the following libraries:

| Used library                     | Version | Comment                   |
| -------------------------------- | ------- | ------------------------- |
| fu-hsi/PMS Library               | ^1.1.0  | PMS5003 driver            |
| sensirion/Sensirion I2C SCD4x    | ^0.4.0  | CO2 sensor driver         |
| bblanchon/ArduinoJson            | ^7.1.0  |                           |
| dvarrel/AHT20                    | ^1.0.0  | Temperature sensor driver |
| adafruit/Adafruit NeoPixel       | ^1.15.1 | RGB LED driver            |
| github.com/tzapu/WiFiManager.git | 2.0.17  |                           |
| ArduinoOTA                       | 2.0.0   | ESP32 framework built-in; optional OTA updates |

## Compilation

### Platform IO

If you use platform IO, you can just use `make compile-platformio` to compile,
upload, and start the monitor. Note that, by default, the `CONF_USE_WEB_SERVER`
configuration flag is enabled by default for Platform IO.

### Over-the-air updates

To enable authenticated OTA updates, define a password in `include/config.hpp`
and replace theplaceholder:

```C
#define OTA_PASSWORD "replace-with-a-strong-password"
```

The first OTA-enabled firmware must be installed over USB with the normal
`release` environment. Once that firmware is running and connected to Wi-Fi,
subsequent builds can be uploaded over the network:

```sh
pio run -e release-ota -t upload --upload-port GAIA-A08-xxxx.local
```

If mDNS name resolution is unavailable, use the device's IP address instead.

### Arduino CLI/IDE

This branch does not support Arduino IDE and Arduinno CLI. If you need to
compilation using the Arduino tools, use the `arduino` branch:
https://github.com/aqicn/gaia-a08-arduino/tree/arduino

## Running

Once the sensor is running with the firmware, and `CONF_AQICN` is enabled, you
can check your station Data from https://aqicn.org/data-feed/verification/.

To see you station, you first need to enter the token you previously got from
the aqicn.org data-platform.

## Gaia A08 - HW GPIO mapping

![GAIA A08 GPIO mapping](doc/gaia-a08-gpio.png "GAIA A08 GPIO")

```C
#define GPIO_RGB_LED    1
#define GPIO_5V_PWR_EN  2

#define GPIO_PMS1_RX    4
#define GPIO_PMS1_EN    5
#define GPIO_PMS2_EN    6
#define GPIO_PMS2_RX    7

#define GPIO_SDA        8
#define GPIO_SCL        9

#define GPIO_GREEN_LED  10
```

## FAQ

- After flashing my custom firmware to the GAIA A08, can I revert back to stock
  firmware? Yes, you can flash back the stock firmware from this page:
  https://firmware.aqicn.org/gaia/updater/#/en/a08
