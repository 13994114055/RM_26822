# MirocFly 上位机操作手册（Host_Lichee_RV_Nano）

> 本文件是**上位机（LicheeRV Nano / SG2002）的操作手册**：指导后来者如何在本目录**编译、部署、运行**各功能模块，以及排错。
> 想了解"是什么/为什么"请看 `README.md`（本目录总览）、`../README.md`、`../agent.md`；困难与原理见 `difficulty_and_method.md`。
> 各功能实现放在 `experiments/` 实验档案里，每个实验有自身 `README.md`。

---

## 0. 一次性准备

- **连接板子**：板子 USB-C 接电脑；电脑出现 RNDIS 网卡（`enp0s20f0u3`），板子地址 **10.222.2.1**。
  ```
  sudo dhcpcd enp0s20f0u3        # 电脑侧拿 IP（如 10.222.2.100）
  ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@10.222.2.1
  ```
  换镜像后若报 `REMOTE HOST IDENTIFICATION HAS CHANGED`：`ssh-keygen -R 10.222.2.1`；公钥丢失则重装 `ssh-copy-id`。
- **工具链**：`LicheeRV-Nano-Build_official/host-tools/gcc/riscv64-linux-musl-x86_64/`。各实验 `Makefile` 已引用，无需手动配置。
- **板上库链接副本**：首次在本目录执行 `./fetch_board_libs.sh`（把板上 scpcom `.so` 拉到 `board_libs/`，仅作链接输入，**不部署回板**）。
- **建议设一个 shell 快捷函数**（省去长参数）：
  ```
  bsh(){ ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@10.222.2.1 "$@"; }
  ```

## 1. 通用铁律（所有功能都适用）

1. **运行必须带库路径**：`LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`。
2. **板上没有 `timeout`**：用"后台 + `sleep` + `killall`"：
   ```
   bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./prog ... >/tmp/x.log 2>&1 &); sleep 12; killall prog'
   ```
3. **停止程序用 SIGTERM（`killall`），不要 `kill -9`**——否则泄漏 ION 内存；正常退出会回收。
4. **ION 检查**：`bsh 'sed -n "2p" /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary'`（`used` 应为 0）。异常后 `bsh 'reboot'` 干净重启。
5. **程序统一放** `/root/mirocfly/`（`scp prog root@10.222.2.1:/root/mirocfly/`）。
6. **相机/推流类程序同一时刻只能跑一个**（都独占相机）。

## 2. 功能操作手册（按实验）

### 2.1 相机取帧（实验 `2026-09-26_04_vision_module`）
```bash
cd experiments/2026-09-26_04_vision_module && make
scp frame_grab root@10.222.2.1:/root/mirocfly/
bsh 'cd /root/mirocfly && rm -f cam_*.raw && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5 >/tmp/fg.log 2>&1 &); sleep 12; killall frame_grab'
scp root@10.222.2.1:/root/mirocfly/cam_1280x720.raw .
python3 ../2026-09-24_01_lowlevel_mmf/nv21_to_bmp.py cam_1280x720.raw 1280 720 out.bmp   # 转 BMP 查看
```
参数：`./frame_grab [宽] [高] [帧数] [warmup] [keep] [hmirror] [vflip]`。`warmup` 默认 10（丢启动黑帧，别设 0）。

### 2.2 RTSP 实时预览（实验 `2026-09-26_05_rtsp_stream`）
```bash
cd experiments/2026-09-26_05_rtsp_stream && make
scp rtsp_grab root@10.222.2.1:/root/mirocfly/
bsh 'cd /root/mirocfly && (RTSP_IP=10.222.2.1 LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./rtsp_grab 1280 720 1 >/tmp/rtsp.log 2>&1 &)'
ffplay -rtsp_transport tcp rtsp://10.222.2.1:8554/live      # 电脑上看
bsh 'killall rtsp_grab'                                     # 看完停掉
```
**必须**用 `RTSP_IP=10.222.2.1` 指定绑定网卡（否则自动绑到 wlan0，电脑连不上）。

### 2.3 绿色目标检测（实验 `2026-09-26_06_hsv_green`）
```bash
cd experiments/2026-09-26_06_hsv_green && make
scp green_detect root@10.222.2.1:/root/mirocfly/
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_detect 1280 720 60 >/tmp/gd.log 2>&1 &); sleep 16; killall green_detect; grep -aE "FOUND|no green" /tmp/gd.log'
scp root@10.222.2.1:/root/mirocfly/green.bmp .               # 带框图
```
PC 端调阈值：`cd pc_tune && python3 detect_green.py <raw> <w> <h> [out.bmp] [--h .. --s .. --v ..]`。
默认阈值（OpenCV HSV）`H 60-91 / S 40-170 / V 55-170`；小/远目标把 `min_area` 调小（程序最后一个参数）。

