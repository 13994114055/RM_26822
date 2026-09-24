/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.c
  * Description        : Code for freertos applications
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* task handler */
TaskHandle_t pid_task_handle;
TaskHandle_t imu_task_handle;
TaskHandle_t i2c_task_handle;
TaskHandle_t hitdetect_task_handle;
TaskHandle_t vision_task_handle;
TaskHandle_t flow_task_handle;
TaskHandle_t rc_task_handle;
TaskHandle_t count30s_task_handle;
TaskHandle_t sm_task_handle;

/* Idle task control block and stack */
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

static StaticTask_t idle_task_tcb;
static StaticTask_t timer_task_tcb;

/* External Idle and Timer task static memory allocation functions */
extern void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );
extern void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize );

/*
  vApplicationGetIdleTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &idle_task_tcb;
  *ppxIdleTaskStackBuffer = &idle_task_stack[0];
  *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}
/*
  vApplicationGetTimerTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize )
{
  *ppxTimerTaskTCBBuffer = &timer_task_tcb;
  *ppxTimerTaskStackBuffer = &timer_task_stack[0];
  *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}

/* add user code begin 1 */

/* add user code end 1 */

/**
  * @brief  initializes all task.
  * @param  none
  * @retval none
  */
void freertos_task_create(void)
{
  /* create pid_task task */
  xTaskCreate(pid_task_func,
              "pid_task",
              384,
              NULL,
              5,
              &pid_task_handle);

  /* create imu_task task */
  xTaskCreate(imu_task_func,
              "imu_task",
              384,
              NULL,
              4,
              &imu_task_handle);

  /* create i2c_task task */
  xTaskCreate(i2c_task_func,
              "i2c_task",
              256,
              NULL,
              2,
              &i2c_task_handle);

  /* create hitdetect_task task */
  xTaskCreate(hitdetect_task_func,
              "hitdetect_task",
              128,
              NULL,
              4,
              &hitdetect_task_handle);

  /* create vision_task task */
  xTaskCreate(vision_func,
              "vision_task",
              256,
              NULL,
              3,
              &vision_task_handle);

  /* create flow_task task */
  xTaskCreate(flow_task_func,
              "flow_task",
              256,
              NULL,
              3,
              &flow_task_handle);

  /* create rc_task task */
  xTaskCreate(rc_task_func,
              "rc_task",
              256,
              NULL,
              3,
              &rc_task_handle);

  /* create count30s_task task */
  xTaskCreate(count30s_task_func,
              "count30s_task",
              128,
              NULL,
              1,
              &count30s_task_handle);

  /* create sm_task task */
  xTaskCreate(sm_task_func,
              "sm_task",
              512,
              NULL,
              2,
              &sm_task_handle);
}

/**
  * @brief  freertos init and begin run.
  * @param  none
  * @retval none
  */
void wk_freertos_init(void)
{
  /* add user code begin freertos_init 0 */

  /* add user code end freertos_init 0 */

  /* enter critical */
  taskENTER_CRITICAL();

  freertos_task_create();
	
  /* add user code begin freertos_init 1 */

  /* add user code end freertos_init 1 */

  /* exit critical */
  taskEXIT_CRITICAL();

  /* start scheduler */
  vTaskStartScheduler();
}

/**
  * @brief pid_task function.
  * @param  none
  * @retval none
  */
void pid_task_func(void *pvParameters)
{
  /* add user code begin pid_task_func 0 */

  /* add user code end pid_task_func 0 */

  /* add user code begin pid_task_func 2 */

  /* add user code end pid_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin pid_task_func 1 */

    vTaskDelay(1);

  /* add user code end pid_task_func 1 */
  }
}


/**
  * @brief imu_task function.
  * @param  none
  * @retval none
  */
void imu_task_func(void *pvParameters)
{
  /* add user code begin imu_task_func 0 */

  /* add user code end imu_task_func 0 */

  /* add user code begin imu_task_func 2 */

  /* add user code end imu_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin imu_task_func 1 */

    vTaskDelay(1);

  /* add user code end imu_task_func 1 */
  }
}


/**
  * @brief i2c_task function.
  * @param  none
  * @retval none
  */
void i2c_task_func(void *pvParameters)
{
  /* add user code begin i2c_task_func 0 */

  /* add user code end i2c_task_func 0 */

  /* add user code begin i2c_task_func 2 */

  /* add user code end i2c_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin i2c_task_func 1 */

    vTaskDelay(1);

  /* add user code end i2c_task_func 1 */
  }
}


/**
  * @brief hitdetect_task function.
  * @param  none
  * @retval none
  */
