#pragma once

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure FDCAN (mémoire, débit, filtre global, horodatage matériel) et le démarre. */
void CanBus_Init(FDCAN_HandleTypeDef *hfdcan, uint32_t bitrate);

/* Envoie les informations de démarrage (lignes commençant par '#'). */
void CanBus_PrintInfo(UART_HandleTypeDef *huart);

/* A appeler en boucle dans main() : lit les trames et les envoie au PC en CSV. */
void CanBus_Process(UART_HandleTypeDef *huart);

/* A appeler depuis le callback du bouton utilisateur : demande un arrêt/reprise. */
void CanBus_ToggleRequest(void);

/* Envoie une trame CAN classique (nécessite CAN_MONITOR_ONLY = 0). Retourne 0 si OK. */
int CanBus_Send(uint32_t id, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif
