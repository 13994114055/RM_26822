# 下位机（飞控）开发指导 + 交接 — BetaFPV G473 / INAV 10.0.0(G4)

> 本文件是 **MirocFly 下位机（飞行控制器）** 的**权威开发指导 + 交接文档**。
> 上位机（LicheeRV Nano，视觉/决策）见 `../../Host_Lichee_RV_Nano/AGENT_GUIDE.md`。
> 最后更新：2026-10-09。

---

## 0. 给新 AI / 新同学的交接说明（先读这段）

### 你是谁、要做什么
你接手的是 **MirocFly** 项目的**下位机（飞控）**部分：把一块 **BetaFPV G473** 板（跑自研移植的 **INAV 10.0.0 for STM32G4**）与上位机 **LicheeRV Nano** 对接起来，最终实现**自主视觉打击**（上位机做视觉/决策，下位机做稳定飞行/执行）。

### 工作约定（务必遵守）
- **教学优先**：用户是学生，目标是**面试与项目成长**。多解释"**为什么**"、上岗面试会怎么问，不要只给结论。
- **别吹捧、不迁就沉没成本**：只从"就业 + 项目成长性"客观评估；发现方案不对要直说。
- **先汇报再执行**：每个小阶段先给用户汇报思路/预期结果，得到确认再动手（尤其是刷固件、改配置、编译）。
- **别污染原始文件**：需要改动时复制到新目录（如 `inav-g4dbg/`），保留原树。
- **进展即提交**：每完成一小步就 `commit` + `push`（远程 `git@github.com:13994114055/RM_26822.git`，分支 `main`）。
- **中文交流**。

### 机器分工
| 机器 | 地址/入口 | 干什么 |
|---|---|---|
| **i5 / Arch**（本机） | 工作目录 `~/code/26822_RM` | 代码、交叉编译、量化、刷固件、配置 FC |
| **LicheeRV Nano**（上位机） | `ssh root@10.222.2.1`（USB-RNDIS 网卡） | 运行视觉/决策，与 FC 通过 UART0 通信 |
| 4060 / Win11 | — | **仅**用于模型训练 |

> ssh 到 LicheeRV：`ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@10.222.2.1`

### 三分钟上手
1. 读本文件 → 读 `connection/README.md`（对接方案）→ 读 `../README.md`（总览）。
2. FC 连线：拔桨，USB 接 i5（`/dev/ttyACM0`），用 `connection/fccli.py`（docker `--device`）读 `status`。
3. 上位机链路：LicheeRV `UART0(/dev/ttyS0, A16/A17) @230400` ↔ FC `UART2`，**TX/RX 交叉**。
4. 当前卡点：**ANGLE 模式振荡**、**CoG 偏左导致起飞左翻**、**黑匣子日志待分析**、**光流/测距待验证**（见第 5 节）。

---

## 1. 任务背景

- **项目**：MirocFly — 面向"自主视觉打击"的小型无人机。上位机（LicheeRV Nano，SG2002）负责摄像头推理、目标决策；下位机（飞控）负责姿态稳定与执行。
- **接口抽象**：上位机给下位机下发 `TargetInfo{ 偏移X, 偏移Y, 面积Area(∝1/距离²), 状态Status }`；链路层只换编码器（近期用 **MSP 摇杆值**，远期换自定义帧 `AA 55 | X | Y | Area | Status | CRC8`）。
- **RC 优先**：物理 CRSF 直连为准，**ch8 拨杆**切换自主/手动（`MSP RC Override`，模式**永久 ID 50**）；上位机停发则回落到物理 RC。

---

## 2. 硬件与固件现状

### 2.1 主控定型
- **BetaFPV G473**：STM32G474（512KiB flash，168MHz PLLR-HSE），BMI270 IMU（SPI1），四合一 DSHOT ESC，板载 ELRS，板载 W25N01G SPI3 blackbox（16M）。与**此前烧毁的板子同款**，已重新购入。
- 板级细节（UART/电机/I2C/blackbox 引脚）见 `README.md`「板级信息」表。

