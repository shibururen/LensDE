# Native command interface

This directory contains the `lensde::cli` declarations used by the native LensDE executable. They let the executable pass a command's arguments to the tracing or optimization code and report the exit status. [Command declarations](cli/command.hpp)

`main.cpp` selects one of two modes. [Executable entry point](../../src/main.cpp)

- `run_single_command(argc, argv)` runs one operation from its command-line arguments and returns its exit code.
- `run_server_mode()` runs when the executable starts with `--server`. It reads commands from standard input and writes each command's exit code to standard output, keeping the same executable process open.

The native CLI prepares the inputs, calls the optical routines, and writes the results for each command. [Command header guide](cli/README.md) · [CLI implementation](../../src/cli/README.md)
