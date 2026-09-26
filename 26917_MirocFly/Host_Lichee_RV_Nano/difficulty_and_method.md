# MirocFly 上位机 — 困难与解决办法记录

> **用途**：只记录开发中**遇到的困难**与**对应的解决办法**，供复盘、写简历、面试复习。
> 不写项目概述（那在 `README.md` / `agent.md`）。

> ⚠️ **维护约定（重要）**
> 1. **每遇到并解决一个新困难，就更新本文件**（追加到对应分类 + 更新日志）。
> 2. **定期同步更新**本飞控项目的所有上下文文档：
>    - 根目录 `README.md`（给人看）、`agent.md`（给 AI Agent）
>    - `Host_Lichee_RV_Nano/AGENT_GUIDE.md`、`Low_MCU/AGENT_GUIDE.md`
>    - 例如：镜像基座变更、相机方案变更、进度变化时，务必一起更新。
> 3. 记录格式：**现象 → 根因 → 解决办法**（能复现的命令/路径尽量写全）。

---

## A. 工具链 / 编译

### A1. 板子上没有 g++，无法板载编译
- **现象**：板子上 `which g++` / `g++ --version` → `not found`。
- **根因**：镜像不带编译器工具链。
- **解决**：改用**交叉编译**（在电脑编，产物 scp 到板子跑）。工具链项目里就有：
  `LicheeRV-Nano-Build/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-g++`（Xuantie V2.6.1 / GCC 10.2）。

### A2. opencv-mobile 没有 VideoCapture / imwrite
- **现象**：`cv::VideoCapture`、`cv::imwrite` 编不过。
- **根因**：`opencv-mobile-5.0.0-licheerv-nano` 预编译库**只有 core/imgproc/video 等，没有 videoio、imgcodecs 模块**。
- **解决**：SG2002 的 CSI 摄像头**不走标准 VideoCapture，必须走 CVI 中间件（MMF）**；抓帧得到 `VIDEO_FRAME_INFO_S`（这也正是 YOLO 推理要的格式）。参考 `CVI_HW_OpenCV` 的 `cvi_frames_read.patch`。

### A3. Makefile 链接踩坑
- **现象**：`Relocations in generic ELF (EM: 243)`（用宿主 g++ 编 riscv 目标）；`undefined reference to vdecDbg / ini_parse`；`hidden symbol __sync_fetch_and_add_1 ... referenced by DSO`。
- **根因**：make 内置 `CXX=g++` 未覆盖；缺 `-lvdec`/`-lini`；`-latomic` 位置导致 libgcc 原子符号冲突。
- **解决**：显式 `CXX := $(TC)g++`；链接补 `-lvdec -lmisc -lini`；`-latomic -lpthread -lm` 放到链接行末尾；用 `--start-group ... --end-group`。

### A4. 是否需要 ROS2
- **结论**：**不需要**。板子 256MB、任务是"摄像头→识别→串口"单流水线，ROS2 纯增负担。单 C++ 程序更轻、延迟更低。

### A5. 试图构建 scpcom 整棵 SDK 时踩的坑（并得出"不需要构建"的结论）
- **背景**：为"用 scpcom 的中间件"，一度去构建 `scpcom/LicheeSG-Nano-Build` 整棵树。
- **坑 1（CMake 4）**：Arch 的 `cmake 4.4.3` 拒绝 `3rdparty/json-c` 里老的 `cmake_minimum_required(<3.5)` → `CMake Error at CMakeLists.txt:3`。
  **绕过**：`export CMAKE_POLICY_VERSION_MINIMUM=3.5`。
- **坑 2（内核/驱动 uapi）**：`middleware/modules/sys` 编译报 `'VB_IOCTL_GET_VB_INIT' undeclared`——该符号在 **`osdrv/interdrv/include/chip/cv181x/uapi/linux/vb_uapi.h`**（内核驱动 uapi），复用 official 树的内核头里没有。
- **坑 3（构建系统调用方式）**：`source build/envsetup_soc.sh` 里 `defconfig` 由 `build/common_functions.sh` 提供；**注意别把 `source` 接管道**（`source x | tail` 会让函数定义丢在子 shell，表现为 `defconfig: 未找到命令`）。
- **教训（重要）**：编中间件**内部模块**（`modules/sys` 等）才需要 kernel/osdrv 头。**我们自写 app 只需要 scpcom 的"公有头 + 板上库"**，**根本不需要**编 kernel/middleware。别为了"能编 SDK"去补内核头——那是牛角尖。

