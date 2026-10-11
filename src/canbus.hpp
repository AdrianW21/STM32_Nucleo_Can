#pragma once

#include "can_bus.hpp"
#include "stm32f4xx_hal.h"

enum class CanChannel : uint8_t {
    HighSpeed,
    MediumSpeed
};

class CanBus : public ICanBus {
public:
    CanBus();
    void init();
    void start() override;
    bool transmit(uint32_t id, const uint8_t* data, uint8_t length) override;
    bool transmit(CanChannel channel, uint32_t id, const uint8_t* data, uint8_t length);
    bool receive(CanFrame& frame) override;
    bool receive(CanChannel channel, CanFrame& frame);
    void processRx() override;
    void processRx(CanChannel channel);

private:
    CAN_HandleTypeDef hsCan_{};
    CAN_HandleTypeDef msCan_{};
    UART_HandleTypeDef huart3_{};
    bool hsInitialized_ = false;
    bool msInitialized_ = false;

    void initMonitorUart();
    bool initController(CAN_HandleTypeDef& handle, CAN_TypeDef* instance,
                        GPIO_TypeDef* port, uint16_t txPin, uint16_t rxPin,
                        uint32_t filterBank, uint32_t prescaler);
    CAN_HandleTypeDef& controller(CanChannel channel);
    bool isInitialized(CanChannel channel) const;
    void printReceivedFrame(CanChannel channel, const CanFrame& frame);
};
