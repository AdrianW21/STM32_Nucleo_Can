#include <unity.h>

#include "socketcan_bus.hpp"

void test_vcan_loopback()
{
    SocketCanBus sender;
    SocketCanBus receiver;
    sender.start();
    receiver.start();

    const uint8_t expectedData[] = {0x10, 0x20, 0x30, 0x40};
    TEST_ASSERT_TRUE(sender.transmit(0x321u, expectedData, sizeof(expectedData)));

    CanFrame frame{};
    bool received = false;
    for (int attempt = 0; attempt < 1000 && !received; ++attempt) {
        received = receiver.receive(frame);
    }

    TEST_ASSERT_TRUE(received);
    TEST_ASSERT_EQUAL_UINT32(0x321u, frame.id);
    TEST_ASSERT_EQUAL_UINT8(sizeof(expectedData), frame.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedData, frame.data, sizeof(expectedData));
}

void setup() {}
void tearDown() {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_vcan_loopback);
    return UNITY_END();
}