---

## B. 相机链路（核心困难）

### B1. 用仓库静态 `libmaix_mmf.a` 建 VPSS 组失败
- **现象**：`[_mmf_vpss_init_new] CVI_VPSS_CreateGrp(grp:0) failed with 0xc0068004, retry!`（然后无限重试/卡死）；`mmf_vi_init` 却返回 OK，导致永远取不到帧。
- **根因**：仓库里的 `libmaix_mmf.a` 与**板上中间件/内核版本不匹配**。（板上 `test_mmf` 用的是镜像自带的 libmaix，行为不同。）
- **解决**：**弃用 libmaix**，改自写**低层 CVI MMF** 程序（`app/capture/capture.c`）。

### B2. VPSS 建组ION不足
- **现象**：`ioctl SYS_ION_ALLOC failed` → `CVI_VPSS_CreateGrp 0xc0068004`。
- **根因**：256MB 板子、ION carveout 105MB，其中 ISP/fast-image 预留 22MB；VPSS 建组自身要占一大块。
- **解决**：**跳过 VPSS**，直接 `CVI_VI_GetChnFrame` 从 VI 通道取帧。

### B3. VI 通道使能 OOM
- **现象**：`vi_sdk_enable_chn NG, Out of memory` / `CVI_VI_EnableChn failed 0xc00e8040`。
- **根因**：VI 通道**默认深度 8**（`u32Depth=0` 驱动取 8）→ 1440p 下约 44MB 缓冲，超出余量。
- **解决**：把 VI 通道深度改小（`CHN_ATTR_420_SDR8.u32Depth = 1`，或自定义 chn attr）；VB 池块按 VI 输出尺寸配。

### B4. 反复运行后"永远 OOM / 卡死"（关键根因）
- **现象**：干净重启后第一次能跑，之后必然失败/卡死。
- **根因**：**每次失败运行都会泄漏 ION**（`VI_DMA_BUF` 14.5MB + `ISP_SHARED_BUFFER_0` + `VbPool`），程序清理不干净，累积耗尽 carveout。
- **证据**：`/sys/kernel/debug/ion/cvi_carveout_heap_dump/summary` 里堆着多个 `VI_DMA_BUF`；`[0] carveout heap size:105MB, used:87MB`。
- **解决**：
  - 立即：**每个实验前干净重启**；
  - 根治：程序的清理路径要与官方 sample 一致（`SAMPLE_COMM_VI_DestroyIsp` → `SAMPLE_COMM_VI_DestroyVi` → `SAMPLE_COMM_SYS_Exit`），并修错误路径的段错误。

### B5. 出帧全 0 / 花屏、`VIFPS=0`
- **现象**：帧内容全 0 或花屏；`/proc/cvitek/vi_dbg` 显示 `VIWdma0ErrStatus=0x3000000`、`VIFPS=0`；`/proc/mipi-rx` 有 `decode=raw10` 但 `WcErr`（字计数错误）。
- **排查**：`vendor` 自带 `test_mmf`、`sensor_test` **同样失败/卡死** → 排除"我们代码"。
- **根因**：**Sipeed 官方镜像里 gc4653 的中间件本身是坏的**（scpcom 也证实"未修改镜像的示例都不工作"）；自编镜像同理。
- **解决**：**换 scpcom 镜像**（`scpcom/LicheeSG-Nano-Build` 的 `licheervnano-e_sd.img`，他替换了 `libmaix_mmf` 并把三处硬编码像素格式改成 NV21）。

### B6. 最终验证：相机链路打通（板载 test_mmf）
- **操作**：scpcom 镜像上跑 `/mnt/system/usr/bin/test_mmf 4`。
- **结果**：生成一批 `~470–495KB` 的 `venc_stream*.jpg`（黑帧只会几 KB）→ **真实画面，相机链路正常**。
- **结论**：**相机硬件全部正常，问题自始至终是"镜像中间件"**。

