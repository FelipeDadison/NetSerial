/**
 * @example websocket.ino
 * @brief WebSocket server example using NetSerial
 *
 * Connect with: websocat ws://<esp32_ip>/ws
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "NetSerial.h"

AsyncWebServer server(80);
NetSerial wsSerial(server, "/ws");

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
        Serial.print(WiFi.localIP());
        Serial.println("/ws");
    } else {
        Serial.println("\nFailed to connect to WiFi, restarting...");
        delay(2000);
        ESP.restart();
    }

    // Start server and NetSerial
    server.begin();

    if (wsSerial.begin()) {
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
        wsSerial.println("Message from ESP32");
        Serial.println("Sent message to WebSocket clients");
        Serial.printf("Connected clients: %u\n", wsSerial.getClientCount());
    }

    delay(100);
}