#include "PMS.h"
#include "sensors.hpp"
#include "main.hpp"
#include "config.hpp"
#include "network.hpp"
#include "uploader.hpp"
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

With PMS_SAMPLE_INTERVAL set, the sensors don't run continuously. Instead,
every interval, the next sensor is warmed up, then read for SAMPLE_TIME, and
powered down again. The average of those readings is the sample that is
reported until the next one is taken. The cycle is otherwise the same: fault
detection is judged over the warm-up and the reading, and if the sensor whose
turn it is turns out faulty when warmed up, the other one is warmed up right
away to take the sample instead.

*/

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
#if PMS_SAMPLE_INTERVAL > 0
// how often the next sensor is warmed up and read for a sample
constexpr unsigned long CYCLE_TIME = PMS_SAMPLE_INTERVAL * 1000UL;
// how long a sensor is read for each sample, at most as long as the
// accumulators keep readings (a minute)
constexpr unsigned long SAMPLE_TIME = 60 * 1000;
static_assert(CYCLE_TIME >= WAIT_TIME + SAMPLE_TIME,
              "PMS_SAMPLE_INTERVAL must be at least 90 seconds");
#else
// how long each PMS sensor reports before switching to the other one
constexpr unsigned long CYCLE_TIME = 5 * 60 * 1000;
#endif

static Accumulator<int> pm1, pm25, pm10;

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
// whether anything that is published changed in this iteration of the worker
static bool publishPending;

// Written by the worker, read by the tasks reporting them.
static std::atomic<int> reportedActivePms{-1}, reportedIncomingPms{-1};
static std::atomic<PmsFault> pmsFault[NUM_PMS_SENSORS];

// The sensors report whole µg/m³, so the averages are rounded to tenths (see
// getSensorReadings()).
static void averagePmReadings(PmReadings &readings)
{
    readings.hasPm1 = pm1.avg(readings.pm1);
    readings.hasPm25 = pm25.avg(readings.pm25);
    readings.hasPm10 = pm10.avg(readings.pm10);
    readings.pm1 = roundToTenth(readings.pm1);
    readings.pm25 = roundToTenth(readings.pm25);
    readings.pm10 = roundToTenth(readings.pm10);
}

#if PMS_SAMPLE_INTERVAL > 0
// The last sample when sampling (none taken yet while zero-initialized). Only
// the worker writes it, as a seqlock: sampleSeq is odd while the sample is being
// updated, and readers retry if it was odd or changed. The sample itself isn't
// atomic, so that it can be copied as a whole; the fences keep the copy between
// the sequence accesses on each side.
static PmReadings sample;
static std::atomic<unsigned> sampleSeq{0};

void getPmReadings(PmReadings &readings)
{
    unsigned seq;
    do
    {
        seq = sampleSeq.load(std::memory_order_acquire);
        readings = sample;
        std::atomic_thread_fence(std::memory_order_acquire);
    } while (seq & 1 || seq != sampleSeq.load(std::memory_order_relaxed));
}

// Average what the sensor reported this cycle into the sample, and have it
// published and uploaded.
static void takeSample()
{
    PmReadings s;
    averagePmReadings(s);

    unsigned seq = sampleSeq.load(std::memory_order_relaxed);
    sampleSeq.store(++seq, std::memory_order_relaxed); // odd: being updated
    std::atomic_thread_fence(std::memory_order_release);
    sample = s;
    sampleSeq.store(++seq, std::memory_order_release); // even: done

    publishPending = true;
#ifdef ANY_UPLOADER_ENABLED
    uploaderUploadSoon();
#endif
}
#else
void getPmReadings(PmReadings &readings)
{
    averagePmReadings(readings);
}
#endif

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

static bool otherSensorHealthy(int pmsNum)
{
    int other = (pmsNum + 1) % NUM_PMS_SENSORS;
    return other != pmsNum && sensorHealthy(other);
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
        publishPending = true;
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
        3072,              // Stack size (bytes)
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
            pm1.add(pmsData.PM_AE_UG_1_0);
            pm25.add(pmsData.PM_AE_UG_2_5);
            pm10.add(pmsData.PM_AE_UG_10_0);
        }
    }
}

