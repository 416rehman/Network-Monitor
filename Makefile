CC = g++
CFLAGS = -Wall -std=c++17
DEBUG_FLAGS = -g -DDEBUG
InterfaceMonitorName = intfMonitor
netMonitorName = netMonitor

ifdef DEBUG
	CFLAGS += $(DEBUG_FLAGS)
endif

IntfMonitorSrc = interfaceMonitor.cpp

netMonitorSrc = networkMonitor.cpp

all: clean interfaceMonitor netMonitor

interfaceMonitor:
	mkdir -p ./bin
	$(CC) $(CFLAGS) -o ./bin/$(InterfaceMonitorName) $(IntfMonitorSrc)

netMonitor:
	mkdir -p ./bin
	$(CC) $(CFLAGS) -o ./bin/$(netMonitorName) $(netMonitorSrc)

clean:
	rm -f ./bin/$(InterfaceMonitorName) ./bin/$(netMonitorName)
