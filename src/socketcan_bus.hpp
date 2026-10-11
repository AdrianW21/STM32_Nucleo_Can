#pragma once

#include "can_bus.hpp"

class SocketCanBus : public ICanBus {
public:
    explicit SocketCanBus(const char* interfaceName = "vcan0");
    ~SocketCanBus() override;

    void start() override;
    bool transmit(uint32_t id, const uint8_t* data, uint8_t length) override;
    bool receive(CanFrame& frame) override;
    void processRx() override;

private:
    const char* interfaceName_;
    int socket_ = -1;
};