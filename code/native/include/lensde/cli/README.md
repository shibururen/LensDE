# Entry points for native commands

This directory declares the two `lensde::cli` entry points called by the native executable. Both use the same command pipeline, which loads inputs, runs the requested optical calculation, and reports its status. [`command.hpp`](command.hpp) · [Executable entry point](../../../src/main.cpp)

- `run_single_command(argc, argv)` creates a fresh `CommandContext` from the argument list and returns the command's exit code. [Single-command implementation](../../../src/cli/command.cpp)
- `run_server_mode()` splits each request line from standard input into arguments and passes them to `run_single_command`. It reports each command's exit code on standard output as `__OPTILAND_SERVER_DONE__ <code>`. [Persistent command protocol](../../../src/cli/server_protocol.cpp)

Follow `CommandContext::execute()` to see the order of input preparation, optical evaluation, optimization, and result writing. [Command pipeline](../../../src/cli/command_execution.cpp)
