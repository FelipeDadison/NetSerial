/**
 * @file NetSerial.h
 * @brief Multi-protocol Serial communication wrapper for ESP32 (TCP, WebSocket, UDP)
 * @author Felipe Dadison
 * @version 1.1.0
 * @date 2026-10-16
 *
 * NetSerial provides a unified Stream interface supporting TCP, WebSocket, and UDP
 * communication protocols for ESP32 devices. It implements the Arduino Stream class
 * for seamless integration with existing serial code.
 */

#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <vector>
#include <algorithm>
#include <atomic>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncUDP.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

/**
 * @class NetSerial
 * @brief Multi-protocol serial communication class supporting TCP, WebSocket, and UDP
 *
 * Provides a Stream-based interface for non-blocking serial communication over
 * multiple transport protocols. Compatible with standard Arduino Serial API.
 */
class NetSerial : public Stream {
    public:
        /**
         * @brief Construct TCP server NetSerial instance
         * @param port TCP port number (default 23 for Telnet)
         */
        NetSerial(uint16_t port = 23);

        /**
         * @brief Construct WebSocket-based NetSerial instance
         * @param server Reference to AsyncWebServer instance
         * @param path WebSocket endpoint path (default "/ws")
         */
        NetSerial(AsyncWebServer& server, const char* path = "/ws");

        /**
         * @brief Construct UDP-based NetSerial instance
         * @param udpServer Reference to AsyncUDP instance
         * @param port UDP port number
         */
        NetSerial(AsyncUDP& udpServer, uint16_t port);

        /**
         * @brief Destructor - automatically calls end() and frees all resources
         */
        ~NetSerial();

        NetSerial(const NetSerial&) = delete;
        NetSerial& operator=(const NetSerial&) = delete;

        /**
         * @brief Initialize the NetSerial instance and start listening for connections
         * Initializes RX ring buffer, starts server/websocket listeners based on mode.
         * Can be called again after end().
         * @return true if the transport was started successfully
         */
        bool begin();

        /**
         * @brief Stop the NetSerial instance
         * Closes all connections and discards pending RX/TX data
         */
        void end();

        /**
         * @brief Check if NetSerial is running
         * @return true if begin() succeeded and end() has not been called
         */
        bool isRunning() const;

        /**
         * @brief Get the number of connected clients
         * @return Number of active client connections
         */
        size_t getClientCount() const;

        /**
         * @brief Write a single byte to all connected clients
         * @param b Single byte to write
         * @return Number of bytes written
         */
        size_t write(uint8_t b) override;

        /**
         * @brief Write buffer to all connected clients
         * @param buffer Pointer to data buffer
         * @param size Number of bytes to write
         * @return Number of bytes written
         */
        size_t write(const uint8_t* buffer, size_t size) override;

        /**
         * @brief Get number of bytes that can be written without being dropped
         * @return Free space in bytes, 0 if no client is connected
         */
        int availableForWrite() override;

        /**
         * @brief Get number of bytes available to read from RX buffer
         * @return Number of bytes available
         */
        int available() override;

        /**
         * @brief Read a single byte from RX buffer (non-blocking)
         * @return Byte read, or -1 if no data is available
         */
        int read() override;

        /**
         * @brief Peek at the next byte without removing it from RX buffer
         * @return Next byte available, or -1 if none available
         */
        int peek() override;

        /**
         * @brief Send pending output immediately (WebSocket mode only, no-op otherwise)
         */
        void flush() override;

        /**
         * @brief Set WebSocket keep-alive ping interval
         * Applied to every client at connect time. 0 disables (default).
         * @param seconds Ping interval in seconds
         */
        void keepAlive(uint16_t seconds);

        /**
         * @brief Select WebSocket frame type for outgoing data
         * Text frames (default) must carry valid UTF-8; use binary frames for raw data.
         * @param enabled true to send binary frames, false to send text frames
         */
        void setBinaryMode(bool enabled);

        /**
         * @brief Set the maximum number of simultaneous clients
         * New clients above the limit are rejected. Default is 8.
         * @param maxClients Maximum number of clients
         */
        void setMaxClients(uint8_t maxClients);

    private:

        std::atomic<bool> _running{false};
        int _peekByte = -1;
        uint8_t _maxClients = 8;

        RingbufHandle_t _rxRing = nullptr;
        const size_t RX_RING_BUFFER_SIZE = 4096;

        SemaphoreHandle_t _mutex = nullptr;

