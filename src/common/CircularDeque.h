#ifndef TWILIGHT_COMMON_CIRCULARDEQUE_H
#define TWILIGHT_COMMON_CIRCULARDEQUE_H

#include "common/log.h"

#include <cstdint>
#include <limits>

template <typename T, size_t LEN>
class CircularDeque {
    static_assert(LEN < std::numeric_limits<decltype(LEN)>::max() - LEN - 1, "Size too large");

public:
    CircularDeque() : first_(0), size_(0) {}

    T& front() {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("front() called on an empty queue");
        return data()[first_];
    }

    T& back() {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("back() called on an empty queue");
        return data()[(first_ + size_ - 1) % LEN];
    }

    const T& front() const {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("front() called on an empty queue");
        return data()[first_];
    }

    const T& back() const {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("back() called on an empty queue");
        return data()[(first_ + size_ - 1) % LEN];
    }

    void pop_front() {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("pop() called on an empty queue");
        front().~T();
        first_ = (first_ + 1) % LEN;
        size_--;
    }

    void pop_back() {
        if (empty())
            NamedLogger("ArrayCircularQueue").error_quit("pop() called on an empty queue");
        back().~T();
        size_--;
    }

    void push_front(const T& copy) {
        if (full())
            NamedLogger("ArrayCircularQueue").error_quit("push() called on a full queue");
        new (&front()) T(copy);
        first_ = (first_ + LEN + 1) % LEN;
        size_++;
    }

    void push_front(T&& move) {
        if (full())
            NamedLogger("ArrayCircularQueue").error_quit("push() called on a full queue");
        new (&front()) T(std::move(move));
        first_ = (first_ + LEN + 1) % LEN;
        size_++;
    }

    void push_back(const T& copy) {
        if (full())
            NamedLogger("ArrayCircularQueue").error_quit("push() called on a full queue");
        new (&back()) T(copy);
        size_++;
    }

    void push_back(T&& move) {
        if (full())
            NamedLogger("ArrayCircularQueue").error_quit("push() called on a full queue");
        new (&back()) T(std::move(move));
        size_++;
    }

    void clear() {
        for (size_t i = 0; i < size_; i++)
            data()[(first_ + i) % LEN].~T();
        first_ = size_ = 0;
    }

    size_t size() const { return size_; }

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ >= LEN; }

private:
    T* data() { return reinterpret_cast<T*>(storage); }
    const T* data() const { return reinterpret_cast<const T*>(storage); }

    size_t first_, size_;
    alignas(alignof(T)) uint8_t storage[LEN * sizeof(T)];
};

#endif
