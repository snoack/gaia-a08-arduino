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
#include <Preferences.h>
#include <atomic>
#include <string.h>
#include "main.hpp"
#include "sensors.hpp"
#include "indicator.hpp"
#include "aqi.hpp"

WS2812FX ws2812fx = WS2812FX(RGB_LED_COUNT, GPIO_RGB_LED, NEO_GRB + NEO_KHZ800);

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

// WS2812FX drives the strip synchronously from whichever task calls it, so
// only the rgbLedWorker task touches the LED. The tasks that set the state
// below and report AQI colors leave the painting to it.
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

// A latch rather than a comparison against millis(), which wraps roughly every
// 50 days and would otherwise bring the animation back.
static bool withinBootWindow = true;

// WS2812FX brightness: 0 means no scaling (bright!), 1-2 are essentially off,
// so 3 is the lowest perceivable level; 10 was the max the original firmware
// used, so it is known safe. These bounds define how many distinct levels the
// LED renders, which is what Home Assistant is told (see the assert below).
constexpr uint32_t WS2812FX_BRIGHTNESS_MIN = 3;
constexpr uint32_t WS2812FX_BRIGHTNESS_MAX = 10;
static_assert(WS2812FX_BRIGHTNESS_MAX - WS2812FX_BRIGHTNESS_MIN + 1 ==
                  INDICATOR_BRIGHTNESS_LEVELS,
              "advertised brightness levels must match the rendered range");

static void applyState(const IndicatorState &state, uint32_t aqiColor)
{
    if (!state.on)
    {
        ws2812fx.stop();
        return;
    }

    // Map the stored 5-bit resolution onto the coarse WS2812FX range.
    uint32_t level =
        (uint32_t)state.brightness *
        (WS2812FX_BRIGHTNESS_MAX - WS2812FX_BRIGHTNESS_MIN) / INDICATOR_BRIGHTNESS_MAX;

    if (state.mode == INDICATOR_MODE_AQI && withinBootWindow)
    {
        // setMode() restarts the animation, so only enter it once.
        if (ws2812fx.getMode() != FX_MODE_RAINBOW_CYCLE)
        {
            ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
            // Dim the boot rainbow to half the configured brightness.
            ws2812fx.setBrightness(WS2812FX_BRIGHTNESS_MIN + level / 2);
        }
    }
    else
    {
        // Brightness rides the separate setBrightness() scale above, so the
        // color is painted at full value.
        uint32_t color = state.mode == INDICATOR_MODE_AQI
                             ? aqiColor
                             : ((uint32_t)state.r << 16) | ((uint32_t)state.g << 8) | state.b;
        ws2812fx.setMode(FX_MODE_STATIC);
        ws2812fx.setColor(color);
        ws2812fx.setBrightness(WS2812FX_BRIGHTNESS_MIN + level);
    }

    // Resume only on the off-to-on edge: start() resets every segment runtime,
    // which would restart the boot rainbow from its first frame.
    if (!ws2812fx.isRunning())
    {
        ws2812fx.start();
    }
}

// Runs in its own task rather than from loop(), which does not start until
// setup() returns; setup() blocks for seconds in wifiInit(), and the strip
// would otherwise sit unserviced (no boot rainbow) until then.
static void rgbLedWorker(void *parameter)
{
    while (1)
    {
        if (withinBootWindow && millis() >= 8000)
        {
            withinBootWindow = false;
            repaintPending.store(true, std::memory_order_relaxed);
        }

        IndicatorState state = indicatorState.load(std::memory_order_relaxed);
        uint32_t aqiColor = reportedAqiColor.load(std::memory_order_relaxed);

        if (repaintPending.exchange(false, std::memory_order_relaxed) ||
            (state.mode == INDICATOR_MODE_AQI && aqiColor != lastAqiColor))
        {
            applyState(state, aqiColor);
            lastAqiColor = aqiColor;
        }

        ws2812fx.service();
        // Short delay so the boot rainbow animates smoothly; service()
        // self-throttles internally, so this only bounds how often it is polled.
        vTaskDelay(5 / portTICK_PERIOD_MS);
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

    ws2812fx.init();
    ws2812fx.setSpeed(500);

    applyState(indicatorState.load(std::memory_order_relaxed),
               reportedAqiColor.load(std::memory_order_relaxed));

    xTaskCreate(
        rgbLedWorker,   // Function that should be called
        "rgbLedWorker", // Name of the task (for debugging)
        2048,           // Stack size (bytes)
        NULL,           // Parameter to pass
        3,              // Task priority - medium
        NULL            // Task handle
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
