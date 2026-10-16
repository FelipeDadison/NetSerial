# NetSerial

Multi-protocol Serial communication wrapper for ESP32 supporting TCP, WebSocket, and UDP transports with Arduino Stream interface.

Since `NetSerial` is a `Stream`, everything you use with `Serial` works the same way: `print()`, `println()`, `printf()`, `readStringUntil()`, `parseInt()`, etc.

## Installation

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/FelipeDadison/NetSerial.git
```

## Usage

### TCP Server (using global instance)
```cpp
#include "NetSerial.h"

// Use the pre-instantiated global: nSerial or wSerial (TCP on port 23)

void setup() {
    wSerial.begin();
}

void loop() {
    while (wSerial.available()) {
        int byte = wSerial.read();
        // Handle incoming data
    }
    wSerial.println("data");
    delay(1000);
}
```

### TCP Server (custom instance)
```cpp
#include "NetSerial.h"

NetSerial tcpSerial(8023);  // Custom port

void setup() {
    tcpSerial.begin();
}

void loop() {
    while (tcpSerial.available()) {
        int byte = tcpSerial.read();
        // Handle incoming data
    }
    tcpSerial.println("data");
    delay(1000);
}
```

### WebSocket
```cpp
#include "NetSerial.h"
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
NetSerial wsSerial(server, "/ws");

void setup() {
    server.begin();
    wsSerial.keepAlive(10);
    wsSerial.begin();
}
```

### UDP
```cpp
#include "NetSerial.h"
#include <AsyncUDP.h>

AsyncUDP udp;
NetSerial udpSerial(udp, 8888);

void setup() {
    udpSerial.begin();
}
```

Complete sketches are in [`examples/`](examples).

## Testing

| Transport | Command |
|-----------|---------|
| TCP | `nc <esp32_ip> 23` (or PuTTY in *Raw* mode) |
| WebSocket | `websocat ws://<esp32_ip>/ws` |
| UDP | `nc -u <esp32_ip> 8888` |

Prefer `nc` over `telnet` for TCP: telnet clients send option negotiation bytes that will show up as received data.

## API

### Constructors
- `NetSerial(uint16_t port = 23)` - TCP server
- `NetSerial(AsyncWebServer& server, const char* path = "/ws")` - WebSocket
- `NetSerial(AsyncUDP& udpServer, uint16_t port)` - UDP

### Methods
- `bool begin()` - Start the transport, returns `false` on failure. Can be called again after `end()`
- `void end()` - Stop the transport, close all clients and discard pending data
- `bool isRunning()` - Check status
- `size_t getClientCount()` - Connected clients
- `size_t write(uint8_t b)` / `size_t write(const uint8_t* buffer, size_t size)` - Send data to all clients
- `int read()` - Read a byte, returns `-1` immediately if no data is available
- `int peek()` - Next byte without removing it, `-1` if none
- `int available()` - Bytes available
- `void flush()` - Send pending WebSocket output now (no-op for TCP/UDP)
- `void keepAlive(uint16_t seconds)` - WebSocket ping interval, applied to clients connecting afterwards (0 = disabled, default)
- `void setBinaryMode(bool enabled)` - Send WebSocket output as binary frames instead of text frames (default: text)
- `void setMaxClients(uint8_t maxClients)` - Maximum simultaneous clients, new clients above the limit are rejected (default: 8)
- `int availableForWrite()` - Bytes that can be written without being dropped, `0` if no client is connected
- `setTimeout()` / `getTimeout()` - Inherited from `Stream`, used by `readBytes()`, `readString()`, `parseInt()`, etc.

### Global instance
`nSerial` is a pre-instantiated TCP instance on port 23, and `wSerial` is another name for the same instance (use whichever you prefer, they are the same object). It does not allocate any network resources until `begin()` is called.

## Behavior

- **RX buffer**: 4 KB shared by all clients. When full, incoming data is dropped.
- **TCP**: each `write()` is sent immediately (Nagle disabled). If a client's send buffer doesn't have room for the whole `write()`, it is dropped for that client, so lines are never cut in half. Dead clients are detected with TCP keep-alive (10 s).
- **WebSocket**: output is batched and sent every 25 ms. If a client can't keep up, the output is held until it can; up to 4 KB is held, after that new data is dropped. In text mode, a UTF-8 character split between two writes is never split between two frames. Text frames must be valid UTF-8, so call `setBinaryMode(true)` when sending raw bytes.
- **UDP**: a client is registered when it sends its first packet and removed after 30 s without sending anything. Packets from new clients above the client limit are ignored. Each `write()` is a separate datagram, so `println("x")` sends two datagrams (`"x"` and `"\r\n"`).
- **Threading**: `read()`, `peek()` and `available()` must be called from a single task.

## Security

NetSerial has no authentication or encryption. Anyone on the same network can connect, read everything written to it and send data to it. If you use it as a command console, only enable it on trusted networks, or call `end()` when it isn't needed.

## Dependencies

- [ESP32Async/AsyncTCP](https://github.com/ESP32Async/AsyncTCP)
- [ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)
- AsyncUDP (included in the ESP32 Arduino core)

## License

MIT