void pmsSensorWorker(void *parameters)
{
    int activePms = -1;
    int incomingPms = -1;
    // the sensor warmed up last, to take turns while none is active
    int lastPms = -1;
    unsigned long cycleStart = 0;
    unsigned long warmStart = 0;
#if PMS_SAMPLE_INTERVAL > 0
    unsigned long readStart = 0;
    // whether this cycle's sample has been taken
    bool sampled = false;
#endif

    // Start with every sensor powered down; the state machine below powers one
    // up on the first iteration (no active sensor counts as unhealthy).
    for (int i = 0; i < NUM_PMS_SENSORS; i++)
    {
        pinMode(enablePins[i], OUTPUT);
        digitalWrite(enablePins[i], LOW);
    }

    while (1)
    {
#if PMS_SAMPLE_INTERVAL > 0
        if (activePms != -1 && millis() - readStart >= SAMPLE_TIME)
        {
            takeSample();
            digitalWrite(enablePins[activePms], LOW);
            Serial.printf("Took sample from PMS sensor %d\n", activePms + 1);
            activePms = -1;
            sampled = true;
        }
#endif
        if (incomingPms == -1)
        {
            // Move on every cycle, or right away if the next sensor isn't known
            // to be unhealthy and is needed: when sampling, if none is active
            // yet and this cycle's sample is still missing, otherwise if the
            // active sensor is unhealthy.
            int nextPms = ((activePms != -1 ? activePms : lastPms) + 1) % NUM_PMS_SENSORS;
#if PMS_SAMPLE_INTERVAL > 0
            bool canMoveOn = activePms == -1;
            bool needed = !sampled;
#else
            bool canMoveOn = nextPms != activePms;
            bool needed = !sensorHealthy(activePms);
#endif
            bool due = millis() - cycleStart >= CYCLE_TIME;
            if (canMoveOn && (due || (needed && sensorHealthy(nextPms))))
            {
                // Power up the incoming sensor, make-before-break if another one
                // is active, then start counting down its warm-up time.
                incomingPms = lastPms = nextPms;
#if PMS_SAMPLE_INTERVAL > 0
                sampled = false;
#endif
                digitalWrite(enablePins[incomingPms], HIGH);
                Serial.printf("Warming up PMS sensor %d\n", incomingPms + 1);
                warmStart = millis();
                // Moving on early keeps to the cycle, so that falling back from
                // a faulty sensor doesn't delay the next cycle.
                if (due)
                {
                    cycleStart = warmStart;
                }
                // Judge it only by what it sends during this warm-up.
                windowFrames[incomingPms] = windowImplausible[incomingPms] = 0;
                lastFrame[incomingPms] = warmStart;
            }
        }
        else if (millis() - warmStart >= WAIT_TIME)
        {
            // The incoming sensor is warm. It takes over unless it's faulty
            // and the other one isn't; otherwise it goes back to standby
            // until the next cycle.
            updateFault(incomingPms, /*endOfWindow=*/true);
            if (sensorHealthy(incomingPms) || !otherSensorHealthy(incomingPms))
            {
                if (activePms != -1)
                {
                    digitalWrite(enablePins[activePms], LOW);
                }
                Serial.printf("Switching to PMS sensor %d\n", incomingPms + 1);
                activePms = incomingPms;
#if PMS_SAMPLE_INTERVAL > 0
                // The sample covers only this reading: readings left over
                // from the previous one are at least CYCLE_TIME old by the
                // time the sample is taken, so they have expired.
                readStart = millis();
#endif
            }
            else
            {
                digitalWrite(enablePins[incomingPms], LOW);
                Serial.printf("Not switching to faulty PMS sensor %d\n", incomingPms + 1);
            }
            incomingPms = -1;
        }
        if (activePms != reportedActivePms.load(std::memory_order_relaxed) ||
            incomingPms != reportedIncomingPms.load(std::memory_order_relaxed))
        {
            reportedActivePms.store(activePms, std::memory_order_relaxed);
            reportedIncomingPms.store(incomingPms, std::memory_order_relaxed);
            publishPending = true;
        }

        if (activePms != -1)
        {
            readSensor(activePms, sensorHealthy(activePms));
            updateFault(activePms);
        }
        if (incomingPms != -1)
        {
            readSensor(incomingPms, false);
        }
        // Publish once per iteration, so that changes happening together are
        // published together.
        if (publishPending)
        {
#ifdef CONF_MQTT
            mqttPublishSoon();
#endif
            publishPending = false;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
