#include "socketcan_bus.hpp"

#include "can_frame.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

SocketCanBus::SocketCanBus(const char* interfaceName)
    : interfaceName_(interfaceName)
{
}

SocketCanBus::~SocketCanBus()
{
    if (socket_ >= 0) {
        close(socket_);
    }
}

void SocketCanBus::start()
{
    if (socket_ >= 0) {
        return;
    }

    socket_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_ < 0) {
        return;
    }

    ifreq interfaceRequest{};
    std::strncpy(interfaceRequest.ifr_name, interfaceName_, IFNAMSIZ - 1);
    if (ioctl(socket_, SIOCGIFINDEX, &interfaceRequest) < 0) {
        close(socket_);
        socket_ = -1;
        return;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = interfaceRequest.ifr_ifindex;
    if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(socket_);
        socket_ = -1;
        return;
    }

    const int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
}

bool SocketCanBus::transmit(uint32_t id, const uint8_t* data, uint8_t length)
{
    if (socket_ < 0 || !isValidStandardCanFrame(id, data, length)) {
        return false;
    }

    can_frame frame{};
    frame.can_id = id;
    frame.can_dlc = length;
    std::memcpy(frame.data, data, length);
    return write(socket_, &frame, sizeof(frame)) == static_cast<ssize_t>(sizeof(frame));
}

bool SocketCanBus::receive(CanFrame& frame)
{
    if (socket_ < 0) {
        return false;
    }

    can_frame rawFrame{};
    const ssize_t bytesRead = read(socket_, &rawFrame, sizeof(rawFrame));
    if (bytesRead != static_cast<ssize_t>(sizeof(rawFrame))) {
        return false;
    }

    frame.id = rawFrame.can_id & CAN_SFF_MASK;
    frame.length = std::min<uint8_t>(rawFrame.can_dlc, 8u);
    frame.remote = (rawFrame.can_id & CAN_RTR_FLAG) != 0;
    frame.extended = (rawFrame.can_id & CAN_EFF_FLAG) != 0;
    std::memcpy(frame.data, rawFrame.data, frame.length);
    return true;
}

void SocketCanBus::processRx()
{
    CanFrame frame{};
    while (receive(frame)) {
        std::printf("CAN %s 0x%lX DLC %u", frame.extended ? "EXT" : "STD",
                    static_cast<unsigned long>(frame.id), frame.length);
        if (frame.remote) {
            std::printf(" RTR");
        } else {
            for (uint8_t index = 0; index < frame.length; ++index) {
                std::printf(" %02X", frame.data[index]);
            }
        }
        std::printf("\n");
    }
}