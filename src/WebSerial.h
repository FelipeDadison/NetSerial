/**
 * @file WebSerial.h
 * @brief Multi-protocol Serial communication wrapper for ESP32 (TCP, WebSocket, UDP)
 * @author Felipe Dadison
 * @version 1.0.0
 * @date 2026-03-10
 * 
 * WebSerial provides a unified Stream interface supporting TCP, WebSocket, and UDP
 * communication protocols for ESP32 devices. It implements the Arduino Stream class
 * for seamless integration with existing serial code.
 */

#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <vector>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncUDP.h>
#include <freertos/ringbuf.h>

/**
 * @class WebSerial
 * @brief Multi-protocol serial communication class supporting TCP, WebSocket, and UDP
 * 
 * Provides a Stream-based interface for non-blocking serial communication over
 * multiple transport protocols. Compatible with standard Arduino Serial API.
 */
class WebSerial : public Stream {
    public:
        /**
         * @brief Construct TCP server WebSerial instance
         * @param port TCP port number (default 23 for Telnet)
         */
        WebSerial(uint16_t port = 23);

        /**
         * @brief Construct WebSocket-based WebSerial instance
         * @param server Reference to AsyncWebServer instance
         * @param path WebSocket endpoint path (default "/ws")
         */
        WebSerial(AsyncWebServer& server, const char* path = "/ws");

        /**
         * @brief Construct UDP-based WebSerial instance
         * @param udpServer Reference to AsyncUDP instance
         * @param port UDP port number
         */
        WebSerial(AsyncUDP& udpServer, uint16_t port);

        /**
         * @brief Destructor - automatically calls end()
         */
        ~WebSerial();

        /**
         * @brief Initialize the WebSerial instance and start listening for connections
         * Initializes RX ring buffer, starts server/websocket listeners based on mode
         */
        void begin();
        
        /**
         * @brief Stop the WebSerial instance and clean up resources
         * Closes all connections and deallocates buffers and timers
         */
        void end();

        /**
         * @brief Check if WebSerial is running
         * @return true if begin() was called and end() has not been called
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
         * @brief Get number of bytes available to read from RX buffer
         * @return Number of bytes available
         */
        int available() override;
        
        /**
         * @brief Read a single byte from RX buffer (blocking)
         * @return Byte read, or -1 if timeout or error
         */
        int read() override;
        
        /**
         * @brief Peek at the next byte without removing it from RX buffer
         * @return Next byte available, or -1 if none available
         */
        int peek() override;
        
        /**
         * @brief Flush the output buffer (WebSocket mode only)
         */
        void flush() override;

        /**
         * @brief Set the read timeout duration
         * @param timeout Timeout in milliseconds
         */
        void setTimeout(unsigned long timeout);
        
        /**
         * @brief Get the current read timeout duration
         * @return Timeout in milliseconds
         */
        unsigned long getTimeout() const;

    private:

        bool _running = false;
        unsigned long _timeout = 1000;

        RingbufHandle_t _rxRing = nullptr;
        const size_t RX_RING_BUFFER_SIZE = 4096;

        AsyncServer* _tcpServer = nullptr;
        std::vector<AsyncClient*> _tcpClients;

        AsyncWebSocket* _ws = nullptr;
        AsyncWebServer* _parentServer = nullptr;
        std::vector<AsyncWebSocketClient*> _wsClients;

        SemaphoreHandle_t _bufferMutex = nullptr;
        esp_timer_handle_t _flushTimer = nullptr;

        String _wsSendBuffer = "";
        const unsigned long WS_FLUSH_INTERVAL_MS = 25;

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
        
        /// @brief Handle TCP client disconnection
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
        
        /// @brief Push received data into RX ring buffer
        /// @param data Pointer to data buffer
        /// @param len Length of data
        void pushRxData(const uint8_t* data, size_t len);
        
        /// @brief Add or update UDP client in client list
        /// @param ip Client IP address
        /// @param port Client port number
        void addOrUpdateUdpClient(const IPAddress& ip, uint16_t port);
        
        /// @brief Remove UDP clients that haven't communicated recently
        void removeInactiveUdpClients();
        /// @}
};

extern WebSerial wSerial;