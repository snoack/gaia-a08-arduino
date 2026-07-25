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
#include "uploader.hpp"

#ifdef ANY_UPLOADER_ENABLED
#include <WiFi.h>
#include <HTTPClient.h>

static constexpr char SOFTWARE_VERSION[] = "GAIA-uploader/1.1";

static void logHttpResponse(HTTPClient &http, int httpResponseCode)
{
    if (httpResponseCode > 0)
    {
        String response = http.getString();
        Serial.println(httpResponseCode);
        Serial.println(response);
    }
    else
    {
        Serial.print("Error on sending POST: ");
        Serial.println(httpResponseCode);
    }
}

#ifdef CONF_SENSOR_COMMUNITY
static void uploadDataToSensorCommunity(
    JsonDocument &doc,
    const char *pin)
{
    char sensorId[48];
    snprintf(sensorId, sizeof(sensorId), "esp32-%s", mac);

    HTTPClient http;
    http.setUserAgent(SOFTWARE_VERSION);
    http.begin("https://api.sensor.community/v1/push-sensor-data/");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Sensor", sensorId);
    http.addHeader("X-Pin", pin);

    unsigned char body[192]; // worst-case json len is 175 bytes
    size_t bodyLength = serializeJson(doc, body, sizeof(body));
    int httpResponseCode = http.POST(body, bodyLength);
    Serial.printf("Sensor.Community upload (Pin %s):\n", pin);
    logHttpResponse(http, httpResponseCode);

    http.end();
}

static void uploadPmDataToSensorCommunity(const SensorReadings &readings)
{
    JsonDocument doc;
    doc["software_version"] = SOFTWARE_VERSION;
    JsonArray measurements = doc["sensordatavalues"].to<JsonArray>();

    if (readings.hasPm1)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["value_type"] = "P0";
        measurement["value"] = readings.pm1;
    }
    if (readings.hasPm10)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["value_type"] = "P1";
        measurement["value"] = readings.pm10;
    }
    if (readings.hasPm25)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["value_type"] = "P2";
        measurement["value"] = readings.pm25;
    }

    if (measurements.size() == 0)
    {
        Serial.println("Skipping Sensor.Community upload: PM data unavailable");
        return;
    }

    uploadDataToSensorCommunity(doc, "1");
}

static void uploadMetDataToSensorCommunity(const SensorReadings &readings)
{
    JsonDocument doc;
    doc["software_version"] = SOFTWARE_VERSION;
    JsonArray measurements = doc["sensordatavalues"].to<JsonArray>();

    if (readings.hasTemperature)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["value_type"] = "temperature";
        measurement["value"] = readings.temperature;
    }
    if (readings.hasHumidity)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["value_type"] = "humidity";
        measurement["value"] = readings.humidity;
    }

    if (measurements.size() == 0)
    {
        Serial.println("Skipping Sensor.Community AHT20 upload: data unavailable");
        return;
    }

    uploadDataToSensorCommunity(doc, "7");
}

static void uploadCo2DataToSensorCommunity(const SensorReadings &readings)
{
    if (!readings.hasCo2)
    {
        Serial.println("Skipping Sensor.Community SCD4x upload: data unavailable");
        return;
    }

    JsonDocument doc;
    doc["software_version"] = SOFTWARE_VERSION;
    doc["sensordatavalues"][0]["value_type"] = "co2_ppm";
    doc["sensordatavalues"][0]["value"] = readings.co2;

    uploadDataToSensorCommunity(doc, "17");
}
#endif

#ifdef CONF_OPENSENSEMAP
static void uploadDataToOpenSenseMap(const SensorReadings &readings)
{
    JsonDocument doc;
    JsonArray measurements = doc.to<JsonArray>();

#ifdef OPENSENSEMAP_PM1_SENSOR_ID
    if (readings.hasPm1)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_PM1_SENSOR_ID;
        measurement["value"] = readings.pm1;
    }
#endif
#ifdef OPENSENSEMAP_PM25_SENSOR_ID
    if (readings.hasPm25)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_PM25_SENSOR_ID;
        measurement["value"] = readings.pm25;
    }
#endif
#ifdef OPENSENSEMAP_PM10_SENSOR_ID
    if (readings.hasPm10)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_PM10_SENSOR_ID;
        measurement["value"] = readings.pm10;
    }
#endif
#ifdef OPENSENSEMAP_TEMPERATURE_SENSOR_ID
    if (readings.hasTemperature)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_TEMPERATURE_SENSOR_ID;
        measurement["value"] = readings.temperature;
    }
#endif
#ifdef OPENSENSEMAP_HUMIDITY_SENSOR_ID
    if (readings.hasHumidity)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_HUMIDITY_SENSOR_ID;
        measurement["value"] = readings.humidity;
    }
#endif
#ifdef OPENSENSEMAP_CO2_SENSOR_ID
    if (readings.hasCo2)
    {
        JsonObject measurement = measurements.add<JsonObject>();
        measurement["sensor"] = OPENSENSEMAP_CO2_SENSOR_ID;
        measurement["value"] = readings.co2;
    }
