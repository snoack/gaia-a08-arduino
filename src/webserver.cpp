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
#include <WiFi.h>
#include "network.hpp"
#include "sensors.hpp"
#include <ESPAsyncWebServer.h>

#ifdef CONF_USE_WEB_SERVER

AsyncWebServer server(80);

void webServerRealtimeHandler(AsyncWebServerRequest *request)
{
    JsonDocument doc = getSensorDataJson();

    // AsyncResponseStream owns its buffer and is freed once the response has
    // been sent, so the JSON does not have to outlive this handler. A shared
    // buffer would not do: responses are sent asynchronously, and concurrent
    // requests would overwrite each other's payload.
    AsyncResponseStream *response =
        request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void webServerInit()
{

    server.on("/realtime", HTTP_GET, webServerRealtimeHandler);
    server.begin();
}

#endif
