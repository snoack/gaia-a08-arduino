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

#ifndef _GAIA_AQI
#define _GAIA_AQI

// Air Quality Index using the aqicn.org conversion: the Instant AQI (aka
// InstantCast), i.e. no time averaging, applied over the PM breakpoint tables.
// The AQI is the maximum of the per-pollutant sub-indices.
// See https://aqicn.org/faq/2015-03-15/air-quality-nowcast-a-beginners-guide/

// AQI category bands. Ordered from best to worst air quality; the underlying
// values (0..) are used to index per-category tables such as the indicator
// colors. AQI_CATEGORY_COUNT must stay last: it auto-counts the categories.
enum AqiCategory
{
    AQI_GOOD = 0,
    AQI_MODERATE,
    AQI_UNHEALTHY_SENSITIVE,
    AQI_UNHEALTHY,
    AQI_VERY_UNHEALTHY,
    AQI_HAZARDOUS,
    AQI_CATEGORY_COUNT,
};

// The combined AQI plus the pollutant that drove it.
struct AqiResult
{
    // The largest per-pollutant sub-index.
    int aqi;
    // The pollutant driving the AQI, as a specie string ("pm25", "pm10", ...).
    const char *pollutant;
};
AqiResult computeAqi(float pm25, float pm10);

// The category band and its inclusive AQI bounds.
struct AqiCategoryResult
{
    AqiCategory category;
    int aqiLow;
    int aqiHigh;
};
AqiCategoryResult aqiCategory(int aqi);

#endif // _GAIA_AQI
