#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef MQTT_TOPIC
#define MQTT_TOPIC "vehicle/can/frames"
#endif

#ifndef STM32_UART_RX_PIN
#define STM32_UART_RX_PIN 18
#endif

#ifndef STM32_UART_TX_PIN
#define STM32_UART_TX_PIN 17
#endif

#ifndef STM32_UART_BAUD
#define STM32_UART_BAUD 115200
#endif