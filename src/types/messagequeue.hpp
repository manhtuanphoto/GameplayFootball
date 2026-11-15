// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_MESSAGEQUEUE
#define _HPP_MESSAGEQUEUE

#include "defines.hpp"

#include "base/properties.hpp"
#include "types/command.hpp"
#include "managers/environmentmanager.hpp"

namespace blunted {

  template <typename T = intrusive_ptr<Command>>
  class MessageQueue {

    public:
      MessageQueue() {}
      virtual ~MessageQueue() {}

      void EnableInlineExecutionOnCurrentThread(void *caller) {
        inlineThreadId_ = std::this_thread::get_id();
        inlineCaller_ = caller;
        inlineEnabled_.store(true, std::memory_order_release);
      }

      void DisableInlineExecution() {
        inlineEnabled_.store(false, std::memory_order_release);
      }

      inline void PushMessage(T message, bool notify = true) {
        if constexpr (std::is_same_v<T, intrusive_ptr<Command>>) {
          if (inlineEnabled_.load(std::memory_order_acquire) &&
              std::this_thread::get_id() == inlineThreadId_) {
            for (;;) {
              T queued;
              {
                std::unique_lock<std::mutex> lock(queue.mutex);
                if (queue.data.empty()) break;
                queued = queue.data.front();
                queue.data.pop_front();
              }
              if (queued) queued->Handle(inlineCaller_);
            }
            if (message) message->Handle(inlineCaller_);
            return;
          }
        }

        {
          std::unique_lock<std::mutex> lock(queue.mutex);
          queue.data.push_back(std::move(message));
        }
        if (notify) NotifyWaiting(e_NotificationSubject_One);
      }

      inline void NotifyWaiting(e_NotificationSubject notificationSubject = e_NotificationSubject_All) {
        if (notificationSubject == e_NotificationSubject_One) messageNotification.notify_one();
        if (notificationSubject == e_NotificationSubject_All) messageNotification.notify_all();
      }

      inline T GetMessage(bool &MsgAvail) {
        T message;
        std::unique_lock<std::mutex> lock(queue.mutex);
        if (!queue.data.empty()) {
          message = std::move(queue.data.front());
          queue.data.pop_front();
          MsgAvail = true;
        } else {
          MsgAvail = false;
        }
        return message;
      }

      inline void Purge() {
        std::unique_lock<std::mutex> lock(queue.mutex);
        queue.data.clear();
      }

      inline T WaitForMessage(int timeout_ms = -1) {
        bool dud;
        return WaitForMessage(dud, timeout_ms);
      }

      inline T WaitForMessage(bool &isMessage, int timeout_ms = -1) {
        isMessage = false;
        T message;

        std::unique_lock<std::mutex> lock(queue.mutex);
        while (!isMessage) {
          if (queue.data.empty()) {
            if (timeout_ms == -1) {
              messageNotification.wait(lock);
            } else {
              if (messageNotification.wait_for(lock, std::chrono::milliseconds(timeout_ms)) == std::cv_status::timeout &&
                  queue.data.empty()) {
                return message;
              }
            }
          }
          if (!queue.data.empty()) {
            message = std::move(queue.data.front());
            queue.data.pop_front();
            isMessage = true;
          }
        }
        return message;
      }

      inline int GetPending() {
        queue.Lock();
        int size = static_cast<int>(queue.data.size());
        queue.Unlock();
        return size;
      }

    protected:
      Lockable<std::deque<T>> queue;
      std::condition_variable messageNotification;

      std::thread::id inlineThreadId_;
      void *inlineCaller_ = nullptr;
      std::atomic<bool> inlineEnabled_{false};

  };

}

#endif