### 2.2 固件 = 自研移植的 INAV 10.0.0（STM32G4）
- ⚠️ **官方 INAV 不支持 STM32G4**（只有 F4/F7/H7/AT32），本固件是把 INAV **移植到 G4** 的成果。版本行：`INAV/BETAFPVG473 10.0.0 (3d2c8fd) Dev`。
- ✅ **已刷入板子**（原厂 Betaflight 4.5.1 → 我们的 INAV）；`GYRO/ACC=BMI270 OK`。
- ✅ **可在 i5 本机重编**（`drone/inav-g4dbg/`，见第 3 节）。
- 体积紧张：`FLASH 458682B/480KiB = 93.3%`、`RAM 80.2%`；target 里已 `#undef` 掉动态滤波/FrSkyOSD 等省 flash（→ 滤波策略要保守）。
- 移植改动归档：`drone/inav-g4-port.patch`（相对上游 `3d2c8fd`，31 改 + 20 新文件，不含 STM32G4 厂商库与 build 产物）。

### 2.3 已完成 / 当前状态（截至 2026-10-09）
- ✅ 固件刷写、板级校验。
- ✅ 上下位机对接配置写入并持久化（UART2=MSP / UART3=CRSF / UART4=MTF-02P）。
- ✅ **上位机↔下位机 MSP 链路实测打通**（LicheeRV UART0 ↔ FC UART2 @230400）。
- ✅ EzTune 关闭 + INAV 默认 PID/滤波已应用（见 5.2）。
- ⏳ 黑匣子日志已导出待分析；真机联调未完成（振荡/CoG/光流待解决）。

---

## 3. 工程环境与工具链（i5 / Arch）

### 3.1 编译 G4 固件
```bash
# 在 Low_MCU/new_mcu/drone/inav-g4dbg/ 下
export CMAKE_POLICY_VERSION_MINIMUM=3.5
cmake -B build -G Ninja -DCOMPILER_VERSION_CHECK=OFF   # 必须关校验，否则去 Arm 官网下 13.2 工具链
ninja -C build BETAFPVG473                             # -> build/bin/BETAFPVG473.elf / .hex
```
- 依赖：`arm-none-eabi-gcc`（本机 16.2.0）、`cmake`、`ninja`。
- `inav-g4dbg/` 是 `drone/inav/`（Windows 原树）的副本（去 `.git`/`build-*`），**gitignore**，仅用于 i5 改/编固件，**不污染原树**。

### 3.2 刷固件（USB DFU）
1. **按住 BOOT 键插 USB** → 进 DFU（设备 `0483:df11`）。
2. 用容器里的 `dfu-util`：
```bash
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb --network host tpuc_mlir:latest bash -lc \
  'apt-get update -qq && apt-get install -y -qq dfu-util && \
   dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D /fw/BETAFPVG473-...bin'
```
- 可烧写文件：`drone/firmware/BETAFPVG473-motor-af-i2c-fix/`（hex/bin/elf，地址 `0x08000000`）。

### 3.3 配置 / 调试 FC（USB VCP）
- 脚本：`connection/fccli.py`（纯 stdlib，通过 USB VCP 与 FC CLI 交互）。
- 权限：`/dev/ttyACM0` 属 `root:uucp`，用户不在 `uucp` 组 → 走 **docker `--device`** 免 sudo。
```bash
docker run --rm --device=/dev/ttyACM0 -v <new_mcu>:/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --cmd 'version' --cmd 'status'
docker run --rm --device=/dev/ttyACM0 -v <new_mcu>:/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --config /mcu/drone/config/g473_msprc.txt
```
- ⚠️ **打开 INAV Configurator 会独占 `/dev/ttyACM0`**，与 `fccli.py` 冲突 → 先 `pkill -f inav-configurator`。
- Configurator：`/home/user/Zip_app/INAV_Configurator-linux-x64-10.0.0/inav-configurator`（10.0.0-RC1）。

### 3.4 上位机链路验证
- `connection/msp_probe.c`：在 LicheeRV 上跑，验证 FC↔上位机 MSP（ATTITUDE/STATUS/RAW_IMU，LINK OK）。需交叉编译（RISC-V）。

---

## 4. 上下位机对接（接口已定稿并落地）

> 完整方案/接线/验证清单见 **`connection/README.md`**；可粘贴配置见 **`drone/config/g473_msprc.txt`**。

