# 实验 24：控制端（L1~L4 + 状态机）

> 2026-11-03 建，2026-11-04 第一版跑通（i5 + mock）。
> 上位机"视觉 → 控制 → MSP"的主程序；视觉检测由同学提供（接口 `vision_iface.h` 已冻结）。

## 架构

```
L0  VIO（占位）    MAVLink ODOMETRY ──→ 高度/位置（接口已留，解析待接）
L1  视觉输入       UDP :7788 ──→ vif_frame_t（同学 / mock_vision）
L2  目标处理       选目标（锁定/迟滞）→ 丢检保持 → EMA 平滑
L3  控制映射       偏移×kp + bias → 死区 → 限幅 → roll/pitch
L4  执行+权限      查 ACTIVEBOXES 授权 → 授权才发非中性 → MSP SET_RAW_RC
```

**两条铁律**：
1. **单一出口**：`send_rc()` 只在主循环末尾调用一次；
2. **未授权/断流/丢目标 → 中性**，绝不"带着旧指令跑"。

## 状态机（当前 mask=7，只做 XY）

```
INIT → IDLE ──目标出现──→ ALIGN ──对准 0.5s──→ APPROACH ──面积>阈值──→ COMMIT
         ↑                    │                    │                      │
         └──── 真丢失 ────────┘                    │              面积跳变/IMU>3g
                                                   └── 超时 ──→ RECOVER ←──── IMPACT
                                                                   │
                                                                  DONE
任意状态：AUTH_OFF（飞手收回授权）→ ABORT
```

| 状态 | 进入动作 | 每拍动作 | 退出条件 | 超时→ |
|---|---|---|---|---|
| INIT | — | 立刻进 IDLE | — | — |
| IDLE | 发中性 | 等目标 | EV_TARGET | — |
| ALIGN | 发中性 | 对准（限幅 ±300µs） | \|偏移\|<60px 持续 0.5s | 8s→IDLE |
| APPROACH | — | 对准 + 前推 | 面积 > `A_NEAR` | 10s→RECOVER |
| COMMIT | — | 保持上一拍（闭眼冲） | 面积 > `A_IMPACT` 或 IMU>3g | 1.5s→RECOVER |
| IMPACT | **立刻中性** | — | — | 1s→RECOVER |
| RECOVER | 发中性 | 等姿态稳 | 2s | →DONE |
| DONE / ABORT | 发中性 | — | — | — |

## 事件（谁产生）

| 事件 | 产生者 | 触发 |
|---|---|---|
| `EV_TARGET` | L2 目标处理 | 本帧有目标（含保持期） |
| `EV_LOST` | L2 | 首次缺失 |
| `EV_IMPACT` | L3/L4 | 面积 > `A_IMPACT`，或 IMU 模长 > 3g |
| `EV_AUTH_OFF` | L4 门控 | AUTH 从 1 变非 1 |

## 参数（env，`MIROCFLY_` 前缀）

| 变量 | 默认 | 含义 |
|---|---|---|
| `KP` | 0.5 | 像素 → µs 增益 |
| `ALPHA` | 0.35 | EMA 系数（≈95ms@30fps） |
| `HOLD` | 12 | 丢检保持帧数（≈400ms@30fps） |
| `MAXDELTA` | 300 | 摇杆最大偏离 |
| `BIAS` | 0 | 垂直基准（12° 安装标定值） |
| `DEADBAND` | 8 | 死区（px） |
| `ROLLSIGN` / `PITCHSIGN` | +1 / −1 | 方向符号 |
| `CLS` / `MINCONF` | 0 / 40 | 目标类别 / 最低置信度 |
| `TOL_PX` / `HOLD_MS` | 60 / 500 | 对准判据 |
| `A_NEAR` / `A_IMPACT` | 6000 / 12000 | 接近 / 撞击面积阈值 |

## 构建与自测（i5，无需板子/FC）

```bash
make                      # → vision_bridge + mock_vision
./mock_vision static &    # 假目标（static/sweep/step/loss/multi）
./vision_bridge --no-fc --force-auth
```

**已验证（2026-11-04）**：

| 场景 | 观察 |
|---|---|
| `static` | INIT→IDLE→ALIGN→APPROACH→COMMIT→RECOVER 全链路 ✓ |
| `step` | 目标偏 300px 时 `out=1649/1351`（= 1500±300×0.5）✓ 映射与限幅正确 |
| `loss` | 目标消失 → `APPROACH→IDLE`；重现 → `IDLE→ALIGN` ✓ 不盲冲 |
| 超时 | COMMIT 1.5s→RECOVER；RECOVER 2s→DONE ✓ |

## 板端部署（待办）

```bash
make board
scp vision_bridge.rv mock_vision.rv root@<板 IP>:/root/
# 板上（先拔桨；接真 FC）
./vision_bridge.rv --dry --dev /dev/ttyS0
```

## 未完成（下一步）

| 项 | 说明 |
|---|---|
| **L0 VIO 解析** | MAVLink ODOMETRY(331) 解析接 `vio_poll()`；用于高度/返航 |
| **起飞/降落/返航** | 需 **mask=15**（油门交上位机）；当前只做 XY |
| **AUTH_OFF 真机验证** | 需真 FC：拨 ch6/ch8 收授权 → 应 ABORT |
| **IMU 冲击阈值** | 需真机/黑匣子校准（当前 3g 是 exp16 经验值） |
| **看门狗** | 主循环卡死检测（无人飞行前必做） |
| **面积阈值标定** | `A_NEAR/A_IMPACT` 按真目标在 0.4m 处的实际面积定 |

