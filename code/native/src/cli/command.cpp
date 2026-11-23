// Create one CommandContext for a single argv request and execute its staged pipeline.
// Both the executable entry and persistent server protocol use this request-local boundary.
// Keep command-owned state local so repeated server requests do not reuse a prior command object.

#include "command_context.hpp"
namespace lensde { namespace cli {
int run_single_command(int argc, char** argv) {
    detail::CommandContext command(argc, argv);
    return command.execute();
}
} }
