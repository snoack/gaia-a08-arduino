#ifndef _INDICATOR_HPP
#define _INDICATOR_HPP

#include <stdint.h>
#include <Arduino.h>

enum IndicatorMode : uint8_t
{
    // Color follows the measured AQI (see indicatorReportAqi).
    INDICATOR_MODE_AQI,
    // Color is whatever was last set explicitly.
    INDICATOR_MODE_USER,
};

static constexpr uint8_t INDICATOR_BRIGHTNESS_SIZE = 5;
static constexpr uint8_t INDICATOR_BRIGHTNESS_MAX = (1 << INDICATOR_BRIGHTNESS_SIZE) - 1;

// The state of the RGB LED, as persisted across reboots. It fits in a word so
// that it can be read or published in a single load or store, which is what
// lets the tasks setting it and the one driving the LED hand it over without
// locking. The fields fill the whole word, so two states equal field by field
// are equal as bytes too, which both the atomic and the check guarding the
// flash write rely on when comparing the whole word, padding included.
struct alignas(uint32_t) IndicatorState
{
    // The color, only meaningful in INDICATOR_MODE_USER.
    uint8_t r, g, b;

    uint8_t brightness : INDICATOR_BRIGHTNESS_SIZE;
    bool on : 1;
    IndicatorMode mode : 2;
};
static_assert(sizeof(IndicatorState) == sizeof(uint32_t),
              "IndicatorState must fit a word to be published atomically");

// The number of distinct brightness levels the LED can actually render,
// and so the brightness_scale advertised to Home Assistant.
static constexpr uint8_t INDICATOR_BRIGHTNESS_LEVELS = 8;

extern void ledInit();

extern void indicatorReportAqi(float pm25, float pm10);

// Apply a new state and persist it. Must be called from a single task only:
// the NVS write is not reentrant (if this ever needs more than one caller,
// move the write into the ledWorker task).
extern void indicatorSetState(const IndicatorState &state);
extern IndicatorState indicatorGetState();

#endif
