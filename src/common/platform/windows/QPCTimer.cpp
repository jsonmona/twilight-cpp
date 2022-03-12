#include "QPCTimer.h"

static uint64_t qpcFreq;

static uint64_t qpc() {
    LARGE_INTEGER val;
    QueryPerformanceCounter(&val);
    return static_cast<uint64_t>(val.QuadPart);
}

QPCTimer::QPCTimer() {
    static_assert(sizeof(LARGE_INTEGER::QuadPart) == sizeof(decltype(qpcFreq)), "LARGE_INTEGER must be 64-bits wide");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcFreq = static_cast<uint64_t>(freq.QuadPart);

    referencePoint = 0;
}

void QPCTimer::setFrequency(Rational freq) {
    setInterval(freq.inv());
}

void QPCTimer::setInterval(Rational interval) {
    referencePoint = qpc();
    ticks = 0;
    intervalNumerator = interval.num() * qpcFreq;
    intervalDenominator = interval.den();
}

bool QPCTimer::checkInterval() {
    uint64_t diff = qpc() - referencePoint;
    uint64_t nowTick = diff * intervalNumerator / intervalDenominator;

    if (ticks <= nowTick) {
        ticks = nowTick + 1;
        return true;
    }
    return false;
}
