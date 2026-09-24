#ifndef __BOARD_CAN_TASK_H
#define __BOARD_CAN_TASK_H

#include "main.h"

//INCLUDE部分
#include "Check_Task.h"
#include "Can_Feedback.h"
#include "Gimbal_Task.h"

void Send_Rc(void);
void Send_Message(void);
void Send_Message_1(void);
void Board_Can_Init(void);
void Board_Can_Task(void);

//EXTERN部分
extern bool board_ready_flag;

#endif