| 用途 | G473 口 | 引脚 | 对端 | 参数 |
|---|---|---|---|---|
| **上位机 MSP** | UART2 | PA2/PA3 | LicheeRV **UART0 `/dev/ttyS0`**（A16/A17） | **230400** |
| **CRSF 接收机** | UART3 | PB10/PB11 | 遥控接收机 | auto |
| **光流+测距** | UART4 | PC10/PC11 | MTF-02P | 115200 |
| 配置/调试 | USB VCP | — | i5 | 115200 |
| （释放名额） | UART1 | — | 空 | 仅 RX，接不了外设，**必须清空** |

- ✅ **2026-10-09 实测打通**：LicheeRV `UART0(/dev/ttyS0, A16/A17) @230400` ↔ FC `UART2(MSP)`，**TX/RX 必须交叉**。
- 要点：**UART1(ttyS1) 与板载 Wi-Fi/BT(AIC8800) 共用引脚，不可用**；**UART0 需先释放 console getty**（板内 `/etc/inittab` 注释相应行）。
- ⚠️ **必须 `serial 0 0 ...` 清空 UART1**：INAV 限 **最多 3 个 MSP 端口**（`MAX_MSP_PORT_COUNT=3`）；默认 VCP+UART1 已占 2，再加 UART2+UART4 会超限 → 启动时 serial 配置被**整体重置**（表现为"存不住"）。
- MTF-02P 走 **MSP**：`opflow_hardware=MSP` + `rangefinder_hardware=MSP`（INAV 原生解析 `MSP2_SENSOR_OPTIC_FLOW`/`MSP2_SENSOR_RANGEFINDER`）。
- 策略：保留物理 CRSF；上位机 `MSP RC Override`（永久 ID 50）绑 ch8（`aux 0 50 3 1700 2100`），`msp_override_channels=15`；`failsafe_procedure=DROP`。

---

## 5. 当前进度与已知问题

### 5.1 ✅ 已解决
- 固件刷写 + 板级校验（BMI270）。
- 上下位机 MSP 链路打通（UART0/ttyS0@230400）。
- serial 配置持久化 bug（根因 `MAX_MSP_PORT_COUNT=3`，解法清空 UART1）。
- UART1/ttyS1 与 AIC8800 冲突诊断；释放 UART0 console。
- ✅ **"离地即翻/自旋"根因（2026-10-10）**：**电机输出编号 M1–M4 与物理接线错位**（飞控倒装 180° 重装后未重映射）→ 姿态修正发给了错误电机 → 发散（翻/自旋）。**修复**：mixer 改成匹配真实编号的"180° 轮换"版 + `motor_direction_inverted=OFF` + `align_board_roll=1800`（倒装仅靠它补传感器朝向）。**现能起飞、不再自旋**。排查法见 §5.5；基线 dump：`drone/firmware/INAV_10.0.0_cli_20261010_dirfix.txt`。

### 5.2 ✅ EzTune 关闭 + INAV 默认 PID（Plan A）
- **背景**：INAV 的 **EzTune** 特性在开启时会**覆盖** `mc_p/i/d`（每次启动重算）；此前 `ez_enabled=ON`（`ez_response≈92`），导致 PID 不受手动配置控制。
- **已应用并持久化**：`ez_enabled=OFF`，恢复 INAV 默认 PID：
  - `mc_p_pitch/roll=40/40`、`mc_i=30/30`、`mc_d=23/23`、`mc_p_yaw=85`、`mc_i_yaw=45`、`mc_d_yaw=0`
  - `gyro_main_lpf_hz=60`、`dterm_lpf_hz=110`、`tpa_rate=0`、`setpoint_kalman_q=100`

### 5.3 ⏳ 已知问题（**当前卡点**）
1. **ANGLE 模式振荡加重**：Plan A 之后，ANGLE 自稳模式振荡更明显（此前用户在 ACRO 模式）。需要**黑匣子分析 + 降低 P**。
2. ~~**CoG 偏左 → 起飞左翻**~~ → **已澄清（2026-10-10）**：所谓"左翻/自旋"实为**电机编号错位**（见 §5.1/§5.5），**不是 CoG、也不是 PID**；修复后能飞。CoG 仍偏左值得物理核对，但**不再是卡点**。
3. **ANGLE 自稳/PID（有电池后再调）**：能飞，但需在 ANGLE 短悬停看**是否振荡/慢漂**再决定是否降 P/调滤波。
   - 本机已具备**自写 INAV blackbox v2 解码器**（直接读 `~/Downloads/blackbox_log_*.TXT`，出 `roll/pitch/yaw`、`gyroADC`、`motor`）：无需 `blackbox_decode`。
   - 已分析：`blackbox_log_2026-10-10_152838/160315/162041/164110.TXT` → 旧配置下是**方向类错误（翻/自旋）**，**并非 PID 振荡**；也印证"先修方向、再谈 PID"。
   - 待办：短悬停取新日志 → 看 `gyroADC/axisP/axisD/motor` 的振荡频率/幅度 → 给 P/D/滤波建议。
