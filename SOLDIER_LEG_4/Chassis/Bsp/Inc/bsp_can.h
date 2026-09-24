#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include "main.h"

//INCLUDE²¿·Ö
#include "can.h"

void CAN_Init(void);

void Can_Err_Read(CAN_HandleTypeDef *hcan);
void Can_Power_Read(CAN_HandleTypeDef *hcan);
uint8_t Can_TxMessage(CAN_HandleTypeDef *hcan,uint32_t id,uint8_t len,uint8_t *data);
uint8_t Can_Extld_TxMessage(CAN_HandleTypeDef *hcan,uint32_t id,uint8_t len,uint8_t *data);

#endif

