#ifndef COMMON_UTIL_H_
#define COMMON_UTIL_H_

#include <atomic>
#include <optional>
#include <string>
#include <type_traits>

#include "common/ByteBuffer.h"

std::optional<ByteBuffer> loadEntireFile(const char *path);

bool writeByteBuffer(const char *filename, const ByteBuffer &data);

template <typename Fn>
void stringSplit(std::string_view str, char ch, Fn callback) {
    size_t begin = 0;
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == ch) {
            callback(str.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    callback(str.substr(begin, str.size() - begin));
}

// From https://stackoverflow.com/a/21298525
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type,
          typename = typename std::enable_if<std::is_unsigned<T>::value>::type>
constexpr T constexpr_nextPowerOfTwo(T value, uintmax_t maxb = sizeof(T) * CHAR_BIT, uintmax_t curb = 1) {
    return maxb <= curb ? value : constexpr_nextPowerOfTwo(((value - 1) | ((value - 1) >> curb)) + 1, maxb, curb << 1);
}

#if defined(ATOMIC_BOOL_LOCK_FREE) && ATOMIC_BOOL_LOCK_FREE >= 1
class spinlock {
public:
    spinlock() : af(false) {}
    spinlock(const spinlock &copy) = delete;
    spinlock(spinlock &&move) = delete;

    spinlock &operator=(const spinlock &copy) = delete;
    spinlock &&operator=(spinlock &&move) = delete;

    void lock() noexcept {
        while (true) {
            if (!af.exchange(true, std::memory_order_acquire)) {
                return;
            }

            while (af.load(std::memory_order_relaxed)) {
                _mm_pause();  // __builtin_ia32_pause on GCC
            }
        }
    }

    bool try_lock() noexcept {
        return !af.load(std::memory_order_relaxed) && !af.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept { af.store(false, std::memory_order_release); }

private:
    std::atomic<bool> af;
};
#else
class spinlock {
public:
    spinlock() {}
    spinlock(const spinlock &copy) = delete;
    spinlock(spinlock &&move) = delete;

    spinlock &operator=(const spinlock &copy) = delete;
    spinlock &&operator=(spinlock &&move) = delete;

    void lock() noexcept {
        while (af.test_and_set(std::memory_order_acquire))
            _mm_pause();
    }

    bool try_lock() noexcept { return !af.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept { af.clear(std::memory_order_release); }

private:
    std::atomic_flag af = ATOMIC_FLAG_INIT;
};
#endif

// Great for tracking min-max of last few numbers
template <class T, size_t LEN>
struct MinMaxTrackingRingBuffer {
    size_t idx = 0, size = 0;
    size_t minCnt = 0, maxCnt = 0;
    T min = 0, max = 0;
    T data[LEN];

    void push(const T val) {
        if (size == LEN)
            pop();

        if (val < min || minCnt == 0) {
            min = val;
            minCnt = 1;
        } else if (val == min)
            minCnt++;

        if (max < val || maxCnt == 0) {
            max = val;
            maxCnt = 1;
        } else if (val == max)
            maxCnt++;

        data[(idx + size) % LEN] = val;
        size++;
    }

    T pop() {
        const T ret = data[idx];
        idx = (idx + 1) % LEN;
        size--;

        if (size > 0) {
            if (min == ret) {
                minCnt--;

                if (minCnt <= 0) {
                    minCnt = 1;
                    min = data[idx];
                    for (size_t i = 1; i < size; i++) {
                        T now = data[(idx + i) % LEN];
                        if (now == min)
                            minCnt++;
                        else if (now < min) {
                            minCnt = 1;
                            min = now;
                        }
                    }
                }
            }
            if (max == ret) {
                maxCnt--;

                if (maxCnt <= 0) {
                    maxCnt = 1;
                    max = data[idx];
                    for (size_t i = 1; i < size; i++) {
                        T now = data[(idx + i) % LEN];
                        if (now == max)
                            maxCnt++;
                        else if (max < now) {
                            maxCnt = 1;
                            max = now;
                        }
                    }
                }
            }
        }

        return ret;
    }
};

#endif