4. **MTF-02P 光流/测距未验证**：目标 POSHOLD/ALTHOLD（RTH 不用光流）。测距**误差 4–5cm**（规格：<2m 时 2cm、>2m 时 1.5%），原因待查。光流需在 INAV `status` 确认 `OPFLOW`/`RANGEFINDER` 就绪，或 `debug_mode=FLOW`。
   - 手册：https://micoair.cn/zh/docs/sensors/sensors/mtf-02-02p-sensors

### 5.4 待办阶梯
0. ~~方向类问题~~ → ✅ **已完成（2026-10-10）**。**无电池期间优先做"上位机侧"**（MSP 链路台架联调 + 视觉），飞行项等电池。
1. （有电池后）拔桨→绑绳→短悬停：确认自稳、**无明显自旋/慢漂**。
2. 取 ANGLE 黑匣子 → 解析 → 视振荡决定是否**降 P/调滤波**（小四轴 + 本板关了动态滤波，PID 保守）。
3. **拔桨**联调：MSP 收发、ch8 拨杆切换、failsafe。
4. 物理核对 CoG（仍偏左则配重）。
5. **MTF-02P 标定**（`opflow_scale`、`align_opflow`）→ POSHOLD/ALTHOLD。

### 5.5 ✅ 方向类问题排查法（编号 / 朝向 / 混控 / yaw 符号）
> 教训：**"离地即翻/自旋/乱飘" 大概率是方向类问题，不是 PID**。调 PID 前先把"三轴方向"验对。

**概念分工（务必别混用）**
- `align_board_roll/pitch/yaw`：只补**传感器朝向**（飞控倒装/旋转 → 改这里）。
- `mmix`（电机混控）：决定"逻辑 roll/pitch/yaw → 哪个**输出**"。**旋转飞控不影响它**；只有**电机接线/编号错位**才改它。
- `motor_direction_inverted`：**只翻 yaw 符号**（`mixer.c:241` 置 `motorYawMultiplier=-1`），不动 roll/pitch。

**本次事故**：飞控倒装(roll 180°) + 电机编号错位。曾用"镜像 mixer"去补 → roll/pitch 反 → **翻**；换回又因 `motor_direction_inverted` 错 → **yaw 自旋**。正解 = mixer 换成匹配真实编号的**180° 轮换版** + `motor_direction_inverted=OFF`。

**改硬件后的"5 分钟方向自检"（拔桨！）**
1. **板朝向**：水平放 → `attitude` roll/pitch≈0；抬机头→同向；抬右侧→同向。（不对 → 改 `align_board_*`）
2. **电机编号/转向**（Configurator `Motors` 页）：M1–M4 对应的**物理角位/旋向**与图示一致。
3. **闭环方向**（ANGLE、低-中油门、拔桨）：
   - 压机头下 → **前桨加速**；压哪侧下 → **该侧电机加速**；拧偏航 → **反向抵抗**。
   - 哪个轴帮倒忙 → 对应改 `align_board`/`mmix`/`motor_direction_inverted`，**一次只改一处**。
4. 通过再上桨，**绑绳 → 短飞**。

**工具**：① 自写 INAV blackbox v2 解码器（读 `~/Downloads/blackbox_log_*.TXT`）；② 台架 `msp_live.py`（读 `ATTITUDE/RAW_IMU/MOTOR`）。

---

## 6. 关键坑与教训（血泪清单）

