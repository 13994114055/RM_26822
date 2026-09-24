#include "Gimbal_Task.h"

//全局变量定义部分
Self_Rescue_t Self_Rescue; //自救
Shoot_Status_t Shoot_Status;
Gimbal_Status_t Gimbal_Status; 
Shoot_Condition_t Shoot_Condition; 

//建立控制器结构体
PID_t Yaw_P_Pid; //云台
PID_t Yaw_S_Pid;
PID_t Pitch_P_Pid; 
PID_t Pitch_S_Pid; 

PID_t Auto_Yaw_P_Pid; //自瞄云台
PID_t Auto_Yaw_S_Pid;
PID_t Auto_Pitch_P_Pid; 
PID_t Auto_Pitch_S_Pid; 

PID_t Abs_Yaw_P_Pid;

PID_t L_Rpm_Pid; //发射机构
PID_t R_Rpm_Pid;
PID_t L_Torque_Pid;
PID_t R_Torque_Pid;

PID_t D_Rpm_Pid;
PID_t D_Pos_Pid;


//建立前馈控制结构体
Feedforward_t Dyaw_FD;		 //YAW轴
Feedforward_t Shoot_FD[2]; //摩擦轮(0为左 1为右 )

bool Aim_Permission = 0;  //自瞄许可
bool Fire_Permission = 0; //开火许可
bool Single_Flag;					//单发标志
bool Jammed_flag = 0;		  //卡弹标志位
bool break_chassis_flag = 0;  //底盘断电标志位

bool gimbal_ready_flag; //当前线程初始化完成标志 

/*******************************************************************************************************
Gimbal任务初始化
********************************************************************************************************/
void Gimbal_Init(void)
{
	//初始化PID
	PID_Init(&Pitch_P_Pid,    35,  	 15, 	0,    70,     0,     2,  0,0,0,0, 0,RADIAN,NONE); //云台
	PID_Init(&Pitch_S_Pid, 16384,  1000, 	0,  2000,     0,     0,  0,0,0,0, 0,NO_CIRCLE,NONE);          
	PID_Init(&Yaw_P_Pid,   	  35, 	 10,  0, 	  75,     0, 	   5,  0,0,0,0, 0,RADIAN,NONE);             
	PID_Init(&Yaw_S_Pid,    16384, 4000,  0,  2100,     0,     0,  0,0,0,0, 0,NO_CIRCLE,NONE);          

	PID_Init(&Auto_Pitch_P_Pid,  		  60,  	 15, 	0,   250,     0,     4,  0,0,0,0, 4,RADIAN,NONE); //云台   //300
	PID_Init(&Auto_Pitch_S_Pid, 	 16384,  1000, 	0,  4000,     0,     0,  0,0,0,0, 0,NO_CIRCLE,NONE);          
	PID_Init(&Auto_Yaw_P_Pid,  	  		60, 	 10,  0, 	 200,     0, 	   4,  0,0,0,0, 4,RADIAN,NONE);          //75   
	PID_Init(&Auto_Yaw_S_Pid,  		 16384,  4000,  0,  4000,     0,     0,  0,0,0,0, 0,NO_CIRCLE,NONE);          

	
	PID_Init(&Abs_Yaw_P_Pid , 10000, 5000, 0,  8000,     0,  900,  0,0,0,0, 0,RADIAN,NONE);
	
	PID_Init(&L_Rpm_Pid   ,  6000.0f,1000.0f,0,    8.0f,     0.0f,      0.0f,0,0,0,0, 0,NO_CIRCLE,NONE); //发射机构
	PID_Init(&R_Rpm_Pid   ,  6000.0f,1000.0f,0,    8.0f,     0.0f,      0.0f,0,0,0,0, 0,NO_CIRCLE,NONE);
	PID_Init(&L_Torque_Pid, 16000.0f,3000.0f,0,    2.0f,     0.0f,      0.0f,0,0,0,0,10,NO_CIRCLE,NONE);
	PID_Init(&R_Torque_Pid, 16000.0f,3000.0f,0,    2.0f,     0.0f,      0.0f,0,0,0,0,10,NO_CIRCLE,NONE);
	
	PID_Init(&D_Pos_Pid,     16000.0f,  9000.0f, 0, 8000.0f,     0.0f,    800.0f, 0,0,0,0, 0,RADIAN,NONE);
	PID_Init(&D_Rpm_Pid,     16000.0f,  3000.0f, 0,    8.0f,     0.0f,      0.0f,0,0,0,0,10,NO_CIRCLE,NONE);	
	
	
	//初始化前馈函数(c0=静态增益 c1=速度补偿 c2=加速度补偿)
	static float ffc_c_dyaw[3] = {  255, 0.00000000001, 0.000000000001}; //YAW轴
	
	static float ffc_c_L[3]    = {0.00012, 0.00000000004, 0.000000000001}; //摩擦轮
	static float ffc_c_R[3]    = {0.00012, 0.00000000004, 0.000000000001}; 
	
	Feedforward_Init(&Dyaw_FD    ,16384,ffc_c_dyaw,0.05,1,1); //YAW轴
	
	Feedforward_Init(&Shoot_FD[0], 0.30,ffc_c_L   ,0.05,1,1);	//摩擦轮
	Feedforward_Init(&Shoot_FD[1], 0.30,ffc_c_R   ,0.05,1,1); 
	
	//发射机构状态关闭
	Shoot_Condition = Close;
	
	//初始化完成
	gimbal_ready_flag  = 1;
	
	//等待
	while(!all_ready_flag) {osDelay(1);}
}

