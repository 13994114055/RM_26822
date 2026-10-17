> ⭐ **【第一要务 · 教学优先】** 所有的开发都是以学习为目的，让我学会东西为前提，而并非把目标完成放在第一位，我要穷究原理，哪怕工期可能延误，我会多抽出其他时间，所以你要以教学和传授方法知识为第一要务。
> 📖 **开工前必读**：`../../../agent问答示例.md` —— 先学习其中的逻辑与思路（要做什么 → 为什么 → 由谁做基础），并作为**贯穿始终的回答方式**。

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
- **RC 优先**：物理 CRSF 直连为准，**拨杆切换自主/手动**（`MSP RC Override`，模式**永久 ID 50**）；上位机停发则回落到物理 RC。**开关映射（2026-10-12 定稿，实验20）**：`ch5`=ARM；**`ch6>1300 或 ch8>1300`=上位机接管三轴**（同 modeId 挂两条 aux = **OR**）；`ch8>900`=ANGLE；`ch6`=追踪范式（全 B）；`ch7[1300,1700)`=定点 / `[1700,2100)`=定高。

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
- ⚠️ **刷机与配置区（2026-10-15 实测核实）**：FLASH 布局 = `0x08000000` 16K(code) + **`0x08004000` 16K(配置区)** + `0x08008000` 480K(code)。**`.bin` 覆盖配置区且该区为零填充 → 刷 bin 会清零配置**；**`.hex` 不含配置区记录 → 刷 hex 保留配置**。→ **优先刷 `.hex`**（`dfu-util ... -D xxx.hex`）。
- **刷前必做**：把板上配置实读存档（CLI `diff all`）——权威恢复文件：`drone/firmware/INAV_10.0.0_cli_20261015_full-diffall.txt`（含 10-14 正装改动，可整段回粘 + `save`）。
- **当前固件备份状态**：`firmware/BETAFPVG473-motor-af-i2c-fix/`（sha256 见其 README）与 `inav/build-g4-final/`（`bin/` + `inav_10.0.0_*.bin/hex`）**各一份、md5 完全一致**，且与板上在跑版本一致（版本串构建时间 `Sep 26 2026 / 18:30:49`）。
- **🔙 回退（rollback，2026-10-15 定）**：若新固件不可用 → **刷回当前在飞版**：
  `firmware/BETAFPVG473-motor-af-i2c-fix/BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.hex`（同一 `dfu-util` 命令，只换文件名；**`.hex` 保留配置**）。验证：刷后 `fccli.py --cmd version` 应显示 `Sep 26 2026 / 18:30:49`、`status` 中 `GYRO/ACC=BMI270 OK`。配置若丢 → 回粘 `firmware/INAV_10.0.0_cli_20261015_full-diffall.txt` + `save`。

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
- `tools/msp_probe.c`：在 LicheeRV 上跑，验证 FC↔上位机 MSP（ATTITUDE/STATUS/RAW_IMU，LINK OK）。需交叉编译（RISC-V）。

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
- 策略：保留物理 CRSF；上位机 `MSP RC Override`（永久 ID 50）。**2026-10-12（实验20）改 aux**：`ch5`=ARM(`aux 0 0 0 1775 2100`)；`ch6>1300`→override(`aux 1 50 1 1300 2100`)；`ch8>1300`→override(`aux 2 50 3 1300 2100`，与上条 **OR**)；`ch8>900`→ANGLE(`aux 3 1 3 900 2100`，注：800 被 INAV 下限钳到 900)；**删 HORIZON**。**`msp_override_channels=7`**（只接管 roll/pitch/yaw，油门留飞行员）。**⚠️ 掩码位序 = 内部通道号 AERT（`ROLL=0 PITCH=1 YAW=2 THROTTLE=3`），非负载 AETR；`11` 会误接管油门→切油**。`failsafe_procedure`：**2026-10-15 实读确认为 `LAND`**（慢降、不返航），配置稿已同步。配置见 `drone/config/g473_msprc.txt`。

---

## 5. 当前进度与已知问题

