#include "Ref_Task.h"

//INCLUDE部分
#include "Super_Cap.h"
#include "Chassis_Task.h"
#include "Can_Feedback.h"
#include "Remote_Control.h"
#include "Parameter.h"
//EXTERN部分
extern DMA_HandleTypeDef hdma_usart6_rx;
extern DMA_HandleTypeDef hdma_usart6_tx;
//全局变量定义部分
fifo_s_t Referee_FIFO; //裁判系统接收数据队列
uint8_t Referee_FIFO_Buffer[REFEREE_FIFO_BUF_LENGTH];

unpack_data_t Referee_Unpack_OBJ; //protocol解析包结构体

uint8_t Referee_Buffer[2][REFEREE_USART_RX_BUF_LENGHT]; //裁判系统串口双缓冲区

int Rest_UI_Flag;
bool ref_ready_flag; //当前线程初始化完成标志 

/*******************************************************************************************************
Ref任务初始化
********************************************************************************************************/
void Ref_Init(void)
{
	Referee_StructInit();
	fifo_s_init(&Referee_FIFO, Referee_FIFO_Buffer, REFEREE_FIFO_BUF_LENGTH);
	Referee_UARTInit(Referee_Buffer[0], Referee_Buffer[1], REFEREE_USART_RX_BUF_LENGHT);
	
	ref_ready_flag = 1;
	
	//等待
	while(!all_ready_flag) {osDelay(1);}
}

/*******************************************************************************************************
Ref任务
********************************************************************************************************/
void Ref_Task(void)
{
	static int UI_PushUp_Counter = 0;
	Referee_UnpackFifoData(&Referee_Unpack_OBJ, &Referee_FIFO);
	if(UI_PushUp_Counter<1000)
	{
		if(UI_PushUp_Counter%7 == 0)  Show_Fire();
		if(UI_PushUp_Counter%11 == 0) Show_Gyro();
		if(UI_PushUp_Counter%17 == 0) Show_Autoaim();
		if(UI_PushUp_Counter%19 == 0) Show_Shift();
		if(UI_PushUp_Counter%23 == 0) Crosshair_show();
		if(UI_PushUp_Counter%29 == 0) Status_static_show();
		if(UI_PushUp_Counter%31 == 0) Direction_static_show();
		if(UI_PushUp_Counter%37 == 0) Status_static_show1();
		if(UI_PushUp_Counter%41 == 0) Show_Fall();
		if(UI_PushUp_Counter%43 == 0) Show_Sinple();
		if(UI_PushUp_Counter%47 == 0) Show_YE();
		
		
	}
	else
	{
		if(UI_PushUp_Counter%5 == 0) Status_flash_show();
		if(UI_PushUp_Counter%7 == 0) Direction_flash_show();
		if(UI_PushUp_Counter%11 == 0) Status_flash_show1();
		if(UI_PushUp_Counter%13 == 0) YE_flash_show();
		
	}
	if(UI_PushUp_Counter>30000) UI_PushUp_Counter=1000;

	if(Rest_UI_Flag == 1) UI_PushUp_Counter = 0;
	UI_PushUp_Counter++;
}

void Show_Fire(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "001" , UI_Graph_Add, 2, UI_Color_Pink, 25,4,5,768,750,"Fire");
	UI_PushUp_String(&UI_String, Robot_ID_Current);
}

void Show_Gyro(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "002" , UI_Graph_Add, 2, UI_Color_Pink, 25,4,5,1106,750,"Gyro");
	UI_PushUp_String(&UI_String, Robot_ID_Current);
}

void Show_Autoaim(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "003" , UI_Graph_Add, 2, UI_Color_Pink, 25,6,5,225,600,"Single");
	UI_PushUp_String(&UI_String, Robot_ID_Current);
}

void Show_Shift(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "004" , UI_Graph_Add, 2, UI_Color_Pink, 25,4,5,65,600,"Auto");
	UI_PushUp_String(&UI_String, Robot_ID_Current);
}

void Show_Fall(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "005" , UI_Graph_Add, 2, UI_Color_Pink, 25,4,5,1510,750,"Fall");
	UI_PushUp_String(&UI_String, Robot_ID_Current);

}

void Show_YE(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "25" , UI_Graph_Add, 2, UI_Color_Pink, 40,2,5,955,800,"YE");
	UI_PushUp_String(&UI_String, Robot_ID_Current);
}	

void Show_Sinple(void)
{
	memset(UI_String.String.stringdata,' ',30);
	UI_Draw_String(&UI_String.String, "006" , UI_Graph_Add, 2, UI_Color_Purple, 40,6,5,1500,650,"Sinple");
	UI_PushUp_String(&UI_String, Robot_ID_Current);

}

