#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace emerick::engine {

// A write publishes all of its parts at once, so the reader always sees whole records.
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity) : data_(std::bit_ceil(capacity)), mask_(data_.size() - 1) {}

    [[nodiscard]] std::size_t readable() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t writable() const {
        return data_.size() - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire));
    }

    bool write(std::span<const std::uint8_t> first, std::span<const std::uint8_t> second = {}) {
        if (first.size() + second.size() > writable()) {
            return false;
        }
        const std::size_t head = head_.load(std::memory_order_relaxed);
        copyIn(head, first);
        copyIn(head + first.size(), second);
        head_.store(head + first.size() + second.size(), std::memory_order_release);
        return true;
    }

    bool peek(std::span<std::uint8_t> out) const {
        if (out.size() > readable()) {
            return false;
        }
        copyOut(tail_.load(std::memory_order_relaxed), out);
        return true;
    }

    bool read(std::span<std::uint8_t> out) {
        if (!peek(out)) {
            return false;
        }
        skip(out.size());
        return true;
    }

    void skip(std::size_t bytes) {
        tail_.store(tail_.load(std::memory_order_relaxed) + bytes, std::memory_order_release);
    }

private:
    void copyIn(std::size_t at, std::span<const std::uint8_t> bytes) {
        const std::size_t start = at & mask_;
        const std::size_t firstPart = std::min(bytes.size(), data_.size() - start);
        std::memcpy(data_.data() + start, bytes.data(), firstPart);
        std::memcpy(data_.data(), bytes.data() + firstPart, bytes.size() - firstPart);
    }

    void copyOut(std::size_t at, std::span<std::uint8_t> bytes) const {
        const std::size_t start = at & mask_;
        const std::size_t firstPart = std::min(bytes.size(), data_.size() - start);
        std::memcpy(bytes.data(), data_.data() + start, firstPart);
        std::memcpy(bytes.data() + firstPart, data_.data(), bytes.size() - firstPart);
    }

    std::vector<std::uint8_t> data_;
    std::size_t mask_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

// The ring stores bytes, so this is the one place we view headers and samples as their object bytes.
template <class T>
    requires std::is_trivially_copyable_v<T>
std::span<const std::uint8_t> ringBytes(std::span<const T> values) {
    return {reinterpret_cast<const std::uint8_t*>(values.data()), values.size_bytes()};
}

template <class T>
    requires std::is_trivially_copyable_v<T>
std::span<std::uint8_t> writableRingBytes(std::span<T> values) {
    return {reinterpret_cast<std::uint8_t*>(values.data()), values.size_bytes()};
}

template <class Header, class T = std::uint8_t>
bool writeRecord(SpscRing& ring, const Header& header, std::span<const T> payload = {}) {
    return ring.write(ringBytes(std::span(&header, 1)), ringBytes(payload));
}

template <class T>
bool readValue(SpscRing& ring, T& value) {
    return ring.read(writableRingBytes(std::span(&value, 1)));
}

template <class T>
bool readInto(SpscRing& ring, std::span<T> values) {
    return ring.read(writableRingBytes(values));
}

}  // namespace emerick::engine
