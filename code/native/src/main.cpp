// Enter the native single-command runner or the persistent ready/done server protocol.
// Scientific parsing and execution are delegated to the request-local CLI implementation.
// Keep argv forwarding and exception-to-exit-code behavior at this narrow executable boundary.

#include "lensde/cli/command.hpp"
#include "optiland/cli_arguments.hpp"
#include <exception>
#include <iostream>
using namespace lensde::cli;
int main(int argc, char** argv) {
    try {
        if (has_arg(argc, argv, "--server")) {
            return run_server_mode();
        }
        return run_single_command(argc, argv);
    } catch (const std::exception& exc) {
        std::cerr << "Optiland command failed: " << exc.what() << "\n";
        return 99;
    } catch (...) {
        std::cerr << "Optiland command failed with an unknown exception\n";
        return 99;
    }
}
