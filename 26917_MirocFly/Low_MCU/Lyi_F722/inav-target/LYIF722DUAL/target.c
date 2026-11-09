/*
 * INAV target: LYI F722 DUAL — 定时器/输出映射
 * MOTOR1-4 = PB04/PB05/PB06/PB07, MOTOR5-8 = PC08/PC09/PB00/PB01, LED_STRIP = PA08
 * 依据板上 BF `resource` 输出（2026-11-08）
 */
#include <stdbool.h>
#include <platform.h>
#include "drivers/io.h"
#include "drivers/bus.h"
#include "drivers/timer.h"
#include "drivers/sensor.h"
#include "drivers/pwm_mapping.h"

BUSDEV_REGISTER_SPI_TAG(busdev_icm42688,   DEVHW_ICM42605, GYRO_SPI_BUS, GYRO1_CS_PIN, NONE, 0, DEVFLAGS_NONE, IMU_ICM42605_ALIGN);

timerHardware_t timerHardware[] = {
    DEF_TIM(TIM3, CH1, PB4, TIM_USE_OUTPUT_AUTO, 0, 0), // M1
    DEF_TIM(TIM3, CH2, PB5, TIM_USE_OUTPUT_AUTO, 0, 0), // M2
    DEF_TIM(TIM4, CH1, PB6, TIM_USE_OUTPUT_AUTO, 0, 0), // M3
    DEF_TIM(TIM4, CH2, PB7, TIM_USE_OUTPUT_AUTO, 0, 0), // M4
    DEF_TIM(TIM8, CH3, PC8, TIM_USE_OUTPUT_AUTO, 0, 0), // M5
    DEF_TIM(TIM8, CH4, PC9, TIM_USE_OUTPUT_AUTO, 0, 0), // M6
    DEF_TIM(TIM3, CH3, PB0, TIM_USE_OUTPUT_AUTO, 0, 0), // M7
    DEF_TIM(TIM3, CH4, PB1, TIM_USE_OUTPUT_AUTO, 0, 0), // M8
    DEF_TIM(TIM1, CH1, PA8, TIM_USE_LED,         0, 0), // LED_STRIP
};

const int timerHardwareCount = sizeof(timerHardware) / sizeof(timerHardware[0]);