## 已知取舍

- **单线程 50Hz**：收帧、状态机、发 RC 同一个循环，无锁、日志顺序=执行顺序；
- 视觉断流 >300ms → 目标视为无效 → 状态机回 IDLE；
- 上位机死机 → 停止发 RC → FC 约 0.7s 回落物理 RC（**不是 LAND**）。

---

## 9. 2026-11-05 更新：权限层 + yaw 跟随（按开关矩阵）

### 9.1 开关矩阵（用户定稿）

| 开关 | 何时有效 | 作用 |
|---|---|---|
| **ch5** | 永远 | 解锁/上锁（唯一人工急停） |
| **ch8 > 1700** | 永远 | 上位机接管门 |
| **ch6** | 仅接管时 | 子模式：<1300 空闲 / 1300–1700 **yaw 跟随** / >1700 预留（roll） |
| **ch7** | 接管关闭时 | FC 自己的定高/定点（1300–1700 定高、1700–2100 定点） |

> 纪律：**ch7 拨高位（定点）时不要同时给上位机接管**——两者都会闭环 XY。用户已确认接受此纪律。

### 9.2 上位机状态（权限层 + 任务层）

```
MONITOR      未接管（ch8 低）        → 中性
WAIT_ARM     接管但未解锁            → 中性（等 ch5）
WAIT_AIR     已解锁但 z<30cm         → 中性（等离地）
TRACK_IDLE   接管中、有高度          → 中性；ch6=yaw 且有目标 → ALIGN
ALIGN        yaw 跟随 + 俯仰居中     → 居中 0.5s → APPROACH
APPROACH     yaw 跟随 + 俯仰 + 前推  → 面积>阈值 → COMMIT
COMMIT       闭眼保持                → 冲击/面积 → IMPACT
IMPACT/RECOVER 中性                  → 回 TRACK_IDLE
```

### 9.3 判据来源

| 量 | 来源 |
|---|---|
| 是否解锁 | `ACTIVEBOXES` bit0（ARM） |
| 高度 | `MSP_ALTITUDE(109)`（i32 cm） |
| ch5–ch8 | `MSP_RC(105)` 的 rc[4..7] |
| 撞击 | 面积突增 或 IMU > 3g |

### 9.4 新增参数

| 变量 | 默认 | 含义 |
|---|---|---|
| `MIROCFLY_YAW_KP` | 0.6 | yaw 角速度增益（µs/px） |
| `MIROCFLY_ALT_AIR` | 30 | 离地判据（cm） |
| `MIROCFLY_SIM_ARMED` / `SIM_ALT` / `SIM_AUX` | 1 / 100 / "1000,1500,1500,2000" | `--no-fc` 时模拟 FC 状态 |

### 9.5 已验证（i5 + mock）

| 场景 | 结果 |
|---|---|
| ch8=1000 | 停在 `MONITOR`，输出中性 ✓ |
| ch8=2000 且未解锁 | `WAIT_ARM`，中性 ✓ |
| 已解锁、alt=5cm | `WAIT_AIR`，中性 ✓ |
| alt=100cm、ch6=1500、有目标 | `TRACK_IDLE→ALIGN`，`out=(1500, 俯仰, yaw跟随)` ✓ |
| 同上但无目标 | 停在 `TRACK_IDLE`，不抖（已修空转）✓ |
| ch6=1000 | `TRACK_IDLE`，中性 ✓ |

### 9.6 尚未实现

- yaw 符号未实机标定（目标偏右是否右转）；
- ch6>1700 的 roll 档（预留，未实现）；
- 起飞/降落/返航（需 mask=15）；
- VIO 解析（`vio_poll()` 仍为空）。

### 9.7 2026-11-05 补：SEARCH（自转搜索）

**触发**：接管中（ch8>1700）且 ch6 在跟踪档，但**视野里没有目标**。

```
TRACK_IDLE --无目标--> SEARCH --连续 3 帧命中--> ALIGN
                         │ 超时（默认 12s）→ 翻转方向 → TRACK_IDLE
ALIGN/APPROACH --丢失--> SEARCH
```

| 参数 | 默认 | 含义 |
|---|---|---|
| `MIROCFLY_SEARCH_YAW` | 100 µs | 搜索时的固定偏航量（≈40°/s，取决于 FC 的 yaw rate） |
| `MIROCFLY_SEARCH_MS` | 12000 | 单向搜索超时，超时后反向扫 |

**已验证**：
- 目标消失 → `ALIGN/APPROACH → SEARCH`，yaw 输出 `1600`（1500+100）✓
- 目标回来 → `SEARCH → ALIGN` ✓
- 居中目标 → 仍能走完 `ALIGN→APPROACH→COMMIT` ✓

**修掉的两个 bug**：
1. SEARCH 的 yaw 被输出层清零（active 列表漏了 SEARCH）；
2. **目标丢失后的保持衰减把 `sx` 从 100 衰减到 51，被误判为准对** → 加"必须本帧真检到（`lost==0`）"条件。