### B7. 自写程序取帧全黑 → 真根因是"中间件版本混用"（关键）
- **现象**：我们的 `app/camera`（`sophgo_middleware.c` NV21 版，链接**sipeed/官方中间件**）取到的帧**尺寸/格式都对**（如 2560×1440 NV21 = 5,529,600 B），但内容**恒为 0**。
- **对照**：同一块板、同一 `sensor_cfg.ini`、同一次运行批次——板上 `test_mmf`（**scpcom 中间件**）出真图，我们的程序全黑。
- **根因**：**中间件版本不匹配**。板上镜像 = scpcom 构建（`libsys 87be70b`，**拆分式** `libvi.so`/`libvpss.so`）；我们链接的是 sipeed/官方构建（**单体** `libsys.so`，依赖集不同）。混用 → VI/VPSS 通路不出数据（`CVI_VPSS_GetChnFrame` 返回空缓冲）。
- **解决**：自写程序必须①用 **scpcom 的公有头**编译；②链接/运行**板上 scpcom 的库**（`LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`）；③**不要**部署/带入自己的 sipeed 库。
- **验证（2026-09-26）**：`app/camera_scpcom/`（编译 scpcom 树 `maix_mmf/sophgo_middleware.c`，链接板上 50 个 `.so`，`-lsample` 等）编译链接通过；运行日志显示 `maix multi-media version:87be70b-musl_riscv64`、GC4653 Init OK，**出真图 → Form A 成立**。还需配合 **B9（丢前导黑帧）** 才能稳定出图。链接板载库时的两个坑：① `maix_mmf.h` 在 `sample/test_mmf/`（上一级）；② 板载 `libvi.so/libvpss.so` 需要 `libgdc.so` 的符号，必须显式 `-lgdc`（musl 无惰性绑定，否则运行时报 `CVI_GDC_GenLDCMesh: symbol not found`）。

### B8. 澄清：`test_mmf 4` 到底是相机还是合成图？
- sipeed 仓库的 `_test_venc_jpg` 用 `_prepare_image` 造**合成彩条** → **不是**相机测试（会误导人）。
- **scpcom 版的 `_test_venc_jpg` 走 `mmf_vi_frame_pop` 取相机帧** → 是真相机测试（所以你会看到"mode 4 是摄像头对准的画面"）。
- **教训**：判断"相机是否可用"必须看**具体是哪份源码编出来的二进制**，别拿一个 sample 的结果套另一个仓库。

### B9. 出图成功后仍"单帧全黑" → 抓到的是流水线启动首帧
- **现象**：Form A 隔离实验（scpcom 源码 + 板上库）日志完全健康、帧尺寸/格式正确，但抓单帧仍全黑（Y 全 0、UV=128）。
- **根因**：VI/ISP/VPSS 流水线**启动后的前导帧是黑的**；出图程序的预热循环"一成功就 break 并直接使用该帧"，抓到的正是这张启动黑帧。
- **证据**：连抓 20 帧 → 第 0 帧 mean=0，第 1 帧 mean≈86，之后稳定 130~157、max≈200。
- **解决**：抓帧前**先丢弃前导帧**（`camera.c` 默认 warmup=10）再取帧。单帧模式即稳定出真图（Y mean≈143，非黑像素 100%）。
- **教训**：黑帧不等于中间件坏/硬件坏——**先看是"第几帧"**。B7 的版本混用与 B9 的首帧黑是两个独立问题，叠加时都表现为"全黑"。

### B10. 输出文件无限累积占满 SD → 自动清理旧文件
- **现象**：每次抓帧生成 `cam_<w>x<h>.raw`（720p≈1.38MB、1440p≈5.5MB），长期反复实验会堆满板子存储。
- **解决**：`camera.c` 在启动/结束时清理 `cam_*.{raw,bmp,png}`，只保留最新 `keep` 个（默认 10；`argv[5]` 或环境变量 `MIROCFLY_KEEP`，`0`=关闭）。实测造 12 个旧文件后目录只剩 keep 个。
- **备注**：这只约束**磁盘/SD 占用**；板上 ION/RAM 的占用由 VB 池/通道深度决定，是另一处（当前运行结束 ION `used=0`，Form A 清理路径干净）。

### B11. RTSP 从 USB 网连不上（Connection refused）→ 自动绑定选了 wlan0
- **现象**：板上 `rtsp_grab` 打印 `rtsp://10.163.237.207:8554/live` 并显示 "streaming"，但主机从 USB 网 `10.222.2.1:8554` 连 → Connection refused。
- **根因**：scpcom `rtsp_server_init(NULL,...)` 自动按 `end0→eth0→wlan0→usb0` 选**第一个有 IP 的网卡**；板上 wlan0 有 IP → 只绑 wlan0（`/proc/net/tcp` 可验证 `:216A` = 8554），未监听 usb0。
- **解决**：显式绑定 `rtsp_server_init("10.222.2.1", 8554)`（本实验用环境变量 `RTSP_IP`/参数传入）；或传 `0.0.0.0`（`rtsp_get_server_urls()` 会枚举所有网卡）。
- **附**：scpcom `sophgo_middleware.c` 未开放 `mmf_enc_h264_init`（被 `#if 0`）→ RTSP 固定 **H265**；退出用 `killall rtsp_grab`(SIGTERM，触发清理) 而**不要 `kill -9`**，否则泄漏 ION（见 B4）。

