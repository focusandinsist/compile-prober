#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>  

namespace compile_prober {

template <class Key, class Value> 
using map_t = std::unordered_map<Key, Value>;

template <class Value> 
using set_t = std::unordered_set<Value>;

using clock_t = std::chrono::steady_clock;
static_assert(clock_t::is_steady, "Compile Prober requires a monotonic clock");
using time_point_t = std::chrono::time_point<clock_t>;
extern time_point_t COMPILATION_START;
using TimeStamp = int64_t;

struct TimeSpan {
  int64_t start;
  int64_t end;
};

// 这段代码是一个典型的 C++ 性能打点函数，它的作用是：获取从“编译/程序启动开始”到现在所经过的纳秒数。
inline TimeStamp ns_from_start() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() -
                                                              COMPILATION_START)
      .count();
}

enum EventCategory {
  TU,
  PREPROCESS,
  FUNCTION,
  STRUCT,
  NAMESPACE,
  GIMPLE_PASS,
  RTL_PASS,
  SIMPLE_IPA_PASS,
  IPA_PASS,
  UNKNOWN
};

struct TraceEvent {
  std::string name;
  EventCategory category;
  TimeSpan ts;
  std::optional<map_t<std::string, std::string>> args;
};

struct FinishedFunction {
  void *decl;
  const char *name;
  const char *file_name;
  const char *scope_name;
  EventCategory scope_type;
};
}
