#include "can_bus.h"

#include <cstdio>
#include <cstring>

/* 1 = écoute seule (aucune trame émise, aucun ACK : sans danger pour la voiture) */
#define CAN_MONITOR_ONLY      1
/* 1 = la capture est active dès le démarrage, 0 = en pause jusqu'au premier appui */
#define CAN_START_RUNNING     1
/* Anti-rebond du bouton */
#define TOGGLE_DEBOUNCE_MS    300

/* Format des lignes envoyées au PC :
 *   F,<temps en s>,<id hex>,<ext 0/1>,<dlc>,<données hex sans espaces>
 *   # ...   (messages d'information, ignorés par le CSV)                     */

namespace {

FDCAN_HandleTypeDef *can = nullptr;

uint32_t canClk     = 0;
uint32_t canBitrate = 0;
uint32_t canPresc   = 0;
uint32_t canTq      = 0;

bool     running    = false;
uint32_t segment    = 0;     // numéro de la phase de capture (change à chaque reprise)
uint32_t segCount   = 0;     // trames imprimées dans la phase en cours
uint32_t lostCount  = 0;

uint32_t lastStatusTick = 0;
uint32_t lastToggleTick = 0;
volatile bool toggleRequested = false;

/* Horodatage matériel étendu : le compteur du FDCAN fait 16 bits (un tick = 1 temps-bit). */
int64_t tsExt  = 0;
int64_t tsBase = 0;

int64_t unwrapTs(uint16_t ts)
{
    /* Différence signée modulo 65536 : tolère les trames un peu plus anciennes que le compteur */
    const int16_t d = static_cast<int16_t>(ts - static_cast<uint16_t>(tsExt));
    const int64_t v = tsExt + d;
    if (d > 0) tsExt = v;
    return v;
}

void splitTime(int64_t v, uint32_t &sec, uint32_t &usec)
{
    int64_t ticks = v - tsBase;
    if (ticks < 0) ticks = 0;
    const uint64_t us = static_cast<uint64_t>(ticks) * 1000000ULL / canBitrate;
    sec  = static_cast<uint32_t>(us / 1000000ULL);
    usec = static_cast<uint32_t>(us % 1000000ULL);
}

bool computeTiming(uint32_t clk, uint32_t bitrate,
                   uint32_t &presc, uint32_t &tq, uint32_t &seg1, uint32_t &seg2)
{
    static const uint32_t candidates[] = {30, 25, 20, 16, 15, 10, 8};
    for (uint32_t t : candidates) {
        const uint32_t div = bitrate * t;
        if (div == 0 || (clk % div) != 0) continue;
        const uint32_t p = clk / div;
        if (p < 1 || p > 512) continue;
        const uint32_t sample = t * 875 / 1000;
        presc = p;
        tq    = t;
        seg1  = sample - 1;
        seg2  = t - sample;
        return true;
    }
    return false;
}

uint8_t dlcToBytes(uint32_t dl)
{
    if (dl & 0xFFFF0000u) dl >>= 16;     // selon la version de la HAL, le DLC est décalé de 16 bits
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

void uartWrite(UART_HandleTypeDef *huart, const char *s, int n)
{
    if (n > 0) HAL_UART_Transmit(huart, reinterpret_cast<uint8_t *>(const_cast<char *>(s)),
                                 static_cast<uint16_t>(n), 100);
}

/* Lit jusqu'à maxFrames trames de la FIFO. print = false : on lit et on jette (pause). */
void readAll(UART_HandleTypeDef *huart, bool print, uint32_t maxFrames)
{
    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];
    char line[64];

    for (uint32_t i = 0; i < maxFrames; ++i) {
        if (HAL_FDCAN_GetRxFifoFillLevel(can, FDCAN_RX_FIFO0) == 0) break;
        if (HAL_FDCAN_GetRxMessage(can, FDCAN_RX_FIFO0, &hdr, data) != HAL_OK) break;

        const int64_t v = unwrapTs(static_cast<uint16_t>(hdr.RxTimestamp));
        if (!print) continue;

        const uint8_t len = dlcToBytes(hdr.DataLength);
        uint32_t sec, usec;
        splitTime(v, sec, usec);

        char hex[17] = {0};
        for (uint8_t k = 0; k < len; ++k) std::snprintf(hex + 2 * k, 3, "%02X", data[k]);

        const int n = std::snprintf(line, sizeof(line), "F,%lu.%06lu,%lX,%u,%u,%s\r\n",
                                    static_cast<unsigned long>(sec),
                                    static_cast<unsigned long>(usec),
                                    static_cast<unsigned long>(hdr.Identifier),
                                    (hdr.IdType == FDCAN_EXTENDED_ID) ? 1u : 0u,
                                    static_cast<unsigned>(len), hex);
        uartWrite(huart, line, n);
        ++segCount;
    }
}

} // namespace

