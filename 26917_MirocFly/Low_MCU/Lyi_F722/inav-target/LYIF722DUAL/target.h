/*
 * INAV target: LYI F722 DUAL (STM32F722, 512KB)
 * 引脚来源：板上 Betaflight 4.5.1 `resource`（2026-11-08 读取）
 * 结构参照：src/main/target/ALEXF722V3（同族硬件，双陀螺）
 */
#pragma once

#define TARGET_BOARD_IDENTIFIER "LYI7"
#define USBD_PRODUCT_STRING     "LYIF722DUAL"

/*** 指示灯 ***/
#define LED0                    PA14
#define LED1                    PA13
#define BEEPER                  PC14
#define BEEPER_INVERTED

// *** SPI & I2C ***
#define USE_SPI

#define USE_SPI_DEVICE_1
#define SPI1_NSS1_PIN           PA4
#define SPI1_NSS2_PIN           PC15
#define SPI1_SCK_PIN            PA5
#define SPI1_MISO_PIN           PA6
#define SPI1_MOSI_PIN           PA7

#define USE_SPI_DEVICE_2
#define SPI2_NSS_PIN            PB12
#define SPI2_SCK_PIN            PB13
#define SPI2_MISO_PIN           PB14
#define SPI2_MOSI_PIN           PB15

#define USE_SPI_DEVICE_3
#define SPI3_NSS_PIN            PA15
#define SPI3_SCK_PIN            PC10
#define SPI3_MISO_PIN           PC11
#define SPI3_MOSI_PIN           PB2

#define USE_I2C
#define USE_I2C_DEVICE_1
#define I2C1_SCL                PB8
#define I2C1_SDA                PB9

// *** IMU：单陀螺 ICM42688P（经 ICM42605 驱动识别） ***
#define USE_TARGET_IMU_HARDWARE_DESCRIPTORS
#define GYRO_SPI_BUS            BUS_SPI1
#define GYRO1_CS_PIN            SPI1_NSS1_PIN

#define USE_IMU_ICM42605
#define IMU_ICM42605_ALIGN      CW0_DEG      /* ★ 待台架验证 */

// *** OSD：SPI3 ***
#define USE_MAX7456
#define MAX7456_SPI_BUS         BUS_SPI3
#define MAX7456_CS_PIN          SPI3_NSS_PIN

// *** 黑匣子 Flash：SPI2（16MB） ***
#define USE_FLASHFS
#define USE_FLASH_M25P16
#define M25P16_CS_PIN           SPI2_NSS_PIN
#define M25P16_SPI_BUS          BUS_SPI2
#define ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT

// *** UART ***
#define USE_VCP
#define USE_UART1
#define USE_UART2
#define USE_UART3
#define USE_UART4
#define USE_UART5
#define USE_UART6

#define UART1_TX_PIN            PA9
#define UART1_RX_PIN            PA10
#define UART2_TX_PIN            PA2
#define UART2_RX_PIN            PA3
#define UART3_TX_PIN            PB10
#define UART3_RX_PIN            PB11
#define UART4_TX_PIN            PA0
#define UART4_RX_PIN            PA1
#define UART5_TX_PIN            PC12
#define UART5_RX_PIN            PD2
#define UART6_TX_PIN            PC6
#define UART6_RX_PIN            PC7

#define SERIAL_PORT_COUNT       7

#define DEFAULT_RX_TYPE         RX_TYPE_SERIAL
#define SERIALRX_PROVIDER       SERIALRX_CRSF
#define SERIALRX_UART           SERIAL_PORT_USART2

// *** 气压计 ***
#define DEFAULT_I2C_BUS         BUS_I2C1
#define USE_BARO
#define BARO_I2C_BUS            BUS_I2C1
#define USE_BARO_DPS310
#define USE_BARO_BMP280

// *** ADC ***
#define USE_ADC
#define ADC_CHANNEL_1_PIN       PC1
#define ADC_CHANNEL_2_PIN       PC0
#define VBAT_ADC_CHANNEL        ADC_CHN_1
#define CURRENT_METER_ADC_CHANNEL ADC_CHN_2

// *** LED2812 ***
#define USE_LED_STRIP
#define WS2811_PIN              PA8

// *** 其它 ***
#define DEFAULT_FEATURES        (FEATURE_OSD | FEATURE_CURRENT_METER | FEATURE_VBAT | FEATURE_TELEMETRY)

#define USE_DSHOT
#define USE_ESC_SENSOR
#define USE_SERIAL_4WAY_BLHELI_INTERFACE

#define TARGET_IO_PORTA         0xffff
#define TARGET_IO_PORTB         0xffff
#define TARGET_IO_PORTC         0xffff
#define TARGET_IO_PORTD         (BIT(2))

#define MAX_PWM_OUTPUT_PORTS    8
