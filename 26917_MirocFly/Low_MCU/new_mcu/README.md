# 新主控（new_mcu）— BetaFPV G473

> 本目录是**下位机（飞行控制器）新主控**的工作区。
> 上位机在 `../../Host_Lichee_RV_Nano/`（本目录不涉及上位机代码）。

## 现状（截至 2026-10-09）

- **主控定型**：**BetaFPV G473**（STM32G474，BMI270 IMU，四合一 DSHOT ESC）；与**此前烧毁的板子同款**，已**重新购入**。
- **固件：自研移植的 INAV 10.0.0（STM32G4）**。
  - ⚠️ **官方 INAV 不支持 STM32G4**（只支持 F4/F7/H7/AT32），本固件是把 INAV **移植到 STM32G4** 的成果。版本行：`INAV/BETAFPVG473 10.0.0 (3d2c8fd) Dev`。
  - ✅ **已刷入板子**（原厂 Betaflight → 我们的 INAV）；`GYRO/ACC=BMI270 OK`。
  - ✅ **可在 i5 本机重编**（`drone/inav-g4dbg/`，见下）。
- **本土化（上下位机对接）已定稿并落地**：✅ 上位机 MSP（UART2）/ CRSF（UART3）/ 光流 MTF-02P（UART4）配置**已写入并持久化**；详见 `connection/README.md`。
- **剩余待办**：接外设实测（MTF-02P / CRSF / LicheeRV）、加速度计/罗盘校准、arming、真机联调（拔桨→绑绳→短飞）。

## 目录结构

```
new_mcu/
├── README.md
├── connection/               # ★ 上下位机对接（方案 + 工具）
│   ├── README.md                 # 接口映射/接线/INAV 配置/验证清单/测试计划
│   └── fccli.py                  # 通过 USB VCP 与 FC CLI 交互（docker 免 sudo 跑；纯 stdlib）
├── drone.zip                 # 下位机工作区整包（Windows 侧导出，~1GB）(gitignore)
└── drone/
    ├── inav/                 # 自移植 INAV 源码树（Windows 侧原树）(gitignore)
    │   ├── AGENTS.md         # INAV 上游 AI 上下文
    │   ├── src/main/target/BETAFPVG473/   # 自制板级 target（target.h/c/config.c/CMakeLists）
    │   ├── src/main/drivers/…             # G4 改动：rcc/dma/exti/io/bus_spi/bus_i2c_hal/timer/adc_impl/config_streamer
    │   ├── src/main/vcp_hal/…             # G4 USB CDC(VCP)
    │   └── build-g4-final/                # Windows 侧构建目录
    ├── inav-g4dbg/           # ★ i5 上的调试构建工作区（inav/ 副本去 .git/build-*）(gitignore)
    │   └── build/bin/BETAFPVG473.elf      # i5 构建产物（arm-none-eabi-gcc + ninja）
    ├── inav-g4-port.patch    # G4 移植改动补丁（相对上游 3d2c8fd；可复现）
    ├── firmware/             # 可烧写固件 + 板子 CLI dump
    │   ├── BETAFPVG473-motor-af-i2c-fix/  # hex/bin/elf（烧写地址 0x08000000）+ README
    │   └── INAV_10.0.0_cli_*.txt          # 早期 CLI dump
    ├── config/               # 可粘贴的 INAV CLI 配置
    │   └── g473_msprc.txt                 # 上位机 MSP + CRSF + MTF-02P 光流 配置
    ├── betafpvg47/           # Betaflight 参考 + 机架
    │   ├── bf-source-4.5.1/                # Betaflight 4.5.1 源码（对照用）(gitignore)
    │   ├── BTFL_cli_…_diffall.txt          # 原厂 Betaflight CLI diffall
    │   └── 机架.SLDPRT / 机架.STEP        # 机架模型
    ├── .tools/               # Windows(COM4) 脚本：CLI status、ESC 4-way 探测等（Ruby40 等入口已忽略）
    └── tools/                # esc-configurator 源码等
```

> 说明：原工作区由 **Windows** 侧制作（脚本路径 `D:\RM\drone\.tools`，串口 `COM4`）。
> **`inav-g4dbg/`** = 为在 **i5** 上改/编固件而复制的工作树（不污染原 `inav/`）；仅做配置时用不到它。

## 已完成的移植要点（面试/复盘价值高）

