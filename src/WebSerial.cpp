/**
 * @file WebSerial.cpp
 * @brief Implementation of multi-protocol Serial communication for ESP32
 * @author Felipe Dadison
 * @version 1.0.1
 * @date 2026-07-14
 * 
 * Implements TCP, WebSocket, and UDP transport layers for the WebSerial class.
 * Manages async client connections, ring buffers, and protocol-specific event handling.
 */

#include "WebSerial.h"

WebSerial::WebSerial(uint16_t port) {
    _tcpServer = new AsyncServer(port);
    _mode = Mode::TCP;
}

WebSerial::WebSerial(AsyncWebServer& server, const char* path) {
    _ws = new AsyncWebSocket(path);
    server.addHandler(_ws);
    _parentServer = &server;
    _mode = Mode::WS;
}

WebSerial::WebSerial(AsyncUDP& udpServer, uint16_t port) {
    _udpServer = &udpServer;
    _udpPort = port;
    _mode = Mode::UDP;
}

WebSerial::~WebSerial() {
    end();
}

void WebSerial::begin() {
    if (_running) return;

    if (!_rxRing) {
        _rxRing = xRingbufferCreate(RX_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    }

    if (_mode == Mode::TCP && _tcpServer) {
        _tcpServer->onClient([this](void* arg, AsyncClient* client) {
            this->onTcpClientConnect(arg, client);
        }, nullptr);
        _tcpServer->begin();
    } else if (_mode == Mode::WS && _ws) {
        _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                            AwsEventType type, void* arg, uint8_t* data, size_t len) {
            this->onWsEvent(server, client, type, arg, data, len);
        });

        if (!_bufferMutex) {
            _bufferMutex = xSemaphoreCreateMutex();
        }

        if (!_flushTimer) {
            esp_timer_create_args_t args = {
                .callback = [](void* arg) {
                    auto self = static_cast<WebSerial*>(arg);
                    self->flushWSBuffer();
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "wsFlush"
            };
            esp_timer_create(&args, &_flushTimer);
            esp_timer_start_periodic(_flushTimer, WS_FLUSH_INTERVAL_MS * 1000);
        }
    } else if (_mode == Mode::UDP && _udpServer) {
        if (_udpServer->listen(_udpPort)) {
            _udpServer->onPacket([this](AsyncUDPPacket& packet) {
                this->onUdpPacket(packet);
            });
        }
    }

    _running = true;
}

void WebSerial::end() {
    if (!_running) return;

    if (_rxRing) {
        vRingbufferDelete(_rxRing);
        _rxRing = nullptr;
    }

    if (_mode == Mode::TCP && _tcpServer) {
        for (auto* c : _tcpClients) {
            if (c) c->close();
        }
        _tcpClients.clear();
        _tcpServer->end();
        delete _tcpServer;
        _tcpServer = nullptr;
    } else if (_mode == Mode::WS && _ws) {
        if (_flushTimer) {
            esp_timer_stop(_flushTimer);
            esp_timer_delete(_flushTimer);
            _flushTimer = nullptr;
        }

        flushWSBuffer();

        if (_bufferMutex) {
            vSemaphoreDelete(_bufferMutex);
            _bufferMutex = nullptr;
        }

        _ws->closeAll();
        if (_parentServer) {
            _parentServer->removeHandler(_ws);
            _parentServer = nullptr;
        }
        _wsClients.clear();
        delete _ws;
        _ws = nullptr;
    } else if (_mode == Mode::UDP && _udpServer) {
        _udpServer->close();
        _udpClients.clear();
        _udpServer = nullptr;
    }

    _running = false;
}

bool WebSerial::isRunning() const {
    return _running;
}

size_t WebSerial::getClientCount() const {
    if (_mode == Mode::TCP) return _tcpClients.size();
    if (_mode == Mode::WS) return _wsClients.size();
    if (_mode == Mode::UDP) return _udpClients.size();
    return 0;
}

size_t WebSerial::write(uint8_t b) {
    return write(&b, 1);
}

size_t WebSerial::write(const uint8_t* buffer, size_t size) {
    if (!_running) return 0;

    if (_mode == Mode::TCP) {
        size_t sent = 0;
        for (auto* c : _tcpClients) {
            if (c && c->connected()) {
                sent = c->write((const char*)buffer, size);
            }
        }
        return sent;
    } else if (_mode == Mode::WS) {
        if (!_bufferMutex) return 0;
        
        size_t sent = 0;

        if (xSemaphoreTake(_bufferMutex, portMAX_DELAY)) {
            _wsSendBuffer += String((const char*)buffer, size);
            xSemaphoreGive(_bufferMutex);
        }

        sent = size;
        return sent;
    } else if (_mode == Mode::UDP) {
        if (_udpServer) {
            size_t sent = 0;
            for (const auto& client : _udpClients) {
                AsyncUDPMessage msg(size);
                msg.write(buffer, size);
                _udpServer->sendTo(msg, client.ip, client.port);
                sent = size;
            }
            return sent;
        }
        return 0;
    }

    return 0;
}

int WebSerial::available() {
    if (!_rxRing) return 0;

    size_t itemSize;
    UBaseType_t itemsWaiting = xRingbufferGetCurFreeSize(_rxRing);
    return RX_RING_BUFFER_SIZE - itemsWaiting;
}