#endif

    if (measurements.size() == 0)
    {
        Serial.println("Skipping openSenseMap upload: no data available");
        return;
    }

    unsigned char body[448]; // worst-case json len is 339 bytes
    size_t bodyLength = serializeJson(doc, body, sizeof(body));

    char url[96];
    snprintf(
        url,
        sizeof(url),
        "https://ingress.opensensemap.org/boxes/%s/data",
        OPENSENSEMAP_BOX_ID);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", OPENSENSEMAP_ACCESS_TOKEN);

    int httpResponseCode = http.POST(body, bodyLength);
    Serial.println("openSenseMap upload:");
    logHttpResponse(http, httpResponseCode);

    http.end();
}
#endif

#ifdef CONF_AQICN
#if !defined(LATITUDE) || !defined(LONGITUDE)
#error "CONF_AQICN requires LATITUDE and LONGITUDE"
#endif

static void uploadDataToAqicn(const SensorReadings &readings)
{
    if (!readings.hasPm25)
    {
        Serial.println("Skipping AQICN upload: PM2.5 data unavailable");
        return;
    }

    JsonDocument doc;
    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;
    doc["station"]["location"]["latitude"] = LATITUDE;
    doc["station"]["location"]["longitude"] = LONGITUDE;

    doc["readings"][0]["specie"] = "pm25";
    doc["readings"][0]["value"] = readings.pm25;
    doc["readings"][0]["unit"] = "µg/m3";
    doc["readings"][1]["specie"] = "pm10";
    doc["readings"][1]["value"] = readings.pm10;
    doc["readings"][1]["unit"] = "µg/m3";
    doc["readings"][2]["specie"] = "pm1";
    doc["readings"][2]["value"] = readings.pm1;
    doc["readings"][2]["unit"] = "µg/m3";
    doc["readings"][3]["specie"] = "temperature";
    doc["readings"][3]["value"] = readings.temperature;
    doc["readings"][3]["unit"] = "C";
    doc["readings"][4]["specie"] = "humidity";
    doc["readings"][4]["value"] = readings.humidity;
    doc["readings"][4]["unit"] = "%";

    if (readings.hasCo2)
    {
        doc["readings"][5]["specie"] = "co2";
        doc["readings"][5]["value"] = readings.co2;
        doc["readings"][5]["unit"] = "ppm";
    }

    doc["token"] = AQICN_TOKEN;

    unsigned char jsonBody[512]; // worst-case JSON length is 505 bytes
    size_t jsonLength = serializeJson(doc, jsonBody, sizeof(jsonBody));
    Serial.printf("Posting: %s with len %d \n", jsonBody, jsonLength);

    HTTPClient http;
    http.setUserAgent(SOFTWARE_VERSION);
    http.begin("https://aqicn.org/sensor/upload");
    // http.begin("http://192.168.1.214:88/sensor/upload");
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(jsonBody, jsonLength);
    Serial.println("AQICN upload:");
    logHttpResponse(http, httpResponseCode);

    http.end();
}
#endif

// One minute is what the AQICN firmware traditionally uses and it is also the
// default in openSenseMap's generated firmware code. Sensor.Community asks not
// to send more than once per minute, while requiring data to be sent at least
// every 5 minutes to show as online on their map, with their own firmware
// sending every 145s. Uploading once a minute therefore satisfies all of them,
// and keeps a single schedule for every uploader.
// https://github.com/sensebox/node-sketch-templater/blob/master/templates/homev2_ethernet.tpl
// https://forum.sensor.community/t/how-often-should-i-send-data-to-https-api-sensor-community-v1-push-sensor-data/785
static constexpr TickType_t UPLOAD_INTERVAL = pdMS_TO_TICKS(60 * 1000);

void uploaderWorker(void *params)
{
    const TickType_t startedAt = xTaskGetTickCount();

    while (1)
    {
        // Sleep until the next interval boundary. Deriving the delay from the
        // elapsed time keeps the uploads aligned to the interval without
        // accumulating drift, and a cycle that overruns simply misses
        // boundaries instead of queueing up requests to catch up, which would
        // exceed the rate the servers expect.
        TickType_t elapsed = xTaskGetTickCount() - startedAt;
        vTaskDelay(UPLOAD_INTERVAL - elapsed % UPLOAD_INTERVAL);

        // Check WiFi connection status
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Error in WiFi connection");
            continue;
        }

        const SensorReadings readings = getSensorReadings();

#ifdef CONF_AQICN
        uploadDataToAqicn(readings);
#endif

#ifdef CONF_SENSOR_COMMUNITY
        uploadPmDataToSensorCommunity(readings);
        uploadMetDataToSensorCommunity(readings);

        if (co2SensorAvailable())
        {
            uploadCo2DataToSensorCommunity(readings);
        }
#endif

#ifdef CONF_OPENSENSEMAP
        uploadDataToOpenSenseMap(readings);
#endif
    }
}

void uploaderInit()
{
    xTaskCreate(
        uploaderWorker,   // Function that should be called
        "uploaderWorker", // Name of the task (for debugging)
        8192,             // Stack size (bytes)
        NULL,             // Parameter to pass
        3,                // Task priority - medium
        NULL              // Task handle
    );
}
#endif
