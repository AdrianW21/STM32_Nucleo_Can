#pragma once

#include "can_frame.hpp"

class ICanBus {
public:
    virtual ~ICanBus() = default;

    virtual void start() = 0;
    virtual bool transmit(uint32_t id, const uint8_t* data, uint8_t length) = 0;
    virtual bool receive(CanFrame& frame) = 0;
    virtual void processRx() = 0;
};