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
#include "aqi.hpp"
#include <Wire.h>

bool i2c_initialized = false;

// Publish a reading, or null when the sensor has no current value. Null tells
// Home Assistant (via the discovery availability template) that a sensor which
// was producing readings has stopped, so it shows as unavailable.
template <typename TVariant, typename TValue>
void addOptionalReading(TVariant &&target, bool hasData, TValue value)
{
    if (hasData)
    {
        target.set(value);
    }
    else
    {
        target.set(nullptr);
    }
}

void InitializeI2C()
{
    if (i2c_initialized)
    {
        return;
    }

    Wire.begin(GPIO_SDA, GPIO_SCL);
}

JsonDocument getSensorDataJson()
{
    const SensorReadings readings = getSensorReadings();
    JsonDocument doc;

    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;
#ifdef LATITUDE
    doc["station"]["location"]["latitude"] = LATITUDE;
#endif
#ifdef LONGITUDE
    doc["station"]["location"]["longitude"] = LONGITUDE;
#endif
    addOptionalReading(doc["readings"]["pm1"], readings.hasPm1, readings.pm1);
    addOptionalReading(doc["readings"]["pm25"], readings.hasPm25, readings.pm25);
    addOptionalReading(doc["readings"]["pm10"], readings.hasPm10, readings.pm10);
    addOptionalReading(
        doc["readings"]["temperature"],
        readings.hasTemperature,
        readings.temperature);
    addOptionalReading(
        doc["readings"]["humidity"],
        readings.hasHumidity,
        readings.humidity);

    if (readings.hasPm25 && readings.hasPm10)
    {
        // aqicn.org Instant AQI (InstantCast) from the averaged PM concentrations.
        AqiResult aqi = computeAqi(readings.pm25, readings.pm10);
        doc["readings"]["aqi"] = aqi.aqi;
        doc["readings"]["main_pollutant"] = aqi.pollutant;
    }
    else
    {
        doc["readings"]["aqi"] = nullptr;
        doc["readings"]["main_pollutant"] = nullptr;
    }

    if (co2SensorAvailable())
    {
        addOptionalReading(
            doc["readings"]["co2"],
            readings.hasCo2,
            readings.co2);
    }

    return doc;
}

SensorReadings getSensorReadings()
{
    SensorReadings readings;
    readings.hasPm1 = pm1.avg(readings.pm1);
    readings.hasPm25 = pm25.avg(readings.pm25);
    readings.hasPm10 = pm10.avg(readings.pm10);
    readings.hasTemperature = temperature.avg(readings.temperature);
    readings.hasHumidity = humidity.avg(readings.humidity);
    // The sensor reports whole ppm and the value is only ever read by humans,
    // where sub-ppm resolution from averaging is noise on a 400-5000 scale.
    // Rounding here keeps every consumer consistent by construction.
    float co2Avg;
    readings.hasCo2 = co2.avg(co2Avg);
    readings.co2 = (int)lroundf(co2Avg);
    return readings;
}