/*******************************************************************************************************
Gimbal任务
********************************************************************************************************/
void Gimbal_Task(void)
{
	Variable_Information_Acquisition(&INS,&Gimbal_Motor,&Shoot_Status,&Gimbal_Status);
	Gimbal_Control(&Rc_Ctrl,&Pc_Ctrl,&Shoot_Status,&Gimbal_Status,&Shoot_Condition,&Self_Rescue,&Controlled_State);
	Auto_Aim(&aim_rx,&Gimbal_Status);
	Gimbal_Target_Limit(&Gimbal_Status);
	Shoot_Control(&Heat_Control,&Shoot_Status,&Shoot_Condition);
	Gimbal_Controllor(&Shoot_Status,&Gimbal_Status,&Self_Rescue,&Controlled_State);
	Gimbal_Can_Data_Send(&Shoot_Status,&Gimbal_Status);
}

/*******************************************************************************************************
云台关键数据获取
********************************************************************************************************/
void Variable_Information_Acquisition(INS_t *ins,
																	    Gimbal_Motor_t *gm,
																			Shoot_Status_t *ss,
																			Gimbal_Status_t *gs)
{

	//获取摩擦轮RPM
	ss->L_Rpm = gm->Dji_3508[1].speed_rpm; 
	ss->R_Rpm = gm->Dji_3508[2].speed_rpm;
	
	//获取拨盘RPM
	ss->D_Rpm = gm->Dji_3508[0].speed_rpm;
	
	//获取拨盘电流值
	ss->D_Cur = gm->Dji_3508[0].given_current;
	
	//估算出来的拨盘位置
	ss->D_Pos = Half_Circle_RADIAN(ss->D_Pos + ((Limit_Min(ss->D_Rpm,20)/19.0f/2.5f)/60.0f)*2.0f*PI*DWT_GetDeltaT(&ss->dwt_dpos));
	
	//计算摩擦轮的转动加速度
	ss->acc.L_A_Rpm = ((ss->L_Rpm - ss->acc.L_Last_Rpm)*((2.0f*PI)/60.0f))/DWT_GetDeltaT(&ss->acc.dwt_l);
	ss->acc.R_A_Rpm = ((ss->R_Rpm - ss->acc.R_Last_Rpm)*((2.0f*PI)/60.0f))/DWT_GetDeltaT(&ss->acc.dwt_r);
	ss->acc.L_Last_Rpm = ss->L_Rpm;
	ss->acc.R_Last_Rpm = ss->R_Rpm;
	
	//计算获取云台电机绝对位置
	gs->abs_yaw = Half_Circle_RADIAN((gm->Dji_6020[0].ecd/8191.0f)*2*PI);
	
	//计算获取云台陀螺仪数据
	gs->yaw     = ins->Yaw;
	gs->d_yaw   = ins->Gyro[2];
	gs->pitch   = ins->Roll;
	gs->d_pitch = ins->Gyro[1];
}

