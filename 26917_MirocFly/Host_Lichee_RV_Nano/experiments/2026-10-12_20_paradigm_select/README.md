# 2026-10-12_20_paradigm_select（实验）

**目的**：**上位机接管三轴（roll/pitch/yaw）+ 开关选范式 + 基础光流阻尼/定点**。
飞行员只管油门；接管期间**始终叠加光流水平速度阻尼**（防漂/防摆）；用 ch6/ch7/ch8 选模式。

## 开关映射（上位机读 `MSP_RC`；内部序 4=ch5 5=ch6 6=ch7 7=ch8）
| 通道 | 作用 | 区间 | 处理 |
|---|---|---|---|
| ch5 | ARM | `[1775,2100)` | FC |
| ch6>1300 | **MSP RC OVERRIDE**（接管） | `[1300,2100)` | FC |
| ch8>1300 | **MSP RC OVERRIDE**（与上条 **OR**） | `[1300,2100)` | FC |
| ch8>800 | **ANGLE** | `[800,2100)` | FC |
| ch6>1300 | 范式 **B**（roll 追踪） | — | 上位机 |
| ch7 | **定点** `[1300,1700)` / **定高** `[1700,2100)`（优先于 ch6） | — | 上位机 |

**接管 `ovr = (ch6>1300) || (ch8>1300)`**；`ch6<1300 且 ch8<1300` → 手动（可回退）。

## 上位机状态机
```
if !ovr:       中位(FC 忽略)
else:
  基础层(始终): 光流估水平速度 v -> 阻尼 kd
  ch7[1300,1700) 定点  : 速度阻尼 + 位置积分(kpp)  -> 真定点(被推回)
  ch7[1700,2100) 定高  : 桩(打印未实现) + 仅阻尼   (需 mask=15 接管油门)
  ch6>1300      范式B  : roll=1500+kpvis·offx + 阻尼 ; pitch=1500-kpvis·offy + 阻尼
  其它           中位  : roll/pitch=1500 + 仅阻尼
```
- **光流→速度**（依 FC `navigation_pos_estimator_flow.c`）：`v_右=+(flowX-bodyX)·(π/180)·alt`、`v_前=-(flowY-bodyY)·(π/180)·alt`；阻尼：右移→左滚、前移→抬头。
- 定点进入时**清零锚点**；位置项加**泄漏**防饱和；光流不可用 → 阻尼/积分归零 + 告警。

## FC aux（下位机，需 `save`）
```
aux 0  0  0 1775 2100     # ch5 ARM
aux 1 50  1 1300 2100     # ch6 override
aux 2 50  3 1300 2100     # ch8 override   (OR, 默认 MODE_OPERATOR_OR)
aux 3  1  3  800 2100     # ch8 ANGLE
```
（删除旧的 ch6 ANGLE/HORIZON、ch8 override[1700,2100]；`msp_override_channels=7` 不变。）

## 用法
```
# run/dry: [mode] [w] [h] [frames] [serial_dev] [baud]
./paradigm dry 1280 720 0 /dev/ttyS0 230400
# mon: [mode] [serial_dev] [baud]  (只读, 无相机)
./paradigm mon /dev/ttyS0 230400
# 省略 serial_dev -> PTY(mock FC, 仅逻辑自测)
```
env：`MIROCFLY_KP`(kpvis) `MIROCFLY_KD`(速度增益) `MIROCFLY_KPP`(位置增益) `MIROCFLY_POSLEAK` `MIROCFLY_MAXDELTA` `MIROCFLY_ALPHA` `MIROCFLY_ALT_M` `MIROCFLY_SX/SY` `MIROCFLY_CAM_ROT`(0|180) `MIROCFLY_SECS`。

**相机倒装冗余**：`MIROCFLY_CAM_ROT=180` → 检测前把图像旋转 180°（`cv::rotate`），对 A/B/定点通用。换装后务必**重跑静态标定**（右→roll>1500、上→pitch>1500）。注意 `SX/SY` 只作用于阻尼/定点项，**管不了范式B的视觉方向**，倒装必须靠 `CAM_ROT`。

## 预启动（脱离电脑）
- `./run_paradigm.sh [secs=120] [kpvis=0.4] [kd=150] [kpp=60]`：`nohup` 后台跑、日志 `paradigm_<时间>.log`、到点自停。
- `./stop_paradigm.sh`。

## 状态
🟡 代码就绪、已编译并部署板端；**`mon` 已扩展显示 `MSP_ALTITUDE`(z/vz) 与 R/P/Y/T**（2026-10-15，用于定高验证；注意**未解锁时 z 恒 0**，解锁后跟测距动）；FC aux 已改（`ch7≥1300`=定高）。**待**：`dry` 验 ch6/ch7/ch8 读值 → 台架验证（B/定点/阻尼）→ 短飞。

## 相机安装规范（固定前视，GC4653）
- **光学**：焦距 3.05mm / F2.5 / 畸变 5% / FOV 对角 90°、水平 81°、**竖直 51°**（1/3"，2560×1440，2.0µm）。1m 处约覆盖 1.6m×0.92m。
- **朝向**：yaw/roll = 0（光轴对齐机头、图像水平与机身水平齐）；**pitch 下倾约 10–15°**（竖直 FOV 宽 → 不敏感；目标 1–15m、巡航 1–1.5m）。
- **倒装**：绕光轴 180° → 设 **`MIROCFLY_CAM_ROT=180`**（检测前 `cv::rotate`，对 A/B/定点通用）。**别做成左右镜像**（正常旋转没问题）。
- **标定**（换装后必做）：
  1. 目标放画面**右** → `roll>1500`；放**左** → `<1500`；
  2. 目标放**上/下** → `pitch` 方向正确；
  3. 目标放**期望攻击方向** → 读 `sy` → 设 `pitch_bias_px = sy`（垂直基准 = 画面中心 + 该值）。

## 文件
```
├── paradigm.cpp      # 状态机 + 视觉B + 光流阻尼/定点 + MSP v1/v2
├── camera.c/h sophgo_middleware.h
├── msp.c/h           # v1 + v2(复制自 exp19)
├── run_paradigm.sh stop_paradigm.sh
├── Makefile / README.md / .gitignore
```