### B12. 绿色检测上板的坑（opencv-mobile / OpenCV5）
- **HSV 标度不同**：PC(PIL) 的 H 是 **0-255**，OpenCV 的 H 是 **0-179**；换算 `Hcv = Hpil*179/255`（PIL H85-130 ⇒ OpenCV H60-91）。
- **OpenCV5 拆了 `geometry` 模块**：`findContours/contourArea/moments/boundingRect` 不在 imgproc，需 `#include <opencv2/geometry.hpp>` 且链接 `-lopencv_geometry`，否则报 `'contourArea' is not a member of 'cv'`。
- **可视化编码**：opencv-mobile **无 imgcodecs**（不能 `imwrite`）；用中间件 `mmf_enc_jpg` 也失败——`CVI_VENC_SendFrame failed with -1`（输入是普通内存、非 DMA/VB 缓冲）。**正解：直接写 BMP**（纯 C：54B 头 + 自底向上 BGR 行）。
- **小目标过滤**：在 640×360 缩略图上检测时，`min_area` 别设大（200 会把 3 米外 ~20cm 目标滤掉）→ 改 **20**。
- **链接**：opencv-mobile 静态库按 `-lopencv_geometry -lopencv_imgproc -lopencv_features -lopencv_core` 顺序，放进 `--start-group`。

---

## C. 内存 / ION 认知（由困难衍生）

### C1. `free` 只显示 128MB，误以为板子只有 128MB
- **根因**：板子实为 **256MB DDR**（`config.json`: "C906B + DDR 256MB"），其中 **~128MB 预留给媒体**（ION 105MB + ISP 20MB + 帧缓冲 7.8MB），故 Linux 侧只看到 128MB。
- **结论**：抓帧内存问题是"ION/carveout 被占"，不是"板子内存小"。

### C2. ION 定位手段
- `cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary` → 总大小/已用/各 buffer 明细（**能看出泄漏**）。
- `dmesg | grep -i carveout` / `ion` → 分配失败记录（如 `ion allocated len=0xdcda00 failed`）。

---

## D. 硬件误判与更正

### D1. 一度怀疑相机模组/排线/传感器坏（甚至想换板）
- **当时依据**：MIPI 有信号但帧全 0；排线换方向会出现"无 MIPI 信号"（接触敏感）；反复插拔过。
- **更正**：scpcom 镜像 `test_mmf 4` 出真实图 → **传感器、排线、模组全部正常**。
- **教训**：**"I2C 能识别但 MIPI 出不了图"不一定硬件坏**——相同症状也可能是中间件坏。**先用一个已验证可用的中间件（scpcom）做隔离实验**，再下硬件结论。

### D2. 社区"短接 11 和 15"的说法
- **状态**：未确认出处（疑似相机稳压器 enable 相关，参考 OV5647 issue #66/PR #31 的 regulator-enable→3.3V 做法）。
- **处置**：scpcom 镜像下相机已正常，**该硬件改法暂不需要**；保留备查。

### D3. 误判"`sensor_cfg.ini` 被我们改坏了"（黑帧甩锅给配置）
- **当时依据**：`deploy.sh` 会把 `sensor_cfg.ini` 拷到板上 `/mnt/data/`；怀疑覆盖了镜像自带的、导致黑帧。
- **查证**：镜像 `/etc/init.d/S02config` 决定走哪条分支——
  - 有 `/boot/alpha` 或 `/boot/epsilon` → **alpha**（`lane_id=2,1,0 / mclk=0`）+ pinmux `devmem 0x0300116C=0x3, 0x0300118C=0x5`；
  - 否则（默认）→ **beta**（`lane_id=4,3,2 / mclk=1`）+ pinmux `devmem 0x0300116C=0x3, 0x0300118C=0x5`。
  实测板上**无 alpha/epsilon 标记**、pinmux 寄存器读数=`0x3/0x5`（beta 值）、且我们 cfg 内容**正是 beta** → **并未改坏配置**。
