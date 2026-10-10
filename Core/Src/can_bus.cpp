#include "can_bus.h"

#include <cstdio>
#include <cstring>

/* 1 = écoute seule (aucune trame émise, aucun ACK : sans danger pour la voiture)
 * 0 = mode normal (émission possible) : à n'utiliser que sur un banc de test */
#define CAN_MONITOR_ONLY 1

namespace {

FDCAN_HandleTypeDef *can = nullptr;

uint32_t canClk     = 0;
uint32_t canBitrate = 0;
uint32_t canPresc   = 0;
uint32_t canTq      = 0;
uint32_t rxCount    = 0;
uint32_t lostCount  = 0;
uint32_t lastStatusTick = 0;

/* Calcule prescaler / segments pour un débit donné, point d'échantillonnage ~87 % */
bool computeTiming(uint32_t clk, uint32_t bitrate,
                   uint32_t &presc, uint32_t &tq, uint32_t &seg1, uint32_t &seg2)
{
    static const uint32_t candidates[] = {30, 25, 20, 16, 15, 10, 8};
    for (uint32_t t : candidates) {
        const uint32_t div = bitrate * t;
        if (div == 0 || (clk % div) != 0) continue;
        const uint32_t p = clk / div;
        if (p < 1 || p > 512) continue;
        const uint32_t sample = t * 875 / 1000;   // nombre de quanta avant le point d'échantillonnage
        presc = p;
        tq    = t;
        seg1  = sample - 1;
        seg2  = t - sample;
        return true;
    }
    return false;
}

void uartWrite(UART_HandleTypeDef *huart, const char *s, int n)
{
    if (n > 0) HAL_UART_Transmit(huart, reinterpret_cast<uint8_t *>(const_cast<char *>(s)),
                                 static_cast<uint16_t>(n), 100);
}

    uint8_t dlcToBytes(uint32_t dl)
{
    if (dl & 0xFFFF0000u) dl >>= 16;     // HAL qui stocke le DLC décalé de 16 bits
    return (dl > 8) ? 8 : static_cast<uint8_t>(dl);
}

    uint32_t bytesToDlc(uint8_t len)
{
    switch (len) {
        case 0:  return FDCAN_DLC_BYTES_0;
        case 1:  return FDCAN_DLC_BYTES_1;
        case 2:  return FDCAN_DLC_BYTES_2;
        case 3:  return FDCAN_DLC_BYTES_3;
        case 4:  return FDCAN_DLC_BYTES_4;
        case 5:  return FDCAN_DLC_BYTES_5;
        case 6:  return FDCAN_DLC_BYTES_6;
        case 7:  return FDCAN_DLC_BYTES_7;
        default: return FDCAN_DLC_BYTES_8;
    }
}

} // namespace

extern "C" {

void CanBus_Init(FDCAN_HandleTypeDef *hfdcan, uint32_t bitrate)
{
    can = hfdcan;
    canBitrate = bitrate;

    /* On repart d'un contrôleur à l'état initial pour appliquer nos paramètres */
    if (HAL_FDCAN_DeInit(can) != HAL_OK) Error_Handler();

    canClk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);

    uint32_t presc = 0, tq = 0, seg1 = 0, seg2 = 0;
    if (!computeTiming(canClk, bitrate, presc, tq, seg1, seg2)) {
        Error_Handler();   // horloge incompatible avec ce débit
    }
    canPresc = presc;
    canTq    = tq;

    /* --- Mode et format --- */
    can->Init.FrameFormat       = FDCAN_FRAME_CLASSIC;
#if CAN_MONITOR_ONLY
    can->Init.Mode              = FDCAN_MODE_BUS_MONITORING;
#else
    can->Init.Mode              = FDCAN_MODE_NORMAL;
#endif
    can->Init.AutoRetransmission = ENABLE;
    can->Init.TransmitPause      = DISABLE;
    can->Init.ProtocolException  = ENABLE;   // ignore les trames CAN FD au lieu de les compter en erreur

    /* --- Débit nominal --- */
    can->Init.NominalPrescaler     = presc;
    can->Init.NominalTimeSeg1      = seg1;
    can->Init.NominalTimeSeg2      = seg2;
    can->Init.NominalSyncJumpWidth = (seg2 > 4) ? 4 : seg2;

    /* --- Zones de la mémoire des messages (tout est fixé ici) --- */
    can->Init.MessageRAMOffset     = 0;
    can->Init.StdFiltersNbr        = 0;                     // pas de filtre : le filtre global accepte tout
    can->Init.ExtFiltersNbr        = 0;
    can->Init.RxFifo0ElmtsNbr      = 32;
    can->Init.RxFifo0ElmtSize      = FDCAN_DATA_BYTES_8;
    can->Init.RxFifo1ElmtsNbr      = 0;
    can->Init.RxFifo1ElmtSize      = FDCAN_DATA_BYTES_8;
    can->Init.RxBuffersNbr         = 0;
    can->Init.RxBufferSize         = FDCAN_DATA_BYTES_8;
    can->Init.TxEventsNbr          = 0;
    can->Init.TxBuffersNbr         = 0;
    can->Init.TxFifoQueueElmtsNbr  = 8;
    can->Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;
    can->Init.TxElmtSize           = FDCAN_DATA_BYTES_8;

    if (HAL_FDCAN_Init(can) != HAL_OK) Error_Handler();

    /* Toutes les trames (ID standard et étendus) vont dans la FIFO 0 */
    if (HAL_FDCAN_ConfigGlobalFilter(can,
            FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_FILTER_REMOTE,
            FDCAN_FILTER_REMOTE) != HAL_OK) Error_Handler();

    if (HAL_FDCAN_Start(can) != HAL_OK) Error_Handler();

    lastStatusTick = HAL_GetTick();
}

