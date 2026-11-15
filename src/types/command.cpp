// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "command.hpp"

namespace blunted {

  Command::Command(const std::string &name) : handled(false), name(name) {
  }

  Command::~Command() {
  }

  void Command::Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    handled.store(false, std::memory_order_release);
  }

  bool Command::Handle(void *caller) {
    bool result = Execute(caller);

    {
      std::lock_guard<std::mutex> lock(mutex);
      handled.store(true, std::memory_order_release);
    }
    processed.notify_all();
    OnHandled();

    return result;
  }

  void Command::Wait() {
    if (handled.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> lock(mutex);
    processed.wait(lock, [this]() { return handled.load(std::memory_order_relaxed); });
  }

}