- **教训**：下"配置被改坏"结论前，先查**板子实际处于哪个配置分支**（引导日志/寄存器/标记文件）。

---

## E. 调试基础设施 / 远程访问

### E1. 每次换镜像后 SSH 公钥丢失
- **现象**：`Permission denied (publickey,...)`。
- **解决**：重装 `ssh-copy-id -i ~/.ssh/id_ed25519.pub root@10.222.2.1`。

### E2. 换镜像后 SSH 报 "REMOTE HOST IDENTIFICATION HAS CHANGED"
- **根因**：新镜像主机密钥不同，`known_hosts` 有旧记录。
- **解决**：`ssh-keygen -R 10.222.2.1`。
- **临时绕过**（不改文件）：`ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no ...`。

### E3. `sudo dd` 卡在密码（无法刷机）
- **根因**：在无 tty 的 shell（如工具内）里 sudo 无法读密码；桌面是 **Hyprland 且未运行图形 polkit agent**，`pkexec` 也用不了。
- **解决**：在**独立终端**里执行；或把用户加入 `disk` 组。

### E4. 板上没有 `timeout` 命令
- **现象**：`sh: timeout: not found`。
- **解决**：用后台 + 定时杀：`./prog > /tmp/x.log 2>&1 & CP=$!; sleep N; kill $CP`。

### E5. zsh 下 `sed` 有别名坑
- **解决**：用 `\sed`（或 `command sed`）。

---

## F. 刷机 / 镜像

### F1. dd 目标设备名会变
- **现象**：有时 `/dev/sdb`，有时 `/dev/sda`。
- **解决**：每次刷前 `lsblk -o NAME,SIZE,RM,LABEL` 确认**可移动、16M+rootfs 布局**的那个；**别写系统盘 `/dev/nvme0n1`**。

### F2. scp 多文件引号坑
- **现象**：`scp host:"a b c"` → 被当单个路径，"No such file"。
- **解决**：单文件分开写；或 `scp "host:/path/glob*.jpg" .` 让远端展开通配符。

### F3. scp 本地路径误用板子的 `~`
- **现象**：在板子的 root shell 里跑 scp，`~/Downloads` 变成 `/root/Downloads`（不存在）。
- **解决**：**scp 在电脑上执行**，本地路径写绝对路径 `/home/user/...`。

### F4. scpcom 镜像 RNDIS 可能有问题（本次未遇到）
- **背景**：issue #826 提到 scpcom 镜像 `S30gadget_nic` 缺失 → 需在 rndis 上跑 `udhcpd`。
- **本次实际**：scpcom 镜像 RNDIS/USB 网络**正常**（ping 通、`enp0s20f0u3` 起来、lsusb 认到 `sipeed licheervnano`），未触发。

---

## G. 调试命令速查

```bash
# MIPI 链路（decode 应为 raw10；看 EccErr/CrcErr/WcErr/各 lane 状态）
cat /proc/mipi-rx
# VI 状态（VIFPS/VIDevFPS/SOF 计数/DMA 错误）
cat /proc/cvitek/vi_dbg ; cat /proc/cvitek/vi
# ION/carveout 占用与泄漏明细
cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary
# 内核/系统日志（看 vpss_open、ion alloc failed 等）
dmesg | grep -iE 'ion|carveout|vpss|vi ' ; tail -40 /var/log/messages
# 相机抓帧测试（scpcom 镜像，出 jpg）
cd /root && /mnt/system/usr/bin/test_mmf 4 ; ls -la /root/*.jpg
# 传感器/VI 工具
/mnt/system/usr/bin/sensor_test
```

---

## H. 未解决 / 待办

### H1. 导出照片分辨率偏低
- **现象**：`test_mmf 4` 出的 jpg 分辨率偏低（GC4653 原生 2560×1440）。
- **方向**：调 **VPSS 输出通道 / VENC** 的尺寸（改 `test_mmf` 参数或用自写程序配置到 1280×720 / 2560×1440）。也关系到 YOLO 输入尺寸。

### H2. 自写程序黑帧 → Form A 已出图并固化（已完成）
- **结论**：黑帧 = **中间件版本混用（B7）** + **抓启动首帧（B9）** 两个独立问题叠加。
- **方案（Form A，已验证）**：用 **scpcom 公有头**编译 + **链接板上 scpcom 库**，运行 `LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`，**不部署自带 .so**；抓帧前丢前导帧。
- **已固化**：正式相机层 `experiments/2026-09-26_04_vision_module/`（`camera.h` API + `frame_grab` CLI，共享 `board_libs/`）；原 `experiments/2026-09-26_03_scpcom_formA/` 转为实验存档。
- **下一步**：VI→VPSS 双路（一路取帧给 AI、一路 VENC→RTSP 给人看）。
- **注意**：**不需要**构建 scpcom 的 kernel/middleware（见 A5）。

