# STM32 Nucleo CAN Bus Project

This is a C++ CAN bus project for a NUCLEO-F439ZI using PlatformIO and the STM32Cube HAL. It supports the Ford HS-CAN and MS-CAN wiring documented in `Info/` and sends received-frame telemetry to an ESP32-S3 over UART.

## Selected board
- NUCLEO-F439ZI

## Features
- HAL-based CAN1 and CAN2 configuration
- HS-CAN on PD1 (TX) and PD0 (RX)
- MS-CAN on PB13 (TX) and PB12 (RX)
- HS-CAN at 500 kbit/s and MS-CAN at 125 kbit/s
- CAN transmit example
- CAN receive monitor over USART3 on PD8 (TX) and PD9 (RX), connected to the ESP32-S3
- Ready for extension to a real bus application

## Requirements
- PlatformIO Core or VS Code PlatformIO extension
- STM32 Nucleo board
- ST-LINK programmer/debugger

## Project structure
```text
stm32-nucleo-can/
├── platformio.ini
├── README.md
├── Info/
├── esp32/
│   ├── platformio.ini
│   ├── include/
│   └── src/
└── src/
    ├── main.cpp
    ├── can_bus.hpp
    ├── canbus.hpp
    ├── canbus.cpp
    └── can_frame.hpp
```

## Build
```bash
cd stm32-nucleo-can
pio run
```

## Run tests
The host-native Unity tests cover hardware-independent CAN frame validation:
```bash
cd stm32-nucleo-can
/home/x/.platformio/penv/bin/pio test -e native
```

## Upload
```bash
cd stm32-nucleo-can
pio run --target upload
```

## Monitor serial output
```bash
cd stm32-nucleo-can
pio device monitor
```

## ESP32-S3 telemetry bridge
The standalone ESP32-S3 PlatformIO project receives STM32 telemetry over UART and publishes it to MQTT. See [`esp32/README.md`](esp32/README.md) for wiring, local Wi-Fi/MQTT configuration, and build instructions.

## Notes
- HS-CAN uses `CAN1` on `PD1` (TX) and `PD0` (RX) at 500 kbit/s; MS-CAN uses `CAN2` on `PB13` (TX) and `PB12` (RX) at 125 kbit/s.
- CAN frames received on either controller are sent through `USART3` (`PD8` TX, `PD9` RX) at 115200 baud. The telemetry labels each frame as HS-CAN or MS-CAN.
- Connect the CAN transceiver to the bus using the proper terminators and wiring.
- This project is intentionally simple and meant as a starting point for real CAN applications.