### 5.1 ✅ 已解决
- 固件刷写 + 板级校验（BMI270）。
- 上下位机 MSP 链路打通（UART0/ttyS0@230400）。
- serial 配置持久化 bug（根因 `MAX_MSP_PORT_COUNT=3`，解法清空 UART1）。
- UART1/ttyS1 与 AIC8800 冲突诊断；释放 UART0 console。
- ✅ **"离地即翻/自旋"根因（2026-10-10）**：**电机输出编号 M1–M4 与物理接线错位**（飞控倒装 180° 重装后未重映射）→ 姿态修正发给了错误电机 → 发散（翻/自旋）。**修复**：mixer 改成匹配真实编号的"180° 轮换"版 + `motor_direction_inverted=OFF` + `align_board_roll=1800`（倒装仅靠它补传感器朝向）。**现能起飞、不再自旋**。排查法见 §5.5；基线 dump：`drone/firmware/INAV_10.0.0_cli_20261010_dirfix.txt`。
- ⚠️ **2026-10-14 飞控改回正装 → 撤销倒装补偿**：飞控**不再倒置**（正装），故 `align_board_roll` **1802→0**、mixer 由"180°轮换版"**恢复标准 QuadX**（`mmix0..3 = REAR_R/FRONT_R/REAR_L/FRONT_L`），已 `save`。**电机转向另有变动（由用户处理：ESC/物理）**；`motor_direction_inverted` 当时为 `ON`（待与电机方向一起定）。**务必先按 §5.5 拔桨复验三轴方向再飞**（不可跳）。回退值：`align_board_roll=1800` + 180°轮换版 mixer（见 dirfix dump）。
- ✅ **上位机操控下位机（2026-10-11, 实验18）**：上位机 `takeover`（HSV 绿目标→roll/pitch→`MSP_SET_RAW_RC`）经 `MSP RC Override` 接管。**纠正 `msp_override_channels` 位序坑**（见 §6）→ 改 `7` 并 `save`（`get = 7` 已确认）。**台架验证通过**：接管生效（上位机用 `MSP_RC` 回读我方发送值吻合）、**油门未被覆盖**（mask=7）、**混控 Δ 与实测 `mmix` 一致**、符号自洽。实测：`mmix`=M1:+roll+pitch / M2:+roll-pitch / M3:-roll+pitch / M4:-roll-pitch；`align_board_roll=1800`、`motor_direction_inverted=OFF`、`min_check=1100`（油门需 >1100 才出混控）、AIRMODE 已启用。真机待标定 kp/符号（另有开放问题：水平误差 roll vs yaw，见上位机实验18）。
- ✅ **接管三轴+范式/定点/定高选择（2026-10-12, 实验20）**：上位机 `paradigm` 读 `MSP_RC` 判 `ovr=(ch6>1300)||(ch8>1300)`，`ch6>1300`→范式B、`ch7`→定点/定高；**aux 已按上表改并 `save`**（`aux 0/1/2/3` 已核）。上位机 `mon`/`dry` 已验证（开关读值/模式判定/视觉符号）。**光流 MTF-02P**：`OPFLOW/RANGEFINDER=OK`，上位机经 MSP v2 读，`q≈40–60`（质量一般）。**下一步**：台架验 B追踪/定点/阻尼。

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
4. **MTF-02P 光流/测距（已在线）**：FC `status` 显示 `OPFLOW/RANGEFINDER=OK`；上位机经 **MSP v2** 读（`MSP2_INAV_OPTICAL_FLOW`/`MSP_SONAR_ALTITUDE`），实测 `q≈40–60`（**质量一般**，疑地面纹理/光照不足）。**规格**：光流**最小工作距离 8cm**、环境光 **>60Lux**、视场 42°、1m 处最大测速 7m/s、50Hz；测距 **0.02–6m（精度 2cm）**。→ 飞行高度须 **≥8cm**；质量差就换纹理地面/亮光，并标定 `opflow_scale`(内置 `MSP2_INAV_OPFLOW_CALIBRATION` 0x2032/手转30s)、`align_opflow`。手册：https://micoair.cn/zh/docs/sensors/sensors/mtf-02-02p-sensors
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

**工具**：① 自写 INAV blackbox v2 解码器 `tools/blackbox_decode.py`（读 `~/Downloads/blackbox_log_*.TXT`，出姿态/舵面/电机 + 频谱）；② 台架实时读 `tools/msp_live.py`（i5，读 `ATTITUDE/RAW_IMU/MOTOR`）/ `tools/telem_mon.c`（板上只读遥测）。详见 `../../tools/README.md`。

### 5.6 定高/定点（固件 Z 分支）计划 — 2026-10-14 定稿 / 2026-10-15 修订

