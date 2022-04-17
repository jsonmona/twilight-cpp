#include "LocalClock.h"

LocalClock::LocalClock() : epoch(std::chrono::steady_clock::now()) {}

LocalClock::~LocalClock() {}

std::chrono::microseconds LocalClock::time() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - epoch);
}