void CanBus_PrintInfo(UART_HandleTypeDef *huart)
{
    char line[120];
    int n = std::snprintf(line, sizeof(line),
        "\r\n[CAN] horloge=%lu Hz, debit=%lu bit/s, prescaler=%lu, quanta/bit=%lu, mode=%s\r\n",
        static_cast<unsigned long>(canClk),
        static_cast<unsigned long>(canBitrate),
        static_cast<unsigned long>(canPresc),
        static_cast<unsigned long>(canTq),
        CAN_MONITOR_ONLY ? "ecoute seule" : "normal");
    uartWrite(huart, line, n);
}

void CanBus_Process(UART_HandleTypeDef *huart)
{
    if (can == nullptr) return;

    /* --- Lecture des trames (scrutation, sans interruption) --- */
    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];
    char line[80];

    for (int guard = 0; guard < 64; ++guard) {
        if (HAL_FDCAN_GetRxFifoFillLevel(can, FDCAN_RX_FIFO0) == 0) break;
        if (HAL_FDCAN_GetRxMessage(can, FDCAN_RX_FIFO0, &hdr, data) != HAL_OK) break;

        ++rxCount;

        uint8_t len = dlcToBytes(hdr.DataLength);

        int n = std::snprintf(line, sizeof(line), "%s 0x%03lX [%u]:",
                              (hdr.IdType == FDCAN_EXTENDED_ID) ? "EXT" : "STD",
                              static_cast<unsigned long>(hdr.Identifier),
                              static_cast<unsigned>(len));
        for (uint8_t i = 0; i < len; ++i) {
            n += std::snprintf(line + n, sizeof(line) - n, " %02X", data[i]);
        }
        n += std::snprintf(line + n, sizeof(line) - n, "\r\n");
        uartWrite(huart, line, n);
    }

    /* --- Trames perdues (FIFO pleine) --- */
    if (__HAL_FDCAN_GET_FLAG(can, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST)) {
        ++lostCount;
        __HAL_FDCAN_CLEAR_FLAG(can, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
    }

    /* --- Ligne de statut une fois par seconde --- */
    if (HAL_GetTick() - lastStatusTick >= 1000) {
        lastStatusTick = HAL_GetTick();

        FDCAN_ProtocolStatusTypeDef ps;
        FDCAN_ErrorCountersTypeDef  ec;
        HAL_FDCAN_GetProtocolStatus(can, &ps);
        HAL_FDCAN_GetErrorCounters(can, &ec);

        /* Activity : 0x00 pas synchronisé, 0x08 bus au repos, 0x10 réception, 0x18 émission */
        int n = std::snprintf(line, sizeof(line),
            "[status] recues=%lu perdues=%lu activity=0x%02lX lec=%lu rxerr=%lu busoff=%lu\r\n",
            static_cast<unsigned long>(rxCount),
            static_cast<unsigned long>(lostCount),
            static_cast<unsigned long>(ps.Activity),
            static_cast<unsigned long>(ps.LastErrorCode),
            static_cast<unsigned long>(ec.RxErrorCnt),
            static_cast<unsigned long>(ps.BusOff));
        uartWrite(huart, line, n);
    }
}

int CanBus_Send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (can == nullptr) return -1;
    if (len > 8) len = 8;

    uint8_t buf[8] = {0};
    std::memcpy(buf, data, len);

    FDCAN_TxHeaderTypeDef tx = {};
    tx.Identifier          = id;
    tx.IdType              = FDCAN_STANDARD_ID;
    tx.TxFrameType         = FDCAN_DATA_FRAME;
    tx.DataLength          = bytesToDlc(len);   // équivalent FDCAN_DLC_BYTES_x
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_OFF;
    tx.FDFormat            = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    tx.MessageMarker       = 0;

    return (HAL_FDCAN_AddMessageToTxFifoQ(can, &tx, buf) == HAL_OK) ? 0 : -1;
}

} // extern "C"