**目标**：无气压计/无 GPS 时，用 MTF-02P **测距**提供高度参考，使 INAV 的 `ALTHOLD` 可用；**定高优先，定点（POSHOLD）条件重开**。定高定点全部在固件完成，上位机定高冻结（详见根 `../../agent.md` §3.5）。

**为什么先只做定高**：
- **ALTHOLD 不在 `isUsingNAVModes` 列表**（`fc/rc_modes.c:305`），**不触发 GPS-home 解锁拦截**。
- POSHOLD/RTH/WP 等会触发：解锁要求 `navigationPositionEstimateIsHealthy()`（含 `STATE(GPS_FIX_HOME)`），无 GPS 时返回 `NAV_ARMING_BLOCKER_MISSING_GPS_FIX` **拒绝解锁**（`navigation/navigation.c:6376,6392`）。
- POSHOLD 还要求 `estVelStatus == EST_TRUSTED`（即 `eph < inav_max_eph_epv`，默认 1000cm），而 `eph` 仅在 **XY 传感器（光流）更新超时**时按 100/s 增长（`navigation_pos_estimator.c:768`）→ **定点稳定性直接取决于光流健康**。

**阶段 0：光流/地面测试 — ✅ 已完成（2026-10-15，只读 `paradigm mon`）**
- **结论**：光照度不是主因，**表面纹理主导**。光滑地面 `q≈20–24`（38/50/70cm 均如此，**低于门限 35 → 光流永不启用**）；有纹理面（鼠标垫/纹理垫）`q≈37–53`（29–61cm，**≥35 → 可启用**）。同架同静止只换面：21 → 39。
- 强光直射反而更差（`q≈20`，疑高光过曝）；`q` 随运行时间缓慢上升（上电后稍等数秒更稳）。
- 判定口径（源码）：`q` 为 0–100 标度；`sensors/opflow.c` `SQUAL_HIGH=35`（进入）/`SQUAL_LOW=10`（退出，滞回）。
- 运动测试：手移主要由 **fY** 轴拾取（86→347），输出量化步进≈43，`opflow_scale` 未标定。
- **操作性结论**：测试/飞行区铺**纹理地垫（鼠标垫即可）+ 高度 0.3–0.6m + 上电等数秒** → POSHOLD 的“光流健康”前提**可满足**（仍受无 GPS 解锁拦截限制）。
- **门限判定**：✅ 达标 → **POSHOLD 可（在解决解锁拦截后）重开**。

**接管与油门**
- 一期：`mask=7`，飞行员扶油门中位试定高。
- 二期：`mask=15`，上位机发中性 1500（**解锁/上锁时仍须发低位**，否则解锁条件不满足）。
- `althold_throttle_type`：**先 `STICK`**（进入时油门值即“保持点”，对固定输入最可控）。

**改动点（Z 分支）**
- `navigation/navigation_pos_estimator.c` 的 `estimationCalculateCorrection_Z`：气压计与 GPS 均无效、测距有效时，用 `surface.alt` 修正 `est.pos.z`，权重复用 `w_z_surface_p/v`；同步更新 epv，使 `EST_Z_VALID` 可收敛/失效。
- 解锁时记录地面零点；测距超时/可靠度低/超 `max_surface_altitude` 时 Z 失效并回落；不动气压计/GPS 分支。
- **先验符号/坐标系**（`DEBUG_ALTITUDE`）——WP1 第一件事。

**失效与安全**
- `failsafe_procedure = LAND`（慢降），**不返航**；**“失联悬停”不采用**。
- `ch5`=锁桨（唯一人工急停）；`ch8`=接管（高区间启用 `OVERRIDE+ANGLE+ALTHOLD`；POSHOLD 视阶段 0 结论）。
- **看门狗（无人飞行前必做）**：上位机“控制循环心跳+监督线程”，超时 → 停发 RC（借 FC failsafe 落地）。

**剩余只读核实**：紧急降落是否依赖高度估计。

