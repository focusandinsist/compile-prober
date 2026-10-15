#include <gcc-plugin.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "c-family/c-pragma.h"
#include "cpplib.h"
#include "tracking.h"
#include <tree-pass.h>

namespace compile_prober {

time_point_t COMPILATION_START;

namespace {

struct PreprocessFrame {
  std::string file_name;
  TimeStamp start = 0;
  bool tracked = false;
};

struct PreprocessEvent {
  std::string file_name;
  TimeSpan ts;
};

struct OptPassEvent {
  const opt_pass *pass = nullptr;
  TimeSpan ts{};
};

struct ScopeEvent {
  std::string name;
  EventCategory type = UNKNOWN;
  TimeSpan ts{};
};

struct FunctionEvent {
  std::string name;
  std::string file_name;
  TimeSpan ts{};
};

std::vector<PreprocessFrame> preprocess_stack;
std::vector<PreprocessEvent> preprocess_events;
std::vector<OptPassEvent> pass_events;
OptPassEvent active_pass;
std::vector<ScopeEvent> scope_events;
std::vector<FunctionEvent> function_events;
map_t<void *, TimeStamp> parse_starts;

map_t<std::string, std::string> file_to_include_directory;
map_t<std::string, std::string> normalized_files_map;
set_t<std::string> normalized_files;
set_t<std::string> conflicted_files;

void register_include_location(const char *file_name, const char *dir_name) {
  if (!file_name || !dir_name || file_to_include_directory.contains(file_name)) {
    return;
  }

  std::string file(file_name);
  std::string directory(dir_name);
  file_to_include_directory.emplace(file, directory);

  if (file.size() <= directory.size() ||
      file.compare(0, directory.size(), directory) != 0 ||
      file[directory.size()] != '/') {
    return;
  }

  auto normalized = file.substr(directory.size() + 1);
  normalized_files_map[file] = normalized;
  if (!normalized_files.insert(normalized).second) {
    conflicted_files.insert(normalized);
  }
}

std::string normalized_file_name(const std::string &file_name) {
  auto it = normalized_files_map.find(file_name);
  if (it != normalized_files_map.end() &&
      !conflicted_files.contains(it->second)) {
    return it->second;
  }
  return file_name;
}

EventCategory pass_type(opt_pass_type type) {
  switch (type) {
  case opt_pass_type::GIMPLE_PASS:
    return GIMPLE_PASS;
  case opt_pass_type::RTL_PASS:
    return RTL_PASS;
  case opt_pass_type::SIMPLE_IPA_PASS:
    return SIMPLE_IPA_PASS;
  case opt_pass_type::IPA_PASS:
    return IPA_PASS;
  }
  return UNKNOWN;
}

bool is_active_file(const std::string &file_name) {
  return std::any_of(preprocess_stack.begin(), preprocess_stack.end(),
                     [&](const PreprocessFrame &frame) {
                       return frame.tracked && frame.file_name == file_name;
                     });
}

} // namespace

void start_preprocess_file(const char *file_name, cpp_reader *pfile) {
  if (!file_name) {
    preprocess_stack.push_back({{}, 0, false});
    return;
  }

  std::string name(file_name);
  if (name == "<command-line>" || name == "<built-in>") {
    preprocess_stack.push_back({std::move(name), 0, false});
    return;
  }

  const auto now = ns_from_start();
  if (is_active_file(name)) {
    preprocess_stack.push_back({std::move(name), now, false});
    return;
  }

  if (pfile) {
    auto *cpp_buffer = cpp_get_buffer(pfile);
    auto *cpp_file = cpp_get_file(cpp_buffer);
    auto *dir = cpp_get_dir(cpp_file);
    auto *real_dir_name = realpath(dir->name, nullptr);
    auto *real_file_name = realpath(file_name, nullptr);
    if (real_dir_name && real_file_name) {
      register_include_location(real_file_name, real_dir_name);
      name.assign(real_file_name);
    }
    free(real_dir_name);
    free(real_file_name);
  }

  preprocess_stack.push_back({std::move(name), now, true});
}

void end_preprocess_file() {
  if (preprocess_stack.empty()) {
    fprintf(stderr, "COMPILE-PROBER warning: unmatched preprocess leave\n");
    return;
  }

  auto frame = std::move(preprocess_stack.back());
  preprocess_stack.pop_back();
  if (frame.tracked) {
    preprocess_events.push_back(
        {std::move(frame.file_name), {frame.start, ns_from_start()}});
  }
}

void finish_preprocessing_stage() {
  while (!preprocess_stack.empty()) {
    end_preprocess_file();
  }
}

void write_preprocessing_events() {
  finish_preprocessing_stage();
  for (const auto &event : preprocess_events) {
    add_event(TraceEvent{normalized_file_name(event.file_name), PREPROCESS,
                         event.ts, std::nullopt});
  }
}

void start_parse_function(void *decl) {
  if (decl) {
    parse_starts[decl] = ns_from_start();
  }
}

void end_parse_function(FinishedFunction info) {
  const auto now = ns_from_start();
  auto start_it = parse_starts.find(info.decl);
  const auto start = start_it == parse_starts.end() ? now : start_it->second;
  if (start_it != parse_starts.end()) {
    parse_starts.erase(start_it);
  }

  const TimeSpan ts{std::min(start, now), now};
  function_events.push_back(
      {info.name ? info.name : "<anonymous>",
       info.file_name ? info.file_name : "<unknown>", ts});

  if (!info.scope_name) {
    return;
  }

  if (!scope_events.empty() && scope_events.back().name == info.scope_name &&
      scope_events.back().type == info.scope_type) {
    scope_events.back().ts.end = ts.end;
  } else {
    scope_events.push_back(
        {info.scope_name, info.scope_type, {ts.start, ts.end}});
  }
}

void start_opt_pass(const opt_pass *pass) {
  const auto now = ns_from_start();
  if (active_pass.pass) {
    active_pass.ts.end = now;
    pass_events.push_back(active_pass);
  }
  active_pass = {pass, {now, now}};
}

void write_opt_pass_events() {
  if (active_pass.pass) {
    active_pass.ts.end = ns_from_start();
    pass_events.push_back(active_pass);
    active_pass = {};
  }

  for (const auto &event : pass_events) {
    map_t<std::string, std::string> args;
    args["static_pass_number"] =
        std::to_string(event.pass->static_pass_number);
    add_event(TraceEvent{event.pass->name ? event.pass->name : "<unnamed-pass>",
                         pass_type(event.pass->type), event.ts,
                         std::move(args)});
  }
}

void write_all_scopes() {
  for (const auto &event : scope_events) {
    add_event(TraceEvent{event.name, event.type, event.ts, std::nullopt});
  }
}

void write_all_functions() {
  for (const auto &event : function_events) {
    map_t<std::string, std::string> args;
    args["file"] = normalized_file_name(event.file_name);
    add_event(TraceEvent{event.name, FUNCTION, event.ts, std::move(args)});
  }
}

} // namespace compile_prober
