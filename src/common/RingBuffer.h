#ifndef TWILIGHT_COMMON_RINGBUFFER_H
#define TWILIGHT_COMMON_RINGBUFFER_H

#include "common/util.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

template <class T, size_t MIN_SIZE>
class RingBuffer {
public:
    static_assert(std::is_trivial<T>::value,
                  "Ring buffer should only be used with trivial types (If not, just use deque)");
    static_assert(MIN_SIZE > 0, "Must reserve size greater than 0");
    constexpr static size_t SIZE = constexpr_nextPowerOfTwo(MIN_SIZE);

    RingBuffer() : readPos_(0), bufferSize_(0) {}
    RingBuffer(const RingBuffer &copy) = delete;
    RingBuffer(RingBuffer &&move) = delete;
    ~RingBuffer() {}

    RingBuffer &operator=(const RingBuffer &copy) = delete;
    RingBuffer &operator=(RingBuffer &&move) = delete;

    size_t size() const { return bufferSize_; }

    size_t available() const { return SIZE - bufferSize_; }

    void clear() {
        readPos_ = 0;
        bufferSize_ = 0;
    }

    void drop(size_t amount) {
        assert(bufferSize_ >= amount);
        readPos_ = (readPos_ + amount) % SIZE;
        bufferSize_ -= amount;
    }

    void write(T val) {
        assert(bufferSize_ < SIZE);
        buffer_[(readPos_ + bufferSize_) % SIZE] = val;
        bufferSize_++;
    }

    void write(const T *arr, size_t amount) {
        assert(bufferSize_ <= SIZE - amount);
        if (amount == 0)
            return;

        const size_t writePos = (readPos_ + bufferSize_) % SIZE;
        const size_t endPos = (readPos_ + bufferSize_ + amount) % SIZE;
        if (endPos <= writePos) {
            size_t firstWriteAmount = SIZE - writePos;
            size_t secondWriteAmount = amount - firstWriteAmount;
            memcpy(buffer_ + writePos, arr, firstWriteAmount * sizeof(T));
            memcpy(buffer_, arr + firstWriteAmount, secondWriteAmount * sizeof(T));
        } else {
            memcpy(buffer_ + writePos, arr, amount * sizeof(T));
        }
        bufferSize_ += amount;
    }

    T read() {
        assert(bufferSize_ > 0);
        T ret = buffer_[readPos_];
        readPos_ = (readPos_ + 1) % SIZE;
        bufferSize_--;
        return ret;
    }

    void read(T *arr, size_t amount) {
        assert(bufferSize_ >= amount);
        if (amount == 0)
            return;

        if (SIZE <= readPos_ + amount) {
            size_t firstReadAmount = SIZE - readPos_;
            size_t secondReadAmount = amount - firstReadAmount;
            memcpy(arr, buffer_ + readPos_, firstReadAmount * sizeof(T));
            memcpy(arr + firstReadAmount, buffer_, secondReadAmount * sizeof(T));
            readPos_ = secondReadAmount;
        } else {
            memcpy(arr, buffer_ + readPos_, amount * sizeof(T));
            readPos_ += amount;
        }
        bufferSize_ -= amount;
    }

private:
    size_t readPos_;
    size_t bufferSize_;
    T buffer_[SIZE];
};

#endif