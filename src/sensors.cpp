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
template <typename TVariant>
void addOptionalReading(TVariant &&target, bool hasData, float value)
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

void getMinimalSensorData(JsonDocument &doc)
{
    bool hasPm1 = pm1.hasData();
    bool hasPm25 = pm25.hasData();
    bool hasPm10 = pm10.hasData();
    bool hasTemperature = temperature.hasData();
    bool hasHumidity = humidity.hasData();
    float pm1Avg = pm1.avg();
    float pm25Avg = pm25.avg();
    float pm10Avg = pm10.avg();
    float temperatureAvg = temperature.avg();
    float humidityAvg = humidity.avg();

    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;

    doc["station"]["location"]["latitude"] = LATITUDE;
    doc["station"]["location"]["longitude"] = LONGITUDE;
    addOptionalReading(doc["readings"]["pm1"], hasPm1, pm1Avg);
    addOptionalReading(doc["readings"]["pm25"], hasPm25, pm25Avg);
    addOptionalReading(doc["readings"]["pm10"], hasPm10, pm10Avg);
    addOptionalReading(doc["readings"]["temperature"], hasTemperature, temperatureAvg);
    addOptionalReading(doc["readings"]["humidity"], hasHumidity, humidityAvg);

    if (hasPm25 && hasPm10)
    {
        // aqicn.org Instant AQI (InstantCast) from the averaged PM concentrations.
        AqiResult aqi = computeAqi(pm25Avg, pm10Avg);
        doc["readings"]["aqi"] = aqi.aqi;
        doc["readings"]["main_pollutant"] = aqi.pollutant;
    }
    else
    {
        doc["readings"]["aqi"] = nullptr;
        doc["readings"]["main_pollutant"] = nullptr;
    }

    if (co2.hasData())
    {
        doc["readings"]["co2"] = round(co2.avg());
    }
}

bool getSerialisedSensorData(JsonDocument &doc)
{
    if (!pm25.hasData())
    {
        return false;
    }

    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;

    doc["station"]["location"]["latitude"] = LATITUDE;
    doc["station"]["location"]["longitude"] = LONGITUDE;

    doc["readings"][0]["specie"] = "pm25";
    doc["readings"][0]["value"] = pm25.avg();
    doc["readings"][0]["unit"] = "µg/m3";

    doc["readings"][1]["specie"] = "pm10";
    doc["readings"][1]["value"] = pm10.avg();
    doc["readings"][1]["unit"] = "µg/m3";

    doc["readings"][2]["specie"] = "pm1";
    doc["readings"][2]["value"] = pm1.avg();
    doc["readings"][2]["unit"] = "µg/m3";

    doc["readings"][3]["specie"] = "temperature";
    doc["readings"][3]["value"] = temperature.avg();
    doc["readings"][3]["unit"] = "C";

    doc["readings"][4]["specie"] = "humidity";
    doc["readings"][4]["value"] = humidity.avg();
    doc["readings"][4]["unit"] = "%";

    if (co2.hasData())
    {
        doc["readings"][4]["specie"] = "co2";
        doc["readings"][4]["value"] = round(co2.avg());
        doc["readings"][4]["unit"] = "ppm";
    }

    doc["token"] = TOKEN;
    return true;
}
