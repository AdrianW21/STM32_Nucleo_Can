#pragma once

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure FDCAN (mémoire, débit, filtre global) et le démarre.
 * bitrate : 500000, 250000, 125000, 1000000...
 * Le prescaler et les segments sont calculés automatiquement à partir de
 * l'horloge FDCAN réelle, donc indépendants des réglages de CubeMX. */
void CanBus_Init(FDCAN_HandleTypeDef *hfdcan, uint32_t bitrate);

/* Affiche une ligne d'information de démarrage (horloge, prescaler, débit). */
void CanBus_PrintInfo(UART_HandleTypeDef *huart);

/* A appeler en boucle dans main() : lit les trames, les affiche,
 * et affiche une ligne [status] par seconde. */
void CanBus_Process(UART_HandleTypeDef *huart);

/* Envoie une trame CAN classique (nécessite CAN_MONITOR_ONLY = 0). Retourne 0 si OK. */
int CanBus_Send(uint32_t id, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif
