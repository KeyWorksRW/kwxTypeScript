/*
 * kwx_ts — command-line entry point.
 *
 * Usage: kwx_ts <file> [args...]
 *
 * Reads the entry file as UTF-8, evaluates it as a global-scope QuickJS script
 * (through kwx_runtime_eval_file, which handles BOM stripping), and returns the
 * resulting exit code.
 */

#include "quickjs_host.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  kwx_trace_process_start();
  kwx_trace_mark("main-begin");

  if (argc < 2) {
    std::fprintf(stderr, "usage: kwx_ts <file> [args...]\n");
    return 2;
  }

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }

  const std::string entry_path = arguments[1];
  kwx_runtime_set_args(arguments);

  if (!kwx_runtime_init()) {
    return 1;
  }

  int exit_code = 0;
  const bool ok = kwx_runtime_eval_file(entry_path, exit_code);

  kwx_runtime_shutdown();

  if (!ok) {
    return (exit_code != 0) ? exit_code : 1;
  }

  return exit_code;
}
