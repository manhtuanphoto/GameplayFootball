// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_COMMAND
#define _HPP_COMMAND

#include "defines.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include "types/refcounted.hpp"

namespace blunted {

  class Command : public RefCounted {

    public:
      Command(const std::string &name);
      virtual ~Command();

      inline bool IsReady() const { return handled.load(std::memory_order_acquire); }
      void Reset();
      bool Handle(void *caller = NULL);
      void Wait();

      const std::string &GetName() const { return name; }

    protected:
      virtual bool Execute(void *caller = NULL) = 0;
      virtual void OnHandled() {}

      std::mutex mutex; // locks 'handled & processed'
      std::atomic<bool> handled;
      std::condition_variable processed;

      std::string name;

  };
}

#endif
