#include "PMS.h"
#include "sensors.hpp"
#include "main.hpp"
#include "config.hpp"
#include "indicator.hpp"

/*

When using two PMS sensors, the sensors are activated alternately to roughly
halve each one's runtime and extend its lifespan: one sensor reports for the
duration of the duty cycle, then the next sensor takes over.

Activating a sensor can be done by pulling up the PMS enable pin.
By defualt, the pin is pulled-up. So only the deactivation is needed,
and the deactivation is done by pulling down the pin.

When transitioning from deactivated to active, the sensor fan starts to spin.
It is common practice to wait 30 seconds before using the sensor values.

To avoid a gap in the readings across a switch, the handover is make-before-
break: the incoming sensor is powered up while the outgoing sensor keeps
reporting, and only once the incoming sensor has warmed up does it take over
and the outgoing sensor is powered down. Both sensors sample the same air, so
the brief overlap causes no discontinuity in the pooled accumulators.

*/

// how long each PMS sensor reports before switching to the other one
constexpr unsigned long DUTY_CYCLE = 5 * 60 * 1000;
// warm up time before a newly activated sensor's samples are used
constexpr unsigned long WAIT_TIME = 30 * 1000;

Accumulator<int> pm1, pm25, pm10;

void pmsSensorWorker(void *);

constexpr int enablePins[NUM_PMS_SENSORS] = {
    GPIO_PMS1_EN,
#if NUM_PMS_SENSORS == 2
    GPIO_PMS2_EN,
#endif
};

PMS pmsReaders[NUM_PMS_SENSORS] = {
    PMS(Serial0),
#if NUM_PMS_SENSORS == 2
    PMS(Serial1),
#endif
};

void pmsSensorInit()
{
    pinMode(GPIO_5V_PWR_EN, OUTPUT);
    digitalWrite(GPIO_5V_PWR_EN, HIGH);

    Serial0.begin(9600, SERIAL_8N1, GPIO_PMS1_RX, -1);
#if NUM_PMS_SENSORS == 2
    Serial1.begin(9600, SERIAL_8N1, GPIO_PMS2_RX, -1);
#endif

    xTaskCreate(
        pmsSensorWorker,   // Function that should be called
        "pmsSensorWorker", // Name of the task (for debugging)
        2048,              // Stack size (bytes)
        NULL,              // Parameter to pass
        3,                 // Task priority - medium
        NULL               // Task handle
    );
}

// Drain any available frame from a sensor so its UART buffer stays fresh. Only
// the sensor we currently trust feeds the accumulators; the warming-up sensor
// is still read (and its raw values dumped) so we can keep its data path live.
void readSensor(int pmsNum, bool report)
{
    PMS::DATA pmsData;
    if (pmsReaders[pmsNum].readUntil(pmsData, 100))
    {
#ifdef DEBUG_SENSOR_VALUES
        char s[64];
        snprintf(
            s,
            sizeof(s),
            "PMS%1d:  PM1.0: %5d, PM 2.5: %5d, PM10: %5d%s",
            pmsNum + 1,
            pmsData.PM_AE_UG_1_0,
            pmsData.PM_AE_UG_2_5,
            pmsData.PM_AE_UG_10_0,
            report ? "" : " (warming up)");
        Serial.println(s);
#endif
        if (report)
        {
            pm1.add(pmsData.PM_AE_UG_1_0);
            pm25.add(pmsData.PM_AE_UG_2_5);
            pm10.add(pmsData.PM_AE_UG_10_0);
            indicatorReportAqi(pmsData.PM_AE_UG_2_5, pmsData.PM_AE_UG_10_0);
        }
    }
}

void pmsSensorWorker(void *parameters)
{
    int activePms = -1;
    int incomingPms = -1;
    unsigned long cycleStart = 0;

    // Start with every sensor powered down; the state machine below powers one
    // up on the first iteration (activePms == -1) as the initial warm-up.
    for (int i = 0; i < NUM_PMS_SENSORS; i++)
    {
        pinMode(enablePins[i], OUTPUT);
        digitalWrite(enablePins[i], LOW);
    }

    while (1)
    {
        if (incomingPms == -1)
        {
            if (activePms == -1 ||
                (NUM_PMS_SENSORS > 1 && millis() - cycleStart >= DUTY_CYCLE))
            {
                // Make-before-break: power up the incoming sensor while the active
                // one keeps reporting, then start counting down its warm-up time.
                incomingPms = (activePms + 1) % NUM_PMS_SENSORS;
                digitalWrite(enablePins[incomingPms], HIGH);
                Serial.printf("Warming up PMS sensor %d\n", incomingPms + 1);
                cycleStart = millis();
            }
        }
        else if (millis() - cycleStart >= WAIT_TIME)
        {
            // The incoming sensor is warm: hand over and power the outgoing
            // one down. On first warm-up there is no outgoing sensor yet.
            if (activePms != -1)
            {
                digitalWrite(enablePins[activePms], LOW);
            }
            Serial.printf("Switching to PMS sensor %d\n", incomingPms + 1);
            activePms = incomingPms;
            incomingPms = -1;
        }

        if (activePms != -1)
        {
            readSensor(activePms, true);
        }
        if (incomingPms != -1)
        {
            readSensor(incomingPms, false);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