        uint16_t _tcpPort = 0;
        AsyncServer* _tcpServer = nullptr;
        std::vector<AsyncClient*> _tcpClients;
        const uint32_t TCP_KEEPALIVE_MS = 10000;
        const uint8_t TCP_KEEPALIVE_COUNT = 3;

        AsyncWebSocket* _ws = nullptr;
        AsyncWebServer* _parentServer = nullptr;
        uint16_t _wsKeepAliveSeconds = 0;
        bool _wsBinary = false;

        esp_timer_handle_t _flushTimer = nullptr;

        String _wsSendBuffer = "";
        const unsigned long WS_FLUSH_INTERVAL_MS = 25;
        const size_t WS_MAX_BUFFER_SIZE = 4096;
        // AsyncTCP defaults to a 5 s ack timeout, which kills WS clients
        // during TCP stalls that would recover on their own.
        const uint32_t WS_ACK_TIMEOUT_MS = 30000;
        const unsigned long WS_CLOSE_TIMEOUT_MS = 1000;

        AsyncUDP* _udpServer = nullptr;
        uint16_t _udpPort = 0;

        struct UdpClient {
            IPAddress ip;
            uint16_t port;
            unsigned long lastSeen;

            bool operator==(const UdpClient& other) const {
                return ip == other.ip && port == other.port;
            }
        };

        std::vector<UdpClient> _udpClients;
        const unsigned long UDP_CLIENT_TIMEOUT_MS = 30000;
        unsigned long _lastUdpCleanup = 0;
        const unsigned long UDP_CLEANUP_INTERVAL_MS = 5000;
        const size_t UDP_MAX_PAYLOAD = 1460;

        enum class Mode { TCP, WS, UDP } _mode;

        /// @name TCP Event Handlers
        /// @{
        /// @brief Handle new TCP client connection
        /// @param arg Callback argument
        /// @param client Connected AsyncClient pointer
        void onTcpClientConnect(void* arg, AsyncClient* client);

        /// @brief Handle incoming data from TCP client
        /// @param arg Callback argument
        /// @param client Source AsyncClient pointer
        /// @param data Pointer to received data
        /// @param len Length of received data
        void onTcpClientData(void* arg, AsyncClient* client, void* data, size_t len);

        /// @brief Handle TCP client disconnection and free the client
        /// @param arg Callback argument
        /// @param client Disconnected AsyncClient pointer
        void onTcpClientDisconnect(void* arg, AsyncClient* client);
        /// @}

        /// @name WebSocket Event Handlers
        /// @{
        /// @brief Handle WebSocket events (connect, disconnect, data)
        /// @param server Pointer to AsyncWebSocket instance
        /// @param client Pointer to AsyncWebSocketClient
        /// @param type Event type (WS_EVT_CONNECT, WS_EVT_DISCONNECT, WS_EVT_DATA)
        /// @param arg Callback argument
        /// @param data Pointer to event data (NULL for connection events)
        /// @param len Length of event data
        void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
        /// @}

        /// @name UDP Handlers
        /// @{
        /// @brief Handle incoming UDP packet
        /// @param packet Received AsyncUDPPacket
        void onUdpPacket(AsyncUDPPacket& packet);
        /// @}

        /// @name Helper Methods
        /// @{
        /// @brief Flush pending WebSocket output buffer
        void flushWSBuffer();

        /// @brief Push received data into RX ring buffer, dropping what does not fit
        /// @param data Pointer to data buffer
        /// @param len Length of data
        void pushRxData(const uint8_t* data, size_t len);

        /// @brief Discard all data in the RX ring buffer
        void clearRxData();

        /// @brief Length of the longest prefix that does not end in a partial UTF-8 sequence
        /// @param data Pointer to text data
        /// @param len Length of data
        /// @return Number of bytes that can be sent as a complete UTF-8 text
        static size_t utf8CompleteLength(const char* data, size_t len);

        /// @brief Add or update UDP client in client list (caller must hold _mutex)
        /// @param ip Client IP address
        /// @param port Client port number
        /// @return false if the client is new and the client limit was reached
        bool addOrUpdateUdpClient(const IPAddress& ip, uint16_t port);

        /// @brief Remove UDP clients that haven't communicated recently (caller must hold _mutex)
        void removeInactiveUdpClients();

        /// @brief Take the internal mutex
        void lock() const;

        /// @brief Release the internal mutex
        void unlock() const;
        /// @}
};

extern NetSerial nSerial;
extern NetSerial& wSerial;