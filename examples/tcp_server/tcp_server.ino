/**
 * @example tcp_server.ino
 * @brief Simple TCP server example using NetSerial
 *
 * Connect with: nc <esp32_ip> 23
 */

#include <Arduino.h>
#include <WiFi.h>
#include "NetSerial.h"

// Use the pre-instantiated global wSerial (TCP on port 23), nSerial is the same instance
// Or create custom instance: NetSerial tcpSerial(8023);

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n[TCP Server Example]");

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
        Serial.print("Connect with: nc ");
        Serial.print(WiFi.localIP());
        Serial.println(" 23");
    } else {
        Serial.println("\nFailed to connect to WiFi, restarting...");
        delay(2000);
        ESP.restart();
    }

    if (wSerial.begin()) {
        Serial.println("TCP Server started on port 23");
    }
}

void loop() {
    // Check for incoming data
    if (wSerial.available()) {
        String data = "";
        while (wSerial.available()) {
            data += (char)wSerial.read();
        }
        Serial.print("Received from TCP: ");
        Serial.println(data);
    }

    // Send periodic message
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 5000) {
        lastSend = millis();
        wSerial.println("Hello from ESP32");
        Serial.println("Sent message to TCP clients");
        Serial.printf("Connected clients: %u\n", wSerial.getClientCount());
    }

    delay(100);
}