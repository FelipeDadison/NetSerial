/**
 * @example tcp_server.cpp
 * @brief Simple TCP server example using WebSerial
 * 
 * Connect with: telnet <esp32_ip> 23
 */

#include <Arduino.h>
#include <WiFi.h>
#include "WebSerial.h"

// Use the pre-instantiated global wSerial (TCP on port 23)
// Or create custom instance: WebSerial tcpSerial(8023);

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
        Serial.print("Connect with: telnet ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to WiFi");
    }
    
    wSerial.begin();
    
    if (wSerial.isRunning()) {
        Serial.println("TCP Server started on port 23");
    }
}

void loop() {
    // Check for incoming data
    if (wSerial.available()) {
        char data = wSerial.read();
        Serial.print("Received: ");
        Serial.println(data);
    }
    
    // Send periodic message
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 5000) {
        lastSend = millis();
        wSerial.write((const uint8_t*)"Hello from ESP32\n", 17);
        Serial.println("Sent message to clients");
        Serial.printf("Connected clients: %u\n", wSerial.getClientCount());
    }
    
    delay(100);
}
