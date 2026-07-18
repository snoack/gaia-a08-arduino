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
#include <Wire.h>

bool i2c_initialized = false;

void InitializeI2C()
{
    if (i2c_initialized)
    {
        return;
    }

    Wire.begin(GPIO_SDA, GPIO_SCL);
}

bool getMinimalSensorData(JsonDocument &doc)
{
    float pm25Avg;
    if (!pm25.avg(pm25Avg))
    {
        return false;
    }

    float pm1Avg, pm10Avg, temperatureAvg, humidityAvg, co2Avg;
    pm1.avg(pm1Avg);
    pm10.avg(pm10Avg);
    temperature.avg(temperatureAvg);
    humidity.avg(humidityAvg);
    bool hasCo2 = co2.avg(co2Avg);

    doc["station"]["id"] = stationID;
    doc["station"]["mac"] = mac;
    doc["station"]["location"]["latitude"] = LATITUDE;
    doc["station"]["location"]["longitude"] = LONGITUDE;
    doc["readings"]["pm1"] = pm1Avg;
    doc["readings"]["pm25"] = pm25Avg;
    doc["readings"]["pm10"] = pm10Avg;
    doc["readings"]["temperature"] = temperatureAvg;
    doc["readings"]["humidity"] = humidityAvg;

    if (hasCo2)
    {
        doc["readings"]["co2"] = round(co2Avg);
    }
    return true;
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
        doc["readings"][4]["specie"] = "co2";
        doc["readings"][4]["value"] = round(co2Avg);
        doc["readings"][4]["unit"] = "ppm";
    }

    doc["token"] = TOKEN;
    return true;
}