- **STM32G4 平台支持**：`rcc`（时钟）、`dma`、`exti`、`io`、`bus_spi`、`bus_i2c_hal`、`timer_*`、`adc_impl`、`config_streamer`、`vcp_hal`(USB CDC) + 新 target。
- **`firmware/BETAFPVG473-motor-af-i2c-fix/README.md` 记录的三处修复**：
  1. 电机 4 `PC13/TIM8_CH4N` 复用号 **AF5 → AF6**（对照 Betaflight 4.5.1）；
  2. 蜂鸣器 `PA8/TIM1_CH1` **AF1 → AF6**；
  3. `bus_i2c_hal.c` **缺 STM32G4 分支** → I2C 设备表为空；补 G4 硬件映射（G4 RCC/PCLK，启用 I2C1 PA15/PB7）。
- **构建体积**：`FLASH 458682B/480KiB = 93.3%`、`RAM 88.7KB/108KiB = 80.2%`（**很紧张**，target 里已 `#undef` 掉动态滤波/FrSkyOSD 等以省 flash）。

## 板级信息（已按板上 `status` 确认）

| 项 | 内容 |
|---|---|
| MCU | STM32G474（512KiB flash），`Clock=168MHz (PLLR-HSE)` |
| IMU | **实为 BMI270**（`GYRO=BMI270, ACC=BMI270`，SPI1，locked dma）；target 里同时有 ICM42605 定义但板上是 BMI270 |
| UART | **仅 UART1~4 + VCP**（无 UART5/7）：UART1 PA9/PA10、UART2 PA2/PA3、UART3 PB10/PB11、UART4 PC10/PC11 |
| 默认接收机 | `RX_TYPE_SERIAL` + CRSF，绑定 `USART3` |
| 电机 | DSHOT，M1=PB0/TIM3_CH3、M2=PB1/TIM3_CH4、M3=PB6/TIM8_CH1、M4=PC13/TIM8_CH4N |
| I2C1 | PA15(SCl)/PB7(SDA)，target 预留 baro/mag（板载无 baro/mag，`BARO/MAG=UNAVAILABLE`） |
| Blackbox | W25N01G/M25P16 SPI3（PB9 CS），`FLASH: JEDEC 0x00852018 16M` |

## 接口对接（已定稿 2026-09-30）

> 详见 **`connection/README.md`**（方案/接线/验证/测试）与可粘贴配置 **`drone/config/g473_msprc.txt`**。

| 用途 | G473 | 对端 | 波特率 |
|---|---|---|---|
| 上位机 MSP | UART2 (PA2/PA3) | LicheeRV `/dev/ttyS1` | 460800 |
| CRSF 接收机 | UART3 (PB10/PB11) | 遥控接收机 | auto |
| 光流+测距 | UART4 (PC10/PC11) | MTF-02P | 115200 |

- 策略：**保留物理 CRSF**（`receiver_type=SERIAL`）；上位机用 `MSP RC Override`（模式**永久 ID 50**）绑 **ch8**（`aux 0 50 3 1700 2100`）接管；`msp_override_channels=15`。
- MTF-02P 走 **MSP**：`opflow_hardware=MSP` + `rangefinder_hardware=MSP`（INAV 原生解析 `MSP2_SENSOR_OPTIC_FLOW`/`MSP2_SENSOR_RANGEFINDER`）。
- ⚠️ **必须 `serial 0 0 ...` 清空 UART1**：INAV 限 **最多 3 个 MSP 端口**（`MAX_MSP_PORT_COUNT=3`），本板默认 VCP+UART1 已占 2 个；再加 UART2+UART4 会超限 → 启动时 serial 配置被**整体重置**（表现为"存不住"）。清空 UART1 后 MSP=VCP+UART2+UART4=3，合规。详见 `connection/README.md`。

## 待办（硬件到位后，按顺序）

1. **确认硬件与板级**：实际 IMU 型号、UART 焊盘引出、供电；拔桨验证电机顺序/转向、IMU 朝向。
2. **验证 LicheeRV `/dev/ttyS1`** 已暴露且 pinmux=UART1（回环测试）。
3. **按 `connection/README.md` 接线并联调**：拔桨（MSP 收发 + 拨杆切换 + failsafe）→ 绑绳 → 短飞。
4. **光流/测距标定**：`opflow_scale`、`align_opflow`；确认测距量程/单位。

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
- 说明：整棵 `inav/`（1.6G）、**`inav-g4dbg/`（i5 调试构建树）**、`bf-source-4.5.1/`、`esc-configurator-src/`、`.tools/Ruby40/`、`drone.zip` 均**不入库**（见 `26917_MirocFly/.gitignore`）。

## 参考

- 接口抽象（上位机视角）：`../../Host_Lichee_RV_Nano/AGENT_GUIDE.md`。
- AT32 阶段经验（**已冻结**，仅参考）：`../archive_at32/agent_at32.md`。
- INAV 上游 AI 上下文：`drone/inav/AGENTS.md`。
