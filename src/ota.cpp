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

#include "config.hpp"

#ifdef OTA_PASSWORD

#include <ArduinoOTA.h>
#include "main.hpp"
#include "network.hpp"

static constexpr bool stringsEqual(const char *left, const char *right)
{
    return *left == *right &&
           (*left == '\0' || stringsEqual(left + 1, right + 1));
}

static_assert(sizeof(OTA_PASSWORD) > 8,
              "OTA_PASSWORD must contain at least 8 characters");
static_assert(!stringsEqual(OTA_PASSWORD, "replace-with-a-strong-password"),
              "OTA_PASSWORD must not use the placeholder value");

void otaInit()
{
    ArduinoOTA.setHostname(stationID);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart(
        []()
        {
            Serial.println(ArduinoOTA.getCommand() == U_FLASH
                               ? "Starting OTA firmware update"
                               : "Starting OTA filesystem update");
        });
    ArduinoOTA.onEnd(
        []()
        {
            Serial.println("\nOTA update complete");
        });
    ArduinoOTA.onProgress(
        [](unsigned int progress, unsigned int total)
        {
            Serial.printf("OTA progress: %u%%\r", progress * 100U / total);
        });
    ArduinoOTA.onError(
        [](ota_error_t error)
        {
            Serial.printf("OTA error: %u\n", error);
        });

    ArduinoOTA.begin();
    Serial.printf("OTA updates ready at %s.local\n", stationID);
}

void otaHandle()
{
    ArduinoOTA.handle();
}

#endif