/*******************************************************************************************************
云台控制各项参数初始化
********************************************************************************************************/
void Gimbal_Control_Init(Shoot_Status_t *ss,
										     Gimbal_Status_t *gs,
												 Shoot_Condition_t *sc)
{ 
	*sc = Close; //发射机构状态重置
	
	gs->yaw_ref   = gs->yaw; //云台目标重置
	gs->pitch_ref = gs->pitch;
	
	ss->D_Target_Pos = ss->D_Pos; //拨盘目标重置
	
	Aim_Permission  = 0; //许可重置
	Fire_Permission = 0; 
}

/*******************************************************************************************************
遥控器模式
********************************************************************************************************/
void Rc_Mode(RC_Ctrl_t *rc_ctrl,
						 Shoot_Condition_t *sc)
{	
	if     (switch_is_down(rc_ctrl->rc.s[1])){*sc = Close;} //关闭发射机构
	else if(switch_is_mid (rc_ctrl->rc.s[1])){*sc = Close;} //关闭发射机构
	else if(switch_is_up(rc_ctrl->rc.s[1])) 
	{
		*sc = Open; 						//开启摩擦轮  
		if(rc_ctrl->rc.ch[0] == 660) Fire_Permission = 1; //允许开火
		else Fire_Permission = 0;
	}
	
	if(rc_ctrl->rc.ch[1] == 660)	Aim_Permission = 1; 	//开启自瞄
	else 	Aim_Permission = 0;

	
}

/*******************************************************************************************************
键鼠模式初始化
********************************************************************************************************/
void Pc_Init(PC_Ctrl_t *pc_ctrl)
{
	pc_ctrl->Q = 0;
	pc_ctrl->q_t = 0;
}

