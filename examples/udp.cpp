/**
 * @example udp.cpp
 * @brief UDP server example using WebSerial
 * 
 * Send packets to: <esp32_ip>:8888
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include "WebSerial.h"

AsyncUDP udp;
WebSerial udpSerial(udp, 8888);

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[UDP Server Example]");
    
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
        Serial.println("Send UDP packets to port 8888");
    } else {
        Serial.println("\nFailed to connect to WiFi");
        while (true) {}
    }
    
    // Start UDP server
    udpSerial.begin();
    
    if (udpSerial.isRunning()) {
        Serial.println("UDP server started on port 8888");
    }
}

void loop() {
    // Check for incoming data
    if (udpSerial.available()) {
        String data = "";
        while (udpSerial.available()) {
            data += (char)udpSerial.read();
        }
        Serial.print("Received from UDP: ");
        Serial.println(data);
    }
    
    // Send periodic message
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 5000) {
        lastSend = millis();
        udpSerial.write((const uint8_t*)"UDP message from ESP32\n", 23);
        Serial.println("Sent UDP broadcast");
        Serial.printf("Connected clients: %u\n", udpSerial.getClientCount());
    }
    
    delay(100);
}
