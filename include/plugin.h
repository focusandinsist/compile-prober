
#pragma once
#include <gcc-plugin.h>
#include <pretty-print.h>
#include "comm.h"
#include "perf_output.h"

namespace compile_prober {

void write_all_scopes();
void write_all_functions();

} // namespace compile_prober