/*******************************************************************************************************
键鼠模式
********************************************************************************************************/
void Pc_Mode(RC_Ctrl_t *rc_ctrl,
						 PC_Ctrl_t *pc_ctrl,
						 Shoot_Status_t *ss)
{
	/******左键检测,允许开火******/
	if(rc_ctrl->mouse.press_l) Fire_Permission = 1;
	else Fire_Permission = 0;
	
//	/******右键检测,开启自瞄******/
	if(rc_ctrl->mouse.press_r) Aim_Permission = 1;
	else Aim_Permission = 0;
	
	/******Q键检测,摩擦轮开启******/
	if((rc_ctrl->key.v&KEY_PRESSED_OFFSET_Q) && !pc_ctrl->Q)
	{
		pc_ctrl->Q = 1;
		pc_ctrl->q_t = HAL_GetTick();
	}
	else if(!(rc_ctrl->key.v&KEY_PRESSED_OFFSET_Q) && pc_ctrl->Q)
	{
		pc_ctrl->Q = 0;
		if(HAL_GetTick()-pc_ctrl->q_t < 500)
		{
			if(Shoot_Condition != Close) Shoot_Condition = Close;
			else Shoot_Condition = Open;
		}
	}
	
	/******B键检测,单发切换******/
	if((rc_ctrl->key.v&KEY_PRESSED_OFFSET_B) && !pc_ctrl->B)
	{
		pc_ctrl->B = 1;
		pc_ctrl->b_t = HAL_GetTick();
	}
	else if(!(rc_ctrl->key.v&KEY_PRESSED_OFFSET_B) && pc_ctrl->B)
	{
		pc_ctrl->B = 0;
		if(HAL_GetTick()-pc_ctrl->b_t < 500)
		{
			if(!Single_Flag) Single_Flag = 1;
			else Single_Flag = 0;
		}
	}
		
	/******C键检测,关闭底盘******/
	if((rc_ctrl->key.v&KEY_PRESSED_OFFSET_C) && !pc_ctrl->C)
	{
		pc_ctrl->C = 1;
		pc_ctrl->c_t = HAL_GetTick();
	}
	else if(!(rc_ctrl->key.v&KEY_PRESSED_OFFSET_C) && pc_ctrl->C)
	{
		pc_ctrl->C = 0;
		if(HAL_GetTick()-pc_ctrl->c_t < 500)
		{
			if(!break_chassis_flag) break_chassis_flag = 1;
			else break_chassis_flag = 0;
		}
	}
}
/*******************************************************************************************************
VT03的键鼠模式
********************************************************************************************************/
void Vt03_Pc_Mode(PC_Ctrl_t *pc_ctrl)
{
	/******左键检测,允许开火******/
	if(VT03.mouse_left) Fire_Permission = 1;
	else Fire_Permission = 0;
	
//	/******右键检测,开启自瞄******/
	if(VT03.mouse_right) Aim_Permission = 1;
	else Aim_Permission = 0;
	
	/******Q键检测,摩擦轮开启******/
	if((VT03.key&KEY_PRESSED_OFFSET_Q) && !pc_ctrl->Q)
	{
		pc_ctrl->Q = 1;
		pc_ctrl->q_t = HAL_GetTick();
	}
	else if(!(VT03.key&KEY_PRESSED_OFFSET_Q) && pc_ctrl->Q)
	{
		pc_ctrl->Q = 0;
		if(HAL_GetTick()-pc_ctrl->q_t < 500)
		{
			if(Shoot_Condition != Close) Shoot_Condition = Close;
			else Shoot_Condition = Open;
		}
	}

	/******B键检测,单发切换******/
	if((VT03.key&KEY_PRESSED_OFFSET_B) && !pc_ctrl->B)
	{
		pc_ctrl->B = 1;
		pc_ctrl->b_t = HAL_GetTick();
	}
	else if(!(VT03.key&KEY_PRESSED_OFFSET_B) && pc_ctrl->B)
	{
		pc_ctrl->B = 0;
		if(HAL_GetTick()-pc_ctrl->b_t < 500)
		{
			if(!Single_Flag) Single_Flag = 1;
			else Single_Flag = 0;
		}
	}
	
	/******C键检测,关闭底盘******/
	if((VT03.key&KEY_PRESSED_OFFSET_C) && !pc_ctrl->C)
	{
		pc_ctrl->C = 1;
		pc_ctrl->c_t = HAL_GetTick();
	}
	else if(!(VT03.key&KEY_PRESSED_OFFSET_C) && pc_ctrl->C)
	{
		pc_ctrl->C = 0;
		if(HAL_GetTick()-pc_ctrl->c_t < 500)
		{
			if(!break_chassis_flag) break_chassis_flag = 1;
			else break_chassis_flag = 0;
		}
	}
}

/*******************************************************************************************************
寻找合适的YAW轴复位零点
********************************************************************************************************/
void Chose_Zero_Target(Gimbal_Status_t *gs,
											 Self_Rescue_t *self_re)
{
	static float dis_to_head = 0; //到正方向零点位置最小角度值
	static float dis_to_back = 0; //到负方向零点位置最小角度值
	
	dis_to_head = fabs(Find_Min_RADIAN(gs->abs_yaw,ZERO_HEAD_YAW));
	dis_to_back = fabs(Find_Min_RADIAN(gs->abs_yaw,ZERO_BACK_YAW));
	
	if(dis_to_head <= dis_to_back) self_re->Yaw_Zero_Target = ZERO_HEAD_YAW;
	else                           self_re->Yaw_Zero_Target = ZERO_BACK_YAW;
	
	gs->abs_yaw_ref = self_re->Yaw_Zero_Target; //对绝对位置目标进行赋值
}

