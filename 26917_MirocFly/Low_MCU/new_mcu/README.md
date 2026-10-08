# 新主控（new_mcu）— BetaFPV G473

> 本目录是**下位机（飞行控制器）新主控**的工作区。
> 上位机在 `../../Host_Lichee_RV_Nano/`（本目录不涉及上位机代码）。

## 现状（截至 2026-09-30）

- **主控定型**：**BetaFPV G473**（STM32G474，BMI270 IMU，四合一 DSHOT ESC）。
  - 与**此前已烧毁的板子同款**，已**重新购入**同款板子。
- **固件：自研移植的 INAV 10.0.0，当前“能飞”**。
  - ⚠️ **官方 INAV 不支持 STM32G4**（只支持 F4/F7/H7/AT32），本固件是把 INAV **移植到 STM32G4** 的成果。
  - 版本行：`INAV/BETAFPVG473 10.0.0 Sep 26 2026 (3d2c8fd) Dev / GCC-14.3.1`。
- **尚未做“本土化改造”**（关键）：
  - ❌ 未适配**上位机 MSP 对接**（RC 接管 / 回读）；
  - ❌ 未适配**光流**等外设；
  - ❌ **接口/UART 映射尚未细看、未定**（与 AT32 阶段的约定不再直接成立，见下）。

## 目录结构

```
new_mcu/
├── drone.zip                 # 下位机工作区整包（Windows 侧导出，~1GB）
└── drone/
    ├── inav/                 # 自移植的 INAV 源码树（含 STM32G4 支持改动）
    │   ├── AGENTS.md         # INAV 上游的 AI 上下文（平台说明只提到 F4/F7/H7/AT32）
    │   ├── src/main/target/BETAFPVG473/   # 自制板级 target（target.h/target.c/config.c/CMakeLists）
    │   ├── src/main/drivers/…             # G4 相关改动：rcc / dma / exti / io / bus_spi / bus_i2c_hal / timer / adc_impl / config_streamer
    │   ├── src/main/vcp_hal/…             # G4 USB CDC(VCP) 改动
    │   ├── build-g4-final/                # 构建目录（bin/BETAFPVG473.elf 等）
    │   └── board/                         # INAV 上游板级 cfg（f4/f7/h7/at32）
    ├── firmware/             # 构建产物 + 板子 CLI dump
    │   ├── BETAFPVG473-motor-af-i2c-fix/  # hex/bin/elf（烧写地址 0x08000000）+ README
    │   ├── INAV_10.0.0_cli_20260926_220257.txt   # 早期 CLI（含 serial/aux）
    │   └── INAV_10.0.0_cli_20260927_134533.txt   # 较新 CLI（基本默认态）
    ├── betafpvg47/           # Betaflight 参考 + 机架
    │   ├── bf-source-4.5.1/                # Betaflight 4.5.1 源码（对照用）
    │   ├── BTFL_cli_…_diffall.txt          # 原厂 Betaflight CLI diffall
    │   ├── 机架.SLDPRT / 机架.STEP        # 机架模型
    │   └── esc-configurator-log (2).txt    # ESC 配置日志
    ├── .tools/               # Windows(COM4) 脚本：CLI status、ESC 4-way 探测等
    └── tools/                # esc-configurator 源码等
```

> 说明：该工作区由 **Windows** 侧制作（脚本里路径为 `D:\RM\drone\.tools`，串口 `COM4`），此处为归档副本。

## 已完成的移植要点（面试/复盘价值高）

- **STM32G4 平台支持**：`rcc`（时钟）、`dma`、`exti`、`io`、`bus_spi`、`bus_i2c_hal`、`timer_*`、`adc_impl`、`config_streamer`、`vcp_hal`(USB CDC) + 新 target。
- **`firmware/BETAFPVG473-motor-af-i2c-fix/README.md` 记录的三处修复**：
  1. 电机 4 `PC13/TIM8_CH4N` 复用号 **AF5 → AF6**（对照 Betaflight 4.5.1）；
  2. 蜂鸣器 `PA8/TIM1_CH1` **AF1 → AF6**；
  3. `bus_i2c_hal.c` **缺 STM32G4 分支** → I2C 设备表为空；补 G4 硬件映射（G4 RCC/PCLK，启用 I2C1 PA15/PB7）。
