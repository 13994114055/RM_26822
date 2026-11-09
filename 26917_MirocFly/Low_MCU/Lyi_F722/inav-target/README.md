# LYIF722DUAL — 自建 INAV target

> 2026-11-08 建立。目标：让 INAV 10.0.0 支持这块 **LYI F722 DUAL** 板（厂家只提供 Betaflight）。
> 模板：`src/main/target/ALEXF722V3`（F722 + 双陀螺 + DPS310 + MAX7456 + M25P16）。

## 1. 引脚来源（唯一依据）

板上 Betaflight 4.5.1 的 `resource` 输出（2026-11-08 通过 CLI 读取）：

| 功能 | 引脚 |
|---|---|
| MOTOR 1–4 | PB04 / PB05 / PB06 / PB07 |
| MOTOR 5–8 | PC08 / PC09 / PB00 / PB01 |
| SERIAL1 | TX PA09 / RX PA10 |
| SERIAL2 | TX PA02 / RX PA03 |
| SERIAL3 | TX PB10 / RX PB11 |
| SERIAL4 | TX PA00 / RX PA01 |
| SERIAL5 | TX PC12 / RX PD02 |
| SERIAL6 | TX PC06 / RX PC07 |
| I2C1（气压计） | SCL PB08 / SDA PB09 |
| SPI1（陀螺） | SCK PA05 / MISO PA06 / MOSI PA07；CS1 **PA04**，CS2 **PC15** |
| SPI2（Flash） | SCK PB13 / MISO PB14 / MOSI PB15；CS **PB12** |
| SPI3（OSD） | SCK PC10 / MISO PC11 / MOSI PB02；CS **PA15** |
| ADC | BATT PC01 / CURR PC00 / RSSI PC02 |
| LED / BEEPER | LED0 PA14 / LED1 PA13 / BEEPER PC14 |
| LED_STRIP | PA08 |

## 2. 已写入的配置

| 项 | 值 |
|---|---|
| 板名 | `LYI7` / USB 名 `LYIF722DUAL` |
| IMU | 双陀螺 `USE_DUAL_GYRO`，ICM42688P 走 `USE_IMU_ICM42605` 驱动（INAV 注释：ICM42605/ICM42688P 共用） |
| 气压计 | DPS310（I2C1）+ BMP280 备用 |
| OSD | MAX7456（SPI3） |
| Flash | M25P16 驱动（SPI2，16MB 芯片） |
| 编译单元 | `target_stm32f722xe`（F722 / 512KB） |

## 3. 三个「待台架验证」的不确定项

| # | 项 | 现状 | 怎么验 |
|---|---|---|---|
| 1 | 陀螺朝向 `IMU_ICM42605_ALIGN` | 暂定 `CW0_DEG` | 刷后看 `sensors`：飞机平放时 roll/pitch 应≈0；倾斜方向要对 |
| 2 | 双陀螺是否**都挂 SPI1** | 按 BF 现象推断（`dma shared`） | 刷后 `status` 看 gyro 1/2 是否都识别；不行就把 CS2 改挂 SPI2 试 |
| 3 | Flash/OSD 的 SPI 归属 | Flash→SPI2(PB12)、OSD→SPI3(PA15) | `status` 看 FLASH 容量；OSD 是否显示 |

> 注意：模板 ALEXF722V3 的 OSD/Flash 的 CS 与我们**相反**（模板 OSD=PB12、Flash=PA15），已按 `resource` 表对调。

## 4. 编译步骤

```bash
# 1) 拷进 INAV 源码树
cp -r LYIF722DUAL <inav>/src/main/target/

# 2) 配置（只编这一个 target）
cd <inav>/build
cmake .. -DBUILD_TARGET=LYIF722DUAL -DCOMPILER_VERSION_CHECK=OFF

# 3) 编译
make -j$(nproc)
# 产物：build/bin/LYIF722DUAL.hex
```

## 5. 刷写后的验证清单（拔桨！）

```
□ version          → 显示 INAV + LYIF722DUAL
□ status           → GYRO=ICM42688P、BARO=DPS310、FLASH 16M
□ sensors          → 平放时 roll/pitch≈0，倾斜方向正确
□ motor test       → 1–4 号电机对应正确（拔桨）
□ resource/引脚    → 与本文表一致
□ 电压读数         → 接电池后 vbat 正确（否则调 vbat_scale）
□ 串口             → MSP/CRSF 各通道能通
```
