#include "perf_output.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <gcc-plugin.h>
#include <plugin-version.h>
#include <sys/types.h>
#include <unistd.h>

namespace compile_prober {

constexpr int MINIMUM_EVENT_LENGTH_NS = 1000000;

namespace {

json::object *output_json = nullptr;
std::FILE *trace_file = nullptr;
std::vector<TraceEvent> trace_events;

const char *category_string(EventCategory category) {
  static constexpr const char *names[] = {
      "TU",          "PREPROCESS", "FUNCTION",        "STRUCT",
      "NAMESPACE",   "GIMPLE_PASS", "RTL_PASS",        "SIMPLE_IPA_PASS",
      "IPA_PASS",    "UNKNOWN"};
  const auto index = static_cast<unsigned>(category);
  constexpr auto count = sizeof(names) / sizeof(names[0]);
  return index < count ? names[index] : "UNKNOWN";
}

json::object *new_duration_event(const TraceEvent &event, int pid, int tid) {
  auto *json_event = new json::object;
  json_event->set("name", new json::string(event.name.c_str()));
  json_event->set("ph", new json::string("X"));
  json_event->set("cat", new json::string(category_string(event.category)));
  json_event->set(
      "ts", new json::float_number(static_cast<double>(event.ts.start) * 0.001));
  json_event->set(
      "dur", new json::float_number(static_cast<double>(event.ts.end - event.ts.start) * 0.001));
  json_event->set("pid", new json::integer_number(pid));
  json_event->set("tid", new json::integer_number(tid));

  auto *args = new json::object;
  if (event.args) {
    for (const auto &[key, value] : *event.args) {
      args->set(key.c_str(), new json::string(value.c_str()));
    }
  }
  json_event->set("args", args);
  return json_event;
}

void flush_events() {
  std::stable_sort(trace_events.begin(), trace_events.end(),
                   [](const TraceEvent &left, const TraceEvent &right) {
                     if (left.ts.start != right.ts.start) {
                       return left.ts.start < right.ts.start;
                     }
                     return left.ts.end > right.ts.end;
                   });

  const int pid = getpid();
  constexpr int tid = 0;
  auto *events = new json::array;
  for (const auto &event : trace_events) {
    events->append(new_duration_event(event, pid, tid));
  }
  output_json->set("traceEvents", events);
}

} // namespace

void init_output_file(FILE *file) {
  trace_file = file;
  trace_events.clear();
  output_json = new json::object;
  output_json->set("displayTimeUnit", new json::string("us"));
}

void add_event(const TraceEvent &event) {
  const auto duration = event.ts.end - event.ts.start;
  if (duration <= 0 || duration < MINIMUM_EVENT_LENGTH_NS) {
    return;
  }
  trace_events.push_back(event);
}

void write_all_events() {
  add_event(TraceEvent{"translation unit", TU, {0, ns_from_start()},
                       std::nullopt});
  write_preprocessing_events();
  write_opt_pass_events();
  write_all_functions();
  write_all_scopes();
  flush_events();

#if GCCPLUGIN_VERSION_MAJOR >= 14
  output_json->dump(trace_file, /*formatted=*/false);
#else
  output_json->dump(trace_file);
#endif
  fclose(trace_file);
  trace_file = nullptr;
  delete output_json;
  output_json = nullptr;
}

} // namespace compile_prober
