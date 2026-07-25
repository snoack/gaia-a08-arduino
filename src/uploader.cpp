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

#include <WiFi.h>
#include "sensors.hpp"
#include "main.hpp"
#include <HTTPClient.h>

void UploadDataToAQIC(unsigned char *json_body, size_t request_len)
{
    HTTPClient http;
    http.setUserAgent("GAIA-uploader/1.1");
    http.begin("https://aqicn.org/sensor/upload");
    // http.begin("http://192.168.1.214:88/sensor/upload");
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(json_body, request_len);

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

    http.end();
}

// One minute is what the AQICN firmware traditionally uses.
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
        // exceed the rate the server expects.
        TickType_t elapsed = xTaskGetTickCount() - startedAt;
        vTaskDelay(UPLOAD_INTERVAL - elapsed % UPLOAD_INTERVAL);

        // Check WiFi connection status
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Error in WiFi connection");
            continue;
        }

        JsonDocument doc;
        if (!getSerialisedSensorData(doc))
        {
            continue;
        }
        static unsigned char json_body[512]; // worst-case json len is 494 bytes
        size_t json_len = serializeJson(doc, json_body, sizeof(json_body));

        Serial.printf("Posting: %s with len %d \n", json_body, json_len);
        UploadDataToAQIC(json_body, json_len);
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
