#ifndef _HPP_BASE_CIRCULAR_BUFFER
#define _HPP_BASE_CIRCULAR_BUFFER

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace blunted {

  template <typename T>
  class CircularBuffer {
  public:
    template <bool IsConst>
    class IteratorImpl {
    public:
      using iterator_category = std::random_access_iterator_tag;
      using value_type = T;
      using difference_type = std::ptrdiff_t;
      using pointer = typename std::conditional<IsConst, const T*, T*>::type;
      using reference = typename std::conditional<IsConst, const T&, T&>::type;
      using BufferPtr = typename std::conditional<IsConst, const CircularBuffer*, CircularBuffer*>::type;

      IteratorImpl() noexcept : buf_(nullptr), idx_(0) {}
      IteratorImpl(BufferPtr buf, std::size_t idx) noexcept : buf_(buf), idx_(idx) {}

      template <bool WasConst = IsConst, typename = typename std::enable_if<WasConst>::type>
      IteratorImpl(const IteratorImpl<false> &other) noexcept : buf_(other.buf_), idx_(other.idx_) {}

      reference operator*() const { return (*buf_)[idx_]; }
      pointer operator->() const { return &((*buf_)[idx_]); }
      reference operator[](difference_type n) const { return (*buf_)[static_cast<std::size_t>(static_cast<difference_type>(idx_) + n)]; }

      IteratorImpl &operator++() noexcept { ++idx_; return *this; }
      IteratorImpl operator++(int) noexcept { IteratorImpl tmp(*this); ++idx_; return tmp; }
      IteratorImpl &operator--() noexcept { --idx_; return *this; }
      IteratorImpl operator--(int) noexcept { IteratorImpl tmp(*this); --idx_; return tmp; }

      IteratorImpl &operator+=(difference_type n) noexcept { idx_ = static_cast<std::size_t>(static_cast<difference_type>(idx_) + n); return *this; }
      IteratorImpl &operator-=(difference_type n) noexcept { idx_ = static_cast<std::size_t>(static_cast<difference_type>(idx_) - n); return *this; }

      friend IteratorImpl operator+(IteratorImpl it, difference_type n) noexcept { it += n; return it; }
      friend IteratorImpl operator+(difference_type n, IteratorImpl it) noexcept { it += n; return it; }
      friend IteratorImpl operator-(IteratorImpl it, difference_type n) noexcept { it -= n; return it; }
      friend difference_type operator-(const IteratorImpl &a, const IteratorImpl &b) noexcept {
        return static_cast<difference_type>(a.idx_) - static_cast<difference_type>(b.idx_);
      }

      friend bool operator==(const IteratorImpl &a, const IteratorImpl &b) noexcept { return a.idx_ == b.idx_ && a.buf_ == b.buf_; }
      friend bool operator!=(const IteratorImpl &a, const IteratorImpl &b) noexcept { return !(a == b); }
      friend bool operator<(const IteratorImpl &a, const IteratorImpl &b) noexcept { return a.idx_ < b.idx_; }
      friend bool operator>(const IteratorImpl &a, const IteratorImpl &b) noexcept { return a.idx_ > b.idx_; }
      friend bool operator<=(const IteratorImpl &a, const IteratorImpl &b) noexcept { return a.idx_ <= b.idx_; }
      friend bool operator>=(const IteratorImpl &a, const IteratorImpl &b) noexcept { return a.idx_ >= b.idx_; }

    private:
      template <bool> friend class IteratorImpl;
      BufferPtr buf_;
      std::size_t idx_;
    };

    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

    CircularBuffer() : capacity_(0), head_(0), size_(0) {}
    explicit CircularBuffer(std::size_t capacity) : capacity_(capacity), head_(0), size_(0), data_(capacity) {}

    void push_back(const T &value) {
      if (capacity_ == 0) return;
      if (size_ < capacity_) {
        std::size_t pos = head_ + size_;
        if (pos >= capacity_) pos -= capacity_;
        data_[pos] = value;
        ++size_;
      } else {
        data_[head_] = value;
        ++head_;
        if (head_ >= capacity_) head_ = 0;
      }
    }

    void clear() noexcept {
      head_ = 0;
      size_ = 0;
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    const T &operator[](std::size_t index) const noexcept {
      std::size_t pos = head_ + index;
      if (pos >= capacity_) pos -= capacity_;
      return data_[pos];
    }

    T &operator[](std::size_t index) noexcept {
      std::size_t pos = head_ + index;
      if (pos >= capacity_) pos -= capacity_;
      return data_[pos];
    }

    const T &at(std::size_t index) const {
      if (index >= size_) throw std::out_of_range("CircularBuffer::at");
      return (*this)[index];
    }

    T &at(std::size_t index) {
      if (index >= size_) throw std::out_of_range("CircularBuffer::at");
      return (*this)[index];
    }

    const T &front() const noexcept { return data_[head_]; }
    T &front() noexcept { return data_[head_]; }

    const T &back() const noexcept { return (*this)[size_ - 1]; }
    T &back() noexcept { return (*this)[size_ - 1]; }

    iterator begin() noexcept { return iterator(this, 0); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    iterator end() noexcept { return iterator(this, size_); }
    const_iterator end() const noexcept { return const_iterator(this, size_); }

  private:
    std::size_t capacity_;
    std::size_t head_;
    std::size_t size_;
    std::vector<T> data_;
  };

}

#endif
