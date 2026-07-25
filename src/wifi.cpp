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

#include <atomic>
#include <WiFi.h>
#include <HTTPClient.h>
#include "main.hpp"

#ifdef CONF_USE_WIFI_MANAGER
#include <WiFiManager.h>
WiFiManager wifiManager;
#endif

// How long the link may stay down before wifiReconnect() forces a reconnect.
// Long enough that the core's own auto-reconnect (which is prompt for the
// disconnect reasons it handles) recovers first and clears the pending flag, so
// this only fires for the reasons the core ignores.
static constexpr unsigned long RECONNECT_DELAY = 5000;

// Whether a forced reconnect is pending, and the millis() of the last connect
// attempt (the initial drop counts as the first) to space out retries. A
// separate flag rather than a sentinel timestamp because every timestamp value
// is a valid millis() reading. Written from the Wi-Fi event task and loop().
static std::atomic<bool> reconnectPending{false};
static std::atomic<unsigned long> lastAttemptAt{0};

// Log the Wi-Fi lifecycle over serial and drive the reconnect state. The
// disconnect reason is logged because it is what decides whether the core
// auto-reconnects: a network outage or an explicit reconnect from the network
// controller can produce a reason the core's whitelist ignores (e.g. 193),
// which is exactly when this reconnect path has to step in.
static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        // Link is up: cancel any pending forced reconnect.
        reconnectPending.store(false, std::memory_order_relaxed);
        Serial.printf("[%lu] WiFi: got IP %s (gw %s, RSSI %d dBm)\n",
                      millis(),
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.RSSI());
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        Serial.printf("[%lu] WiFi: lost IP\n", millis());
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        // Start the retry clock on the first drop of an outage; a burst of
        // disconnect events leaves it alone. Cleared on GOT_IP if the core
        // reconnects on its own.
        if (!reconnectPending.exchange(true, std::memory_order_relaxed))
        {
            lastAttemptAt.store(millis(), std::memory_order_relaxed);
        }
        Serial.printf("[%lu] WiFi: disconnected, reason %u (%s)\n",
                      millis(), reason,
                      WiFi.disconnectReasonName((wifi_err_reason_t)reason));
        break;
    }
    default:
        break;
    }
}

void wifiReconnect()
{
    if (!reconnectPending.load(std::memory_order_relaxed) ||
        millis() - lastAttemptAt.load(std::memory_order_relaxed) < RECONNECT_DELAY)
    {
        return;
    }

    Serial.printf("[%lu] WiFi: link down past reconnect delay, forcing reconnect\n", millis());
    // WiFiManager replays its stored credentials from a bare begin(); the static
    // path needs the configured SSID/pass.
    WiFi.disconnect();
    WiFi.begin(
#ifndef CONF_USE_WIFI_MANAGER
        WIFI_SSID, WIFI_PASS
#endif
    );
    // A reconnect that does not take is retried after another interval; GOT_IP
    // clears the pending flag once it succeeds.
    lastAttemptAt.store(millis(), std::memory_order_relaxed);
}

void wifiInit()
{
    // Register before connecting so the whole lifecycle is captured, including
    // the first association and any early failures.
    WiFi.onEvent(onWiFiEvent);

#ifdef CONF_USE_WIFI_MANAGER
    wifiManager.autoConnect("GAIA-A08");

#else
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        // Check for the connection
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        Serial.println("Trying to connecting to WiFi..");
    }
#endif

    Serial.print("Connected to the WiFi network with IP address: ");

    IPAddress ip = WiFi.localIP();
    Serial.println(ip);
}
