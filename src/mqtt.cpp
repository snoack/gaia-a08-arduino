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

#include "sensors.hpp"
#include "main.hpp"

#ifdef CONF_MQTT

#include <algorithm>
#include <WiFi.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "indicator.hpp"

esp_mqtt_client_handle_t client;

// Per-node topics, keyed off the device MAC so any number of GAIA nodes can
// coexist on the same broker without collisions. Built once in mqttInit().
static char dataTopic[48];         // GAIA/<mac>/data       (sensor state)
static char statusTopic[48];       // GAIA/<mac>/status     (device availability + LWT)
#ifdef CONF_HOME_ASSISTANT
static char lightStateTopic[48];   // GAIA/<mac>/light      (light state echo)
static char lightCommandTopic[52]; // GAIA/<mac>/light/set  (commands from HA)

// The effects Home Assistant offers for the light. "AQI" uses category colors,
// "AQI (continuous)" interpolates the reading, and "Solid" is user-selected.
// The rainbow shown at boot is internal and not exposed here.
static constexpr char HA_EFFECT_AQI[] = "AQI";
static constexpr char HA_EFFECT_AQI_CONTINUOUS[] = "AQI (continuous)";
static constexpr char HA_EFFECT_SOLID[] = "Solid";

// Fill in the availability and device blocks shared by every discovery config,
// so all of this node's entities group under one Home Assistant device.
static void haDevice(JsonDocument &cfg)
{
    cfg["avty"][0]["t"] = statusTopic;

    char buf[24];
    cfg["dev"]["ids"][0] = mac;
    snprintf(buf, sizeof(buf), "GAIA A08 (%.4s)", mac);
    cfg["dev"]["name"] = buf;
    cfg["dev"]["mf"] = "AQICN";
    cfg["dev"]["mdl"] = "A08";
}

// Publish a single Home Assistant discovery config for one reading. The state
// topic is shared (dataTopic) and each entity extracts its own field via a
// value_template.
static void haSensor(const char *key, const char *name, const char *devClass,
                     const char *unit, const char *stateClass,
                     const char *const *options = nullptr, size_t optionCount = 0,
                     const char *icon = nullptr)
{
    JsonDocument cfg;
    char buf[48]; // worst-case value len is 40 chars (val_tpl)
    cfg["name"] = name;
    snprintf(buf, sizeof(buf), "%s_%s", mac, key);
    cfg["uniq_id"] = buf;
    cfg["stat_t"] = dataTopic;
    snprintf(buf, sizeof(buf), "{{ value_json.readings.%s }}", key);
    cfg["val_tpl"] = buf;
    cfg["dev_cla"] = devClass;
    if (unit)
    {
        cfg["unit_of_meas"] = unit;
    }
    if (stateClass)
    {
        cfg["stat_cla"] = stateClass;
    }
    copyArray(options, optionCount, cfg["ops"]);
    if (icon)
    {
        cfg["ic"] = icon;
    }

    haDevice(cfg);

    // Layer a per-reading availability on top of the device availability above:
    // the entity is available only when the device is online (avty[0]) and this
    // reading has a value (avty[1]). A null reading marks the entity unavailable
    // rather than letting the null render as an unknown value.
    cfg["avty_mode"] = "all";
    cfg["avty"][1]["t"] = dataTopic;
    char availTpl[96]; // worst-case len is 75 chars (key "main_pollutant")
    snprintf(availTpl, sizeof(availTpl),
             "{{ 'offline' if value_json.readings.%s is none else 'online' }}",
             key);
    cfg["avty"][1]["val_tpl"] = availTpl;

    char topic[128];
    snprintf(topic, sizeof(topic),
             HOME_ASSISTANT_DISCOVERY_PREFIX "/sensor/%s/%s/config", mac, key);

    char payload[576]; // worst-case json len is 495 bytes
    size_t len = serializeJson(cfg, payload, sizeof(payload));
    esp_mqtt_client_publish(client, topic, payload, len, 1, /*retain=*/1);
}

