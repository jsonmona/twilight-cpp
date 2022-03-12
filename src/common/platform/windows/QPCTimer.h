#ifndef TWILIGHT_COMMON_PLATFORM_WINDOWS_QPCTIMER_H
#define TWILIGHT_COMMON_PLATFORM_WINDOWS_QPCTIMER_H

#include <cstdint>

#include "common/Rational.h"
#include "common/log.h"

#include "common/platform/windows/winheaders.h"

class QPCTimer {
public:
    QPCTimer();
    QPCTimer(const QPCTimer& copy) = default;
    QPCTimer(QPCTimer&& move) = default;

    // unit in Hz
    void setFrequency(Rational freq);

    // unit in seconds
    void setInterval(Rational interval);

    bool checkInterval();

private:
    uint64_t intervalNumerator;
    uint64_t intervalDenominator;
    uint64_t referencePoint;
    uint64_t ticks;
};

#endif