### 2.4 MSP 链路自测（实验 `2026-09-26_07_msp_link`，无需硬件）
```bash
cd experiments/2026-09-26_07_msp_link && make
scp msp_test root@10.222.2.1:/root/mirocfly/
bsh 'cd /root/mirocfly && ./msp_test'        # selftest + PTY 模拟 FC + demo
```
真硬件（下位机到位后）：`./msp_test inject /dev/ttyS1 460800 10`（注入）/ `monitor /dev/ttyS1 460800`（回读）。

### 2.5 识别绿色并"飞过去"（实验 `2026-09-26_08_vision_control`，闭环雏形）
```bash
cd experiments/2026-09-26_08_vision_control && make
scp green_fly root@10.222.2.1:/root/mirocfly/
# 离线自测: 内部 PTY 假 FC, 自动收帧打印
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_fly 1280 720 150 >/tmp/gf.log 2>&1 &); sleep 20; killall green_fly; grep -aE "FOUND|neutral|recv" /tmp/gf.log'
# 真 FC(下位机到位后):
#   ./green_fly 1280 720 0 /dev/ttyS1 460800
```
安全：无目标回中；油门默认 1000（不转）；真机联调**拔桨→绑绳→短飞**。

### 2.6 NPU 部署 demo（实验 `2026-09-26_09_npu_demo`）
```bash
cd experiments/2026-09-26_09_npu_demo
# 下载官方模型(cv181x INT8, 若未下载过)
mkdir -p models && curl -L -o models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel \
  https://github.com/sophgo/tdl_models/raw/main/cv181x/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel
make                                  # 编译 npu_hello(底层) + tdl_detect(高层)
scp npu_hello tdl_detect models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel root@10.222.2.1:/root/mirocfly/
LP=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib
# 步骤1: 底层 cviruntime 分类(用板上模型)
bsh "cd /root/mirocfly && LD_LIBRARY_PATH=$LP ./npu_hello /usr/bin/mobilenet_v2_rgb_224_int8.cvimodel"
# 步骤2: 高层 TDL 检测(需把测试图 test.png 也传到板上)
bsh "cd /root/mirocfly && LD_LIBRARY_PATH=$LP ./tdl_detect yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel test.png"
```
> NPU 程序同样适用"后台+sleep+killall"；链接要点：**须链 `board_libs` + `-lgcc_s`，不链 `-ltdl_ex`**（见 `difficulty_and_method.md` B13）。

## 3. 常见问题速查

- **取帧全黑**：先丢前导帧（warmup≥10）；确认用 scpcom 中间件（见 `difficulty_and_method.md` B7/B9）。
- **运行报 `libxxx.so not found`**：漏了 `LD_LIBRARY_PATH`。
- **链接报 `CVI_GDC_GenLDCMesh: symbol not found`**：链接期漏了 `-lgdc`。
- **RTSP 连不上（Connection refused）**：日志里的 IP 是 wlan0 → 用 `RTSP_IP=10.222.2.1`。
- **反复运行后启动失败/卡死**：ION 泄漏 → `bsh 'reboot'`。
- **黑帧/花图**：先查 `sensor id`（应 `0x4653`），别用 `deploy.sh` 覆盖板上 `sensor_cfg.ini`（板默认 beta 已可用）。
- **图像上下反**：`frame_grab` 的 `hmirror/vflip` 开关（中间件默认等价旋转 180°）。

## 4. 板子命令速查

```bash
# MIPI 链路
cat /proc/mipi-rx
# VI 状态
cat /proc/cvitek/vi_dbg
# ION 占用/泄漏
cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary
# 内核日志
dmesg | grep -iE 'ion|carveout|vpss|vi'
# 相机自检(板上自带, scpcom 版走相机)
cd /root && /mnt/system/usr/bin/test_mmf 4 ; ls -la /root/*.jpg
# 重启
reboot
```

## 5. 相关文档

- 目录总览：`README.md`；项目总览 / AI 上下文：`../README.md`、`../agent.md`
- 开发指导：`AGENT_GUIDE.md`；困难与解决：`difficulty_and_method.md`
- 实验索引：`experiments/README.md`；下位机存档：`../Low_MCU/archive_at32/agent_at32.md`