// Publish the discovery config for the RGB LED as a JSON-schema light with
// RGB color, brightness, and effect to toggle between AQI and a fixed color.
static void haLight()
{
    JsonDocument cfg;
    char buf[48];
    cfg["name"] = "Indicator";
    snprintf(buf, sizeof(buf), "%s_light", mac);
    cfg["uniq_id"] = buf;
    cfg["schema"] = "json";
    cfg["stat_t"] = lightStateTopic;
    cfg["cmd_t"] = lightCommandTopic;
    cfg["sup_clrm"][0] = "rgb";
    cfg["bri_scl"] = INDICATOR_BRIGHTNESS_LEVELS;
    cfg["effect"] = true;
    cfg["fx_list"][0] = HA_EFFECT_AQI;
    cfg["fx_list"][1] = HA_EFFECT_AQI_CONTINUOUS;
    cfg["fx_list"][2] = HA_EFFECT_SOLID;
    cfg["ic"] = "mdi:led-on";

    haDevice(cfg);

    char topic[128];
    snprintf(topic, sizeof(topic),
             HOME_ASSISTANT_DISCOVERY_PREFIX "/light/%s/light/config", mac);

    char payload[512];
    size_t len = serializeJson(cfg, payload, sizeof(payload));
    esp_mqtt_client_publish(client, topic, payload, len, 1, /*retain=*/1);
}

// Echo the stored light state so Home Assistant reflects it after a command or
// reconnect. The color reported is always the user's, never the live AQI color:
// keeping it lets Home Assistant restore it when switching back out of AQI mode,
// which is also why the AQI color shifting under AQI mode is not echoed here.
static void publishLightState()
{
    IndicatorState state = indicatorGetState();
    JsonDocument doc;
    doc["state"] = state.on ? "ON" : "OFF";
    doc["effect"] = state.mode == INDICATOR_MODE_USER
                        ? HA_EFFECT_SOLID
                        : state.mode == INDICATOR_MODE_AQI_CONTINUOUS
                              ? HA_EFFECT_AQI_CONTINUOUS
                              : HA_EFFECT_AQI;
    doc["brightness"] = std::max(1, (state.brightness + 1) * INDICATOR_BRIGHTNESS_LEVELS /
                                        (INDICATOR_BRIGHTNESS_MAX + 1));
    doc["color"]["r"] = state.r;
    doc["color"]["g"] = state.g;
    doc["color"]["b"] = state.b;

    char payload[96];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    esp_mqtt_client_publish(client, lightStateTopic, payload, len, 1, /*retain=*/1);
}

// Apply a light command from Home Assistant. The JSON schema payload carries an
// on/off state and, optionally, a color and an effect. Setting a color implies
// the Solid effect; the effect field, if present, wins.
static void handleLightCommand(const char *data, int len)
{
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok)
    {
        return;
    }

    IndicatorState state = indicatorGetState();

    if (doc["state"].is<const char *>())
    {
        state.on = strcmp(doc["state"], "ON") == 0;
    }
    if (doc["brightness"].is<int>())
    {
        // Home Assistant sends 1..INDICATOR_BRIGHTNESS_LEVELS (the advertised
        // scale); map each level to the top of its bucket in the stored range.
        int level = constrain(doc["brightness"].as<int>(), 1, INDICATOR_BRIGHTNESS_LEVELS);
        state.brightness =
            (INDICATOR_BRIGHTNESS_MAX + 1) * level / INDICATOR_BRIGHTNESS_LEVELS - 1;
    }
    if (doc["color"]["r"].is<int>() &&
        doc["color"]["g"].is<int>() &&
        doc["color"]["b"].is<int>())
    {
        state.r = doc["color"]["r"];
        state.g = doc["color"]["g"];
        state.b = doc["color"]["b"];
        state.mode = INDICATOR_MODE_USER;
    }
    const char *effect = doc["effect"].as<const char *>();
    if (effect)
    {
        state.mode = strcmp(effect, HA_EFFECT_SOLID) == 0
                         ? INDICATOR_MODE_USER
                         : strcmp(effect, HA_EFFECT_AQI_CONTINUOUS) == 0
                             ? INDICATOR_MODE_AQI_CONTINUOUS
                             : INDICATOR_MODE_AQI;
    }

    indicatorSetState(state);
    publishLightState();
}

