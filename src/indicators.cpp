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

#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <atomic>
#include <string.h>
#include "main.hpp"
#include "sensors.hpp"
#include "indicator.hpp"
#include "aqi.hpp"

static Adafruit_NeoPixel pixels(RGB_LED_COUNT, GPIO_RGB_LED, NEO_GRB + NEO_KHZ800);

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

static Preferences preferences;
static constexpr char PREF_NAMESPACE[] = "gaia";
static constexpr char PREF_LIGHT_KEY[] = "light";

// Only the ledWorker task touches the LED. The tasks that set the state below
// and report AQI colors leave the painting to it.
static std::atomic<IndicatorState> indicatorState{IndicatorState{
    .r = 0,
    .g = 0,
    .b = 0,
    .brightness = INDICATOR_BRIGHTNESS_MAX,
    .on = true,
    .mode = INDICATOR_MODE_AQI,
}};

static std::atomic<uint32_t> reportedAqiColor{UINT32_MAX};
static std::atomic<bool> repaintPending{false};

// Where the AQI signal stood at the last repaint. Taken on every repaint, not
// only in AQI mode, so that returning to that mode does not compare against a
// color from before the LED was set by hand.
static uint32_t lastAqiColor = UINT32_MAX;

// NeoPixel brightness: 0-1 are essentially off, so 2 is the lowest perceivable
// level; 9 (corresponds to 10 in the original code using WS2812FX) was the max
// the original firmware used — known safe. These bounds define how many
// distinct levels the LED renders, which is what Home Assistant is told.
constexpr uint8_t LED_BRIGHTNESS_MIN = 2;
constexpr uint8_t LED_BRIGHTNESS_MAX = 9;
static_assert(LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_MIN + 1 ==
                  INDICATOR_BRIGHTNESS_LEVELS,
              "advertised brightness levels must match the rendered range");

static void applyState(const IndicatorState &state, uint32_t aqiColor)
{
    uint32_t color;
    uint8_t brightness =
        (uint32_t)state.brightness *
        (LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_MIN) / INDICATOR_BRIGHTNESS_MAX;

    if (!state.on)
    {
        color = 0;
    }
    else if (state.mode == INDICATOR_MODE_USER)
    {
        color = ((uint32_t)state.r << 16) | ((uint32_t)state.g << 8) | state.b;
    }
    else if (aqiColor != UINT32_MAX)
    {
        color = aqiColor;
    }
    else
    {
        constexpr uint32_t RAINBOW_CYCLE_MS = 2560;
        uint16_t hue = (millis() % RAINBOW_CYCLE_MS) * 65536 / RAINBOW_CYCLE_MS;
        color = pixels.ColorHSV(hue);
        brightness /= 2;
    }

    pixels.setBrightness(LED_BRIGHTNESS_MIN + brightness);
    pixels.setPixelColor(0, color);
    pixels.show();

    lastAqiColor = aqiColor;
}

// Keep the LED animation independent of setup() and loop(). setup() performs
// synchronous initialization after starting this task, including WiFiManager's
// saved-credential attempt, and loop() has exceptional blocking operations.
static void ledWorker(void *parameter)
{
    while (1)
    {
        IndicatorState state = indicatorState.load(std::memory_order_relaxed);
        uint32_t aqiColor = reportedAqiColor.load(std::memory_order_relaxed);
        if (repaintPending.exchange(false, std::memory_order_relaxed) ||
            (state.on && state.mode == INDICATOR_MODE_AQI &&
                (aqiColor != lastAqiColor || aqiColor == UINT32_MAX)))
        {
            applyState(state, aqiColor);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void indicatorSetState(const IndicatorState &state)
{
    IndicatorState previous = indicatorState.exchange(state, std::memory_order_relaxed);
    repaintPending.store(true, std::memory_order_relaxed);

    // Commands are user-initiated and rare, but Home Assistant re-sends on
    // reconnect, so only write flash when something actually differs.
    if (memcmp(&state, &previous, sizeof(state)) != 0 &&
        preferences.begin(PREF_NAMESPACE, /*readOnly=*/false))
    {
        preferences.putBytes(PREF_LIGHT_KEY, &state, sizeof(state));
        preferences.end();
    }
}

IndicatorState indicatorGetState()
{
    return indicatorState.load(std::memory_order_relaxed);
}

void ledInit()
{
    // Hold the green LED off: the RGB LED is the indicator now, and the green
    // one is either hidden behind it or a distraction when the RGB is off.
    pinMode(GPIO_GREEN_LED, OUTPUT);
    digitalWrite(GPIO_GREEN_LED, LOW);

    // Load before touching the LED: a device that was turned off must not
    // flash on the way to finding that out.
    if (preferences.begin(PREF_NAMESPACE, /*readOnly=*/true))
    {
        // A blob of another size, from an older firmware, would be copied in
        // as-is and leave the rest of the struct uninitialized.
        IndicatorState stored = {};
        if (preferences.getBytes(PREF_LIGHT_KEY, &stored, sizeof(stored)) == sizeof(stored))
        {
            indicatorState.store(stored, std::memory_order_relaxed);
        }
        preferences.end();
    }

    pixels.begin();

    applyState(indicatorState.load(std::memory_order_relaxed),
               reportedAqiColor.load(std::memory_order_relaxed));

    xTaskCreate(
        ledWorker,   // Function that should be called
        "ledWorker", // Name of the task (for debugging)
        2048,        // Stack size (bytes)
        NULL,        // Parameter to pass
        3,           // Task priority - medium
        NULL         // Task handle
    );
}

// Drive the RGB LED from the same combined AQI signal (max of the PM2.5 and
// PM10 sub-indices) that is published to Home Assistant and the web server, so
// the indicator, the app, and aqicn.org all agree.
void indicatorReportAqi(float pm25, float pm10)
{
    reportedAqiColor.store(aqi_colors[aqiCategory(computeAqi(pm25, pm10).aqi)],
                           std::memory_order_relaxed);
}
