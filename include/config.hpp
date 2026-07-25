/**
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*
CONF_USE_WIFI_MANAGER: If defined, the device will use the WiFi manager
to configure the connection to the WiFi access point.
*/
// #define CONF_USE_WIFI_MANAGER

/*
CONF_USE_WEB_SERVER: If defined, the device will expose an HTTP
server from which one can get the real-time data.
*/
#define CONF_USE_WEB_SERVER

/*
aqicn.org support. Comment out to disable uploads.
*/
#define CONF_AQICN

#ifdef CONF_AQICN
// Get your own token from  https://aqicn.org/data-platform/token/
#define AQICN_TOKEN "dummy-token-for-test-purpose-only"

// aqicn.org places the station on its map from these coordinates. The other
// uploaders ask for the location once, when registering the device.
//
// They are also included in the MQTT and web server payloads, which do not
// require them. Move them out of this block to keep reporting the location
// there while uploads to aqicn.org are disabled.
#define LATITUDE 28.7501
#define LONGITUDE 77.1177
#endif

/*
Sensor.Community support. Before enabling this, register the device at
https://devices.sensor.community/ — see README for instructions.
*/
// #define CONF_SENSOR_COMMUNITY

/*
openSenseMap support. Register the device and its measurements at
https://opensensemap.org/ — see README for instructions.
*/
// #define CONF_OPENSENSEMAP

#ifdef CONF_OPENSENSEMAP
#define OPENSENSEMAP_BOX_ID "your-sensebox-id"
#define OPENSENSEMAP_ACCESS_TOKEN "your-sensebox-access-token"

// Define only the measurements registered for this senseBox.
// #define OPENSENSEMAP_PM1_SENSOR_ID "your-pm1-sensor-id"
// #define OPENSENSEMAP_PM25_SENSOR_ID "your-pm25-sensor-id"
// #define OPENSENSEMAP_PM10_SENSOR_ID "your-pm10-sensor-id"
// #define OPENSENSEMAP_TEMPERATURE_SENSOR_ID "your-temperature-sensor-id"
// #define OPENSENSEMAP_HUMIDITY_SENSOR_ID "your-humidity-sensor-id"
// #define OPENSENSEMAP_CO2_SENSOR_ID "your-co2-sensor-id"
#endif

#ifndef CONF_USE_WIFI_MANAGER
// Only needed if the WiFi manager is not used
#define WIFI_SSID "yourNetworkName"
#define WIFI_PASS "yourNetworkPassword"
#endif

/*
Define a nonempty password to enable authenticated over-the-air firmware
updates. The first OTA-enabled firmware must be flashed over USB; subsequent
builds can be uploaded over WiFi with PlatformIO's release-ota environment.
*/
// #define OTA_PASSWORD "replace-with-a-strong-password"

// Comment to disable MQTT support in code
#define CONF_MQTT

#ifdef CONF_MQTT
#define MQTT_BROKER_URI ""
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""

// Comment to disable Home Assistant support so the device automatically
// appears in Home Assistant (managed by its built-in MQTT integration).
#define CONF_HOME_ASSISTANT

// Only change this if you have set a non-default discovery_prefix in
// Home Assistant's MQTT integration options.
#define HOME_ASSISTANT_DISCOVERY_PREFIX "homeassistant"
#endif

// Set to 2 if you have two PMS sensors connected
#define NUM_PMS_SENSORS 1

// Comment to disable printing of debug values to the serial console
#define DEBUG_SENSOR_VALUES
