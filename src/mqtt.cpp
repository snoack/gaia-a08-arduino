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

#include <WiFi.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

esp_mqtt_client_handle_t client;

// Per-node topics, keyed off the device MAC so any number of GAIA nodes can
// coexist on the same broker without collisions. Built once in mqttInit().
static char dataTopic[48];   // GAIA/<mac>/data    (sensor state)
static char statusTopic[48]; // GAIA/<mac>/status  (device availability + LWT)
#ifdef CONF_HOME_ASSISTANT
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

    // Device availability applies to every entity.
    cfg["avty"][0]["t"] = statusTopic;

    // Group all of this node's entities under one Home Assistant device.
    cfg["dev"]["ids"][0] = mac;
    snprintf(buf, sizeof(buf), "GAIA A08 (%.4s)", mac);
    cfg["dev"]["name"] = buf;
    cfg["dev"]["mf"] = "AQICN";
    cfg["dev"]["mdl"] = "A08";

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
#endif
        break;
    case MQTT_EVENT_DISCONNECTED:
        Serial.println("Disconnected from MQTT Broker.");
        break;
    case MQTT_EVENT_PUBLISHED:
        Serial.println("Data published to MQTT Broker");
        break;
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

        JsonDocument doc;
        getMinimalSensorData(doc);
        static unsigned char json_body[320]; // worst-case json len is 280 bytes
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