- **构建体积**：`FLASH 458682B/480KiB = 93.3%`、`RAM 88.7KB/108KiB = 80.2%`（**很紧张**，target 里已 `#undef` 掉动态滤波/FrSkyOSD 等以省 flash）。

## 已知信息（来自 `target.h`，**未最终确认/未本地化**）

| 项 | 内容 |
|---|---|
| MCU | STM32G474（512KiB flash） |
| IMU | `USE_IMU_ICM42605` 与 `USE_IMU_BMI270` **都定义且共用 SPI1 / CS=PA4**（CLI 用 **BMI270**）→ 待确认板上实际 IMU，避免误配 |
| UART | **仅 UART1~4 + VCP**（无 UART5/7）：UART1 PA9/PA10、UART2 PA2/PA3、UART3 PB10/PB11、UART4 PC10/PC11 |
| 默认接收机 | `RX_TYPE_SERIAL` + **CRSF，绑定 `USART3`** |
| 电机 | DSHOT，M1=PB0/TIM3_CH3、M2=PB1/TIM3_CH4、M3=PB6/TIM8_CH1、M4=PC13/TIM8_CH4N |
| I2C1 | PA15(SCl)/PB7(SDA)，target 预留 baro/mag |
| Blackbox | W25N01G/M25P16 SPI3（PB9 CS） |

## 待办（硬件到位后，按顺序）

1. **确认硬件与板级**：实际 IMU 型号、UART 焊盘引出、供电；拔桨验证电机顺序/转向、IMU 朝向。
2. **接口/UART 映射**：重新定义上位机 MSP、物理 RC(CRSF)、光流、磁力计/气压计各接哪个 UART，**不再沿用 AT32 的 UART1/5/7 约定**。
3. **上位机 MSP 对接**：INAV 侧开启 RC 接管（`BOX_MSP_RC_OVERRIDE` + `msp_override_channels`，默认=0 需配置）或 `receiver_type=MSP`；与上位机链路联调（详见 `../../Host_Lichee_RV_Nano/`）。
4. **光流计**适配（MTF-02P 等）。
5. **联调安全**：拔桨 → 绑绳 → 短飞；验证 roll/pitch 符号、摇杆量程、fail-safe。

## G4 移植改动归档（补丁）

- **`drone/inav-g4-port.patch`**：相对 INAV 上游 `3d2c8fd` 的**本地 STM32G4 移植改动**
  （31 个改动文件 + 20 个自研新文件；**不含** ST 厂商库 `lib/main/STM32G4/` 与 build 产物）。
  应用方式：
  ```bash
  cd drone
  git clone https://github.com/iNavFlight/inav.git inav   # 已有则跳过
  cd inav && git checkout 3d2c8fd && git apply ../inav-g4-port.patch
  ```
- **可烧写固件**：`drone/firmware/BETAFPVG473-motor-af-i2c-fix/`（`.hex/.bin/.elf`，烧写地址 `0x08000000`）。
- 说明：整棵 `inav/`（1.6G）、`bf-source-4.5.1/`、`esc-configurator-src/`、`.tools/Ruby40/`、`drone.zip` 均**不入库**（见 `26917_MirocFly/.gitignore`）。

## 参考

- 接口抽象（上位机视角）：`../../Host_Lichee_RV_Nano/AGENT_GUIDE.md`。
- AT32 阶段经验（**已冻结**，仅参考）：`../archive_at32/agent_at32.md`。
- INAV 上游 AI 上下文：`drone/inav/AGENTS.md`。