**✅ 实机只读核实（2026-10-15，FC 经 USB VCP，未改任何配置）**：
- `failsafe_procedure = LAND`（确认；配置稿已同步）
- `msp_override_channels = 7`；`nav_extra_arming_safety = ALLOW_BYPASS`；`nav_mc_althold_throttle = STICK`
- `inav_max_eph_epv = 1000cm`；`inav_max_surface_altitude = 200cm`（**限制测距 Z 分支可用高度 0–2m，必要时上调，上限 1000**）
- `inav_w_z_surface_p/v = 3.5 / 6.1`；`inav_w_xy_flow_v = 2.0`；`nav_mc_hover_thr = 1300`
- ⚠️ **`inav_reset_altitude = FIRST_ARM` 不会直接清零 `est.pos.z`**（它只重置气压计/GPS 参考）。地面零点在**Z 分支内实现**（offset 解锁前跟随、解锁后冻结），其**时机**由该设置决定（需要每次解锁重置则改 `EACH_ARM`）。
- **`inav_allow_dead_reckoning = OFF`** → **POSHOLD 的光流 XY 位置修正需要它 = ON**（`navigation_pos_estimator_flow.c` 仅 GPS 或 dead-reckoning 才做位置修正）
- 传感器：`RANGEFINDER=OK, OPFLOW=OK`；`BARO/GPS/MAG=NONE`
- 其他：PID 环 ~1988Hz（cycle 503us）、System load 14、Heap 余量 1668B、Stack 顶 0x10008000（= CCM 实际到 32K 的旁证）、配置占用 10486/16384

**里程碑**：M1 拔桨比对 `est.pos.z`（0.2/0.5/1.0m）→ M2 绑绳 ALTHOLD 悬停 → M3 遮挡验失效 → M4 拔串口验 LAND → M5 短飞；定点 M6 视阶段 0 结论。

### 5.7 Z 分支实现状态 — 2026-10-15 实现 / 10-16 刷写 / 10-17 台架首验通过

**工作树**：新建 **`drone/inav-g4alt/`**（从 `inav-g4dbg/` 复制、去除 build；**原 `inav/`、`inav-g4dbg/` 保持未改动**，可随时回退）。注意：rsync 排除 `build` 时须用锚定 `/build`，否则会误删 `src/main/build/`（踩过）。

**改动（3 文件）**
- `navigation_pos_estimator_private.h`：`navPositionEstimatorSURFACE_t` 加 `updateDt`；新增 `INAV_SURFACE_DEFAULT_EPV 50.0f`。
- `navigation_pos_estimator_agl.c`：`updatePositionEstimator_SurfaceTopic` 记录 `surface.updateDt`。
- `navigation_pos_estimator.c`：`estimationCalculateCorrection_Z` 新增“测距 Z 分支”（条件：气压计与 GPS-Z 均无效 + `EST_SURFACE_VALID` + reliability≥0.33）；`maxUpdateDt`/清零纳入 `surface.updateDt`。
  - 零点：`shouldResetReferenceAltitude()` 为真时用**滤波均值**更新 offset，解锁后冻结；Z 无效时直接吸附初始化（`pos.z = 测距-offset`、`vel.z=0`、`epv=INAV_SURFACE_DEFAULT_EPV`）。
  - 修正：P/V 复用 `w_z_surface_p/v`，按 `bellCurve`（50cm 窗）+ reliability 加权；`epv` 用测距残差更新。增益结构与 INAV 自带 AGL 估计同构（每秒等效增益一致）。
  - 未动气压计/GPS 分支；无气压计时该分支成为唯一 Z 源。

**构建**（i5，arm-none-eabi-gcc 16.2.0）：`cmake -B build -G Ninja -DCOMPILER_VERSION_CHECK=OFF` + `ninja -C build BETAFPVG473`
- 产物：`inav-g4alt/build/bin/BETAFPVG473.elf`、`inav-g4alt/build/inav_10.0.0_BETAFPVG473.hex`（刷写地址 `0x08000000`）
- 体积：`FLASH1 458410B/480KiB = 93.26%`（+296B）、`RAM 88756B`（+12B）、`CCM 11872B`

**⚠️ 行为变化（回归排查结论）**
- `estAglStatus`: `EST_NONE`→`EST_USABLE`（Z 由无效变有效所致）。消费者多在 `== EST_TRUSTED` 门控 → 本机不触发；`getLandAltitude()` 仅固定翼使用 → 无影响。
- 多旋翼降落检测：`safetyTime` 5000→1000ms（因 Z 有效）、`verticalSpeedSettled` 改用真实垂速 → **台架重点观察**（涉及 LAND 失效与自动上锁）。
- `MSP_SONAR_ALTITUDE` 返回**测距原始值**（非 `agl.pos.z`）→ **上位机 `alt=` 不受影响**（已核实）。
- 待改进（P2，落地阶段再议）：`aglQual` 保持 LOW → 触地检测少一层“距地 <5cm”确认。

