/**
 * @example websocket.cpp
 * @brief WebSocket server example using WebSerial
 * 
 * Connect from web browser: ws://esp32_ip:80/ws
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "WebSerial.h"

AsyncWebServer server(80);
WebSerial wsSerial(server, "/ws");

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[WebSocket Server Example]");
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Connect to: ws://");
        Serial.println(WiFi.localIP());
        Serial.println("/ws");
    } else {
        Serial.println("\nFailed to connect to WiFi");
    }
    
    // Start server and WebSerial
    server.begin();
    wsSerial.begin();
    
    if (wsSerial.isRunning()) {
        Serial.println("WebSocket server started");
    }
}

void loop() {
    // Check for incoming data
    if (wsSerial.available()) {
        String data = "";
        while (wsSerial.available()) {
            data += (char)wsSerial.read();
        }
        Serial.print("Received from WebSocket: ");
        Serial.println(data);
    }
    
    // Send periodic message
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 5000) {
        lastSend = millis();
        wsSerial.write((const uint8_t*)"Message from ESP32\n", 19);
        Serial.println("Sent message to WebSocket clients");
        Serial.printf("Connected clients: %u\n", wsSerial.getClientCount());
    }
    
    delay(100);
}
