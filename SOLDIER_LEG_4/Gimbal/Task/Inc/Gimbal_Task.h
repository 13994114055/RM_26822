#ifndef __GIMBAL_TASK_H
#define __GIMBAL_TASK_H

#include "main.h"

//INCLUDE部分
#include "PID.h"
#include "VT03.h"
#include "Aim_Task.h"
#include "Ins_Task.h"
#include "Limit_Task.h"
#include "Check_Task.h"
#include "Some_Functions.h"
#include "Board_Can_Task.h"

//角度转弧度
#define Ang_PI 0.01745329f
//弧度转角度
#define PI_Ang 57.2957805f

//正负方向零点位置
#define ZERO_HEAD_YAW     1.62468398f
#define ZERO_BACK_YAW    -1.51959324f

//摩擦轮转速
#define Friction_Speed   5700

//拨盘转速
#define Dial_Speed		6000

//云台俯仰角限位
#define PITCH_UP_LIMIT_POSITION     30
#define PITCH_DOWN_LIMIT_POSITION  -21

//卡弹反拨的检测转矩电流值以及转速值
#define Jammed_Rpm              15
#define Jammed_Cur            13000

//控制器灵敏度以及死区设置
#define RC_PITCH_SENSITIVITY        0.0004
#define RC_YAW_SENSITIVITY          0.0006
#define RC_DEADBAND                 5

#define PC_PITCH_SENSITIVITY        0.0008
#define PC_YAW_SENSITIVITY          0.0008
#define PC_DEADBAND                 10


typedef enum
{
	Close,
	Open
}Shoot_Condition_t; //发射机构状态

typedef struct
{
	bool run_flag;         //启动标志
	
	float Yaw_Zero_Target; //零点目标位置
}Self_Rescue_t;

typedef struct 
{
	uint32_t dwt_l; //计算DT
	uint32_t dwt_r;
	
	float L_Last_Rpm; //记录上一次转速
	float R_Last_Rpm;
	
	float L_A_Rpm; //摩擦轮的转动加速度
	float R_A_Rpm;
}Calculate_Acc;

typedef struct 
{
	int16_t Rc_Pitch; //遥控器数据
	int16_t Rc_Yaw;
	int16_t Pc_Pitch;
	int16_t Pc_Yaw;
	
	float abs_yaw;   //绝对数据
	
	float yaw;       //陀螺仪数据
	float d_yaw;
	float pitch; 
	float d_pitch;
	
	float abs_yaw_ref; //绝对控制下的目标值
	
	float yaw_ref;  //相对控制下的目标值
	float pitch_ref; 
	
	float Yaw_Motor_Out;   //电机输出
	float Pitch_Motor_Out; 
}Gimbal_Status_t;

typedef struct 
{
	int16_t L_Rpm; //摩擦轮RPM
	int16_t R_Rpm;
	
	int16_t D_Rpm; //拨盘RPM
	int16_t D_Cur; //拨盘电流值
	float D_Pos; 	 //拨盘位置
	
	float Target_Rpm; 	//设定摩擦轮电机目标转速
	float D_Target_Rpm; //设定拨盘电机目标转速
	float D_Target_Pos; //设定拨盘电机目标位置
	
	float L_Motor_Out; //摩擦轮电机输出
	float R_Motor_Out;
	float D_Motor_Out;
	
	uint32_t dwt_dpos;
	
	bool Dial_Status;	//拨盘状态(0运行 1就绪)
	bool Dial_Flag;		//单发拨盘不动常态0 拨盘转动一次记为1 
	Calculate_Acc acc;
}Shoot_Status_t;

void Gimbal_Init(void);
void Gimbal_Task(void);
void Auto_Aim(Aim_Rx *aim,Gimbal_Status_t *gs);
void Gimbal_Target_Limit(Gimbal_Status_t *gs);
void Gimbal_Can_Data_Send(Shoot_Status_t *ss,Gimbal_Status_t *gs);
void Shoot_Control(Heat_Control_t *hc,Shoot_Status_t *ss,Shoot_Condition_t *sc);
void Variable_Information_Acquisition(INS_t *ins,Gimbal_Motor_t *gm,Shoot_Status_t *ss,Gimbal_Status_t *gs);
void Gimbal_Controllor(Shoot_Status_t *ss,Gimbal_Status_t *gs,Self_Rescue_t *self_re,Controlled_State_t *cs);
void Gimbal_Control(RC_Ctrl_t *rc_ctrl,PC_Ctrl_t *pc_ctrl,Shoot_Status_t *ss,Gimbal_Status_t *gs,Shoot_Condition_t *sc,Self_Rescue_t *self_re,Controlled_State_t *cs);

//EXTERN部分
extern bool gimbal_ready_flag;

extern Shoot_Status_t Shoot_Status;
extern Gimbal_Status_t Gimbal_Status;
extern Shoot_Condition_t Shoot_Condition; 
extern bool Aim_Permission; 
extern bool Fire_Permission; 
extern bool Single_Flag;
extern bool break_chassis_flag;

#endif
