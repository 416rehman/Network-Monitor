#include <iostream>
#include <filesystem>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "common.h"

struct stat_file {
    std::string name;
    int fd = -1;
    std::string content = "";
};

class InterfaceMonitor {
    std::string interfaceName;
    std::vector<stat_file> statFiles;

public:
    bool init(std::string ifName, std::vector<stat_file> filesToMonitor) {
        this->interfaceName = std::move(ifName);
        const std::string path = "/sys/class/net/" + interfaceName;
        if (!std::filesystem::exists(path)) {
            std::cerr << "ERROR: Interface " << interfaceName << " does not exist" << std::endl;
            return false;
        }

        statFiles = std::move(filesToMonitor);

        // Populate file descriptors
        for (auto& statFile : statFiles) {
            statFile.fd = open((path + "/" + statFile.name).c_str(), O_RDONLY);
            if (statFile.fd == -1) {
                std::cerr << "ERROR: Failed to open file " << path << std::endl;
                return false;
            }
        }

        return true;
    }

    void shutdown() {

        for (auto& statFile : statFiles) {
            if (statFile.fd != -1) {
                close(statFile.fd);
            }
        }
    }

    void readStats() {
        for (auto& statFile : statFiles) {
            if (statFile.fd == -1) {
                std::cerr << "ERROR: File descriptor not set for file " << statFile.name << std::endl;
                continue;
            }
            lseek(statFile.fd, 0, SEEK_SET);

            char buffer[1024];
            ssize_t bytesRead = read(statFile.fd, buffer, sizeof(buffer));
            if (bytesRead == -1) {
                std::cerr << "ERROR: Failed to readStats from file " << statFile.name << std::endl;
                exit(1);
            }
            // remove trailing newline
            statFile.content = std::string(buffer, bytesRead - 1);
        }

        std::cout << *this << std::endl;
    }

    bool isUp() {
        // check if interface is up from /sys/class/net/<interface>/operstate
        const std::string path = "/sys/class/net/" + interfaceName + "/operstate";
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            std::cerr << "ERROR: Failed to open file " << path << std::endl;
            return false;
        }

        char buffer[1024];
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead == -1) {
            std::cerr << "ERROR: Failed to readStats from file " << path << std::endl;
            exit(1);
        }
        // remove trailing newline
        const std::string content = std::string(buffer, bytesRead - 1);
        close(fd);

        return content == "up";
    }

    friend std::ostream& operator<<(std::ostream& os, const InterfaceMonitor& monitor) {
        os << "Interface: " << monitor.interfaceName << " ";
        int filesPerLine = 4;
        int filesPrinted = 1;   // start at 1 to account for interface name
        for (const auto& statFile : monitor.statFiles) {
            if (filesPrinted % filesPerLine == 0) {
                os << std::endl;
            }
            const std::string statname = statFile.name.substr(statFile.name.find_last_of('/') + 1);
            os << statname << ": " << statFile.content << " ";
            filesPrinted++;
        }
        os << std::endl;
        return os;
    }
};

InterfaceMonitor MONITOR;

void signalHandler(int signum) {
    if (signum == SIGINT) {
        MONITOR.shutdown();
    }
}

// modified from here: https://stackoverflow.com/a/17997505
void setLinkState(std::string ifName, bool active) {// set link back up if it was down.
    int sockfd;
    struct ifreq ifr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        std::cerr << "ERROR: Failed to create socket for setting link up" << std::endl;
    } else {
        memset(&ifr, 0, sizeof ifr);

        strncpy(ifr.ifr_name, ifName.c_str(), IFNAMSIZ);

        if (active) {
            ifr.ifr_flags |= IFF_UP;
        } else {
            ifr.ifr_flags &= ~IFF_UP;
        }

        ioctl(sockfd, SIOCSIFFLAGS, &ifr);
    }
}

// Use unix socket to communicate with any process wanting to read the stats
int main(int argc, char** argv) {
    // get the interface name from the command line
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <interface> [-s]" << std::endl;
        std::cerr << "  -s: Run in standalone mode (default: run as daemon)" << std::endl;
        std::cerr << "In Daemon mode, the program will create a unix domain socket to write the stats to." << std::endl;
        exit(1);
    }
    std::string interfaceName = argv[1];

    const std::vector<stat_file> filesToMonitor = {
            {"operstate"},
            {"carrier_up_count"},
            {"carrier_down_count"},
            {"statistics/rx_bytes"},
            {"statistics/rx_dropped"},
            {"statistics/rx_errors"},
            {"statistics/rx_packets"},
            {"statistics/tx_bytes"},
            {"statistics/tx_dropped"},
            {"statistics/tx_errors"},
            {"statistics/tx_packets"}
    };

    // If the program is started with the -s flag (standalone), this will keep reading the stats.
    if (argc == 3 && std::string(argv[2]) == "-s") {
        // Register signal handler
        signal(SIGINT, signalHandler);
        if (MONITOR.init(interfaceName, filesToMonitor)) {
            while (true) {
                MONITOR.readStats();
                sleep(1);
            }
        } else {
            std::cerr << "ERROR: Failed to initialize interface monitor" << std::endl;
            exit(1);
        }
    }
    // otherwise, create a unix domain socket to communicate with parent process whose master socket is in the /tmp directory
    else {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock == -1) {
            std::cerr << "ERROR: Failed to create socket" << std::endl;
            exit(1);
        }

        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, common::netMonitorSocketPath);
        if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
            std::cerr << "ERROR: Failed to connect to socket: " << addr.sun_path << std::endl;
            exit(1);
        }
        std::cout << " |-- IntfMonitor - " << interfaceName << " (PID " << getpid() << ") : Connected to socket: " << addr.sun_path << std::endl;

        if (!MONITOR.init(interfaceName, filesToMonitor)) {
            std::cerr << "ERROR: Failed to initialize interface monitor" << std::endl;
            send(sock, common::response::invalidInterface, strlen(common::response::invalidInterface), 0);
        }

        send(sock, "Ready", 5, 0);

        char buffer[1024];
        while (true) {
            ssize_t bytesRead = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesRead == -1) {
                std::cerr << "ERROR: Failed to read from socket" << std::endl;
                exit(1);
            }
            std::string command(buffer, bytesRead);
            if (command == common::commands::monitor) {
                send(sock, common::response::monitoring, strlen(common::response::monitoring), 0);
                MONITOR.readStats();
                if (!MONITOR.isUp()) {
                    send(sock, common::response::linkDown, strlen(common::response::linkDown), 0);
                }

                // sleep for 1 second then send the monitoring response again, the parent process should send the monitor command again after receiving this response
                sleep(1);
                send(sock, common::response::monitoring, strlen(common::response::monitoring), 0);
            }
            else if (command == common::commands::shutdown) {
                send(sock, common::response::done, strlen(common::response::done), 0);
                MONITOR.shutdown();
                break;
            }
            else if (command == common::commands::setLinkUp) {
                setLinkState(interfaceName, true);
            }
        }
        close(sock);
    }

    return 0;
}
