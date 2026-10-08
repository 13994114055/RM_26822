# 2026-09-29_12_armor_led（实验）

**目的**：用**传统 CV** 检测 **RM 风格装甲板的 LED 灯条**（红/蓝）——不依赖训练，作为阶段2 的并行兜底/预研。
当前手头只有**灯条**（没有"黑底+白数字+双灯条"的标准板），故只做 **LED 版**。

**现状**：🟢 **S0 + S1 完成**（合成图 + PC 检测原型，合成集上召回/精确 100%、中心误差 0.34px）；S2（板端移植）待做。

## 计划（分阶段，逐段汇报）
| 阶段 | 内容 | 状态 |
|---|---|---|
| **S0** | 生成**透视(梯形)装甲板**合成测试图 + 真值 labels.csv | ✅ 完成 |
| **S1** | PC 原型 `detect_armor.py`（cv2）：通道差 + 形态学 + minAreaRect + 松弛配对 + 四点四边形中心 | ✅ 完成 |
| S2 | 板端 `armor_detect.cpp`（opencv-mobile，复用 04 相机层）实时输出 offset/area + BMP | ⏳ |
| S3 | 输出成 `TargetInfo`，对齐 08/控制层；可选接 10 的 RTSP 叠加 | ⏳ |

## 关键认知：为什么是"梯形/不等长/不对称"
相机是**透视投影**，装甲板偏航(yaw)/俯仰(pitch)/横滚(roll)时：
- 偏航：近侧灯条长、远侧短 → **左右不等长**；间距变小 → 梯形。
- 俯仰：上下边缘不等宽，灯条倾斜。
- 横滚：两灯条一起倾斜（仍近似平行）。
→ 所以检测算法**不能用"等高对称"硬约束**，要用**松弛约束** + **四点四边形**估中心。

## 目录
```
├── pc_tune/
│   ├── gen_synth_armor.py     # S0: 纯 PIL 生成透视合成图 + 真值
│   ├── detect_armor.py        # S1: cv2 检测原型 + 自动评估
│   └── requirements.txt       # opencv-python / numpy
├── synth_armor/               # 21 张 png + labels.csv + contact_sheet.png
│   └── out/                   # S1 带框图 + _montage.png
└── README.md
```

## 环境（PC, 不动系统）
```bash
python3 -m venv ~/.venvs/mirocfly
~/.venvs/mirocfly/bin/pip install -i https://pypi.tuna.tsinghua.edu.cn/simple -r pc_tune/requirements.txt
```
- 实测 `opencv-python 5.0.0`（**与板端 opencv-mobile 5.0.0 同大版本**）、`numpy 2.5.3`。
- 本机 Python 3.14；`opencv-python 5.0.0.93` 是 `cp37-abi3` wheel（稳定 ABI，兼容 3.14），`numpy` 有 cp314 wheel，**无需旧 Python**。

## S0：合成图生成
```bash
python3 pc_tune/gen_synth_armor.py --out synth_armor --seed 0
```
装甲板模板：黑底 300×210 + 两侧灯条 20×140（红/蓝）+ 可选白数字；用 `Image.PERSPECTIVE`
（自解 8×8 单应）把矩形板渲染成梯形。变体 21 张：front/yaw(轻/强)/pitch/roll/near/small_far/
dark/bright/noisy + red_digit + scene_red_blue。真值 `labels.csv`：`name,color,cx,cy,x0..x3,digit`。

## S1：检测原型 `detect_armor.py`
```bash
~/.venvs/mirocfly/bin/python pc_tune/detect_armor.py        # 跑全量 + 对照 labels.csv 评估
~/.venvs/mirocfly/bin/python pc_tune/detect_armor.py --img synth_armor/red_yaw_strong.png
```
算法：① 通道差 `红=R−max(G,B)` / `蓝=B−max(R,G)` 阈值；② 形态学开/闭；
③ `findContours`+`minAreaRect`→灯条，按 **长宽比/长度/面积**过滤；④ 同色**松弛配对**
（长度比、角度差、y 差、间距比给容差）；⑤ 板中心 = **四条灯条中轴端点**构成的四边形**对角线交点**。

**合成集实测**（21 图 / 22 真值）：**召回 100%、精确 100%、中心误差 平均 0.34px（最大 0.67px）**。
> 注意：合成图是**干净可控**数据，分数偏高属预期；真实相机有光照/模糊/背景杂波，S2 上板才是真正考验。

## 两个算法坑（已修，详见 difficulty B16）
1. 灯条长轴**不能取"最远两点"**（细长矩形的对角线比长边还长）→ 应取**最长边**。
2. 板中心取灯条长边会受 `boxPoints` 角点顺序影响（内/外边）→ 偏差 ~10px；应取**两条短边中点连成的中轴**。

## 下一步（S2）
移植到板端 opencv-mobile（`cvtColor`→通道差→`morphologyEx`→`findContours`/`minAreaRect`），
复用 04 相机层实时出 offset/area，接 S3 的 `TargetInfo`。