/*******************************************************************************************************
云台控制逻辑
********************************************************************************************************/
void Gimbal_Control(RC_Ctrl_t *rc_ctrl,
										PC_Ctrl_t *pc_ctrl,
								    Shoot_Status_t *ss,
										Gimbal_Status_t *gs,
										Shoot_Condition_t *sc,
										Self_Rescue_t *self_re,		
										Controlled_State_t *cs)
{
	if(Remote_Select == UNLINK) //未连接遥控器
	{
		
	}
	else if(Remote_Select == DT7) //连接DT7遥控器
	{
		gs->Pc_Pitch = Limit_Min(rc_ctrl->mouse.y,PC_DEADBAND);
		gs->Pc_Yaw   = Limit_Min(rc_ctrl->mouse.x,PC_DEADBAND);
	}
	else if(Remote_Select == VT_03) //连接VT03遥控器
	{
		gs->Pc_Pitch = Limit_Min(VT03.mouse_y,PC_DEADBAND);
		gs->Pc_Yaw   = Limit_Min(VT03.mouse_x,PC_DEADBAND);
	}
	gs->Rc_Pitch = Limit_Min(rc_ctrl->rc.ch[3],RC_DEADBAND);
	gs->Rc_Yaw   = Limit_Min(rc_ctrl->rc.ch[2],RC_DEADBAND);
	
	if(*cs!=ERO && *cs!=STOP)
	{
		if(Remote_Select == UNLINK) //未连接遥控器
		{
			
		}
		else if(Remote_Select == DT7) //连接DT7遥控器
		{
			if(Down_Cboard_Info.fall_flag) //倒地
			{
				if(((rc_ctrl->key.v&KEY_PRESSED_OFFSET_R) || rc_ctrl->rc.ch[0]==-660) && !self_re->run_flag)//触发自救
				{
					Chose_Zero_Target(gs,self_re);
					self_re->run_flag = 1;
				}
				Gimbal_Control_Init(ss,gs,sc);
			}
			else //正常
			{
				self_re->run_flag = 0; //重启复位
				
				if(*cs == RC) //遥控器模式
				{
					gs->yaw_ref    = Half_Circle_ANGLE(gs->yaw_ref - RC_YAW_SENSITIVITY*gs->Rc_Yaw); //云台
					gs->pitch_ref += RC_PITCH_SENSITIVITY * gs->Rc_Pitch; 
					
					Rc_Mode(rc_ctrl,sc);
				}
				else if(*cs == MOUSE) //键鼠模式
				{
					gs->yaw_ref    =  Half_Circle_ANGLE(gs->yaw_ref - PC_YAW_SENSITIVITY*gs->Pc_Yaw); //云台
					gs->pitch_ref +=  PC_PITCH_SENSITIVITY * gs->Pc_Pitch; 
					
					Pc_Mode(rc_ctrl,pc_ctrl,ss);
				}
			}
		}
		else if(Remote_Select == VT_03) //连接VT03遥控器
		{
			if(Down_Cboard_Info.fall_flag) //倒地
			{
				if((VT03.key&KEY_PRESSED_OFFSET_R) && !self_re->run_flag)//触发自救
				{
					Chose_Zero_Target(gs,self_re);
					self_re->run_flag = 1;
				}
				
				Pc_Init(pc_ctrl); //初始化
				Gimbal_Control_Init(ss,gs,sc);
			}
			else //正常
			{
				self_re->run_flag = 0; //重启复位
				
				if(*cs == MOUSE) //键鼠模式
				{
					gs->yaw_ref =  Half_Circle_ANGLE(gs->yaw_ref - PC_YAW_SENSITIVITY*gs->Pc_Yaw); //云台
					gs->pitch_ref +=  PC_PITCH_SENSITIVITY * gs->Pc_Pitch; 
					
					Vt03_Pc_Mode(pc_ctrl);
				}
			}
		}
	}
	else
	{
		break_chassis_flag = 0;
		
		self_re->run_flag = 0; //重启复位
		
		gs->abs_yaw_ref = gs->abs_yaw; //绝对位置目标值重置
		
		Gimbal_Control_Init(ss,gs,sc);
	}
}

/*******************************************************************************************************
自瞄控制逻辑
********************************************************************************************************/
void Auto_Aim(Aim_Rx *aim,
							Gimbal_Status_t *gs)
{
	if(Aim_Permission && aim->mode != 0)
	{
		gs->yaw_ref = aim->yaw*PI_Ang;
		gs->pitch_ref = -aim->pitch*PI_Ang;
	}
}

/*******************************************************************************************************
对目标值进行限制
********************************************************************************************************/
void Gimbal_Target_Limit(Gimbal_Status_t *gs)
{
	if		 (gs->pitch_ref > PITCH_UP_LIMIT_POSITION  ) gs->pitch_ref = PITCH_UP_LIMIT_POSITION;
	else if(gs->pitch_ref < PITCH_DOWN_LIMIT_POSITION) gs->pitch_ref = PITCH_DOWN_LIMIT_POSITION;
}

