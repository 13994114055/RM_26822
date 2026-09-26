# 2026-09-24_01_lowlevel_mmf（实验，已弃用）

> ⚠️ **已弃用（2026-09-26）**：早期"低层 CVI MMF"版本，在板上（scpcom 中间件）跑不通。
> 后续方案见 `../2026-09-26_02_sipeed_middleware/`、`../2026-09-26_03_scpcom_formA/` 与 `../2026-09-26_04_vision_module/`。
> **重要教训**：自写程序必须用 **scpcom 公有头 + 板上 scpcom 库**（见 `../../difficulty_and_method.md` B7）；
> 且**不要**再用本目录 `deploy.sh` 去覆盖板子 `/mnt/data/sensor_cfg.ini`（板默认 beta 已可用，见 D3）。
>
> 本目录同时含共用小工具：`nv21_to_bmp.py`（PC 端 NV21→BMP）、`detect_green.py`（HSV 绿色检测）。

最小 MMF 抓帧程序：`CSI 摄像头 -> VI(+ISP) -> VPSS -> VENC(JPEG) -> 文件`。
基于 Sipeed `libmaix_mmf` 轻量封装，是后续视觉管线的地基。

## 编译（PC 上交叉编译）

```bash
make          # 用 riscv64-linux-musl 工具链
```
产物 `capture` 是 riscv64 动态链接可执行文件，运行时需要板载中间件 `.so`。

## 部署与运行

一键脚本（需本机可 ssh 到板子）：
```bash
./deploy.sh            # 默认 1280x720 抓 1 帧
./deploy.sh 1920 1080 3 90
```
手动步骤：
```bash
scp capture root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1
cd /root/mirocfly
LD_LIBRARY_PATH=/mnt/system/usr/lib ./capture 1280 720 1 80
exit
scp root@10.222.2.1:/root/mirocfly/capture.jpg .
```

## 传感器配置（重要）

- 传感器型号由 **INI 文件**在运行时决定，**不需要重编译**。
- 优先级：`/mnt/data/sensor_cfg.ini` → `/mnt/system/usr/bin/sensor_cfg.ini` → 默认(IMX327)。
- 本仓库已带 **确认版配置** `sensor_cfg.ini`（LicheeRV Nano 70415 版 + GC4653，lane_id=4,3,2）。
- `./deploy.sh` 会自动把它拷到板子 `/mnt/data/sensor_cfg.ini`。
- 手动部署：
  ```bash
  scp sensor_cfg.ini root@10.222.2.1:/mnt/data/
  ```
- 验证配置是否生效：运行日志应出现
  `Parse /mnt/data/sensor_cfg.ini` → `GC4653 ... Init OK`
- 若你的板子是**旧版 70405**（MIPI 线序不同），把 `lane_id` 改成 `2, 1, 0, -1, -1`。

## 排错

- `mmf_init/vi_init` 失败：多半是传感器 INI 不对或摄像头没接好。看 `sensor id` 打印。
- 运行报 `libxxx.so not found`：确认 `LD_LIBRARY_PATH=/mnt/system/usr/lib`。
- 黑图/花图：先查 `sensor id` 是否匹配实际摄像头，再试不同分辨率。