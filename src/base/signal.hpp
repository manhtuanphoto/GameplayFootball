#ifndef _HPP_BASE_SIGNAL
#define _HPP_BASE_SIGNAL

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace blunted {

  class Trackable {
  public:
    Trackable() : trackableAlive_(std::make_shared<std::atomic<bool>>(true)) {}
    Trackable(const Trackable &) : trackableAlive_(std::make_shared<std::atomic<bool>>(true)) {}
    Trackable &operator=(const Trackable &) { return *this; }
    virtual ~Trackable() {
      if (trackableAlive_) {
        trackableAlive_->store(false, std::memory_order_release);
      }
    }

    std::weak_ptr<std::atomic<bool>> GetTrackableAlive() const {
      return trackableAlive_;
    }

  private:
    std::shared_ptr<std::atomic<bool>> trackableAlive_;
  };

  namespace detail {
    struct SignalStateBase {
      virtual ~SignalStateBase() = default;
      virtual void DisconnectById(std::uint64_t id) = 0;
    };
  }

  class SignalConnection {
  public:
    SignalConnection() = default;
    SignalConnection(std::weak_ptr<detail::SignalStateBase> state, std::uint64_t id)
        : state_(std::move(state)), id_(id) {}

    void disconnect() {
      if (auto sp = state_.lock()) {
        sp->DisconnectById(id_);
      }
      state_.reset();
      id_ = 0;
    }

  private:
    std::weak_ptr<detail::SignalStateBase> state_;
    std::uint64_t id_ = 0;
  };

  template <typename F>
  struct TrackedSlot {
    F func;
    bool hasTrackable = false;
    std::weak_ptr<std::atomic<bool>> trackableAlive;
    const void *ownerPtr = nullptr;
    std::string methodKey;

    template <typename... CallArgs>
    decltype(auto) operator()(CallArgs &&...args) {
      return func(std::forward<CallArgs>(args)...);
    }

    template <typename... CallArgs>
    decltype(auto) operator()(CallArgs &&...args) const {
      return func(std::forward<CallArgs>(args)...);
    }
  };

  template <typename T>
  struct IsTrackedSlot : std::false_type {};

  template <typename F>
  struct IsTrackedSlot<TrackedSlot<F>> : std::true_type {};

  template <typename Signature>
  class Signal;

  template <typename R, typename... Args>
  class Signal<R(Args...)> {
  public:
    using slot_type = std::function<R(Args...)>;

    Signal() : state_(std::make_shared<State>()) {}
    ~Signal() = default;

    Signal(const Signal &) = delete;
    Signal &operator=(const Signal &) = delete;

    template <typename F>
    SignalConnection connect(TrackedSlot<F> tracked) {
      std::lock_guard<std::mutex> lock(state_->mutex);
      const std::uint64_t id = ++state_->nextId;
      typename State::Entry entry;
      entry.id = id;
      entry.hasTrackable = tracked.hasTrackable;
      entry.trackableAlive = std::move(tracked.trackableAlive);
      entry.ownerPtr = tracked.ownerPtr;
      entry.methodKey = std::move(tracked.methodKey);
      auto fnCopy = std::move(tracked.func);
      entry.fn = [fn = std::move(fnCopy)](Args... args) mutable -> R {
        if constexpr (std::is_void_v<R>) {
          fn(std::forward<Args>(args)...);
        } else {
          return fn(std::forward<Args>(args)...);
        }
      };
      state_->entries.push_back(std::move(entry));
      return SignalConnection(state_, id);
    }

    template <typename F, typename = std::enable_if_t<!IsTrackedSlot<std::decay_t<F>>::value>>
    SignalConnection connect(F &&callable) {
      std::lock_guard<std::mutex> lock(state_->mutex);
      const std::uint64_t id = ++state_->nextId;
      typename State::Entry entry;
      entry.id = id;
      entry.fn = [fn = std::forward<F>(callable)](Args... args) mutable -> R {
        if constexpr (std::is_void_v<R>) {
          fn(std::forward<Args>(args)...);
        } else {
          return fn(std::forward<Args>(args)...);
        }
      };
      state_->entries.push_back(std::move(entry));
      return SignalConnection(state_, id);
    }

    template <typename F>
    void disconnect(const TrackedSlot<F> &tracked) {
      std::lock_guard<std::mutex> lock(state_->mutex);
      auto &v = state_->entries;
      v.erase(
          std::remove_if(v.begin(), v.end(), [&](const typename State::Entry &e) {
            return e.ownerPtr != nullptr && e.ownerPtr == tracked.ownerPtr && e.methodKey == tracked.methodKey;
          }),
          v.end());
    }

    void operator()(Args... args) const {
      std::vector<typename State::Entry> snapshot;
      {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto &v = state_->entries;
        v.erase(
            std::remove_if(v.begin(), v.end(), [](const typename State::Entry &e) {
              if (!e.hasTrackable) return false;
              auto sp = e.trackableAlive.lock();
              return !sp || !sp->load(std::memory_order_acquire);
            }),
            v.end());
        snapshot = v;
      }

      for (auto &entry : snapshot) {
        if (entry.hasTrackable) {
          auto sp = entry.trackableAlive.lock();
          if (!sp || !sp->load(std::memory_order_acquire)) continue;
        }
        entry.fn(args...);
      }
    }

  private:
    struct State : detail::SignalStateBase {
      struct Entry {
        std::uint64_t id = 0;
        slot_type fn;
        bool hasTrackable = false;
        std::weak_ptr<std::atomic<bool>> trackableAlive;
        const void *ownerPtr = nullptr;
        std::string methodKey;
      };

      std::mutex mutex;
      std::uint64_t nextId = 0;
      std::vector<Entry> entries;

      void DisconnectById(std::uint64_t id) override {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [id](const Entry &e) { return e.id == id; }),
            entries.end());
      }
    };

    std::shared_ptr<State> state_;
  };

  template <typename PMF, typename Obj, typename... BoundArgs,
            typename = std::enable_if_t<std::is_member_function_pointer_v<std::decay_t<PMF>>>>
  auto Bind(PMF pmf, Obj &&obj, BoundArgs &&...boundArgs) {
    auto bound = std::bind(pmf, std::forward<Obj>(obj), std::forward<BoundArgs>(boundArgs)...);
    TrackedSlot<decltype(bound)> slot{std::move(bound)};
    slot.methodKey.assign(reinterpret_cast<const char *>(&pmf), sizeof(pmf));

    using RawObj = std::remove_reference_t<Obj>;
    if constexpr (std::is_pointer_v<RawObj>) {
      using Pointee = std::remove_cv_t<std::remove_pointer_t<RawObj>>;
      slot.ownerPtr = static_cast<const void *>(obj);
      if constexpr (std::is_base_of_v<Trackable, Pointee>) {
        if (obj != nullptr) {
          slot.hasTrackable = true;
          slot.trackableAlive = obj->GetTrackableAlive();
        }
      }
    }
    return slot;
  }

  template <typename Sig, typename Obj>
  auto Bind(std::reference_wrapper<Signal<Sig>> sigRef, Obj *obj) {
    auto fn = [sigPtr = &sigRef.get(), obj](auto &&...) {
      (*sigPtr)(obj);
    };
    TrackedSlot<decltype(fn)> slot{std::move(fn)};
    slot.ownerPtr = static_cast<const void *>(obj);
    if constexpr (std::is_base_of_v<Trackable, std::remove_cv_t<Obj>>) {
      if (obj != nullptr) {
        slot.hasTrackable = true;
        slot.trackableAlive = obj->GetTrackableAlive();
      }
    }
    return slot;
  }

}

#endif
