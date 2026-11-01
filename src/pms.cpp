#include "PMS.h"
#include "sensors.hpp"
#include "main.hpp"
#include "config.hpp"
#include "indicator.hpp"
#include <atomic>

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

A failing sensor can keep sending well-formed frames full of nonsense (e.g.
thousands of µg/m³ in every size bin), so each frame is also checked for
physical plausibility, and implausible frames are dropped. To tell a failing
sensor from the odd implausible frame, sensors are judged by the share of
implausible frames in windows of WINDOW_FRAMES frames (or a whole warm-up). A
sensor enters fault mode once at least half of a window is implausible, or
after UNRESPONSIVE_TIMEOUT without any frame. It leaves fault mode only after a
window with at most a tenth implausible, so a sensor that is bad around half
the time doesn't flip in and out of fault mode.

The other sensor then takes over right away and reports for whole cycles. The
faulty sensor is still warmed up on each duty cycle as usual, which serves as
its recovery check: if it's still faulty at the end of the warm-up, it goes
back to standby instead of taking over. If both sensors are faulty, they keep
alternating, so that each one gets checked.

*/

// how long each PMS sensor reports before switching to the other one
constexpr unsigned long DUTY_CYCLE = 5 * 60 * 1000;
// warm up time before a newly activated sensor's samples are used
constexpr unsigned long WAIT_TIME = 30 * 1000;
// frames per window in which a reading sensor's implausible share is judged
constexpr int WINDOW_FRAMES = 20;
// how long a powered sensor may go without sending any frame
constexpr unsigned long UNRESPONSIVE_TIMEOUT = 10 * 1000;
// The PMS5003 datasheet gives a maximum range of at least 1000 µg/m³. Allow
// for real readings somewhat beyond it (e.g. in dense smoke), while staying
// well below the values seen from failed sensors (over 3000 µg/m³). The AQI
// saturates far below either.
constexpr uint16_t PM_MAX_PLAUSIBLE = 2000;
// how long without reported readings before the indicator shows no data,
// matching how long the accumulators keep readings
constexpr unsigned long NO_DATA_TIMEOUT = 60 * 1000;

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

const char *const pmsStatusNames[PMS_STATUS_COUNT] = {"standby", "warming_up", "reading"};
const char *const pmsFaultNames[PMS_FAULT_COUNT] = {nullptr, "implausible", "unresponsive"};

// Only the worker touches these.
static int windowFrames[NUM_PMS_SENSORS], windowImplausible[NUM_PMS_SENSORS];
static unsigned long lastFrame[NUM_PMS_SENSORS];
static unsigned long lastReported;

// Written by the worker, read by the tasks reporting them.
static std::atomic<int> reportedActivePms{-1}, reportedIncomingPms{-1};
static std::atomic<PmsFault> pmsFault[NUM_PMS_SENSORS];

PmsStatus pmsSensorStatus(int pmsNum)
{
    return pmsNum == reportedActivePms.load(std::memory_order_relaxed)     ? PMS_READING
           : pmsNum == reportedIncomingPms.load(std::memory_order_relaxed) ? PMS_WARMING_UP
                                                                           : PMS_STANDBY;
}

PmsFault pmsSensorFault(int pmsNum)
{
    return pmsFault[pmsNum].load(std::memory_order_relaxed);
}

// A missing sensor (-1) counts as unhealthy, so the initial warm-up is just a
// sensor taking over from an unhealthy one.
static bool sensorHealthy(int pmsNum)
{
    return pmsNum != -1 && pmsSensorFault(pmsNum) == PMS_FAULT_NONE;
}

// Enter or leave fault mode once a window is complete, or at the end of a
// warm-up (endOfWindow), which serves as a window of its own.
static void updateFault(int pmsNum, bool endOfWindow = false)
{
    PmsFault previous = pmsSensorFault(pmsNum);
    PmsFault fault = previous;
    if (millis() - lastFrame[pmsNum] >= UNRESPONSIVE_TIMEOUT)
    {
        // Judge the sensor only by what it sends once it's back.
        fault = PMS_FAULT_UNRESPONSIVE;
        windowFrames[pmsNum] = windowImplausible[pmsNum] = 0;
    }
    else if (endOfWindow || windowFrames[pmsNum] >= WINDOW_FRAMES)
    {
        if (windowImplausible[pmsNum] * 2 >= windowFrames[pmsNum])
        {
            fault = PMS_FAULT_IMPLAUSIBLE;
        }
        else if (windowImplausible[pmsNum] * 10 <= windowFrames[pmsNum])
        {
            fault = PMS_FAULT_NONE;
        }
        else if (fault == PMS_FAULT_UNRESPONSIVE)
        {
            // Responsive again, but not recovered yet.
            fault = PMS_FAULT_IMPLAUSIBLE;
        }
        windowFrames[pmsNum] = windowImplausible[pmsNum] = 0;
    }

    if (fault != previous)
    {
        // Only this task writes the fault, so no read-modify-write is needed.
        pmsFault[pmsNum].store(fault, std::memory_order_relaxed);
        Serial.printf("PMS sensor %d fault: %s\n", pmsNum + 1,
                      fault == PMS_FAULT_NONE ? "none" : pmsFaultNames[fault]);
    }
}

