// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_REFCOUNTED
#define _HPP_REFCOUNTED

#include "defines.hpp"
#include <atomic>
#include <cassert>

namespace blunted {

  class RefCounted {

    public:
      RefCounted() : refCount(0) {}
      virtual ~RefCounted();

      RefCounted(const RefCounted &) : refCount(0) {}
      RefCounted& operator=(const RefCounted &) { return *this; }

      inline unsigned long GetRefCount() const noexcept {
        return static_cast<unsigned long>(refCount.load(std::memory_order_relaxed));
      }

    private:
      std::atomic<long> refCount;

      friend inline void intrusive_ptr_add_ref(RefCounted *p) noexcept;
      friend inline void intrusive_ptr_release(RefCounted *p) noexcept;

  };

  inline void intrusive_ptr_add_ref(RefCounted *p) noexcept {
    assert(p);
    p->refCount.fetch_add(1, std::memory_order_relaxed);
  }

  inline void intrusive_ptr_release(RefCounted *p) noexcept {
    assert(p);
    if (p->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete p;
    }
  }

}

#endif
