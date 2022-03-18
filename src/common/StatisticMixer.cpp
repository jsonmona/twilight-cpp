#include "StatisticMixer.h"

#include <algorithm>
#include <cmath>
#include <limits>

StatisticMixer::StatisticMixer(size_t initialSize)
    : log(createNamedLogger("StatisticMixer")), arr(initialSize), arrIdx(0) {
    std::fill(arr.begin(), arr.end(), std::numeric_limits<float>::quiet_NaN());
}

StatisticMixer::~StatisticMixer() {
    static_assert(std::numeric_limits<float>::has_quiet_NaN, "Current design requires quiet NaN");
}

void StatisticMixer::setPoolSize(int samples) {
    int prevSize = arr.size();
    if (prevSize == samples)
        return;

    arr.resize(samples);

    if (samples < prevSize)
        arrIdx = samples <= arrIdx ? 0 : arrIdx;
    else
        std::fill(arr.begin() + prevSize, arr.end(), std::numeric_limits<float>::quiet_NaN());
}

void StatisticMixer::pushValue(float val) {
    arr[arrIdx++] = val;
    if (arrIdx >= arr.size())
        arrIdx = 0;
}

StatisticMixer::Stat StatisticMixer::calcStat(bool skipNaN) {
    Stat ret = {};

    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::min();
    double sum = 0;
    for (float now : arr) {
        if (skipNaN && std::isnan(now))
            break;
        min = std::min(min, now);
        max = std::max(max, now);
        sum += now;
    }

    int cnt = 0;
    float avg = static_cast<float>(sum / arr.size());
    double deviation = 0;
    for (float now : arr) {
        if (skipNaN && std::isnan(now))
            break;
        deviation += (avg - now) * (avg - now);
        cnt++;
    }

    ret.min = min;
    ret.max = max;
    ret.avg = avg;
    ret.stddev = static_cast<float>(std::sqrt(deviation / (cnt - 1)));
    return ret;
}
