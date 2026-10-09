# 下位机对接（connection）— BetaFPV G473 ↔ LicheeRV Nano

> 本文件是**上下位机对接方案**：接口映射、INAV 配置、接线、验证与测试计划。
> 上位机侧代码在 `../../../Host_Lichee_RV_Nano/`（本方案**不改上位机代码**，仅约定串口）。
> 状态：🟡 方案定稿（截至 2026-09-30）；**硬件接线与联调待执行**。

---

## 1. 目标
让 LicheeRV Nano（上位机，视觉/决策）通过 **MSP** 向下位机（BetaFPV G473，INAV 10.0.0）注入 RC；物理 CRSF 遥控器优先、可用拨杆切换自主/手动；并为光流计 MTF-02P 预留接口。

## 2. 接口映射（定稿）

| 用途 | G473 口 | 引脚 (TX/RX) | 对端 | 波特率 | INAV `serial` 端口号 |
|---|---|---|---|---|---|
| **上位机 MSP** | UART2 | PA2 / PA3 | LicheeRV **UART0** = `/dev/ttyS0`（引脚 **A16/A17**） | **230400** | `serial 1` |
| **CRSF 接收机** | UART3 | PB10 / PB11 | 物理遥控接收机 | auto (CRSF) | `serial 2` |
| **光流 + 测距** | UART4 | PC10 / PC11 | MTF-02P | **115200** | `serial 3` |
| 配置/调试 | USB VCP | — | i5（USB） | — | 始终 MSP |

**端口号口径**（源码 `io/serial.h`）：`SERIAL_PORT_USART1=0, USART2=1, USART3=2, USART4=3`；USB VCP=20。
**函数位**：`FUNCTION_MSP=1`、`FUNCTION_RX_SERIAL=64`。

> ⚠️ 说明：`FUNCTION_OPTICAL_FLOW`(=16384) 只给 **CXOF** 光流用；**MTF-02P 走 MSP**（见第 5 节），所以 UART4 配 **MSP(1)**，不是 16384。

## 3. 物理接线（已实测打通 2026-10-09）
```
G473 UART2 TX(PA2)  ──>  LicheeRV UART0 RX (A17)
G473 UART2 RX(PA3)  <──  LicheeRV UART0 TX (A16)     （TX↔RX 交叉！接反 = 不通）
GND                 ───  GND                         （必须共地）
电平：两侧均 3.3V
UART4：MTF-02P 模块接 G473 UART4（TX/RX/GND，供电按模块要求）
UART3：CRSF 接收机接 G473 UART3
```
> G473 的 UART 焊盘以**板子丝印/BetaFPV 文档**为准。LicheeRV 用 **2×14 排针**：
> **UART0 = A16(TX, 第18脚) / A17(RX, 第19脚) → `/dev/ttyS0`**。
>
> ⚠️ **为什么不用 UART1/ttyS1**：LicheeRV 的 **UART1 引脚（A18/A19/A28/A29）与板载 Wi-Fi/BT 芯片 AIC8800D 共用**（原理图 `BT_RTX/BT_CTS/BT_TXD/BT_RXD`，`hci0 Bus: SDIO`）→ 冲突。故改用干净空闲的 **UART0**。
> ⚠️ **UART0 默认是调试 console**：需**释放 getty**（本实验已把 `/etc/inittab` 的 `sole::respawn:/sbin/getty -L console ...` 行注释掉；内核 `console=ttyS0` 仍保留，但 `loglevel=0` 基本不打印）。释放后 `/dev/ttyS0` 才可被 MSP 使用。
> ⚠️ **波特率 = 230400**：G473 的 UART2 实测上限 230400（更高不通）。

### ttyS ↔ UART 对应（SG2002）
`/dev/ttyS0`=UART0(0x04140000)、`ttyS1`=UART1(0x04150000)、`ttyS2`=UART2(0x04160000)、`ttyS3`=UART3(0x04170000)。
判定：`cat /proc/tty/driver/serial`（每行 `mmio:` 基址）；或设备树别名 `ls /proc/device-tree/aliases/`。

## 4. INAV CLI 配置
见可粘贴批处理：`../drone/config/g473_msprc.txt`。要点：
- **`serial 0 0 ...`（UART1 = 清空）—— 必须！** 见下方"⚠️ MSP 端口数上限"。
- `serial 1 1 230400 ...`（UART2 = 上位机 MSP，**实测上限 230400**）
- `serial 2 64 ...`（UART3 = 串口接收机 CRSF）+ `set receiver_type = SERIAL`、`set serialrx_provider = CRSF`
- `serial 3 1 115200 ...`（UART4 = MSP，给 MTF-02P）
- `set msp_override_channels = 15`（前 4 路 AETR 可被 MSP 覆盖）
- `aux 0 50 3 1700 2100`（见下）
- `set opflow_hardware = MSP`、`set rangefinder_hardware = MSP`
- `set failsafe_procedure = DROP`

