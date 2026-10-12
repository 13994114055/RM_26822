# tools/ — MirocFly 调试/分析工具集

> 本项目（上位机 LicheeRV ↔ 下位机 BetaFPV G473/INAV）调试、联调与复盘用的**独立小工具**。
> 与实验代码解耦；多数只在 **i5** 或 **LicheeRV** 上直接跑。中文注释。

| 工具 | 运行位置 | 作用 | 来源 |
|---|---|---|---|
| `msp_probe.c` | LicheeRV（交叉编译） | **MSP 链路验证**：向 FC 的 MSP 口发 `STATUS/ATTITUDE/RAW_IMU` 请求并读响应，收到帧即 `LINK OK`。回环自测：板上短接 A16↔A17 应读到自身帧。 | 下位机对接阶段（原 `new_mcu/connection/`） |
| `msp_live.py` | i5（本机 python3） | **MSP 实时监控**（经 FC USB VCP）：读 `ATTITUDE/RAW_IMU/MOTOR/RC` 并打印，用于拔桨方向自检/地面观察。 | 本会话（2026-10-10） |
| `telem_mon.c` | LicheeRV（交叉编译） | **板上只读遥测**：读 FC `ATTITUDE/RAW_IMU`，**只发读请求、不发任何控制**（飞行时旁路监控）。 | 本会话（2026-10-10） |
| `blackbox_decode.py` | i5（本机 python3） | **INAV blackbox v2 解码器**：解析 `.TXT` 日志 → 姿态/角速度/舵面(`axisP/I/D`)/电机/RC 轨迹 + 频谱/CSV，用于自稳/振荡/漂移/"翻-自旋"复盘。纯 stdlib。 | 本会话（2026-10-10） |

---

## 用法

### blackbox_decode.py
```bash
python3 blackbox_decode.py <log.TXT>                 # 默认输出帧数最多的一段: 概览+轨迹
python3 blackbox_decode.py <log.TXT> --list          # 只列各段帧数
python3 blackbox_decode.py <log.TXT> --index 0 --csv seg0.csv
python3 blackbox_decode.py <log.TXT> --spectrum gyroADC[0]   # 主振荡频率
```
- 输出字段名与 blackbox 头 `Field I name` 一致（`loopIteration,time,axisRate[..],axisP[..],axisI[..],axisD[..],axisF[..],gyroADC[..],accSmooth[..],attitude[..],motor[..],rcCommand[..],...`）。
- `attitude` 单位为 **0.1°**（打印时已 /10）；`gyroADC` 为原始单位。
- 段 = 每次 `H Product:Blackbox` 重新开始（多次上电/arm 会是多段）。

### msp_live.py
```bash
python3 msp_live.py /dev/ttyACM0 30      # 读 30 秒
```
- 需 FC 的 USB VCP 空闲（**关掉 INAV Configurator**，它独占 `/dev/ttyACM0`）。

### telem_mon.c（编译到板上）
```bash
TC=<LicheeRV-Nano-Build_official>/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
${TC}gcc -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -Os -o telem_mon telem_mon.c -lm
scp telem_mon root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 '/root/mirocfly/telem_mon /dev/ttyS0 230400'   # 只读, 不影响飞行
```

### msp_probe.c（编译到板上）
```bash
${TC}gcc -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -Os \
  -I../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link \
  -o msp_probe msp_probe.c ../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link/msp.c -lm
ssh root@10.222.2.1 '/root/mirocfly/msp_probe /dev/ttyS0 230400'   # 期望 TOTAL frames received = N -> LINK OK
```

---

## 关键前提/坑（与工具相关）
- **FC 处于 CLI 模式时，UART2 的 MSP 不响应**：用 USB 进 CLI 后需 `save`（重启）或 `exit`，再等 ~15–20s 才能用 `msp_probe`/`telem_mon`（否则误判"链路断"）。
- **相机程序互斥**：`telem_mon`/`msp_probe` 占 `/dev/ttyS0`（UART0）与相机无关；但**相机类程序（frame_grab/rtsp_grab/green_*/tdl_app）同时只能跑一个**。
- **Configurator 独占 `/dev/ttyACM0`**：跑 `msp_live.py` 前先关闭它。
- **blackbox 日志**：平时在 FC 的 flash 里；用 INAV Configurator 的 Blackbox 页 `Download` 导到 i5（`~/Downloads/blackbox_log_*.TXT`）再解析。

## 相关文档
- 上位机：`../Host_Lichee_RV_Nano/AGENT_GUIDE.md`
- 下位机：`../Low_MCU/new_mcu/AGENT_GUIDE.md`、`../Low_MCU/new_mcu/connection/README.md`
- AI 上下文：`../agent.md`