### H3. 高层 TDL SDK 缺失（为 YOLO 铺路）
- **现状**：板上有 NPU 运行时（`/usr/bin/lib/libcvikernel.so`、`libcviruntime.so`、`libcvimath.so`）和模型（`yolov5s_224_int8.cvimodel` 等），但**未找到高层 `libcvi_tdl`**。
- **方向**：到阶段 2 补 TDL SDK + 自定义 YOLOv8n（INT8 cvimodel）。

### H4. 长期基座选择
- **待定**：长期用 **scpcom 镜像**（相机已通），还是把 scpcom 的修复移植回官方镜像。

---

## 更新日志
- **2026-09-24**：建文档。汇总工具链/相机/内存/硬件/远程/刷机各类困难与解决办法；记录关键结论（**相机硬件正常，根因是官方镜像中间件，scpcom 镜像验证通过**）。
- **2026-09-26**：
  - 新增 **B7**（自写程序全黑真根因=**中间件版本混用**：我们用 sipeed 中间件，板上是 scpcom 中间件）；
  - 新增 **B8**（澄清 `test_mmf 4`：sipeed 版是合成彩条、**scpcom 版才是相机**）；
  - 新增 **D3**（误判 `sensor_cfg.ini` 被改坏；实际板默认即 beta，未改坏）；
  - 新增 **A5**（试图构建 scpcom 整树踩的 CMake4/内核头坑，**结论：app 只需公有头+板上库，不需要构建 SDK**）；
  - 更新 **H2**（方案改为 Form A + 隔离实验）。克隆 scpcom 源码树 `LicheeSG-Nano-Build_scpcom/`。
  - **2026-09-26（晚）Form A 验证**：`app/camera_scpcom/` 编译链接通过并**出真图**（B7 补充"验证"+链接坑）；新增 **B9**（首帧黑帧 → warmup 丢帧）、**B10**（输出文件自动清理）；**H2 标记完成**。
  - **2026-09-26（晚）相机层固化**：新建 `app/vision/`（`camera.h` API + `frame_grab` CLI + README），链接副本集中到 `Host_Lichee_RV_Nano/board_libs/`（`fetch_board_libs.sh` 拉取）；`camera_scpcom/` 转实验存档。另加 `hmirror/vflip` 软件朝向开关（按中间件 `bMirror/bFlip` 实现，装好相机后按实测选）。
  - **2026-09-26（晚）目录重组为 experiments/**：`app/{capture,camera,camera_scpcom,vision}` → `experiments/{2026-09-24_01_lowlevel_mmf, 2026-09-26_02_sipeed_middleware, 2026-09-26_03_scpcom_formA, 2026-09-26_04_vision_module}`，新建索引 `experiments/README.md`。
  - **2026-09-26（晚）RTSP 打通**：新增实验 `experiments/2026-09-26_05_rtsp_stream/`，实现 VI→VENC(H265)→RTSP（复用 scpcom `rtsp_server/` + `media_server` 静态库），主机 `ffmpeg` 收到 `hevc 1280x720@30fps` 真实画面；新增 **B11**（RTSP 绑定网卡坑）。
  - **2026-09-26（晚）绿色检测（阶段1 感知）**：新增实验 `experiments/2026-09-26_06_hsv_green/`，PC 调阈值 + 上板 opencv-mobile 实时 HSV 检测，3 米外 ~20cm 绿目标稳定检出 `offset/area` 并存 BMP；新增 **B12**（HSV 标度/OpenCV5 geometry/imgcodecs 缺失→写 BMP/小目标 min_area）。
  - **2026-09-26（晚）MSP 链路（阶段1 链路）**：新增实验 `experiments/2026-09-26_07_msp_link/`（纯 C，不依赖中间件）；实现 MSP v1 编解码/流式解析/串口/`offset→RC`。**无硬件自测法**：用 **PTY（`posix_openpt`）在同进程内模拟 FC** 做端到端收发（客户端↔模拟FC），验证 termios+分帧+校验，`selftest`+`pty` 均 OK。真串口 UART1@460800 待下位机。
