#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <vector>

namespace util {

// Stack-allocated fixed-capacity vector.  Drop-in replacement for std::vector
// on hot paths where heap allocation must be avoided (real-time MIDI, ISR).
// Exceeding capacity silently drops the element (no exception, no UB).
template <typename T, size_t N>
class FixedVec {
  static_assert(N > 0, "Capacity must be > 0");

 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;
  using reference = T&;
  using const_reference = const T&;

  FixedVec() = default;

  FixedVec(std::initializer_list<T> init) {
    for (const auto& v : init) {
      if (size_ >= N) break;
      data_[size_++] = v;
    }
  }

  // Construct from iterator range.
  template <typename It>
  FixedVec(It first, It last) {
    while (first != last && size_ < N) {
      data_[size_++] = *first++;
    }
  }

  // Allow implicit conversion from std::vector for API compatibility.
  template <typename Alloc>
  FixedVec(const std::vector<T, Alloc>& v) {  // NOLINT(google-explicit-constructor)
    const size_t n = std::min(v.size(), N);
    for (size_t i = 0; i < n; ++i) data_[i] = v[i];
    size_ = n;
  }

  // Allow implicit conversion to std::vector for API compatibility.
  operator std::vector<T>() const {  // NOLINT(google-explicit-constructor)
    return std::vector<T>(data_, data_ + size_);
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  static constexpr size_t capacity() { return N; }
  bool full() const { return size_ == N; }

  iterator begin() { return data_; }
  iterator end() { return data_ + size_; }
  const_iterator begin() const { return data_; }
  const_iterator end() const { return data_ + size_; }

  reference operator[](size_t i) { return data_[i]; }
  const_reference operator[](size_t i) const { return data_[i]; }

  reference front() { return data_[0]; }
  const_reference front() const { return data_[0]; }
  reference back() { return data_[size_ - 1]; }
  const_reference back() const { return data_[size_ - 1]; }

  T* data() { return data_; }
  const T* data() const { return data_; }

  void push_back(const T& v) {
    if (size_ < N) data_[size_++] = v;
  }

  void pop_back() {
    if (size_ > 0) --size_;
  }

  void clear() { size_ = 0; }

  // No-op for compatibility with code that calls reserve().
  void reserve(size_t) {}

  void resize(size_t n) {
    if (n > N) n = N;
    if (n > size_) {
      for (size_t i = size_; i < n; ++i) data_[i] = T{};
    }
    size_ = n;
  }

  // Insert before pos. Returns iterator to inserted element, or end() if full.
  iterator insert(iterator pos, const T& v) {
    if (size_ >= N) return end();
    const size_t idx = static_cast<size_t>(pos - data_);
    // Shift elements right.
    for (size_t i = size_; i > idx; --i) {
      data_[i] = data_[i - 1];
    }
    data_[idx] = v;
    ++size_;
    return data_ + idx;
  }

  // Erase single element.
  iterator erase(iterator pos) {
    if (pos >= end()) return end();
    const size_t idx = static_cast<size_t>(pos - data_);
    for (size_t i = idx; i + 1 < size_; ++i) {
      data_[i] = data_[i + 1];
    }
    --size_;
    return data_ + idx;
  }

  // Erase range [first, last).
  iterator erase(iterator first, iterator last) {
    if (first >= last || first >= end()) return end();
    const size_t removed = static_cast<size_t>(last - first);
    const size_t start = static_cast<size_t>(first - data_);
    const size_t remaining = size_ - start - removed;
    for (size_t i = 0; i < remaining; ++i) {
      data_[start + i] = data_[start + removed + i];
    }
    size_ -= removed;
    return data_ + start;
  }

  // Assignment from another FixedVec of same type but potentially different capacity.
  template <size_t M>
  FixedVec& operator=(const FixedVec<T, M>& other) {
    const size_t n = std::min(other.size(), N);
    for (size_t i = 0; i < n; ++i) data_[i] = other[i];
    size_ = n;
    return *this;
  }

 private:
  T data_[N]{};
  size_t size_ = 0;
};

}  // namespace util
