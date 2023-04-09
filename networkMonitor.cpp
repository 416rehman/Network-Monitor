#include "common.h"
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <csignal>
#include <vector>

struct interfaceConnection {
    int socket = -1;
    std::string interfaceName;
    int pid = -1;
};

std::vector<interfaceConnection> INTERFACE_CONNECTIONS;
bool IS_RUNNING = true;

void forkAndExecInterfaceMonitors() {
    for (int i = 0; i < INTERFACE_CONNECTIONS.size(); i++) {
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "ERROR: Failed to fork" << std::endl;
            exit(1);
        } else if (pid == 0) {
            // child process
            char* args[] = {"./intfMonitor", const_cast<char *>(INTERFACE_CONNECTIONS[i].interfaceName.c_str()), NULL};
            execvp(args[0], args);
        } else {
            // parent process
            INTERFACE_CONNECTIONS[i].pid = pid;
        }
    }
}

int main() {
    // Remove the socket file if it exists
    remove(common::netMonitorSocketPath);

    std::cout << "Enter the amount of interfaces to monitor: ";
    int interfaceCount;
    std::cin >> interfaceCount;

    std::cout << "Enter the interface names. " << std::endl;

    for (int i = 0; i < interfaceCount; i++) {
        std::cout << "Interface " << i << ": ";
        std::string interfaceName;
        std::cin >> interfaceName;
        INTERFACE_CONNECTIONS.push_back({-1, interfaceName, -1});
    }

    std::cout << std::endl;

    // ctrl-c handler
    signal(SIGINT, [](int signum) {
        IS_RUNNING = false;
        std::cout << "Shutting down..." << std::endl;
        for (int i = 0; i < INTERFACE_CONNECTIONS.size(); i++) {
            // send shutdown message to interface monitor
            write(INTERFACE_CONNECTIONS[i].socket, common::commands::shutdown, strlen(common::commands::shutdown));
            close(INTERFACE_CONNECTIONS[i].socket);
            // send SIGINT to interface monitor just in case the shutdown message was not received
            kill(INTERFACE_CONNECTIONS[i].pid, SIGINT);
            std::cout << "InterfaceMonitor " << INTERFACE_CONNECTIONS[i].interfaceName << " shut down." << std::endl;
        }
        exit(0);
    });


    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "ERROR: Failed to create socket" << std::endl;
        exit(1);
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, common::netMonitorSocketPath);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        std::cerr << "ERROR: Failed to bind to socket: " << addr.sun_path << std::endl;
        exit(1);
    }

    if (listen(sock, 5) == -1) {
        std::cerr << "ERROR: Failed to listen on socket" << std::endl;
        exit(1);
    }

    forkAndExecInterfaceMonitors();

    fd_set primaryFDSet;
    FD_ZERO(&primaryFDSet);    // zeroize the set
    FD_SET(sock, &primaryFDSet);  // add master socket to set
    int maxFd = sock;

    // Initialize fdSet with all the sockets from the interface monitors
    for (int i = 0; i < interfaceCount; i++) {
        int clientSock = accept(sock, NULL, NULL);
        if (clientSock == -1) {
            std::cerr << "ERROR: Failed to accept connection" << std::endl;
            exit(1);
        }
        char buffer[common::bufferSize];
        int bytesRead = read(clientSock, buffer, common::bufferSize);
        buffer[bytesRead] = '\0';
        if (bytesRead == -1) {
            std::cerr << "ERROR: Failed to read from socket" << std::endl;
            exit(1);
        }
        if (strcmp(buffer, common::response::ready) == 0) {
            #ifdef DEBUG
            std::cout << "Received Ready from interface monitor on socket " << clientSock << std::endl;
            #endif
            FD_SET(clientSock, &primaryFDSet);
            if (clientSock > maxFd) {
                maxFd = clientSock;
            }
            INTERFACE_CONNECTIONS[i].socket = clientSock;
        } else if (strcmp(buffer, common::response::invalidInterface) == 0) {
            std::cerr << "ERROR: Failed to initialize interface monitor on socket " << clientSock << std::endl;
            // cleanup
            close(clientSock);
            kill(INTERFACE_CONNECTIONS[i].pid, SIGINT);
            INTERFACE_CONNECTIONS[i].pid = -1;
            INTERFACE_CONNECTIONS[i].socket = -1;
        }
    }

    // Initial "Monitor" command to send to each interface monitor before entering the main loop.
    // when an interfaceMonitor receives this command, it will print the stats, wait for a second, and then return "Monitoring" to this program
    // when this program receives "Monitoring" from an interfaceMonitor, it will send another "Monitor" command to that interfaceMonitor
    for (auto &interfaceConnection : INTERFACE_CONNECTIONS) {
        if (interfaceConnection.socket == -1) {
            continue;
        }

        if (write(interfaceConnection.socket, common::commands::monitor, strlen(common::commands::monitor)) == -1) {
            std::cerr << "ERROR: Failed to write to socket" << std::endl;
            exit(1);
        }
    }

    // Use select to wait for data on any of the sockets
    while (IS_RUNNING) {
        // Copy the primary fd set to a temporary fd set as select will modify it
        fd_set readFdSet = primaryFDSet;
        if (select(maxFd + 1, &readFdSet, NULL, NULL, NULL) == -1) {
            std::cerr << "ERROR: Failed to select" << std::endl;
            exit(1);
        }

        // Check if the master socket is set
        if (FD_ISSET(sock, &readFdSet)) {
            int clientSock = accept(sock, NULL, NULL);
            if (clientSock == -1) {
                std::cerr << "ERROR: Failed to accept connection" << std::endl;
                exit(1);
            }
            FD_SET(clientSock, &primaryFDSet);
            if (clientSock > maxFd) {
                maxFd = clientSock;
            }
        }

        // Check if any of the client sockets are set
        for (auto &interfaceConnection : INTERFACE_CONNECTIONS) {
            if (interfaceConnection.socket == -1) {
                continue;
            }
            if (FD_ISSET(interfaceConnection.socket, &readFdSet)) {
                char buffer[256];
                int bytesRead = read(interfaceConnection.socket, buffer, 256);

                if (bytesRead == -1) {
                    std::cerr << "ERROR: Failed to read from socket" << std::endl;
                }
                else if (bytesRead == 0) {
                    // connection closed
                    close(interfaceConnection.socket);
                    FD_CLR(interfaceConnection.socket, &primaryFDSet);
                    interfaceConnection.socket = -1;
                    #ifdef DEBUG
                    std::cout << "Connection closed on socket " << interfaceConnection.socket << std::endl;
                    #endif
                }
                // Data received
                else {
                    buffer[bytesRead] = '\0';
                    if (strcmp(buffer, common::response::monitoring) == 0) {
                        // Send "Monitor" command to interface monitor
                        if (write(interfaceConnection.socket, common::commands::monitor, strlen(common::commands::monitor)) == -1) {
                            std::cerr << "ERROR: Failed to write to socket" << std::endl;
                        }
                    }
                    else if (strcmp(buffer, common::response::linkDown) == 0) {
                        std::cout << "Link down on interface " << interfaceConnection.interfaceName << std::endl;
                        std::cout << "Attempting to bring link up..." << std::endl;
                        if (write(interfaceConnection.socket, common::commands::setLinkUp,
                                  strlen(common::commands::setLinkUp)) == -1) {
                            std::cerr << "ERROR: Failed to write to socket" << std::endl;
                        }
                    }

                    else if (strcmp(buffer, common::response::done) == 0 || strcmp(buffer, common::response::invalidInterface) == 0) {
                        // interface monitor is done
                        close(interfaceConnection.socket);
                        FD_CLR(interfaceConnection.socket, &primaryFDSet);
                        interfaceConnection.socket = -1;
                        #ifdef DEBUG
                        std::cout << "Interface monitor on socket " << interfaceConnection.socket << " is removed."
                                  << std::endl;
                        #endif
                    } else {
                        // interface monitor sent data
                        #ifdef DEBUG
                        std::cout << "Received data from interface monitor on socket " << interfaceConnection.socket
                                  << ": " << buffer << std::endl;
                        #endif
                    }
                }
            }
        }
    }

    remove(common::netMonitorSocketPath);
    return 0;
}