void hitdetect_task_func(void *pvParameters)
{
  /* add user code begin hitdetect_task_func 0 */

  /* add user code end hitdetect_task_func 0 */

  /* add user code begin hitdetect_task_func 2 */

  /* add user code end hitdetect_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin hitdetect_task_func 1 */

    vTaskDelay(1);

  /* add user code end hitdetect_task_func 1 */
  }
}


/**
  * @brief vision_task function.
  * @param  none
  * @retval none
  */
void vision_func(void *pvParameters) {
    /* add user code begin vision_func 0 */
    uint8_t tx_data[] = "UART_TEST_12345";
    uint8_t rx_data[32];
    uint8_t i;
    uint8_t pass = 0;

    /* 确保 UART1 收发器使能 */
    usart_transmitter_enable(USART1, TRUE);
    usart_receiver_enable(USART1, TRUE);
    usart_enable(USART1, TRUE);

    /* ---------- UART1 回环测试 ---------- */

    /* 1. 清空接收缓冲 */
    while (usart_flag_get(USART1, USART_RDBF_FLAG) != RESET) {
        usart_data_receive(USART1);
    }

    /* 2. 发送数据 */
    for (i = 0; i < 15; i++) {
        while (usart_flag_get(USART1, USART_TDBE_FLAG) == RESET);
        usart_data_transmit(USART1, tx_data[i]);
    }
    while (usart_flag_get(USART1, USART_TDC_FLAG) == RESET);

    /* 3. 等待数据回环 */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 读取接收数据 */
    i = 0;
    while (usart_flag_get(USART1, USART_RDBF_FLAG) != RESET && i < 32) {
        rx_data[i++] = (uint8_t)usart_data_receive(USART1);
    }

    /* 5. 比较 */
    if (i == 15 && memcmp(tx_data, rx_data, 15) == 0) {
        pass = 1;
    }

    /* ---------- 根据测试结果决定是否启动 PWM ---------- */

    if (pass) {
        /* UART1 回环通过，启动 TIM2 CH1 PWM */

        /* 设置 CH1 占空比：30% */
        /* ARR = 14399，30% → CCR = 4320 */
        tmr_channel_value_set(TMR2, TMR_SELECT_CHANNEL_1, 4320);

        /* 使能 TIM2 计数器和 CH1 输出 */
        tmr_counter_enable(TMR2, TRUE);
        tmr_output_channel_enable(TMR2, TMR_SELECT_CHANNEL_1, TRUE);

        /* 电机应该开始转动 */
    } else {
        /* UART1 测试失败，不启动 PWM */
        /* 可以点亮一个 LED 指示失败 */
    }
    while (1)
    {
        /* 6. 延时，避免任务占用过多 CPU 资源 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
  /* add user code end vision_func 0 */

  /* add user code begin vision_func 2 */

  /* add user code end vision_func 2 */

  /* Infinite loop */
  //while(1)
  {
  /* add user code begin vision_func 1 */

    vTaskDelay(1);

  /* add user code end vision_func 1 */
  }
}


/**
  * @brief flow_task function.
  * @param  none
  * @retval none
  */
void flow_task_func(void *pvParameters)
{
  /* add user code begin flow_task_func 0 */

  /* add user code end flow_task_func 0 */

  /* add user code begin flow_task_func 2 */

  /* add user code end flow_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin flow_task_func 1 */

    vTaskDelay(1);

  /* add user code end flow_task_func 1 */
  }
}


/**
  * @brief rc_task function.
  * @param  none
  * @retval none
  */
void rc_task_func(void *pvParameters)
{
  /* add user code begin rc_task_func 0 */

  /* add user code end rc_task_func 0 */

  /* add user code begin rc_task_func 2 */

  /* add user code end rc_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin rc_task_func 1 */

    vTaskDelay(1);

  /* add user code end rc_task_func 1 */
  }
}


/**
  * @brief count30s_task function.
  * @param  none
  * @retval none
  */
void count30s_task_func(void *pvParameters)
{
  /* add user code begin count30s_task_func 0 */

  /* add user code end count30s_task_func 0 */

  /* add user code begin count30s_task_func 2 */

  /* add user code end count30s_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin count30s_task_func 1 */

    vTaskDelay(1);

  /* add user code end count30s_task_func 1 */
  }
}


/**
  * @brief sm_task function.
  * @param  none
  * @retval none
  */
void sm_task_func(void *pvParameters)
{
  /* add user code begin sm_task_func 0 */

  /* add user code end sm_task_func 0 */

  /* add user code begin sm_task_func 2 */

  /* add user code end sm_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin sm_task_func 1 */

    vTaskDelay(1);

  /* add user code end sm_task_func 1 */
  }
}


/* add user code begin 2 */

/* add user code end 2 */

