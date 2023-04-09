# Network-Monitor

This is a simple network monitor that can be used to monitor the network traffic of multiple interfaces. It is written in C++ for UNIX systems.

## Build
Clone the repository and run the following commands:

    make

This will create the executable `intfMonitor` and `netMonitor` in the `./bin` subdirectory.

*The `netMonitor` executable can be used to monitor the network traffic of multiple interfaces. The `intfMonitor` is meant to be used by the `netMonitor` and should not be used directly. These executables achieve inter-process communication using UNIX sockets.*

## Usage
To use the program, run the following `netMonitor` executable:

    > Enter the amount of interfaces to monitor: 2
    > Enter the interface names. 
    > Interface 0: eth0
    > Interface 1: lo

The program will then start monitoring the network traffic of the specified interfaces. The output will look like this:
    
```       
Link down on interface eth0
Attempting to bring link up...
Interface: lo operstate: unknown carrier_up_count: 0 carrier_down_count: 0
rx_bytes: 331190 rx_dropped: 0 rx_errors: 0 rx_packets: 2664
tx_bytes: 331190 tx_dropped: 0 tx_errors: 0 tx_packets: 2664
```
