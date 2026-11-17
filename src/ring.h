#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

// A single-producer, single-consumer ring of floats. One thread writes, one
// other thread reads, and neither ever blocks or allocates, so the audio
// callback can read from it safely. Positions only grow; the index into the
// buffer is the position modulo the capacity.
class Ring {
public:
    explicit Ring(size_t capacity) : m_buffer(capacity), m_capacity(capacity) {}

    size_t capacity() const { return m_capacity; }

    size_t readable() const {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed);
    }

    size_t writable() const {
        return m_capacity - (m_write.load(std::memory_order_relaxed) - m_read.load(std::memory_order_acquire));
    }

    size_t write(const float *data, size_t count) {
        count = std::min(count, writable());
        const size_t start = m_write.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i)
            m_buffer[(start + i) % m_capacity] = data[i];
        m_write.store(start + count, std::memory_order_release);
        return count;
    }

    size_t read(float *data, size_t count) {
        count = std::min(count, readable());
        const size_t start = m_read.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i)
            data[i] = m_buffer[(start + i) % m_capacity];
        m_read.store(start + count, std::memory_order_release);
        return count;
    }

    // Drops everything waiting, from the reading side.
    void skipAll() { m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<float> m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_write{0};
    std::atomic<size_t> m_read{0};
};
