/**
 * @example udp.ino
 * @brief UDP server example using NetSerial
 *
 * Send packets to: <esp32_ip>:8888 (e.g. nc -u <esp32_ip> 8888)
 * A client only receives data after it has sent at least one packet.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include "NetSerial.h"

AsyncUDP udp;
NetSerial udpSerial(udp, 8888);

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
        Serial.println("\nFailed to connect to WiFi, restarting...");
        delay(2000);
        ESP.restart();
    }

    // Start UDP server
    if (udpSerial.begin()) {
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
        udpSerial.print("UDP message from ESP32\n");
        Serial.println("Sent message to UDP clients");
        Serial.printf("Connected clients: %u\n", udpSerial.getClientCount());
    }

    delay(100);
}