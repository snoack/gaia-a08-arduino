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

#include "aqi.hpp"

// Pollutants, used to index the concentration range within each AQI band.
enum Pollutant
{
    POLLUTANT_PM25 = 0,
    POLLUTANT_PM10,
    POLLUTANT_COUNT,
};

// A concentration range [low, high] in µg/m³.
struct Range
{
    float low;
    float high;
};

// One EPA AQI band: the shared AQI range [aqiLow, aqiHigh] plus the
// concentration range that maps onto it for each pollutant. Rows and cutoffs
// are as used by aqicn.org; the PM cutoffs match those the firmware
// historically used for the LED color.
struct AqiBand
{
    int aqiLow;
    int aqiHigh;
    AqiCategory category;
    Range pollutants[POLLUTANT_COUNT];
};

static const AqiBand aqi_bands[] = {
    //  AQI                  category               PM2.5              PM10
    { 0,   50, AQI_GOOD,                {{  0.0f,  12.0f}, {  0.0f,  54.0f}}},
    { 51, 100, AQI_MODERATE,            {{ 12.1f,  35.4f}, { 55.0f, 154.0f}}},
    {101, 150, AQI_UNHEALTHY_SENSITIVE, {{ 35.5f,  55.4f}, {155.0f, 254.0f}}},
    {151, 200, AQI_UNHEALTHY,           {{ 55.5f, 150.4f}, {255.0f, 354.0f}}},
    {201, 300, AQI_VERY_UNHEALTHY,      {{150.5f, 250.4f}, {355.0f, 424.0f}}},
    {301, 400, AQI_HAZARDOUS,           {{250.5f, 350.4f}, {425.0f, 504.0f}}},
    {401, 500, AQI_HAZARDOUS,           {{350.5f, 500.4f}, {505.0f, 604.0f}}},
};

static constexpr int AQI_BAND_COUNT = sizeof(aqi_bands) / sizeof(aqi_bands[0]);

static int concentrationToAqi(float c, Pollutant p)
{
    for (int i = 0; i < AQI_BAND_COUNT; i++)
    {
        const AqiBand &band = aqi_bands[i];
        const Range &range = band.pollutants[p];
        if (c <= range.high)
        {
            // Piecewise-linear interpolation within the band.
            return (int)(
                (band.aqiHigh - band.aqiLow) /
                (range.high - range.low) *
                (c - range.low) + band.aqiLow + 0.5f
            );
        }
    }

    // Above the top of the table: clamp to the maximum index.
    return aqi_bands[AQI_BAND_COUNT - 1].aqiHigh;
}

AqiResult computeAqi(float pm25, float pm10)
{
    int pm25Aqi = concentrationToAqi(pm25, POLLUTANT_PM25);
    int pm10Aqi = concentrationToAqi(pm10, POLLUTANT_PM10);
    if (pm25Aqi >= pm10Aqi)
    {
        return {pm25Aqi, "pm25"};
    }
    return {pm10Aqi, "pm10"};
}

AqiCategory aqiCategory(int aqi)
{
    for (int i = 0; i < AQI_BAND_COUNT; i++)
    {
        if (aqi <= aqi_bands[i].aqiHigh)
        {
            return aqi_bands[i].category;
        }
    }

    // Above the top of the table: clamp to the worst category.
    return AQI_HAZARDOUS;
}
