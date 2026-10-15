#pragma once

#include "comm.h"
#include "plugin.h"
#include "tracking.h"
#include <json.h>

namespace compile_prober {

void init_output_file(FILE *file);
void add_event(const TraceEvent &event);
void write_all_events();
}
