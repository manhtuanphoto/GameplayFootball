// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_DEFINES
#define _HPP_DEFINES

#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cassert>

#include <fstream>
#include <cmath>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "types/intrusive_ptr.hpp"
#include "base/signal.hpp"

namespace blunted {

  using std::shared_ptr;
  using std::weak_ptr;
  using std::make_shared;
  using std::static_pointer_cast;
  using std::dynamic_pointer_cast;
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;
  using std::placeholders::_4;

  typedef float real;

}

#endif
