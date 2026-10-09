# MirocFly 下位机（Low_MCU）

> 本目录是 MirocFly 的**下位机（飞行控制器，执行器）**开发区。
> 上位机（LicheeRV Nano，视觉/决策）在 `../Host_Lichee_RV_Nano/`。

## 当前状态（截至 2026-10-09）

- **原 AT32F435 方案已淘汰**：直驱空心杯实测升力约 59g < 整机 76g，**无法起飞**（历史存于 `archive_at32/`）。
- **新主控 = BetaFPV G473**（STM32G474，BMI270 IMU，四合一 DSHOT ESC）。与**此前烧毁的板子同款**，已**重新购入**。
- **固件 = 自研移植的 INAV 10.0.0（STM32G4）**：官方 INAV 不支持 G4，本固件为移植版。
  - ✅ **已刷入板子**（原厂 Betaflight → 我们的 INAV），`GYRO/ACC=BMI270 OK`。
  - ✅ **上下位机对接配置已写入并持久化**（UART2=MSP 上位机 / UART3=CRSF / UART4=光流）。
  - ✅ **可在本机（i5）重编 G4 固件**（见 `new_mcu/drone/inav-g4dbg/`）。
- 剩余：接外设（MTF-02P、CRSF 接收机、LicheeRV）实测、校准、真机联调（拔桨→绑绳→短飞）。

## 目录结构

```
Low_MCU/
├── README.md                 # 本文件（下位机总览）
├── archive_at32/             # ⚠️ AT32 方案历史（冻结，不再更新）
│   ├── agent_at32.md              # AT32 开发经验全集（方案更替原因/接口/INAV 配置/教训）
│   ├── AT32F435CGU7_project/      # AT32 Workbench 骨架（非可飞）
│   └── 下位机MCU手册.pdf/.docx
└── new_mcu/                  # 新主控 BetaFPV G473 工作区（详见 new_mcu/README.md）
    ├── README.md
    ├── connection/                # 上下位机对接方案 + 工具
    │   ├── README.md                  # 接口映射/接线/INAV 配置/验证/测试计划
    │   └── fccli.py                   # 通过 USB VCP 与 FC CLI 交互的脚本（docker 免 sudo 跑）
    └── drone/
        ├── inav/                      # 自移植 INAV 源码树（含 G4 支持 + target/BETAFPVG473）(gitignore)
        ├── inav-g4dbg/                # ★ i5 上的调试构建工作区（见下）(gitignore)
        ├── inav-g4-port.patch         # G4 移植改动补丁（相对上游，可复现）
        ├── firmware/                  # 可烧写固件 + CLI dump
        │   └── BETAFPVG473-motor-af-i2c-fix/   # hex/bin/elf（烧写地址 0x08000000）
        ├── config/                    # 可粘贴的 INAV CLI 配置
        │   └── g473_msprc.txt              # 上位机 MSP + CRSF + MTF-02P 光流 配置
        ├── betafpvg47/                # Betaflight 4.5.1 参考 + 机架模型
        ├── .tools/                    # Windows(COM4) 脚本（CLI/ESC 探测）
        └── tools/                     # esc-configurator 源码
```

### 关于 `drone/inav-g4dbg/`（调试构建工作区）
- **作用**：`drone/inav/` 的**副本（去掉 .git 与 build-*）**，专门在 **i5（Arch）** 上**重新编译/修改 G4 固件**用，**避免污染 Windows 侧原 `inav/` 树**。
- 已 **gitignore**（不入库，~569M）；里面 `build/` 是 i5 的构建产物（`build/bin/BETAFPVG473.elf`）。
- 之所以能本机编：i5 有 `arm-none-eabi-gcc`；配置时需 `-DCOMPILER_VERSION_CHECK=OFF`（否则会去 Arm 官网下 13.2 工具链）。
- 若只做**配置**（不改固件），用不到它；只有要**改固件/调试**时才用。

## 上下位机接口（G473，已定稿并落地）

> 完整方案见 `new_mcu/connection/README.md`；可粘贴配置见 `new_mcu/drone/config/g473_msprc.txt`。

| 用途 | G473 口 | 引脚 | 对端 | 参数 |
|---|---|---|---|---|
| **上位机 MSP** | UART2 | PA2/PA3 | LicheeRV `/dev/ttyS1` | **460800** |
| **CRSF 接收机** | UART3 | PB10/PB11 | 遥控接收机 | auto |
| **光流+测距** | UART4 | PC10/PC11 | MTF-02P | 115200 |
| 配置/调试 | USB VCP | — | i5 | 115200 |
| （释放名额）| UART1 | — | 空 | 仅 RX，接不了外设，**必须清空** |

- ⚠️ **必须 `serial 0 0 ...` 清空 UART1**：INAV 限**最多 3 个 MSP 端口**；默认 VCP+UART1 已占 2，再加 UART2+UART4 会超限 → 启动时 serial 配置被整体重置（详见 `new_mcu/connection/README.md`）。
- **上位机统一接口**：`TargetInfo{ 偏移X, 偏移Y, 面积Area(∝1/距离²), 状态Status }`；链路层只换编码器（近期 MSP 摇杆值 / 远期自定义帧 `AA 55 | X | Y | Area | Status | CRC8`）。
- **RC 优先**：物理 CRSF 直连为准，**ch8 拨杆**切换自主/手动（`MSP RC Override`，模式永久 ID 50）；上位机停发回落物理 RC。

## 常用命令（i5）

```bash
# 编译 G4 固件（在 inav-g4dbg/）
export CMAKE_POLICY_VERSION_MINIMUM=3.5
cmake -B build -G Ninja -DCOMPILER_VERSION_CHECK=OFF
ninja -C build BETAFPVG473          # -> build/bin/BETAFPVG473.elf / .hex

# 刷固件（USB DFU；按住 BOOT 插 USB 进 DFU 后用容器里的 dfu-util）
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb --network host tpuc_mlir:latest bash -lc \
  'apt-get update -qq && apt-get install -y -qq dfu-util && \
   dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D /fw/BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.bin'

# 配置 FC（USB VCP，docker 免 sudo；读状态 / 粘配置）
docker run --rm --device=/dev/ttyACM0 -v <new_mcu>:/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --cmd 'version' --cmd 'status'
docker run --rm --device=/dev/ttyACM0 -v <new_mcu>:/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --config /mcu/drone/config/g473_msprc.txt
```
> 权限：`/dev/ttyACM0` 属 `root:uucp`，用户不在 `uucp` 组 → 走 **docker `--device`** 免 sudo。

## 参考

- 新主控详情 / 目录 / 待办：`new_mcu/README.md`
- 对接方案 / 接线 / 测试：`new_mcu/connection/README.md`
- 上位机：`../Host_Lichee_RV_Nano/`（总览、`AGENT_GUIDE.md`、操作手册 `diary.md`）
- 项目总览 / AI 上下文：`../README.md`、`../agent.md`
- AT32 历史与教训：`archive_at32/agent_at32.md`
