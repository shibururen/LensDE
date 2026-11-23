// Enter the multi-island executable through the broker application interface.
// Job loading, policy setup, worker execution, and result publication remain in src/broker/.
// Forward the original argc/argv without adding mode defaults at the executable boundary.

#include "broker/application.hpp"

int main(int argc, char** argv) {
    return lensde::broker::run_application(argc, argv);
}
