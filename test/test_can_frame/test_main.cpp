#include <unity.h>

#include "can_frame.hpp"

void test_accepts_valid_standard_frame()
{
    const uint8_t data[8] = {};

    TEST_ASSERT_TRUE(isValidStandardCanFrame(0x123u, data, sizeof(data)));
}

void test_rejects_null_data()
{
    TEST_ASSERT_FALSE(isValidStandardCanFrame(0x123u, nullptr, 0));
}

void test_rejects_payload_larger_than_can_limit()
{
    const uint8_t data[9] = {};

    TEST_ASSERT_FALSE(isValidStandardCanFrame(0x123u, data, sizeof(data)));
}

void test_rejects_extended_identifier()
{
    const uint8_t data[1] = {};

    TEST_ASSERT_FALSE(isValidStandardCanFrame(0x800u, data, sizeof(data)));
}

void setup() {}
void tearDown() {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_valid_standard_frame);
    RUN_TEST(test_rejects_null_data);
    RUN_TEST(test_rejects_payload_larger_than_can_limit);
    RUN_TEST(test_rejects_extended_identifier);
    return UNITY_END();
}
