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

#pragma once
#include <Arduino.h>
#include <atomic>

template <typename T>
class Accumulator
{
    // The fastest sensor produces one reading per second, so 60 slots are
    // enough to retain every reading from the last minute.
    static constexpr int CAPACITY = 60;
    static constexpr unsigned long MAX_AGE = 60 * 1000;
    // Reserve the high bit as a validity flag and use the remaining bits for
    // rollover-safe elapsed-time comparisons.
    static constexpr unsigned long TIMESTAMP_VALID = 1UL << 31;
    static constexpr unsigned long TIMESTAMP_MASK = ~TIMESTAMP_VALID;

    struct Sample
    {
        std::atomic<T> val;
        std::atomic<unsigned long> timestamp;
    };

    Sample samples[CAPACITY];
    int writeIndex = 0;

public:
    Accumulator()
    {
        for (int i = 0; i < CAPACITY; i++)
        {
            samples[i].timestamp.store(0, std::memory_order_relaxed);
        }
    }

    // Concurrent readers may invalidate expired timestamps, but only one task
    // may add samples because writeIndex is not synchronized.
    void add(T val)
    {
        // Seqlock: invalidate the slot while updating it; readers accept it only
        // when they see the same valid timestamp before and after the value.
        samples[writeIndex].timestamp.store(0);
        samples[writeIndex].val.store(val);
        unsigned long timestamp = millis() & TIMESTAMP_MASK;
        samples[writeIndex].timestamp.store(timestamp | TIMESTAMP_VALID);
        writeIndex = (writeIndex + 1) % CAPACITY;
    }

    bool avg(float &result)
    {
        float t = 0;
        int count = 0;
        unsigned long now = millis() & TIMESTAMP_MASK;
        for (int i = 0; i < CAPACITY; i++)
        {
            unsigned long timestamp = samples[i].timestamp.load();
            if (!(timestamp & TIMESTAMP_VALID))
            {
                continue;
            }
            if (((now - (timestamp & TIMESTAMP_MASK)) & TIMESTAMP_MASK) > MAX_AGE)
            {
                samples[i].timestamp.compare_exchange_strong(timestamp, 0);
                continue;
            }

            T val = samples[i].val.load();
            if (timestamp != samples[i].timestamp.load())
            {
                continue;
            }

            t += val;
            count++;
        }
        result = count ? t / count : 0;
        return count != 0;
    }
};
