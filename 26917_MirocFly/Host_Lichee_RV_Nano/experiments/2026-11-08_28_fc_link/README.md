# 上位机 ↔ 飞控 链路（MSP）— 2026-11-09 实测通过

> 结论：**链路已通**。上位机 `/dev/ttyS0` @ **230400** ↔ 飞控 MSP 口，
> `MSP_RC / ACTIVEBOXES / BOXIDS / ATTITUDE` 全部 20/20 应答。

## 1. 实测到的关键事实（务必记住）

### 1.1 飞控串口映射（LYIF722DUAL + INAV 10.0.0）

```
serial 20 1 115200    # USB VCP → MSP
serial 0  1 115200    # UART0 → MSP
serial 1  0 115200    # 未启用
serial 2  1 230400    # ★ UART2 → MSP @230400（上位机接这里）
serial 3  64 115200   # UART3 → RX_SERIAL（CRSF 接收机口，当前无接收机）
serial 4/5 0          # 未启用

receiver_type = SERIAL
```

**坑**：板子丝印的"串口3"≠ INAV 的 UART3。我们实际接的是 **INAV UART2（MSP）**。
功能码：`1`=MSP、`64`=RX_SERIAL、`2`=GPS、`128`=BLACKBOX。

### 1.2 波特率必须一致，否则"完全静默"

| 上位机 | 飞控 | 结果 |
|---|---|---|
| 115200 | 230400 | ❌ 零应答（不是乱码报错，是**什么都收不到**） |
| **230400** | **230400** | ✅ 全部应答 |

飞控 MSP 口上限 230400，所以上位机必须 230400。

### 1.3 `/dev/ttyS0` 是内核控制台，但仍然可用

```
/proc/cmdline: console=ttyS0,115200 earlycon=sbi
```

- 内核日志会从这根线输出，理论上会污染 MSP 帧；
- 实测 `loglevel=0` 下影响很小，**230400 下工作正常**；
- 若出现不稳定，改用 `ttyS1..ttyS3`（设备树里 `&uart1/2/3 status="okay"`）。

### 1.4 MSP box 映射（重要，之前搞错过）

```
BOXIDS(119)  →  32 个字节，每个字节 = 第 i 位对应的永久 ID (pid)
ACTIVEBOXES(113) → 8 字节位图，第 i 位 = 第 i 个 box 是否激活
```

本板实测（2026-11-09）：

| bit | pid | 含义 |
|---|---|---|
| 0 | 0 | ARM |
| 3 | 1 | ANGLE（当前激活） |
| 12 | 3 | NAV ALTHOLD |
| 17 | 27 | AUTOTRIM（**意外激活，需排查**） |
| **20** | **41** | **MSP RC OVERRIDE ← 授权门** |
| 24 | 50 | CHANGEMISSION（与授权无关） |

**源码依据** `src/main/fc/rc_modes.h`：
```c
BOXMSPRCOVERRIDE = 41
BOXCHANGEMISSION = 50
```

> ⚠️ 旧板（G473）当时用的是 pid 50 —— 那是误判，本板必须用 **41**。

## 2. 工具：`msp_link`

```bash
# 常规自检（读 RC / 模式位）
./msp_link.rv --dev /dev/ttyS0 --baud 230400 --n 20 --send-rc

# 探测固件支持哪些 MSP 命令
MSP_PROBE=1 ./msp_link.rv --dev /dev/ttyS0 --baud 230400

# 统计每个命令的应答成功率
MSP_RATE=1 ./msp_link.rv --dev /dev/ttyS0 --baud 230400

# 回环测试（把该串口 TX/RX 短接，验证板端 UART + 线）
./msp_link.rv --dev /dev/ttyS0 --loopback
```

## 3. 调试过程中修掉的 bug（值得记住）

| # | Bug | 现象 | 修法 |
|---|---|---|---|
| 1 | **只读一次** | `MSP_RC` 40 字节帧被拆成两次 read → 解析不完整 → 0 应答；而 14 字节的 `ACTIVEBOXES` 正常 | 改成"循环读到匹配帧或超时" |
| 2 | 失败后解析器失步 | 一条命令失败后，后面全部失败 | 每次请求前 `tcflush` + `msp_parser_init` + 重试 3 次 |
| 3 | 手写结构体缺对齐 | 150 vs 152 字节，接收端全部丢弃 | 用 `vision_iface.h`，不手写 |
| 4 | 硬编码 pid 50 | 授权永远读不到 | 改为 `MIROCFLY_OVR_PID`（默认 41） |

## 4. 现在的状态

```
✅ 上位机 → 飞控：MSP 请求/应答 20/20
✅ 飞控 → 上位机：RC(17 通道)、ACTIVEBOXES、BOXIDS、ATTITUDE、ALTITUDE
✅ vision_bridge 能读真实飞控状态（AUTH/gate/arm/ch6 均正确）
❌ 授权门验证：需要拨 ch8，但当前**没有接收机**
```

## 5. 下一步（无接收机方案）

台架模式：让飞控把上位机当接收机

```
set receiver_type = MSP
save
```

之后上位机发的 ch5/ch6/ch8 就是"物理 RC"，可完整验证解锁/模式/授权门。
**飞行前必须改回** `set receiver_type = SERIAL`。
