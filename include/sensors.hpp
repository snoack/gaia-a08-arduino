#ifndef _GAIA_SENSORS
#define _GAIA_SENSORS

#include "accumulator.hpp"
#include <ArduinoJson.h>

// Sensor init functions
extern void scd4xSensorInit();
extern void pmsSensorInit();
extern void metSensorInit();

void InitializeI2C();

// Sensor value accumulators
extern Accumulator<int> co2;
extern Accumulator<float> temperature, humidity;
extern bool co2SensorAvailable();

// PMS sensors are numbered from 0. The status says what the sensor is used for
// right now. The fault is the last one detected, held until the sensor shows
// that it has recovered, also while it is on standby.
enum PmsStatus
{
    PMS_STANDBY,
    PMS_WARMING_UP,
    PMS_READING,
    PMS_STATUS_COUNT,
};
enum PmsFault
{
    PMS_FAULT_NONE,
    PMS_FAULT_IMPLAUSIBLE,
    PMS_FAULT_UNRESPONSIVE,
    PMS_FAULT_COUNT,
};
// Names used in the JSON payload and log; PMS_FAULT_NONE's name is null.
extern const char *const pmsStatusNames[PMS_STATUS_COUNT];
extern const char *const pmsFaultNames[PMS_FAULT_COUNT];
extern PmsStatus pmsSensorStatus(int pmsNum);
extern PmsFault pmsSensorFault(int pmsNum);

struct PmReadings
{
    float pm1, pm25, pm10;
    bool hasPm1, hasPm25, hasPm10;
};

struct SensorReadings : PmReadings
{
    float temperature;
    int humidity;
    int co2;
    bool hasTemperature, hasHumidity;
    bool hasCo2;
};

extern SensorReadings getSensorReadings();
// Fill in the PM readings: the live averages, or with PMS_SAMPLE_INTERVAL set,
// the last sample's.
extern void getPmReadings(PmReadings &readings);
extern JsonDocument getSensorDataJson();
extern bool i2c_initialized;

#endif // _GAIA_SENSORS
