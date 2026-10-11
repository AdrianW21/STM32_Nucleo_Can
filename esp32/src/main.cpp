#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "project_config.h"

namespace {

constexpr size_t lineCapacity = 160;
constexpr unsigned long reconnectIntervalMs = 5000;

HardwareSerial stm32Serial(1);
WiFiClient networkClient;
PubSubClient mqttClient(networkClient);
char telemetryLine[lineCapacity];
size_t telemetryLength = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

bool hasNetworkConfiguration()
{
    return WIFI_SSID[0] != '\0' && MQTT_HOST[0] != '\0';
}

void connectWifi()
{
    if (!hasNetworkConfiguration() || WiFi.status() == WL_CONNECTED) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastWifiAttempt < reconnectIntervalMs) {
        return;
    }

    lastWifiAttempt = now;
    Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqtt()
{
    if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastMqttAttempt < reconnectIntervalMs) {
        return;
    }

    lastMqttAttempt = now;
    const String clientId = "stm32-can-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    bool connected;
    if (MQTT_USERNAME[0] != '\0') {
        connected = mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        connected = mqttClient.connect(clientId.c_str());
    }

    Serial.printf("MQTT connect %s\n", connected ? "succeeded" : "failed");
}

void publishTelemetry()
{
    if (telemetryLength == 0) {
        return;
    }

    telemetryLine[telemetryLength] = '\0';
    Serial.printf("%s\n", telemetryLine);
    if (mqttClient.connected() && !mqttClient.publish(MQTT_TOPIC, telemetryLine)) {
        Serial.println("MQTT publish failed");
    }
    telemetryLength = 0;
}

void readStm32Telemetry()
{
    while (stm32Serial.available() > 0) {
        const char incoming = static_cast<char>(stm32Serial.read());
        if (incoming == '\n') {
            if (telemetryLength > 0 && telemetryLine[telemetryLength - 1] == '\r') {
                --telemetryLength;
            }
            publishTelemetry();
        } else if (telemetryLength < lineCapacity - 1) {
            telemetryLine[telemetryLength++] = incoming;
        } else {
            telemetryLength = 0;
        }
    }
}

}

void setup()
{
    Serial.begin(115200);
    stm32Serial.begin(STM32_UART_BAUD, SERIAL_8N1,
                      STM32_UART_RX_PIN, STM32_UART_TX_PIN);
    WiFi.mode(WIFI_STA);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setBufferSize(lineCapacity);

    if (!hasNetworkConfiguration()) {
        Serial.println("Set Wi-Fi and MQTT values in include/secrets.h");
    }
    Serial.printf("STM32 UART ready: RX GPIO%d, TX GPIO%d at %lu baud\n",
                  STM32_UART_RX_PIN, STM32_UART_TX_PIN,
                  static_cast<unsigned long>(STM32_UART_BAUD));
}

void loop()
{
    connectWifi();
    connectMqtt();
    mqttClient.loop();
    readStm32Telemetry();
}