// Publish all discovery configs (retained) so Home Assistant auto-creates the
// entities. Called on every (re)connect to re-announce after reconnects.
static void haPublishDiscovery()
{
    haSensor("pm1", "PM1", "pm1", "µg/m³", "measurement");
    haSensor("pm25", "PM2.5", "pm25", "µg/m³", "measurement");
    haSensor("pm10", "PM10", "pm10", "µg/m³", "measurement");
    haSensor("temperature", "Temperature", "temperature", "°C", "measurement");
    haSensor("humidity", "Humidity", "humidity", "%", "measurement");
    if (co2SensorAvailable())
    {
        haSensor("co2", "CO2", "carbon_dioxide", "ppm", "measurement");
    }
    // The "aqi" device class is unitless (no unit_of_measurement). Exposing it
    // as an entity also makes the device usable with the air-visual-card
    // Lovelace card.
    haSensor("aqi", "AQI", "aqi", nullptr, "measurement");
    // The dominant pollutant is an "enum" keyword sensor feeding the
    // air-visual-card's "main_pollutant".
    static const char *const pollutantOptions[] = {"pm25", "pm10"};
    haSensor("main_pollutant", "Dominant Pollutant", "enum", nullptr, nullptr,
             pollutantOptions, sizeof(pollutantOptions) / sizeof(pollutantOptions[0]),
             "mdi:molecule");
    haLight();
}
#endif // CONF_HOME_ASSISTANT

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        Serial.println("Connected to MQTT Broker!");
#ifdef CONF_HOME_ASSISTANT
        // Mark the device online, then (re)announce the discovery configs.
        esp_mqtt_client_publish(client, statusTopic, "online", 0, 1, /*retain=*/1);
        haPublishDiscovery();
        // Subscribe for light commands and publish the current state so Home
        // Assistant shows the LED as it stands right after (re)connecting.
        esp_mqtt_client_subscribe(client, lightCommandTopic, 1);
        publishLightState();
#endif
        break;
    case MQTT_EVENT_DISCONNECTED:
        Serial.println("Disconnected from MQTT Broker.");
        break;
    case MQTT_EVENT_PUBLISHED:
        Serial.println("Data published to MQTT Broker");
        break;
#ifdef CONF_HOME_ASSISTANT
    case MQTT_EVENT_DATA:
    {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        // The light command is small and single-topic, so ignore anything the
        // broker had to split across events rather than reassembling it.
        if (event->data_len == event->total_data_len &&
            event->topic_len == (int)strlen(lightCommandTopic) &&
            strncmp(event->topic, lightCommandTopic, event->topic_len) == 0)
        {
            handleLightCommand(event->data, event->data_len);
        }
        break;
    }
#endif
    default:
        break;
    }
}

void mqttWorker(void *params)
{

    while (1)
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS); // 10 seconds
        // Check WiFi connection status
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Error in WiFi connection");
            continue;
        }

        JsonDocument doc = getSensorDataJson();
        static unsigned char json_body[320]; // worst-case json len is 290 bytes
        size_t json_len = serializeJson(doc, json_body, sizeof(json_body));

        // Serial.printf("Posting: %s with len %d \n", json_body, json_len);

        // Retain the state so Home Assistant repopulates entities immediately
        // after a restart instead of waiting for the next publish.
        if (esp_mqtt_client_publish(client, dataTopic, (char *)json_body, json_len, 1, /*retain=*/1) == -1)
        {
            Serial.println("Failed to publish data to MQTT Broker");
        }
    }
}

void mqttInit()
{
    // Build the per-node topics from the device MAC (populated by getStationId()
    // before mqttInit() is called).
    snprintf(dataTopic, sizeof(dataTopic), "GAIA/%s/data", mac);
    snprintf(statusTopic, sizeof(statusTopic), "GAIA/%s/status", mac);
#ifdef CONF_HOME_ASSISTANT
    snprintf(lightStateTopic, sizeof(lightStateTopic), "GAIA/%s/light", mac);
    snprintf(lightCommandTopic, sizeof(lightCommandTopic), "GAIA/%s/light/set", mac);
#endif

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = MQTT_BROKER_URI,
        .port = MQTT_PORT,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
    };

    if (strlen(mqtt_cfg.uri) == 0)
    {
        Serial.println("Can not start the MQTT client: MQTT_BROKER_URI is not defined");
        return;
    }

#ifdef CONF_HOME_ASSISTANT
    // Last Will: if the device drops off the network, the broker publishes
    // "offline" to the status topic and Home Assistant greys out the device.
    mqtt_cfg.lwt_topic = statusTopic;
    mqtt_cfg.lwt_msg = "offline";
    mqtt_cfg.lwt_qos = 1;
    mqtt_cfg.lwt_retain = 1;
#endif

    esp_err_t err;
    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == nullptr)
    {
        Serial.println("Failed to create MQTT client");
        return;
    }

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    err = esp_mqtt_client_start(client);
    if (err != ESP_OK)
    {
        Serial.println("Failed to start the MQTT client");
        return;
    }

    xTaskCreate(
        mqttWorker,   // Function that should be called
        "mqttWorker", // Name of the task (for debugging)
        2048,         // Stack size (bytes)
        NULL,         // Parameter to pass
        3,            // Task priority - medium
        NULL          // Task handle
    );
}
#endif