extern "C" {

void CanBus_Init(FDCAN_HandleTypeDef *hfdcan, uint32_t bitrate)
{
    can = hfdcan;
    canBitrate = bitrate;

    if (HAL_FDCAN_DeInit(can) != HAL_OK) Error_Handler();

    canClk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);

    uint32_t presc = 0, tq = 0, seg1 = 0, seg2 = 0;
    if (!computeTiming(canClk, bitrate, presc, tq, seg1, seg2)) Error_Handler();
    canPresc = presc;
    canTq    = tq;

    can->Init.FrameFormat        = FDCAN_FRAME_CLASSIC;
#if CAN_MONITOR_ONLY
    can->Init.Mode               = FDCAN_MODE_BUS_MONITORING;
#else
    can->Init.Mode               = FDCAN_MODE_NORMAL;
#endif
    can->Init.AutoRetransmission = ENABLE;
    can->Init.TransmitPause      = DISABLE;
    can->Init.ProtocolException  = ENABLE;

    can->Init.NominalPrescaler     = presc;
    can->Init.NominalTimeSeg1      = seg1;
    can->Init.NominalTimeSeg2      = seg2;
    can->Init.NominalSyncJumpWidth = (seg2 > 4) ? 4 : seg2;

    can->Init.MessageRAMOffset     = 0;
    can->Init.StdFiltersNbr        = 0;
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

    if (HAL_FDCAN_ConfigGlobalFilter(can,
            FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK) Error_Handler();

    /* Compteur d'horodatage interne : 1 tick = 1 temps-bit nominal */
    if (HAL_FDCAN_ConfigTimestampCounter(can, FDCAN_TIMESTAMP_PRESC_1) != HAL_OK) Error_Handler();
    if (HAL_FDCAN_EnableTimestampCounter(can, FDCAN_TIMESTAMP_INTERNAL) != HAL_OK) Error_Handler();

    if (HAL_FDCAN_Start(can) != HAL_OK) Error_Handler();

    tsExt  = (static_cast<int64_t>(1) << 20) + HAL_FDCAN_GetTimestampCounter(can);
    tsBase = tsExt;

    running   = (CAN_START_RUNNING != 0);
    segment   = running ? 1 : 0;
    segCount  = 0;
    lastStatusTick = HAL_GetTick();
}

void CanBus_PrintInfo(UART_HandleTypeDef *huart)
{
    char line[140];
    int n = std::snprintf(line, sizeof(line),
        "\r\n# CAN horloge=%lu Hz debit=%lu prescaler=%lu quanta=%lu mode=%s\r\n",
        static_cast<unsigned long>(canClk), static_cast<unsigned long>(canBitrate),
        static_cast<unsigned long>(canPresc), static_cast<unsigned long>(canTq),
        CAN_MONITOR_ONLY ? "ecoute seule" : "normal");
    uartWrite(huart, line, n);

    if (running) {
        n = std::snprintf(line, sizeof(line), "# START seg=%lu\r\n", static_cast<unsigned long>(segment));
    } else {
        n = std::snprintf(line, sizeof(line), "# PAUSE seg=%lu (appuie sur le bouton pour demarrer)\r\n",
                          static_cast<unsigned long>(segment));
    }
    uartWrite(huart, line, n);
}

void CanBus_ToggleRequest(void)
{
    toggleRequested = true;     // appelé depuis une interruption : on ne fait que lever un drapeau
}

void CanBus_Process(UART_HandleTypeDef *huart)
{
    if (can == nullptr) return;

    char line[120];
    const uint32_t now = HAL_GetTick();

    /* --- Bouton : arrêt (avec vidage de tout ce qui est déjà reçu) ou reprise --- */
    if (toggleRequested) {
        toggleRequested = false;
        if (now - lastToggleTick >= TOGGLE_DEBOUNCE_MS) {
            lastToggleTick = now;
            if (running) {
                readAll(huart, true, 4096);          // on écrit d'abord toutes les trames en attente
                running = false;
                const int n = std::snprintf(line, sizeof(line),
                    "# STOP seg=%lu frames=%lu lost=%lu\r\n",
                    static_cast<unsigned long>(segment),
                    static_cast<unsigned long>(segCount),
                    static_cast<unsigned long>(lostCount));
                uartWrite(huart, line, n);
            } else {
                running  = true;
                ++segment;
                segCount = 0;
                const int n = std::snprintf(line, sizeof(line), "# START seg=%lu\r\n",
                                            static_cast<unsigned long>(segment));
                uartWrite(huart, line, n);
            }
        }
    }

    /* --- Lecture : imprime si en marche, sinon lit et jette pour ne pas saturer la FIFO --- */
    readAll(huart, running, 64);

    /* Garde le compteur d'horodatage à jour même si le bus est silencieux */
    unwrapTs(HAL_FDCAN_GetTimestampCounter(can));

    if (__HAL_FDCAN_GET_FLAG(can, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST)) {
        ++lostCount;
        __HAL_FDCAN_CLEAR_FLAG(can, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
    }

    /* --- Ligne de statut chaque seconde (en marche) --- */
    if (running && (now - lastStatusTick >= 1000)) {
        lastStatusTick = now;

        FDCAN_ProtocolStatusTypeDef ps;
        FDCAN_ErrorCountersTypeDef  ec;
        HAL_FDCAN_GetProtocolStatus(can, &ps);
        HAL_FDCAN_GetErrorCounters(can, &ec);

        const int n = std::snprintf(line, sizeof(line),
            "# status seg=%lu frames=%lu lost=%lu act=0x%02lX lec=%lu rxerr=%lu\r\n",
            static_cast<unsigned long>(segment), static_cast<unsigned long>(segCount),
            static_cast<unsigned long>(lostCount), static_cast<unsigned long>(ps.Activity),
            static_cast<unsigned long>(ps.LastErrorCode), static_cast<unsigned long>(ec.RxErrorCnt));
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
    tx.DataLength          = bytesToDlc(len);
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_OFF;
    tx.FDFormat            = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    tx.MessageMarker       = 0;

    return (HAL_FDCAN_AddMessageToTxFifoQ(can, &tx, buf) == HAL_OK) ? 0 : -1;
}

} // extern "C"