**✅ M1 台架首验通过（2026-10-17，拔桨解锁状态）**
- 条件：ch5=2000 解锁（`T≈1006~1017` = 油门到底）、ch8=1000、ch7=1000；上位机 `paradigm mon`（已加 `MSP_ALTITUDE(109)` 显示 `z/vz`）。
- 结果：**z 全程跟随 alt（误差 0~3cm），3~83cm 均成立；静止时 `vz` 收敛到 0**。
- 机制验证：**未解锁前 z 被钉在 0**（零点持续跟随，`shouldResetReferenceAltitude()`）；**解锁瞬间零点冻结后 z 正常跟踪** —— 与实现预期完全一致（这也是"必须先解锁才能测 z"的原因）。
- 已知动态特性：快速手动升降时 z 滞后/微超调（1–2s 收敛、不发散）——有限权重修正的正常二阶响应。
- 已知盲区：**在平地外做水平平移**（如从桌边平移到地面）测距会阶跃 → z 出现"假高度"（`bellCurve` 50cm 窗降权 0.1× 减慢收敛、不能消除）；平地飞行无影响。
- 待做：**M3 遮挡验失效**（`EST_SURFACE_VALID` 掉后 z 漂移行为）；M2 绑绳/飞行验证。

**FC 配置（方案 A，2026-10-16 写入并 save）**：`aux 4 3 2 1300 2100`（ch7≥1300 → NAV ALTHOLD；现阶段"中位即定高"）；后续拆分为 `[1300,1700)`=定高 / `[1700,2100)`=定点（需先解 POSHOLD 三门）。

**✅ 首次定高试飞（2026-10-17，黑匣子 `~/Downloads/第一次定高.TXT`，5 段）**
- **段1（12.5s，纯 ANGLE）**：起飞→悬停≈12s，姿态 ±2°、电机 ~1600，正常收尾；**电机长期不对称**（m0 恒低 ~50，疑 CoG/校准）→ 与"水平漂"相关。
- **段4（15.5s，ANGLE+NAV_ALTHOLD）**：`flightModeFlags=10`（bit1=ANGLE｜bit3=NAV_ALTHOLD）→ **定高真实挂上并飞行**；`navFlags=1`（bit0=`estAltStatus==EST_TRUSTED`）→ **Z 分支在飞行中给出可信高度**；测距爬到 ~1.66m；**结尾 ~14.4s 翻滚坠机**（roll→70°、m1 饱和 2000、油门命令顶满但高度反降 → 疑撞击/失控，待归因）。
- **黑匣子字段口径（踩坑记录）**：`mspOverrideFlags` bit0 = **MSP 接管数据流的 failsafe**（`rx/msp_override.c` 本地静态，**不是物理 RX 失控**！）bit1=接管激活；`navState`：1=`NAV_CTL_ALT`、3=`ALT|POS`；`navFlags`：bit0=alt TRUSTED / bit1=AGL / bit2=pos / bit3=terrain-follow / bit5=heading；`flightModeFlags`：bit1=ANGLE、bit3=NAV_ALTHOLD。
- **待办**：坠机归因；"灵敏度高（轻打杆大幅动作，用户 1a）+ 水平漂（2）"→ 取干净悬停段黑匣子分析 (gyro/PID/rates) + CoG 配平。

