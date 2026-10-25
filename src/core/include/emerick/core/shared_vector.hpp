#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace emerick::core {

// A vector whose copies share one buffer until one of them changes, so copying a session with thousands of events
// costs one reference count. Copies live on one thread, because the copy on change reads the use count.
template <class T>
class SharedVector {
public:
    SharedVector() = default;
    SharedVector(std::initializer_list<T> items) : items_(std::make_shared<std::vector<T>>(items)) {}
    explicit SharedVector(std::vector<T> items) : items_(std::make_shared<std::vector<T>>(std::move(items))) {}

    [[nodiscard]] auto begin() const { return items().begin(); }
    [[nodiscard]] auto end() const { return items().end(); }
    [[nodiscard]] std::size_t size() const { return items().size(); }
    [[nodiscard]] bool empty() const { return items().empty(); }
    [[nodiscard]] const T& at(std::size_t index) const { return items().at(index); }
    [[nodiscard]] const T& operator[](std::size_t index) const { return items()[index]; }

    void push_back(T item) { owned().push_back(std::move(item)); }

    // The buffer, or null while empty. Holding it keeps every later change from writing into it.
    [[nodiscard]] std::shared_ptr<const std::vector<T>> shared() const { return items_; }

private:
    [[nodiscard]] const std::vector<T>& items() const {
        static const std::vector<T> kNone;
        return items_ ? *items_ : kNone;
    }

    std::vector<T>& owned() {
        if (!items_ || items_.use_count() > 1) {
            items_ = std::make_shared<std::vector<T>>(items());
        }
        return *items_;
    }

    std::shared_ptr<std::vector<T>> items_;
};

}  // namespace emerick::core
