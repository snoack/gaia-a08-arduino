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

#include <WS2812FX.h>
#include <jled.h>
#include "main.hpp"
#include "sensors.hpp"
#include "indicator.hpp"
#include "aqi.hpp"

WS2812FX ws2812fx = WS2812FX(RGB_LED_COUNT, GPIO_RGB_LED, NEO_GRB + NEO_KHZ800);

void rgbLedWorker(void *parameter);

// One color per AQI category, indexed by AqiCategory.
const uint32_t aqi_colors[] = {
    0x00ff00, // AQI_GOOD                 Green
    0xffff00, // AQI_MODERATE             Yellow
    0xff8000, // AQI_UNHEALTHY_SENSITIVE  Orange
    0xff4000, // AQI_UNHEALTHY            Dark orange
    0xff0000, // AQI_VERY_UNHEALTHY       Red
    0xcc00ff  // AQI_HAZARDOUS            Purple
};
static_assert(sizeof(aqi_colors) / sizeof(aqi_colors[0]) == AQI_CATEGORY_COUNT,
              "aqi_colors must have one entry per AQI category");

void rgbLedInit()
{
    ws2812fx.init();
    ws2812fx.setBrightness(5);
    ws2812fx.setSpeed(500);
    ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
    ws2812fx.start();

    xTaskCreate(
        rgbLedWorker,   // Function that should be called
        "rgbLedWorker", // Name of the task (for debugging)
        2048,           // Stack size (bytes)
        NULL,           // Parameter to pass
        3,              // Task priority - medium
        NULL            // Task handle
    );
}

auto led = JLed(GPIO_GREEN_LED);
void ledLoop(void *parameter)
{
    while (1)
    {
        led.Update();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void ledInit()
{
    // To just turn off the Green LED, use:
    // pinMode(GPIO_GREEN_LED, OUTPUT);
    // digitalWrite(GPIO_GREEN_LED, LOW);
    // To make the green LED breathe:
    led.MaxBrightness(100).Breathe(2000).DelayAfter(1000).Forever();

    xTaskCreate(
        ledLoop,   // Function that should be called
        "ledLoop", // Name of the task (for debugging)
        1024,      // Stack size (bytes)
        NULL,      // Parameter to pass
        3,         // Task priority - medium
        NULL       // Task handle
    );
}

// The first 8 seconds, show the rainbow
bool rgb_is_rainbow_mode = true;

void rgbLedLoop()
{
    ws2812fx.service();
}

void rgbLedWorker(void *parameter)
{
    int rgb_loop_count = 0;

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (rgb_loop_count == 8)
        {
            ws2812fx.setMode(FX_MODE_STATIC);
            ws2812fx.setBrightness(0);
            ws2812fx.setColor(0);
            break;
        }

        rgb_loop_count++;
    }
    rgb_is_rainbow_mode = false;
    vTaskDelete(NULL);
}

// Drive the RGB LED from the same combined AQI signal (max of the PM2.5 and
// PM10 sub-indices) that is published to Home Assistant and the web server, so
// the indicator, the app, and aqicn.org all agree.
void indicatorReportAqi(float pm25, float pm10)
{
    if (rgb_is_rainbow_mode)
    {
        return;
    }
    uint32_t color = aqi_colors[aqiCategory(computeAqi(pm25, pm10).aqi)];
    ws2812fx.setBrightness(10);
    ws2812fx.setColor(color);
}
