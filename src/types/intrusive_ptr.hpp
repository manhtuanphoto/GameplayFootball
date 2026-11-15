#ifndef _HPP_INTRUSIVE_PTR
#define _HPP_INTRUSIVE_PTR

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace blunted {

  template <class T>
  class intrusive_ptr {
  public:
    using element_type = T;

    constexpr intrusive_ptr() noexcept : px(nullptr) {}

    intrusive_ptr(T *p, bool add_ref = true) : px(p) {
      if (px != nullptr && add_ref) intrusive_ptr_add_ref(px);
    }

    intrusive_ptr(const intrusive_ptr &rhs) : px(rhs.px) {
      if (px != nullptr) intrusive_ptr_add_ref(px);
    }

    template <class U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    intrusive_ptr(const intrusive_ptr<U> &rhs) : px(rhs.get()) {
      if (px != nullptr) intrusive_ptr_add_ref(px);
    }

    intrusive_ptr(intrusive_ptr &&rhs) noexcept : px(rhs.px) {
      rhs.px = nullptr;
    }

    template <class U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    intrusive_ptr(intrusive_ptr<U> &&rhs) noexcept : px(rhs.detach()) {}

    ~intrusive_ptr() {
      if (px != nullptr) intrusive_ptr_release(px);
    }

    intrusive_ptr &operator=(const intrusive_ptr &rhs) {
      intrusive_ptr(rhs).swap(*this);
      return *this;
    }

    template <class U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    intrusive_ptr &operator=(const intrusive_ptr<U> &rhs) {
      intrusive_ptr(rhs).swap(*this);
      return *this;
    }

    intrusive_ptr &operator=(intrusive_ptr &&rhs) noexcept {
      intrusive_ptr(std::move(rhs)).swap(*this);
      return *this;
    }

    template <class U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    intrusive_ptr &operator=(intrusive_ptr<U> &&rhs) noexcept {
      intrusive_ptr(std::move(rhs)).swap(*this);
      return *this;
    }

    intrusive_ptr &operator=(T *rhs) {
      intrusive_ptr(rhs).swap(*this);
      return *this;
    }

    void reset() noexcept {
      intrusive_ptr().swap(*this);
    }

    void reset(T *rhs) {
      intrusive_ptr(rhs).swap(*this);
    }

    void reset(T *rhs, bool add_ref) {
      intrusive_ptr(rhs, add_ref).swap(*this);
    }

    T *get() const noexcept { return px; }

    T *detach() noexcept {
      T *ret = px;
      px = nullptr;
      return ret;
    }

    T &operator*() const noexcept { return *px; }
    T *operator->() const noexcept { return px; }

    explicit operator bool() const noexcept { return px != nullptr; }

    void swap(intrusive_ptr &rhs) noexcept {
      std::swap(px, rhs.px);
    }

  private:
    T *px;
  };

  template <class T, class U>
  inline bool operator==(const intrusive_ptr<T> &a, const intrusive_ptr<U> &b) noexcept {
    return a.get() == b.get();
  }

  template <class T, class U>
  inline bool operator!=(const intrusive_ptr<T> &a, const intrusive_ptr<U> &b) noexcept {
    return a.get() != b.get();
  }

  template <class T, class U>
  inline bool operator==(const intrusive_ptr<T> &a, U *b) noexcept {
    return a.get() == b;
  }

  template <class T, class U>
  inline bool operator!=(const intrusive_ptr<T> &a, U *b) noexcept {
    return a.get() != b;
  }

  template <class T, class U>
  inline bool operator==(T *a, const intrusive_ptr<U> &b) noexcept {
    return a == b.get();
  }

  template <class T, class U>
  inline bool operator!=(T *a, const intrusive_ptr<U> &b) noexcept {
    return a != b.get();
  }

  template <class T>
  inline bool operator==(const intrusive_ptr<T> &p, std::nullptr_t) noexcept {
    return p.get() == nullptr;
  }

  template <class T>
  inline bool operator==(std::nullptr_t, const intrusive_ptr<T> &p) noexcept {
    return p.get() == nullptr;
  }

  template <class T>
  inline bool operator!=(const intrusive_ptr<T> &p, std::nullptr_t) noexcept {
    return p.get() != nullptr;
  }

  template <class T>
  inline bool operator!=(std::nullptr_t, const intrusive_ptr<T> &p) noexcept {
    return p.get() != nullptr;
  }

  template <class T>
  inline bool operator<(const intrusive_ptr<T> &a, const intrusive_ptr<T> &b) noexcept {
    return std::less<T *>()(a.get(), b.get());
  }

  template <class T, class U>
  inline intrusive_ptr<T> static_pointer_cast(const intrusive_ptr<U> &p) {
    return static_cast<T *>(p.get());
  }

  template <class T, class U>
  inline intrusive_ptr<T> dynamic_pointer_cast(const intrusive_ptr<U> &p) {
    return dynamic_cast<T *>(p.get());
  }

}

#endif
