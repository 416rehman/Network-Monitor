#include <string>

namespace common {
    const char* const netMonitorSocketPath = "/tmp/netm0n.sock";
    const int bufferSize = 1024;

    namespace commands {
        const char* const shutdown = "Shut Down";
        const char* const monitor = "Monitor";
        const char* const setLinkUp = "Set Link Up";
    }

    namespace response {
        const char* const ready = "Ready";
        const char* const done = "Done";
        const char* const monitoring = "Monitoring";
        const char* const linkDown = "Link Down";
        const char* const invalidInterface = "Invalid Interface";
    }
}

