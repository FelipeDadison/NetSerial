# WebSerial

Multi-protocol Serial communication wrapper for ESP32 supporting TCP, WebSocket, and UDP transports with Arduino Stream interface.

## Installation

Add to your `platformio.ini`:

```ini
lib_deps =
    WebSerial
```

## Usage

### TCP Server (using global instance)
```cpp
#include "WebSerial.h"

// Use the pre-instantiated global: wSerial (TCP on port 23)

void setup() {
    wSerial.begin();
}

void loop() {
    if (wSerial.available()) {
        int byte = wSerial.read();
        // Handle incoming data
    }
    wSerial.write((const uint8_t*)"data\n", 5);
}
```

### TCP Server (custom instance)
```cpp
#include "WebSerial.h"

WebSerial tcpSerial(8023);  // Custom port

void setup() {
    tcpSerial.begin();
}

void loop() {
    if (tcpSerial.available()) {
        int byte = tcpSerial.read();
        // Handle incoming data
    }
    tcpSerial.write((const uint8_t*)"data\n", 5);
}
```

### WebSocket
```cpp
#include "WebSerial.h"
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
WebSerial wsSerial(server, "/ws");

void setup() {
    server.begin();
    wsSerial.begin();
}
```

### UDP
```cpp
#include "WebSerial.h"
#include <AsyncUDP.h>

AsyncUDP udp;
WebSerial udpSerial(udp, 8888);

void setup() {
    udpSerial.begin();
}
```

## API

### Constructors
- `WebSerial(uint16_t port = 23)` - TCP server
- `WebSerial(AsyncWebServer& server, const char* path = "/ws")` - WebSocket
- `WebSerial(AsyncUDP& udpServer, uint16_t port)` - UDP

### Methods
- `void begin()` - Start server
- `void end()` - Stop server
- `bool isRunning()` - Check status
- `size_t write(const uint8_t* buffer, size_t size)` - Send data
- `int read()` - Read byte
- `int available()` - Bytes available
- `size_t getClientCount()` - Connected clients

## Dependencies

- AsyncTCP
- ESPAsyncWebServer
- AsyncUDP

## License

MIT
