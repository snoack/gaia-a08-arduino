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
    float pm1Avg, pm25Avg, pm10Avg, temperatureAvg, humidityAvg;
    bool hasPm1 = pm1.avg(pm1Avg);
    bool hasPm25 = pm25.avg(pm25Avg);
    bool hasPm10 = pm10.avg(pm10Avg);
    bool hasTemperature = temperature.avg(temperatureAvg);
    bool hasHumidity = humidity.avg(humidityAvg);

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

    if (co2SensorAvailable())
    {
        float co2Avg;
        bool hasCo2 = co2.avg(co2Avg);
        addOptionalReading(doc["readings"]["co2"], hasCo2, round(co2Avg));
    }
}

bool getSerialisedSensorData(JsonDocument &doc)
{
    float pm25Avg;
    if (!pm25.avg(pm25Avg))
    {
        return false;
    }

    float pm10Avg, pm1Avg, temperatureAvg, humidityAvg, co2Avg;
    pm10.avg(pm10Avg);
    pm1.avg(pm1Avg);
    temperature.avg(temperatureAvg);
    humidity.avg(humidityAvg);
    bool hasCo2 = co2.avg(co2Avg);

    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;

    doc["station"]["location"]["latitude"] = LATITUDE;
    doc["station"]["location"]["longitude"] = LONGITUDE;

    doc["readings"][0]["specie"] = "pm25";
    doc["readings"][0]["value"] = pm25Avg;
    doc["readings"][0]["unit"] = "µg/m3";

    doc["readings"][1]["specie"] = "pm10";
    doc["readings"][1]["value"] = pm10Avg;
    doc["readings"][1]["unit"] = "µg/m3";

    doc["readings"][2]["specie"] = "pm1";
    doc["readings"][2]["value"] = pm1Avg;
    doc["readings"][2]["unit"] = "µg/m3";

    doc["readings"][3]["specie"] = "temperature";
    doc["readings"][3]["value"] = temperatureAvg;
    doc["readings"][3]["unit"] = "C";

    doc["readings"][4]["specie"] = "humidity";
    doc["readings"][4]["value"] = humidityAvg;
    doc["readings"][4]["unit"] = "%";

    if (hasCo2)
    {
        doc["readings"][5]["specie"] = "co2";
        doc["readings"][5]["value"] = round(co2Avg);
        doc["readings"][5]["unit"] = "ppm";
    }

    doc["token"] = TOKEN;
    return true;
}