int WebSerial::read() {
    if (!_rxRing) return -1;

    size_t itemSize;
    uint8_t* data = (uint8_t*)xRingbufferReceiveUpTo(_rxRing, &itemSize, pdMS_TO_TICKS(_timeout), 1);
    if (data) {
        uint8_t byte = data[0];
        vRingbufferReturnItem(_rxRing, data);
        return byte;
    }
    return -1;
}

int WebSerial::peek() {
    if (!_rxRing) return -1;

    size_t itemSize;
    uint8_t* data = (uint8_t*)xRingbufferReceiveUpTo(_rxRing, &itemSize, 0, 1);
    if (data) {
        uint8_t byte = data[0];
        vRingbufferReturnItem(_rxRing, data);
        return byte;
    }
    return -1;
}

void WebSerial::flush() {

}

void WebSerial::setTimeout(unsigned long timeout) {
    _timeout = timeout;
}

unsigned long WebSerial::getTimeout() const {
    return _timeout;
}

void WebSerial::onTcpClientConnect(void* arg, AsyncClient* client) {
    _tcpClients.push_back(client);

    client->onData([this](void* arg, AsyncClient* c, void* data, size_t len) {
        this->onTcpClientData(arg, c, data, len);
    }, nullptr);

    client->onDisconnect([this](void* arg, AsyncClient* c) {
        this->onTcpClientDisconnect(arg, c);
    }, nullptr);
}

void WebSerial::onTcpClientData(void* arg, AsyncClient* client, void* data, size_t len) {
    pushRxData((uint8_t*)data, len);
}

void WebSerial::onTcpClientDisconnect(void* arg, AsyncClient* client) {
    _tcpClients.erase(
        std::remove(_tcpClients.begin(), _tcpClients.end(), client),
        _tcpClients.end()
    );
}

void WebSerial::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (_bufferMutex) xSemaphoreTake(_bufferMutex, portMAX_DELAY);
            _wsClients.push_back(client);
            if (_bufferMutex) xSemaphoreGive(_bufferMutex);
            if (_wsKeepAliveSeconds > 0) client->keepAlivePeriod(_wsKeepAliveSeconds);
            client->client()->setAckTimeout(WS_ACK_TIMEOUT_MS);
        break;

        case WS_EVT_DISCONNECT:
            if (_bufferMutex) xSemaphoreTake(_bufferMutex, portMAX_DELAY);
            _wsClients.erase(
                std::remove(_wsClients.begin(), _wsClients.end(), client),
                _wsClients.end()
            );
            if (_bufferMutex) xSemaphoreGive(_bufferMutex);
        break;

        case WS_EVT_DATA:
            pushRxData(data, len);
        break;

        default:
        break;
    }
}

void WebSerial::keepAlive(uint16_t seconds) {
    _wsKeepAliveSeconds = seconds;
}

void WebSerial::onUdpPacket(AsyncUDPPacket& packet) {
    addOrUpdateUdpClient(packet.remoteIP(), packet.remotePort());
    
    unsigned long now = millis();
    if (now - _lastUdpCleanup > UDP_CLEANUP_INTERVAL_MS) {
        removeInactiveUdpClients();
        _lastUdpCleanup = now;
    }

    pushRxData(packet.data(), packet.length());
}

void WebSerial::flushWSBuffer() {
    if (!_bufferMutex) return;

    if (xSemaphoreTake(_bufferMutex, portMAX_DELAY)) {
        if (_wsSendBuffer.length() > 0) {
            if (_wsClients.empty()) {
                _wsSendBuffer = "";
            } else {
                bool sent = false;
                for (auto* c : _wsClients) {
                    if (c && c->canSend()) {
                        c->text(_wsSendBuffer);
                        sent = true;
                    }
                }
                if (sent) {
                    _wsSendBuffer = "";
                }
                // Client busy (send queue full): keep the buffer and retry on
                // the next flush tick; discard past 4 KB to prevent OOM.
                else if (_wsSendBuffer.length() > 4096) {
                    _wsSendBuffer = "";
                }
            }
        }

        xSemaphoreGive(_bufferMutex);
    }
}

void WebSerial::pushRxData(const uint8_t* data, size_t len) {
    if (_rxRing) {
        size_t sent = xRingbufferSend(_rxRing, data, len, pdMS_TO_TICKS(_timeout));
        if (!sent) {

        }
    }
}

void WebSerial::addOrUpdateUdpClient(const IPAddress& ip, uint16_t port) {
    UdpClient newClient = {ip, port, millis()};
    
    for (auto& client : _udpClients) {
        if (client.ip == ip && client.port == port) {
            client.lastSeen = millis();
            return;
        }
    }
    
    _udpClients.push_back(newClient);
}

void WebSerial::removeInactiveUdpClients() {
    unsigned long now = millis();
    
    _udpClients.erase(
        std::remove_if(_udpClients.begin(), _udpClients.end(),
            [now, this](const UdpClient& client) {
                return (now - client.lastSeen) > UDP_CLIENT_TIMEOUT_MS;
            }
        ),
        _udpClients.end()
    );
}

WebSerial wSerial;