1. **`MAX_MSP_PORT_COUNT=3`**：MSP 端口数超限 → 整个 serial 配置在启动时被重置。务必清空用不到的 UART1（`serial 0 0 ...`）。
2. **UART1 仅 RX**（SBUS 用），无 TX，接不了双向外设。
3. **UART1/ttyS1 与板载 Wi-Fi/BT(AIC8800) 共用引脚**（LicheeRV 侧），别用；上位机用 **UART0/ttyS0**。
4. **UART0 console getty 占口**：板内 `/etc/inittab` 需注释掉才能自由使用 ttyS0。
5. **EzTune 覆盖 `mc_*`**：调 PID 前先确认 `ez_enabled=OFF`，否则改了也没用。
6. **目标板 flash 紧张（93%）**：动态滤波等被 `#undef`，滤波/调参策略要保守。
7. **`inav-g4dbg/`、`inav/`、`drone.zip` 等均 gitignore**（不入库）；改固件只在 `inav-g4dbg/` 做以保护原树。
8. **权限/容器**：`/dev/ttyACM0` 需 docker `--device`；docker bridge 网络坏 → 用 `--network none`（离线）或 `--network host`（联网装包）。
9. **Configurator 独占 VCP**：与 `fccli.py` 冲突，先关。
10. **改硬件（换板/重焊电机/换桨）后必做"三轴方向自检"**（§5.5），否则症状五花八门（翻/自旋/乱飘），极易误判成 PID/CoG。
11. **别用 `mmix` 去补传感器朝向**：飞控旋转只改 `align_board_*`；改 `mmix` 会"双重补偿"→ 翻。
12. **`motor_direction_inverted` 只管 yaw**（`mixer.c:241`）：yaw 自旋/反向时先查它。
13. **INAV 电机编号 ≠ 物理编号**是高频坑：`mmix` 的行号 = **输出通道**，务必与真实电机一一对齐（本次主因）。
14. **FC 在 CLI 模式时，UART2 的 MSP 不响应**：用 USB 进 CLI 读/改配置后，链路会短暂 `NO RESPONSE` → `save`（重启）或 `exit` 回正常模式，并**等 ~15–20s** 再测（重启后 UART2 MSP 就绪有延迟）。别误判成"接错线"。
15. **电压采样（自制 target）**：`target.h` 的 `VBAT_SCALE_DEFAULT` 曾误设 **110**（=1.1:1），本板实际分压为 **11:1** → 电压只显示 ~0.4V（1S 电池）。正确值 **1100**（2026-10-10 修正源码 + 板上 `set vbat_scale = 1100` 已 `save` 持久化，重启后 `status` 显示 ~4V）。**自制 target 首次上电务必核对 `vbat_scale`/`current_meter_scale`。**

---

## 7. 参考资料索引

**内部**
- 下位机总览（目录/常用命令/接口表）：`../README.md`
- 本工作区详情（板级信息/移植要点/补丁）：`README.md`
- **上下位机对接方案（接线/INAV 配置/验证清单/测试计划）**：`connection/README.md`
- 可粘贴 INAV 配置：`drone/config/g473_msprc.txt`
- 上位机接口抽象：`../../Host_Lichee_RV_Nano/AGENT_GUIDE.md`
- 项目总览 / AI 上下文：`../../README.md`、`../../agent.md`
- AT32 阶段经验（**已冻结**，仅参考）：`../../archive_at32/agent_at32.md`
- INAV 上游 AI 上下文：`drone/inav/AGENTS.md`

**外部**
- MTF-02P 手册：https://micoair.cn/zh/docs/sensors/sensors/mtf-02-02p-sensors
- INAV 官方：https://github.com/iNavFlight/inav

---

## 8. 交接清单（下一个人 / 新 AI 接手时）

- [ ] 确认机器分工与环境（i5 能编译？`arm-none-eabi-gcc`、cmake、ninja、docker `tpuc_mlir:latest`）。
- [ ] 拔桨，USB 接 FC，`fccli.py` 读 `version`/`status`，确认固件为我们的 INAV 10.0.0。
- [ ] 确认 `connection/README.md` 的接线（TX/RX 交叉、共地）与 INAV 配置仍在。
- [ ] 复现链路：LicheeRV 上跑 `msp_probe /dev/ttyS0 230400`。
- [ ] **优先处理 5.3 卡点**：先修 CoG，再黑匣子分析降 P，再光流/测距。
- [ ] 每完成一步 `commit && push`。