**未做**：方案 B（给 `navReadyAltControl` 补测距分支，让 ALTHOLD 进 Configurator 列表）；`inav_reset_altitude` 改 `EACH_ARM`（可选）。

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
16. **板载无气压计 → `ALTHOLD`/`POSHOLD` 当前不可用；已决定改固件加“测距 Z 分支”解锁（2026-10-14）**：INAV 模式准入看**估计器状态**——`canActivateAltHoldMode()` 要求 `estAltStatus >= EST_USABLE`，`canActivatePosHoldMode()` 还要求 `estVelStatus == EST_TRUSTED`（`navigation/navigation.c:6020-6028`）。而 `est.pos.z` 的修正只在**气压计/GPS** 分支（`navigation_pos_estimator.c` `estimationCalculateCorrection_Z`）；测距只进 AGL 估计，且 AGL 还要求 `EST_BARO_VALID`（`navigation_pos_estimator_agl.c:83`）。故无气压计/GPS 时 Z 无法收敛、模式进不去。`baro_hardware=AUTO` 开机检测不到会被写回 `NONE`（`sensors/initialisation.c:87`）。**⚠️ 更正**：旧文引用的 `navReadyAltControl`（`fc_msp_box.c:232`）在 INAV 10 源码中**不存在**，以上述估计器准入为准。计划见 §5.6。
17. **串口设 GPS 但无 GPS 模块 → 无效**；且**别动 UART2=上位机 MSP**（会破坏链路）。本板 UART 已排满（UART1 仅RX / UART2 上位机MSP / UART3 CRSF / UART4 光流MSP）。
18. **`msp_override_channels` 位序 = AERT（内部通道号），不是 AETR**：INAV 内部 `ROLL=0 PITCH=1 YAW=2 THROTTLE=3`（`fc/rc_controls.h`），掩码 `1<<channel`（`rx/msp_override.c:269`）；而 `SET_RAW_RC` 的**负载**顺序才是 AETR（`rcmap={0,1,3,2}`）。故"只接管三轴、油门留飞行员"= **`7`(0111)**；**`11`(1011) 会接管 roll/pitch/throttle 而放开 yaw → payload 油门(1000) 覆盖物理油门 → 空中切油**。Betaflight 掩码则是 AETR（bit2=throttle），**两家不一致，别照搬**。可用上位机实验18/20 `probe`/`MSP_RC`(105, 回读 override **之后**的通道) 确认是否真接管（**别用 `MSP_ACTIVEBOXES` 位掩码**：INAV 按"下发顺序"打包，非 box 枚举号）。
19. **无 GPS 时配置 POSHOLD/RTH/WP 会拦截解锁（2026-10-15 核实）**：`isUsingNAVModes` 只要配置了 **POSHOLD/RTH/COURSEHOLD/CRUISE/WP** 任一开关即为真（`fc/rc_modes.c:305`）；解锁要求 `navigationPositionEstimateIsHealthy()`＝`estPosStatus>=EST_USABLE && estAltStatus>=EST_USABLE && STATE(GPS_FIX_HOME)`（`navigation/navigation.c:6376`），否则返回 `NAV_ARMING_BLOCKER_MISSING_GPS_FIX`（`:6392`）。无 GPS 时 `GPS_FIX_HOME` 依赖“解锁前由光流等建立 home”（要求 `estPosStatus>=EST_USABLE`）。**⚠️ ALTHOLD 不在该列表 → 定高不受影响**——这正是“先定高、定点后置”的原因。另：`eph` 仅在 XY 传感器更新超时时按 100/s 增长（上限 `inav_max_eph_epv`＝1000cm），**光流健康是定点稳定的前提**。`nav_extra_arming_safety=ALLOW_BYPASS`（默认）可用 YAW_HI 杆位临时绕过（无飞手时不可用）。
20. **SURFACE（地表跟随）本板不可用——两处硬依赖气压计（2026-10-17 核实）**：① 模式盒广告条件 `sensors(SENSOR_BARO) && sensors(SENSOR_RANGEFINDER)`（`fc/fc_msp_box.c:226`）；② 地形跟随时高度控制器改读 `posControl.actualState.agl`（`navigation/navigation.c`：`terrainFollow ? agl : abs`），而 AGL 估计要求 `EST_BARO_VALID`（`navigation_pos_estimator_agl.c:83`）→ 无 baro 时 `aglAlt` 恒 0。**定高结论：用 ALTHOLD（Z 分支），平地场景与 SURFACE 等价**。
21. **解锁排查速查（2026-10-16 实测踩坑）**：`status` 的 `Arming disabled flags` 是权威判据——`ARMSW`=ch5 不在解锁位（需 ≥1775，实测开关高档=2000）；**`THR`=油门杆必须收到最低（≈1000，非中位）**；`DSHOTBEEPER`=上电初期蜂鸣器瞬态（几秒自消）；`CLI`=USB CLI 会话中禁解锁。另：**未解锁时零点持续跟随测距 → `MSP_ALTITUDE` 的 `z` 恒≈0**（`WAS_EVER_ARMED` 前速度不积分 + 零点跟随，见 §5.7）→ **验证 z 必须"先解锁"**。TX 未开机时通道读数为失控保护值（ch5 曾见 1750，勿据此调区间）。

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