/*******************************************************************************************************
发射机构控制逻辑
********************************************************************************************************/
void Shoot_Control(Heat_Control_t *hc,
									 Shoot_Status_t *ss,
									 Shoot_Condition_t *sc)
{
	static int back_time = 0; //反拨时间
	static int redu_bullet_num = 1; //热量冗余弹丸设定

//	if(fabs(Find_Min_RADIAN(ss->D_Pos,ss->D_Target_Pos))<=0.1f) //判断拨盘是否就绪
//	{
//		ss->Dial_Status = 1;
//	}
//	else ss->Dial_Status = 0;

	if(*sc == Close) //关闭
	{
		ss->Dial_Status = 0;
		ss->Target_Rpm = 0;
		ss->D_Target_Rpm = 0;	
		ss->D_Target_Pos = ss->D_Pos;
		Jammed_flag = 0;
	}
	else if(*sc == Open) //单开摩擦轮
	{
		if(!Fire_Permission)
		ss->Dial_Flag = 0;
		
		ss->Target_Rpm = Friction_Speed;

		if(!Single_Flag)
		{
			ss->D_Target_Pos = ss->D_Pos;
			if(Aim_Permission)
			{
				if(abs(ss->D_Cur)>=Jammed_Cur && abs(ss->D_Rpm)<=Jammed_Rpm) //判断是否卡弹
					Jammed_flag = 1; 
				
				if(Jammed_flag) //卡弹
				{
					back_time++;
					ss->D_Target_Rpm = -300; 
					
					if(back_time>=160)
					{
						back_time = 0;
						Jammed_flag = 0;
					}
				}
				else //正常
				{
					if(Aim_Permission) //自瞄情况下判断开火
					{
						if(aim_rx.mode==2) //允许开火
						{
							if(hc->bullet_num>=10)
							{
								ss->D_Target_Rpm = Dial_Speed;
							}
							else if(hc->bullet_num<10)
							{
								ss->D_Target_Rpm = Dial_Speed*((float)(hc->bullet_num - redu_bullet_num)/9.0f);
							
								if(ss->D_Target_Rpm<=0) ss->D_Target_Rpm = 0;
							}
							else	ss->D_Target_Rpm = 0;
						}
						else	ss->D_Target_Rpm = 0;
					}
					else
					{
						if(hc->bullet_num>=10)
						{
							ss->D_Target_Rpm = Dial_Speed;
						}
						else if(hc->bullet_num<10)
						{
							ss->D_Target_Rpm = Dial_Speed*((float)(hc->bullet_num - redu_bullet_num)/9.0f);
							
							if(ss->D_Target_Rpm<=0) ss->D_Target_Rpm = 0;
						}
						else	ss->D_Target_Rpm = 0;
					}
				}
			}
			else ss->D_Target_Rpm = 0;					
		}
		else
		{
			if( Fire_Permission && !ss->Dial_Flag)//ss->Dial_Status &&
			{

				if(abs(ss->D_Cur)>=Jammed_Cur && abs(ss->D_Rpm)<=Jammed_Rpm) //判断是否卡弹
					Jammed_flag = 1; 
				
				if(Jammed_flag) //卡弹
				{
					back_time++;
					ss->D_Target_Pos = Half_Circle_RADIAN(ss->D_Target_Pos - PI/4.0f);
					
					if(back_time>=160)
					{
						back_time = 0;
						Jammed_flag = 0;
					}
				}
				else //正常
				{
					ss->D_Target_Pos = Half_Circle_RADIAN(ss->D_Target_Pos + PI/5.0f);
					ss->Dial_Flag = 1;
				}			
			}
			
		}		
	}
}

