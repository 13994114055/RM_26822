# mirovision —— 视觉服务（M1/M2/M3 已完成）

> 同学只交付 `.cvimodel` + `classes.txt`；采集/推理/发布/健壮性都由本服务负责。
> 对外唯一契约：`vif_frame_t`（152 字节）UDP。

## 1. 功能

| 阶段 | 内容 | 状态 |
|---|---|---|
| M1 | 采集 → 推理 → UDP 发布（零拷贝） | ✅ |
| M2 | 可选 RTSP 叠加（画框 + 置信度数字） | ✅ |
| M3 | 相机自恢复 / 模型重载 / 守护重启 / 开机自启 | ✅ |

## 2. 用法

```bash
./mirovision --model best_v2_cv181x_int8_sym.cvimodel \
             --classes classes.txt \
             [--cam 1280x720] \
             [--conf 0.5] \
             [--udp 127.0.0.1:7788] \
             [--rot180]        # 相机倒装时开（VPSS 硬件翻转）
             [--rtsp]          # 开 RTSP 叠加（默认关）
             [--stats 1]       # 每秒统计
             [--frames N]      # 跑 N 帧后退出（0=一直跑）
             [--dump f.raw]    # 第 20 帧存 NV12（调试）
```

## 3. 部署

```bash
# 板上目录（模型统一放这里）
/root/models/
├── mirovision
├── best_v2_cv181x_int8_sym.cvimodel
├── classes.txt
├── run_mirovision.sh      # 守护脚本（崩溃自动重启）
└── S99mirovision          # 开机自启（复制到 /etc/init.d/）

# 手动跑
cd /root/models && ./run_mirovision.sh          # 前台
cd /root/models && ./run_mirovision.sh bg       # 后台（PID 写 /tmp/mirovision.pid）

# 停
/etc/init.d/S99mirovision stop

# 开机自启
cp /root/models/S99mirovision /etc/init.d/ && chmod +x /etc/init.d/S99mirovision
```

## 4. 依赖环境变量（每次都要）

```bash
export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib
```

## 5. 实测性能（LYIF722DUAL 无关，板子是 LicheeRV Nano）

| 配置 | 帧率 | 推理 | 检出 |
|---|---|---|---|
| 不开 RTSP | 21.5 fps | 38.6 ms | 100% |
| 开 RTSP | 18.5 fps | 39.3 ms | 100% |

模型：`best_v2`（50 张图校准），置信度 **0.94–0.96**。

## 6. 健壮性设计

| 故障 | 检测 | 动作 |
|---|---|---|
| 相机无帧 | 连续 300 次拿不到 | `DestoryCamera` → 重开 → 重新设翻转 |
| 推理失败 | 连续 50 次 | 关模型 → 重开 → 重设阈值 |
| 进程崩溃 | 守护脚本 | 2 秒后重启 |
| 断电容错 | init.d | 开机自动拉起 |
| 日志膨胀 | 4 MB | 滚动成 `.old` |

**已知限制**：相机重开后 180° 翻转会重新设置，但**如果相机需要热插拔重枚举**（USB 相机场景），仍要重启进程——这块是本板 MIPI 相机，风险低。

## 7. 验收结果（2026-11-08）

```
✅ 检出 100%（114/114 帧）
✅ 置信度 0.96
✅ 画框位置与物体吻合
✅ UDP 21 fps / 0 坏包 / 0 断流
✅ 杀掉进程后 6 秒内自动重启（PID 786→854）
✅ 重启板子后自动启动（PID 594，RTSP 已监听）
✅ 二进制 26 KB，无 OpenCV 依赖
```
