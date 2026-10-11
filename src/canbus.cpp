#include "canbus.hpp"
#include "can_frame.hpp"

#include <cstdio>

CanBus::CanBus() = default;

void CanBus::initMonitorUart()
{
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef gpioInit{};
    gpioInit.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &gpioInit);

    huart3_.Instance = USART3;
    huart3_.Init.BaudRate = 115200;
    huart3_.Init.WordLength = UART_WORDLENGTH_8B;
    huart3_.Init.StopBits = UART_STOPBITS_1;
    huart3_.Init.Parity = UART_PARITY_NONE;
    huart3_.Init.Mode = UART_MODE_TX_RX;
    huart3_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3_.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3_);
}

void CanBus::printReceivedFrame(CanChannel channel, const CanFrame& frame)
{
    char line[96];
    const char* idType = frame.extended ? "EXT" : "STD";
    const char* channelName = channel == CanChannel::HighSpeed ? "HS" : "MS";
    int length = std::snprintf(line, sizeof(line), "%s-CAN %s 0x%lX DLC %lu",
                               channelName,
                               idType, static_cast<unsigned long>(frame.id),
                               static_cast<unsigned long>(frame.length));

    if (frame.remote) {
        length += std::snprintf(line + length, sizeof(line) - static_cast<size_t>(length), " RTR");
    } else {
        length += std::snprintf(line + length, sizeof(line) - static_cast<size_t>(length), " DATA");
        for (uint32_t index = 0; index < frame.length && index < 8u; ++index) {
            length += std::snprintf(line + length, sizeof(line) - static_cast<size_t>(length),
                                    " %02X", frame.data[index]);
        }
    }
    length += std::snprintf(line + length, sizeof(line) - static_cast<size_t>(length), "\r\n");
    HAL_UART_Transmit(&huart3_, reinterpret_cast<uint8_t*>(line), static_cast<uint16_t>(length), HAL_MAX_DELAY);
}

bool CanBus::initController(CAN_HandleTypeDef& handle, CAN_TypeDef* instance,
                            GPIO_TypeDef* port, uint16_t txPin, uint16_t rxPin,
                            uint32_t filterBank, uint32_t prescaler)
{
    GPIO_InitTypeDef gpioInit{};
    gpioInit.Pin = txPin | rxPin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(port, &gpioInit);

    handle.Instance = instance;
    handle.Init.Prescaler = prescaler;
    handle.Init.Mode = CAN_MODE_NORMAL;
    handle.Init.SyncJumpWidth = CAN_SJW_1TQ;
    handle.Init.TimeSeg1 = CAN_BS1_13TQ;
    handle.Init.TimeSeg2 = CAN_BS2_4TQ;
    handle.Init.TimeTriggeredMode = DISABLE;
    handle.Init.AutoBusOff = DISABLE;
    handle.Init.AutoWakeUp = DISABLE;
    handle.Init.AutoRetransmission = ENABLE;
    handle.Init.ReceiveFifoLocked = DISABLE;
    handle.Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(&handle) != HAL_OK) {
        return false;
    }

    CAN_FilterTypeDef filter{};
    filter.FilterBank = filterBank;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&handle, &filter) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_Start(&handle) != HAL_OK) {
        return false;
    }

    return true;
}

CAN_HandleTypeDef& CanBus::controller(CanChannel channel)
{
    return channel == CanChannel::HighSpeed ? hsCan_ : msCan_;
}

bool CanBus::isInitialized(CanChannel channel) const
{
    return channel == CanChannel::HighSpeed ? hsInitialized_ : msInitialized_;
}

void CanBus::init()
{
    initMonitorUart();
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_CAN2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    if (!hsInitialized_) {
        hsInitialized_ = initController(hsCan_, CAN1, GPIOD, GPIO_PIN_1, GPIO_PIN_0, 0, 5);
    }
    if (!msInitialized_) {
        msInitialized_ = initController(msCan_, CAN2, GPIOB, GPIO_PIN_13, GPIO_PIN_12, 14, 20);
    }
}

void CanBus::start()
{
    if (!hsInitialized_ || !msInitialized_) {
        init();
    }
}

bool CanBus::transmit(uint32_t id, const uint8_t* data, uint8_t length)
{
    return transmit(CanChannel::HighSpeed, id, data, length);
}

bool CanBus::transmit(CanChannel channel, uint32_t id, const uint8_t* data, uint8_t length)
{
    if (!isInitialized(channel) || !isValidStandardCanFrame(id, data, length)) {
        return false;
    }

    CAN_TxHeaderTypeDef txHeader{};
    txHeader.StdId = id;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = length;
    txHeader.TransmitGlobalTime = DISABLE;

    uint32_t mailbox = 0;
    if (HAL_CAN_AddTxMessage(&controller(channel), &txHeader,
                             const_cast<uint8_t*>(data), &mailbox) != HAL_OK) {
        return false;
    }

    return true;
}

void CanBus::processRx()
{
    processRx(CanChannel::HighSpeed);
}

void CanBus::processRx(CanChannel channel)
{
    CanFrame frame{};
    while (receive(channel, frame)) {
        printReceivedFrame(channel, frame);
    }
}

bool CanBus::receive(CanFrame& frame)
{
    return receive(CanChannel::HighSpeed, frame);
}

bool CanBus::receive(CanChannel channel, CanFrame& frame)
{
    if (!isInitialized(channel) ||
        HAL_CAN_GetRxFifoFillLevel(&controller(channel), CAN_RX_FIFO0) == 0u) {
        return false;
    }

    CAN_RxHeaderTypeDef rxHeader{};
    uint8_t rxData[8] = {0};
    if (HAL_CAN_GetRxMessage(&controller(channel), CAN_RX_FIFO0,
                             &rxHeader, rxData) != HAL_OK) {
        return false;
    }

    frame.id = rxHeader.IDE == CAN_ID_STD ? rxHeader.StdId : rxHeader.ExtId;
    frame.length = static_cast<uint8_t>(rxHeader.DLC);
    frame.remote = rxHeader.RTR == CAN_RTR_REMOTE;
    frame.extended = rxHeader.IDE == CAN_ID_EXT;
    for (uint8_t index = 0; index < frame.length && index < 8u; ++index) {
        frame.data[index] = rxData[index];
    }
    return true;
}