// The mass concentrations are cumulative (PM1 is part of PM2.5, which is part
// of PM10), so they can never decrease with particle size.
static bool framePlausible(const PMS::DATA &data)
{
    return data.PM_AE_UG_1_0 <= data.PM_AE_UG_2_5 &&
           data.PM_AE_UG_2_5 <= data.PM_AE_UG_10_0 &&
           data.PM_AE_UG_10_0 <= PM_MAX_PLAUSIBLE;
}

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
// the active sensor, while not faulty, feeds the accumulators; other powered
// sensors are still read (and their raw values dumped) to judge their health.
void readSensor(int pmsNum, bool report)
{
    PMS::DATA pmsData;
    if (pmsReaders[pmsNum].readUntil(pmsData, 100))
    {
        bool plausible = framePlausible(pmsData);
#ifdef DEBUG_SENSOR_VALUES
        char s[80];
        snprintf(
            s,
            sizeof(s),
            "PMS%1d:  PM1.0: %5d, PM 2.5: %5d, PM10: %5d%s%s",
            pmsNum + 1,
            pmsData.PM_AE_UG_1_0,
            pmsData.PM_AE_UG_2_5,
            pmsData.PM_AE_UG_10_0,
            report ? "" : " (not reported)",
            plausible ? "" : " (implausible)");
        Serial.println(s);
#endif
        lastFrame[pmsNum] = millis();
        windowFrames[pmsNum]++;
        if (!plausible)
        {
            windowImplausible[pmsNum]++;
            return;
        }
        if (report)
        {
            lastReported = millis();
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
    // up on the first iteration (no active sensor counts as unhealthy).
    for (int i = 0; i < NUM_PMS_SENSORS; i++)
    {
        pinMode(enablePins[i], OUTPUT);
        digitalWrite(enablePins[i], LOW);
    }

    while (1)
    {
        if (incomingPms == -1)
        {
            // Move on every duty cycle, or right away if the active sensor is
            // unhealthy and the next one isn't known to be.
            int nextPms = (activePms + 1) % NUM_PMS_SENSORS;
            if (nextPms != activePms &&
                (millis() - cycleStart >= DUTY_CYCLE ||
                 (!sensorHealthy(activePms) && sensorHealthy(nextPms))))
            {
                // Make-before-break: power up the incoming sensor while the active
                // one keeps reporting, then start counting down its warm-up time.
                incomingPms = nextPms;
                digitalWrite(enablePins[incomingPms], HIGH);
                Serial.printf("Warming up PMS sensor %d\n", incomingPms + 1);
                cycleStart = millis();
                // Judge it only by what it sends during this warm-up.
                windowFrames[incomingPms] = windowImplausible[incomingPms] = 0;
                lastFrame[incomingPms] = cycleStart;
            }
        }
        else if (millis() - cycleStart >= WAIT_TIME)
        {
            // The incoming sensor is warm. It takes over unless it's faulty
            // and the active one isn't; otherwise it goes back to standby
            // until the next duty cycle.
            updateFault(incomingPms, /*endOfWindow=*/true);
            if (sensorHealthy(incomingPms) || !sensorHealthy(activePms))
            {
                if (activePms != -1)
                {
                    digitalWrite(enablePins[activePms], LOW);
                }
                Serial.printf("Switching to PMS sensor %d\n", incomingPms + 1);
                activePms = incomingPms;
            }
            else
            {
                digitalWrite(enablePins[incomingPms], LOW);
                Serial.printf("Not switching to faulty PMS sensor %d\n", incomingPms + 1);
            }
            incomingPms = -1;
        }
        reportedActivePms.store(activePms, std::memory_order_relaxed);
        reportedIncomingPms.store(incomingPms, std::memory_order_relaxed);

        if (activePms != -1)
        {
            readSensor(activePms, sensorHealthy(activePms));
            updateFault(activePms);
        }
        if (incomingPms != -1)
        {
            readSensor(incomingPms, false);
        }
        if (millis() - lastReported >= NO_DATA_TIMEOUT)
        {
            indicatorClearAqi();
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
