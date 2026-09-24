#ifndef __REF_TASK_H
#define __REF_TASK_H

//INCLUDE部分
#include "fifo.h"
#include "usart.h"
#include "string.h"
#include "stdbool.h"
#include "Referee.h"
#include "cmsis_os.h"
#include "protocol.h"
#include "Check_Task.h"

#define REFEREE_FIFO_BUF_LENGTH     1024
#define REFEREE_USART_RX_BUF_LENGHT 512

#define Max(a,b) ((a) > (b) ? (a) : (b))
#define Robot_ID_Current Robot_Status.robot_id

void Ref_Init(void);
void Ref_Task(void);
void USART6_IRQHandler_Init(void);

void Show_Fire(void);
void Show_Gyro(void);
void Show_Autoaim(void);
void Show_Shift(void);
void Show_Fall(void);
void Show_YE(void);
void Show_Sinple(void);
void Crosshair_show(void);
void Direction_static_show(void);
void Status_static_show(void);
void Status_static_show1(void);

void Status_flash_show(void);
void Status_flash_show1(void);
void Direction_flash_show(void);
void YE_flash_show(void);


//EXTERN部分
extern int Rest_UI_Flag;
extern bool ref_ready_flag;

extern fifo_s_t Referee_FIFO;
extern TaskHandle_t RefereeTask_Handle;
extern unpack_data_t Referee_Unpack_OBJ;
extern uint8_t Referee_FIFO_Buffer[REFEREE_FIFO_BUF_LENGTH];
extern uint8_t Referee_Buffer[2][REFEREE_USART_RX_BUF_LENGHT];

#endif
