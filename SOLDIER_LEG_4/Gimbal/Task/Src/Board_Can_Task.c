#include "Board_Can_Task.h"

//全局变量定义部分
bool board_ready_flag; //当前线程初始化完成标志 

/*******************************************************************************************************
Board_Can任务初始化
********************************************************************************************************/
void Board_Can_Init(void)
{
	board_ready_flag = 1;
	
	while(!all_ready_flag) {osDelay(1);}
}

/*******************************************************************************************************
Board_Can任务
********************************************************************************************************/
void Board_Can_Task(void)
{
	Send_Rc();
	osDelay(1);
	Send_Message();
	Send_Message_1();
}

/*******************************************************************************************************
向下板发送遥控器信息
********************************************************************************************************/
void Send_Rc(void)
{
	uint8_t Data[8];
	
	Data[0] = Rc_Ctrl.rc.ch[1]>>8;
	Data[1] = Rc_Ctrl.rc.ch[1]&0x00FF;
	Data[2] = Rc_Ctrl.rc.ch[4]>>8;
	Data[3] = Rc_Ctrl.rc.ch[4]&0x00FF;
	Data[4] = Rc_Ctrl.key.v>>8;
	Data[5] = Rc_Ctrl.key.v&0x00FF;
	Data[6] = Rc_Ctrl.rc.s[0];	
	Data[7] = Rc_Ctrl.rc.s[1];
	
	Can_TxMessage(&hcan1,0x080,8,Data);
}

/*******************************************************************************************************
板间通讯
********************************************************************************************************/
void Send_Message(void)
{
	uint8_t Data[8];
	
	Data[0] = Link_Sit.err_num>>8;
	Data[1] = Link_Sit.err_num&0x00FF;
	Data[2] = Gimbal_Motor.Dji_6020[0].ecd>>8;
	Data[3] = Gimbal_Motor.Dji_6020[0].ecd&0x00FF;
	Data[4] = (Fire_Permission << 0) + (Single_Flag << 1); //自瞄标志位 单发标志
	Data[5] = (Shoot_Condition << 0) + (break_chassis_flag << 1);//开火状态 底盘脱离云台
	Data[6] = Rc_Ctrl.rc.ch[0]>>8;
	Data[7] = Rc_Ctrl.rc.ch[0]&0x00FF;

	Can_TxMessage(&hcan1,0x160,8,Data);
}

void Send_Message_1(void)
{
	uint8_t Data[8];	
	
	Data[0] = VT03.key>>8;
	Data[1] = VT03.key&0x00FF;
	Data[2] = VT03.mode_sw; 
	Data[3] = 0;
	Data[4] = 0;
	Data[5] = 0;
	Data[6] = 0;
	Data[7] = 0; 

	Can_TxMessage(&hcan1,0x120,8,Data);
}