### ⚠️ MSP 端口数上限（踩坑记录，务必先清空 UART1）
INAV 限制 **最多 3 个 MSP 端口**（`MAX_MSP_PORT_COUNT=3`，见 `io/serial.h`）；
启动时 `validateAndFixConfig()` 调 `isSerialConfigValid()`，**超限则 `pgResetCopy` 把整个 serial 配置重置为默认**（其它 PG 不受影响）。
- 本板**默认 VCP + UART1 已占 2 个 MSP**；若直接再加 UART2(上位机)+UART4(光流) = **4 个 > 3** → 保存后重启 serial 全部回默认（现象像"存不住"）。
- 迷惑点：单独加 UART2 或单独加 UART4（=3 个）能存，两个一起（=4）就回默认。
- **修复**：配置里加 **`serial 0 0 ...` 清空 UART1**（UART1 只有 RX、接不了外部硬件，本就用不了）→ MSP = VCP+UART2+UART4 = 3 个，合规。
- 定位手段：用 `dfu-util -a 0 -s 0x08004000:0x4000 -U cfg.bin` 读 flash → 证实配置**保存正确、CRC 有效**，从而把锅定位到"启动加载校验"而非"保存"。

### 4.1 `aux` 与拨杆（RC 优先 / 硬件兜底）
`aux <槽位> <模式永久ID> <AUX序号> <起始> <结束>`：
- `MSP RC OVERRIDE` 的模式**永久 ID = 50**（不是枚举 41）。
- **ch8** → AUX 序号 = **3**（AUX1=ch5=0…AUX4=ch8=3）。
- `1700 2100` → 脉宽落在 **[1700, 2100) 时启用** → **上位机接管**；区间外 → 模式关闭 → **听物理接收机**。
- ⚠️ **右端开区间**：判断是 `ch >= 1700 且 ch < 2100`。开关高位通常读 ~2000（在内）；若你的开关高位正好到 2100 则不触发 → 上电后在 Receiver 页看 ch8 实际值再微调。

### 4.2 上限/下限（源码口径）
`CHANNEL_RANGE_MIN=900, MAX=2100, STEP=25`；必须 `900 ≤ 值 ≤ 2100`，且 `起始 < 结束`。

## 5. MTF-02P 光流 + 测距（走 MSP）
- MTF-02P 是 **MSP 光流/测距设备**（此前接 AT32 即走 MSP 并能显示数据）。
- INAV 原生支持：`opflow_hardware = MSP` 解析 `MSP2_SENSOR_OPTIC_FLOW`(`{quality,motionX,motionY}`)，
  `rangefinder_hardware = MSP` 解析 `MSP2_SENSOR_RANGEFINDER`(`{quality,distanceMm}`)（源码 `fc_msp.c: mspProcessSensorCommand`）。
- 因此 UART4 配 **MSP** 即可，MTF-02P 会同时上报光流与测距。
- `opflow_scale`/`align_opflow` 需**实飞标定**（先留注释）。

## 6. 上位机侧（已实测）
- 串口：LicheeRV **`/dev/ttyS0` @230400**（UART0，引脚 A16/A17）。
- 现有程序：`Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link/`（`msp_test`）、`.../08_vision_control/`（`green_fly`）。
  真硬件命令形如：`./msp_test inject /dev/ttyS0 230400 10` / `monitor /dev/ttyS0 230400`。
- **前提**：LicheeRV 的 **UART0 console 已释放**（`/etc/inittab` 注释 console getty）；`/dev/ttyS0` 可被 MSP 打开。
- 实测工具：`connection/msp_probe`（发 MSP 请求读响应）+ `fccli.py`（经 USB VCP 配 FC）。

## 7. 接线前验证清单（硬件到位时逐条过）
1. **LicheeRV `/dev/ttyS0`(UART0, A16/A17) 可用**：先**释放 console getty**，再**回环**（A16↔A17 短接，用 `msp_probe /dev/ttyS0 <baud>` 读到自身帧）确认。
2. **G473 UART2/3/4 焊盘**按丝印核实。
3. **共地 + 3.3V 电平 + TX↔RX 交叉**。
4. FC 插 i5 → 出现 `/dev/ttyACM0` → `picocom /dev/ttyACM0` 进 CLI → 粘 `g473_msprc.txt` → `save`（**拔桨**）。
5. 校准：加计/罗盘（如需）；设置 arming。
6. 量 ch8 脉宽，确认 `1700–2100` 触发。
7. 验证拨杆：切换时 OSD/Configurator 显示 `MSP`↔`STD`；上位机停发 → 回落接收机（failsafe）。

## 8. 测试计划（安全渐进）
**拔桨**（跑通 MSP 收发 + 拨杆切换 + failsafe）→ **绑绳** → **短飞**。
上位机侧用 `msp_test monitor /dev/ttyS1` 回读 `ATTITUDE/RAW_IMU`，`inject` 验证注入。

## 9. 待办 / 后续
- 光流 `opflow_scale`、`align_opflow` 标定；确认 MTF-02P 测距量程/单位。
- 撞击检测（上位机轮询 `MSP_RAW_IMU` 或视觉 bbox）与 30s 返航（无 GPS，光流辅助）。
- 去除 INAV 原生 crash detection 干扰（如涉及）。

## 参考
- 下位机总览：`../README.md`；协议历史：`../../archive_at32/agent_at32.md`
- 上位机 MSP 链路：`../../../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link/`
- INAV 源码：`../drone/inav/`（`io/serial.h`、`fc/fc_msp.c`、`rx/msp_override.c`、`io/opflow_msp.c`）
