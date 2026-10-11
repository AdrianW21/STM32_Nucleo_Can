# ESP32-S3 CAN Telemetry Bridge

This is a standalone PlatformIO project for the ESP32-S3. It receives newline-terminated CAN telemetry from the NUCLEO-F439ZI over UART, prints the lines to its USB serial monitor, and publishes them to MQTT.

## UART wiring

| NUCLEO-F439ZI | ESP32-S3 |
| --- | --- |
| PD8 / USART3_TX | GPIO18 / UART1_RX |
| PD9 / USART3_RX | GPIO17 / UART1_TX |
| GND | GND |

The GPIO18/GPIO17 assignment is the default in `include/project_config.h`; change it there if your ESP32-S3 board uses different pins. Both UARTs use 115200 baud, 8-N-1. Connect TX to RX and RX to TX. Use a shared ground and verify the board pinout and UART voltage levels before wiring.

## Configure Wi-Fi and MQTT

Copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi and broker settings. The real `secrets.h` is ignored by Git. MQTT publishes each STM32 telemetry line to `vehicle/can/frames` by default; change `MQTT_TOPIC` in the secrets header to override it.

The MQTT payload is the original STM32 text line, for example `HS-CAN STD 0x123 DLC 8 DATA 10 20 30 40 50 60 70 80`. The bridge also prints incoming lines to its USB serial monitor. Telemetry received while MQTT is disconnected is not buffered for later publishing.

## Build and upload

Run PlatformIO from this directory so it uses this project's configuration:

```sh
cd esp32
pio run
pio run --target upload
pio device monitor
```

This project has its own PlatformIO configuration and dependencies; it does not change the STM32 build configuration in the repository root.