/*******************************************************************************************************
云台控制器
********************************************************************************************************/
void Gimbal_Controllor(Shoot_Status_t *ss,
											 Gimbal_Status_t *gs,
											 Self_Rescue_t *self_re,
											 Controlled_State_t *cs)
{
	//发射机构摩擦轮前馈计算
	Feedforward_Calculate(&Shoot_FD[0],ss->acc.L_A_Rpm);
	Feedforward_Calculate(&Shoot_FD[1],ss->acc.R_A_Rpm);
	//YAW前馈计算
	Feedforward_Calculate(&Dyaw_FD,Down_Cboard_Info.down_dyaw + gs->d_yaw);
	
	if(*cs!=ERO && *cs!=STOP)
	{
		if(Down_Cboard_Info.fall_flag) //倒地
		{
			gs->Yaw_Motor_Out   = 0; //云台
			gs->Pitch_Motor_Out = 0; 
			
			ss->L_Motor_Out = 0; //发射机构
			ss->R_Motor_Out = 0;
			ss->D_Motor_Out = 0;
			
			if(self_re->run_flag) //启动 云台复位
			{
				gs->Yaw_Motor_Out = PID_Calculate(&Abs_Yaw_P_Pid,gs->abs_yaw,gs->abs_yaw_ref);
			}
		}
		else //正常
		{

			if(Aim_Permission && aim_rx.mode != 0)
			{
				PID_Calculate(&Auto_Yaw_P_Pid  ,gs->yaw*Ang_PI  ,gs->yaw_ref*Ang_PI  ); //云台
				PID_Calculate(&Auto_Pitch_P_Pid,gs->pitch*Ang_PI,gs->pitch_ref*Ang_PI);  
				gs->Yaw_Motor_Out   = PID_Calculate(&Auto_Yaw_S_Pid  ,gs->d_yaw  ,Auto_Yaw_P_Pid.Output  ) - Dyaw_FD.Output;
				gs->Pitch_Motor_Out = PID_Calculate(&Auto_Pitch_S_Pid,gs->d_pitch,Auto_Pitch_P_Pid.Output);
			}
			else
			{
				PID_Calculate(&Yaw_P_Pid  ,gs->yaw*Ang_PI  ,gs->yaw_ref*Ang_PI  ); //云台
				PID_Calculate(&Pitch_P_Pid,gs->pitch*Ang_PI,gs->pitch_ref*Ang_PI);  
				gs->Yaw_Motor_Out   = PID_Calculate(&Yaw_S_Pid  ,gs->d_yaw  ,Yaw_P_Pid.Output  ) - Dyaw_FD.Output;
				gs->Pitch_Motor_Out = PID_Calculate(&Pitch_S_Pid,gs->d_pitch,Pitch_P_Pid.Output);
			}
			
			ss->L_Motor_Out = PID_Calculate(&L_Rpm_Pid,ss->L_Rpm, ss->Target_Rpm) //发射机构
											- (10.0f/3.0f)*(3591.0f/187.0f)*(16384.0f/20.0f)*Shoot_FD[0].Output; 
			ss->R_Motor_Out = PID_Calculate(&R_Rpm_Pid,ss->R_Rpm,-ss->Target_Rpm) 
											- (10.0f/3.0f)*(3591.0f/187.0f)*(16384.0f/20.0f)*Shoot_FD[1].Output;
			if(!Single_Flag)
			{
				ss->D_Motor_Out =	PID_Calculate(&D_Rpm_Pid,ss->D_Rpm, ss->D_Target_Rpm); //拨盘 				
			}
			else
			{
				ss->D_Motor_Out =	PID_Calculate(&D_Pos_Pid,ss->D_Pos, ss->D_Target_Pos); //拨盘 
			}
		}
	}
	else
	{
		gs->Pitch_Motor_Out = 0; //云台
		gs->Yaw_Motor_Out   = 0;
		
		ss->L_Motor_Out = 0; //发射机构
		ss->R_Motor_Out = 0;
		ss->D_Motor_Out = 0;
	}
}

/*******************************************************************************************************
向电机发送消息
********************************************************************************************************/
void Gimbal_Can_Data_Send(Shoot_Status_t *ss,
													Gimbal_Status_t *gs)
{
	Dji_Motor_Ctrl(&hcan2,0x1FE,Max_Output(gs->Yaw_Motor_Out,16384),0,Max_Output(gs->Pitch_Motor_Out,16384),0);
	Dji_Motor_Ctrl(&hcan2,0x200,Max_Output(ss->D_Motor_Out  ,16384),Max_Output( ss->L_Motor_Out  ,16384),Max_Output(ss->R_Motor_Out  ,16384),0);

}

