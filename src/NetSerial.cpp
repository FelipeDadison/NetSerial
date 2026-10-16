/**
 * @file NetSerial.cpp
 * @brief Implementation of multi-protocol Serial communication for ESP32
 * @author Felipe Dadison
 * @version 1.1.0
 * @date 2026-10-16
 *
 * Implements TCP, WebSocket, and UDP transport layers for the NetSerial class.
 * Manages async client connections, ring buffers, and protocol-specific event handling.
 */

#include "NetSerial.h"

NetSerial::NetSerial(uint16_t port) {
    _tcpPort = port;
    _mutex = xSemaphoreCreateMutex();
    _mode = Mode::TCP;
}

NetSerial::NetSerial(AsyncWebServer& server, const char* path) {
    _ws = new AsyncWebSocket(path);
    _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });
    _parentServer = &server;
    _mutex = xSemaphoreCreateMutex();
    _mode = Mode::WS;
}

NetSerial::NetSerial(AsyncUDP& udpServer, uint16_t port) {
    _udpServer = &udpServer;
    _udpPort = port;
    _mutex = xSemaphoreCreateMutex();
    _mode = Mode::UDP;
}

NetSerial::~NetSerial() {
    end();

    if (_flushTimer) {
        esp_timer_delete(_flushTimer);
        _flushTimer = nullptr;
    }

    if (_ws) {
        _ws->onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {});

        unsigned long start = millis();
        while (!_ws->getClients().empty() && millis() - start < WS_CLOSE_TIMEOUT_MS) {
            delay(10);
        }

        if (_ws->getClients().empty()) {
            delete _ws;
        }
        _ws = nullptr;
    }

    if (_rxRing) {
        vRingbufferDelete(_rxRing);
        _rxRing = nullptr;
    }

    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool NetSerial::begin() {
    if (_running) return true;
    if (!_mutex) return false;

    if (!_rxRing) {
        _rxRing = xRingbufferCreate(RX_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
        if (!_rxRing) return false;
    }
    clearRxData();

    if (_mode == Mode::TCP) {
        _tcpServer = new AsyncServer(_tcpPort);
        _tcpServer->onClient([this](void* arg, AsyncClient* client) {
            this->onTcpClientConnect(arg, client);
        }, nullptr);
        _tcpServer->begin();
    } else if (_mode == Mode::WS) {
        if (!_ws || !_parentServer) return false;

        if (!_flushTimer) {
            esp_timer_create_args_t args = {
                .callback = [](void* arg) {
                    auto self = static_cast<NetSerial*>(arg);
                    self->flushWSBuffer();
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "wsFlush"
            };
            if (esp_timer_create(&args, &_flushTimer) != ESP_OK) {
                _flushTimer = nullptr;
                return false;
            }
        }

        lock();
        _wsSendBuffer = "";
        _wsSendBuffer.reserve(WS_MAX_BUFFER_SIZE);
        unlock();

        _parentServer->addHandler(_ws);
        esp_timer_start_periodic(_flushTimer, WS_FLUSH_INTERVAL_MS * 1000);
    } else if (_mode == Mode::UDP) {
        if (!_udpServer || !_udpServer->listen(_udpPort)) return false;
        _udpServer->onPacket([this](AsyncUDPPacket& packet) {
            this->onUdpPacket(packet);
        });
    }

    _running = true;
    return true;
}

void NetSerial::end() {
    if (!_running) return;
    _running = false;

    if (_mode == Mode::TCP) {
        if (_tcpServer) {
            _tcpServer->end();
            delete _tcpServer;
            _tcpServer = nullptr;
        }

        std::vector<AsyncClient*> clients;
        lock();
        clients.swap(_tcpClients);
        unlock();

        for (auto* c : clients) {
            c->onData(nullptr, nullptr);
            c->onDisconnect([](void* arg, AsyncClient* client) {
                delete client;
            }, nullptr);
            c->close();
        }
    } else if (_mode == Mode::WS) {
        if (_flushTimer) {
            esp_timer_stop(_flushTimer);
        }

        flushWSBuffer();

        _ws->closeAll();
        _parentServer->removeHandler(_ws);

        lock();
        _wsSendBuffer = "";
        unlock();
    } else if (_mode == Mode::UDP) {
        _udpServer->close();

        lock();
        _udpClients.clear();
        unlock();
    }

    clearRxData();
}

bool NetSerial::isRunning() const {
    return _running;
}

size_t NetSerial::getClientCount() const {
    if (_mode == Mode::WS) return _ws ? _ws->count() : 0;

    size_t count = 0;
    lock();
    if (_mode == Mode::TCP) count = _tcpClients.size();
    else if (_mode == Mode::UDP) count = _udpClients.size();
    unlock();
    return count;
}

size_t NetSerial::write(uint8_t b) {
    return write(&b, 1);
}

size_t NetSerial::write(const uint8_t* buffer, size_t size) {
    if (!_running || !buffer || size == 0) return 0;

    size_t sent = 0;

    if (_mode == Mode::TCP) {
        lock();
        for (auto* c : _tcpClients) {
            if (c->connected() && c->space() >= size) {
                sent = std::max(sent, c->write((const char*)buffer, size));
            }
        }
        unlock();
    } else if (_mode == Mode::WS) {
        lock();
        size_t freeSize = WS_MAX_BUFFER_SIZE - std::min(WS_MAX_BUFFER_SIZE, (size_t)_wsSendBuffer.length());
        sent = std::min(size, freeSize);
        if (sent > 0) {
            _wsSendBuffer.concat((const char*)buffer, sent);
        }
        unlock();
    } else if (_mode == Mode::UDP) {
        lock();
        removeInactiveUdpClients();
        for (const auto& client : _udpClients) {
            sent = std::max(sent, _udpServer->writeTo(buffer, size, client.ip, client.port));
        }
        unlock();
    }

    return sent;
}

int NetSerial::availableForWrite() {
    if (!_running) return 0;

    size_t freeSize = 0;

    if (_mode == Mode::TCP) {
        lock();
        bool first = true;
        for (auto* c : _tcpClients) {
            if (c->connected()) {
                freeSize = first ? c->space() : std::min(freeSize, c->space());
                first = false;
            }
        }
        unlock();
    } else if (_mode == Mode::WS) {
        if (_ws->count() > 0) {
            lock();
            freeSize = WS_MAX_BUFFER_SIZE - std::min(WS_MAX_BUFFER_SIZE, (size_t)_wsSendBuffer.length());
            unlock();
        }
    } else if (_mode == Mode::UDP) {
        lock();
        if (!_udpClients.empty()) freeSize = UDP_MAX_PAYLOAD;
        unlock();
    }

    return freeSize;
}

int NetSerial::available() {
    if (!_rxRing) return 0;

    size_t freeSize = xRingbufferGetCurFreeSize(_rxRing);
    return (RX_RING_BUFFER_SIZE - freeSize) + (_peekByte >= 0 ? 1 : 0);
}

int NetSerial::read() {
    if (_peekByte >= 0) {
        int byte = _peekByte;
        _peekByte = -1;
        return byte;
    }

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

int NetSerial::peek() {
    if (_peekByte < 0) {
        _peekByte = read();
    }
    return _peekByte;
}

void NetSerial::flush() {
    if (_running && _mode == Mode::WS) {
        flushWSBuffer();
    }
}

void NetSerial::keepAlive(uint16_t seconds) {
    _wsKeepAliveSeconds = seconds;
}

void NetSerial::setBinaryMode(bool enabled) {
    _wsBinary = enabled;
}

void NetSerial::setMaxClients(uint8_t maxClients) {
    _maxClients = maxClients;
}

void NetSerial::onTcpClientConnect(void* arg, AsyncClient* client) {
    lock();
    bool full = _tcpClients.size() >= _maxClients;
    unlock();

    if (full || !_running) {
        client->onDisconnect([](void* arg, AsyncClient* c) {
            delete c;
        }, nullptr);
        client->close();
        return;
    }

    client->setNoDelay(true);
    client->setKeepAlive(TCP_KEEPALIVE_MS, TCP_KEEPALIVE_COUNT);

    client->onData([this](void* arg, AsyncClient* c, void* data, size_t len) {
        this->onTcpClientData(arg, c, data, len);
    }, nullptr);

    client->onDisconnect([this](void* arg, AsyncClient* c) {
        this->onTcpClientDisconnect(arg, c);
    }, nullptr);

    lock();
    _tcpClients.push_back(client);
    unlock();
}

void NetSerial::onTcpClientData(void* arg, AsyncClient* client, void* data, size_t len) {
    pushRxData((uint8_t*)data, len);
}

void NetSerial::onTcpClientDisconnect(void* arg, AsyncClient* client) {
    bool found = false;

    lock();
    auto it = std::find(_tcpClients.begin(), _tcpClients.end(), client);
    if (it != _tcpClients.end()) {
        _tcpClients.erase(it);
        found = true;
    }
    unlock();

    if (found) {
        delete client;
    }
}

void NetSerial::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (server->count() > _maxClients) {
                client->close();
                break;
            }
            if (_wsKeepAliveSeconds > 0) client->keepAlivePeriod(_wsKeepAliveSeconds);
            client->client()->setAckTimeout(WS_ACK_TIMEOUT_MS);
        break;

        case WS_EVT_DATA:
            pushRxData(data, len);
        break;

        default:
        break;
    }
}

void NetSerial::onUdpPacket(AsyncUDPPacket& packet) {
    if (!_running) return;

    lock();
    removeInactiveUdpClients();
    bool accepted = addOrUpdateUdpClient(packet.remoteIP(), packet.remotePort());
    unlock();

    if (accepted) {
        pushRxData(packet.data(), packet.length());
    }
}

void NetSerial::flushWSBuffer() {
    if (!_ws || !_mutex) return;

    lock();

    if (_wsSendBuffer.length() > 0) {
        if (_ws->count() == 0) {
            _wsSendBuffer = "";
        } else if (_ws->availableForWriteAll()) {
            if (_wsBinary) {
                _ws->binaryAll(_wsSendBuffer.c_str(), _wsSendBuffer.length());
                _wsSendBuffer = "";
            } else {
                size_t len = utf8CompleteLength(_wsSendBuffer.c_str(), _wsSendBuffer.length());
                if (len > 0) {
                    _ws->textAll(_wsSendBuffer.c_str(), len);
                    _wsSendBuffer.remove(0, len);
                }
            }
        }
    }

    unlock();
}

void NetSerial::pushRxData(const uint8_t* data, size_t len) {
    if (!_running || !_rxRing || !data || len == 0) return;

    size_t freeSize = xRingbufferGetCurFreeSize(_rxRing);
    if (len > freeSize) len = freeSize;

    if (len > 0) {
        xRingbufferSend(_rxRing, data, len, 0);
    }
}

void NetSerial::clearRxData() {
    _peekByte = -1;

    if (!_rxRing) return;

    size_t itemSize;
    void* data;
    while ((data = xRingbufferReceiveUpTo(_rxRing, &itemSize, 0, RX_RING_BUFFER_SIZE)) != nullptr) {
        vRingbufferReturnItem(_rxRing, data);
    }
}

size_t NetSerial::utf8CompleteLength(const char* data, size_t len) {
    size_t start = len;
    size_t back = 0;

    while (start > 0 && back < 4) {
        start--;
        back++;
        uint8_t c = (uint8_t)data[start];
        if ((c & 0xC0) != 0x80) {
            size_t expected = 1;
            if ((c & 0xE0) == 0xC0) expected = 2;
            else if ((c & 0xF0) == 0xE0) expected = 3;
            else if ((c & 0xF8) == 0xF0) expected = 4;
            return (back < expected) ? start : len;
        }
    }

    return len;
}

bool NetSerial::addOrUpdateUdpClient(const IPAddress& ip, uint16_t port) {
    for (auto& client : _udpClients) {
        if (client.ip == ip && client.port == port) {
            client.lastSeen = millis();
            return true;
        }
    }

    if (_udpClients.size() >= _maxClients) return false;

    _udpClients.push_back({ip, port, millis()});
    return true;
}

void NetSerial::removeInactiveUdpClients() {
    unsigned long now = millis();
    if (now - _lastUdpCleanup < UDP_CLEANUP_INTERVAL_MS) return;
    _lastUdpCleanup = now;

    _udpClients.erase(
        std::remove_if(_udpClients.begin(), _udpClients.end(),
            [now, this](const UdpClient& client) {
                return (now - client.lastSeen) > UDP_CLIENT_TIMEOUT_MS;
            }
        ),
        _udpClients.end()
    );
}

void NetSerial::lock() const {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void NetSerial::unlock() const {
    if (_mutex) xSemaphoreGive(_mutex);
}

NetSerial nSerial;
NetSerial& wSerial = nSerial;