void Crosshair_show(void)
{
	UI_Draw_Line(&UI_Graph5.Graphic[0], "10", UI_Graph_Add, 0, UI_Color_Pink,  5,  955, 495, 959, 495);                       //远准心	
	UI_Draw_Line(&UI_Graph5.Graphic[1], "21", UI_Graph_Add, 0, UI_Color_Pink,  5,  955, 450, 959, 450);                       //近准心	
	UI_Draw_Ellipse(&UI_Graph5.Graphic[2], "22", UI_Graph_Add, 0, UI_Color_Green,  1,  957, 495, 15, 10);
	UI_Draw_Ellipse(&UI_Graph5.Graphic[3], "23", UI_Graph_Add, 0, UI_Color_Green,  1,  957, 450, 15, 10);	
	UI_Draw_Ellipse(&UI_Graph5.Graphic[4], "24", UI_Graph_Add, 0, UI_Color_Green,  5,  957, 450, 0, 0);	
	UI_PushUp_Graphs(5, &UI_Graph5, Robot_ID_Current);
}

void Direction_static_show(void)
{
	UI_Draw_Arc		(&UI_Graph1.Graphic[0],"18",UI_Graph_Add,1,UI_Color_Orange, 330,30,5,960,540,100,100);								//灯条位置
	
	UI_PushUp_Graphs(1 ,&UI_Graph1, Robot_ID_Current);
}

void Status_static_show(void)
{ 
	UI_Draw_Float		 (&UI_Graph7.Graphic[0], 	"11", UI_Graph_Add, 0, UI_Color_Green,  30,    6,    3,	1500, 800, Goal_Setting.Target_L0);    		//腿长
	UI_Draw_Rectangle(&UI_Graph7.Graphic[1],  "12", UI_Graph_Add, 1, UI_Color_Green,   5, 1084,  760,	1217, 682); 		          //陀螺状态
	UI_Draw_Rectangle(&UI_Graph7.Graphic[2],  "13", UI_Graph_Add, 1, UI_Color_Green,   5,  755,  760,	 865, 682); 		          //开火状态
	UI_Draw_Rectangle(&UI_Graph7.Graphic[3],  "14", UI_Graph_Add, 1, UI_Color_Green,  30 , 750 , 800, 1170 ,800);							 //超电能量条
	UI_Draw_Float		 (&UI_Graph7.Graphic[4],  "15", UI_Graph_Add, 1, UI_Color_Green,  30,    6,    3,	 388, 780, 0); 					 //小狗模式
	UI_Draw_Rectangle(&UI_Graph7.Graphic[5],  "16", UI_Graph_Add, 1, UI_Color_Green,   7,   43,  610,  155, 565);  	          	//自瞄状态
	UI_Draw_Rectangle(&UI_Graph7.Graphic[6],  "17", UI_Graph_Add, 1, UI_Color_Green,   7,  200,  610,  530, 565);           	  //单发状态

	UI_PushUp_Graphs(7, &UI_Graph7, Robot_ID_Current);
}

void Status_static_show1(void)
{
	UI_Draw_Rectangle(&UI_Graph1.Graphic[0],  "20", UI_Graph_Add, 1, UI_Color_Green,   5, 1500,  710,	1620, 760); 		          //翻倒状态

	UI_PushUp_Graphs(1, &UI_Graph1, Robot_ID_Current);	
}

void Status_flash_show(void)
{
	static int v_cap = 0;
	
	if(Goal_Setting.Target_L0 == DOWN_LEG_LENGTH)
	UI_Draw_Float(&UI_Graph7.Graphic[0], "11", UI_Graph_Change, 0, UI_Color_Green, 	30, 6,   3, 1500, 800, 1);
	else if(Goal_Setting.Target_L0 == MID_LEG_LENGTH)
	UI_Draw_Float(&UI_Graph7.Graphic[0], "11", UI_Graph_Change, 0, UI_Color_Green, 	30, 6,   3, 1500, 800, 2);
	else if(Goal_Setting.Target_L0 == UP_LEG_LENGTH)
	UI_Draw_Float(&UI_Graph7.Graphic[0], "11", UI_Graph_Change, 0, UI_Color_Green, 	30, 6,   3, 1500, 800, 3);
	
	if(Flag.spinning_flag)		UI_Draw_Rectangle(&UI_Graph7.Graphic[1], "12", UI_Graph_Change, 1, UI_Color_Green, 8, 1084, 760, 1217, 715);
	else										  UI_Draw_Rectangle(&UI_Graph7.Graphic[1], "12", UI_Graph_Change, 1, UI_Color_Green, 0, 1084, 760, 1217, 715);
	
	if(Up_Cboard_Info.Friction_Status) 				UI_Draw_Rectangle(&UI_Graph7.Graphic[2], "13", UI_Graph_Change, 1, UI_Color_Green,  8, 755, 760, 865, 715);
	else																		  UI_Draw_Rectangle(&UI_Graph7.Graphic[2], "13", UI_Graph_Change, 1, UI_Color_Green,  0, 755, 760, 865, 715);

	v_cap = pm_od.v_out - 1500;
	
	if(v_cap<=0) v_cap = 0;
	
	if(v_cap>=100)
	{
		UI_Draw_Rectangle(&UI_Graph7.Graphic[3], "14", UI_Graph_Change, 1, UI_Color_Green, 30 , 750 , 800 , 1170-(int)(420*((550 - v_cap)/550.0f)) , 800);
	}
	else
	{
		UI_Draw_Rectangle(&UI_Graph7.Graphic[3], "14", UI_Graph_Change, 1, UI_Color_Yellow, 30 , 750 , 800 , 1170-(int)(420*((550 - v_cap)/550.0f)) , 800);
	}
	
	UI_Draw_Float(&UI_Graph7.Graphic[4], "15", UI_Graph_Change, 1, UI_Color_Green, 	30, 6,   3,  388, 780, Up_Cboard_Info.Break_Chassis_Flag);

	if(Up_Cboard_Info.Auto_Aim_Flag)		UI_Draw_Rectangle(&UI_Graph7.Graphic[5], "16", UI_Graph_Change, 1, UI_Color_Green, 8, 43, 610, 155, 565);
	else 									UI_Draw_Rectangle(&UI_Graph7.Graphic[5], "16", UI_Graph_Change, 0, UI_Color_Green, 0, 43, 610, 155, 565);
		
	if(Up_Cboard_Info.Single_Flag) 		UI_Draw_Rectangle(&UI_Graph7.Graphic[6], "17", UI_Graph_Change, 1, UI_Color_Green,  8,  200, 610, 430, 565);
	else 															UI_Draw_Rectangle(&UI_Graph7.Graphic[6], "17", UI_Graph_Change, 1, UI_Color_Green,  0,  200, 610, 430, 565);

	UI_PushUp_Graphs(7, &UI_Graph7, Robot_ID_Current);
}

void Status_flash_show1(void)
{
	if(Flag.fall_flag) UI_Draw_Rectangle(&UI_Graph1.Graphic[0],  "20", UI_Graph_Change, 1, UI_Color_Green,   5, 1500,  710,	1620, 760); 		       
	else 						   UI_Draw_Rectangle(&UI_Graph1.Graphic[0],  "20", UI_Graph_Change, 1, UI_Color_Green,   0, 1500,  710,	1620, 760); 
		
	UI_PushUp_Graphs(1, &UI_Graph1, Robot_ID_Current);	

}

void Direction_flash_show(void)
{
	static int flow_angle = 0;
	
	flow_angle = (int)(Find_Min_RADIAN(Body.abs_yaw,1.62468398f)*57.2957805f);
	if(flow_angle<0) flow_angle = (180+flow_angle)+180;
	flow_angle = 360 - flow_angle;
	UI_Draw_Arc(&UI_Graph1.Graphic[0],"18",UI_Graph_Change,1,UI_Color_Orange,Whole_Circle_ANGLE(flow_angle-30),Whole_Circle_ANGLE(flow_angle+30),5,960,540,100,100);


	UI_PushUp_Graphs(1,&UI_Graph1, Robot_ID_Current);

}

void YE_flash_show(void)
{
	memset(UI_String.String.stringdata,' ',30);
	if(Up_Cboard_Info.Auto_Aim_Flag)	UI_Draw_String(&UI_String.String, "25" , UI_Graph_Change, 2, UI_Color_Green, 40,2,5,955,800,"YE");
	else UI_Draw_String(&UI_String.String, "25" , UI_Graph_Change, 2, UI_Color_Green, 40,2,0,955,800,"YE");
	UI_PushUp_String(&UI_String, Robot_ID_Current);

}
/*******************************************************************************************************
USART6_IRQHandler初始化
********************************************************************************************************/
void USART6_IRQHandler_Init(void)
{
	static uint16_t this_time_rx_len = 0;
	
	if(huart6.Instance->SR & UART_FLAG_RXNE)
	{
		__HAL_UART_CLEAR_PEFLAG(&huart6);
	}
	else if(USART6->SR & UART_FLAG_IDLE)
	{
		__HAL_UART_CLEAR_PEFLAG(&huart6);

		if ((hdma_usart6_rx.Instance->CR & DMA_SxCR_CT) == RESET)
		{
			__HAL_DMA_DISABLE(&hdma_usart6_rx);
			this_time_rx_len = REFEREE_USART_RX_BUF_LENGHT - hdma_usart6_rx.Instance->NDTR;
			hdma_usart6_rx.Instance->NDTR = REFEREE_USART_RX_BUF_LENGHT;
			hdma_usart6_rx.Instance->CR |= DMA_SxCR_CT;
			__HAL_DMA_ENABLE(&hdma_usart6_rx);
			fifo_s_puts(&Referee_FIFO, (char*)Referee_Buffer[1], this_time_rx_len);
		}
		else
		{
			__HAL_DMA_DISABLE(&hdma_usart6_rx);
			this_time_rx_len = REFEREE_USART_RX_BUF_LENGHT - hdma_usart6_rx.Instance->NDTR;
			hdma_usart6_rx.Instance->NDTR = REFEREE_USART_RX_BUF_LENGHT;
			DMA1_Stream1->CR &= ~(DMA_SxCR_CT);
			__HAL_DMA_ENABLE(&hdma_usart6_rx);
			fifo_s_puts(&Referee_FIFO, (char*)Referee_Buffer[1], this_time_rx_len);
		}
	}
}
