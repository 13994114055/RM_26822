# 2026-09-26_05_rtsp_stream（实验）

**目的**：在相机取帧之上做 **VI → VENC(H265) → RTSP** 实时预览（后续给“给人看画面”用；AI 路复用同一帧）。

**结果**：✅ 成功。主机 `ffmpeg` 收到 `hevc 1280x720@30fps`，画面为真实场景。
**数据流**（改编自 scpcom `sample_vio.c::_test_vi_venc_h26x_rtsp`）：
```
VI 单通道 pop NV21 帧 ──┬─> (预留 AI/检测钩子: 见 rtsp_grab.c 注释)
                        └─> mmf_venc_push -> mmf_venc_pop -> rtsp_send_memory_data -> RTSP :8554
```

## 依赖（均复用 scpcom，不新增）
- `maix_mmf/sophgo_middleware.c`（中间件源码）+ `board_libs/`（板上 scpcom 库）
- `test_mmf/rtsp_server/src/*.cpp`（RTSP 服务端，C++）
- `test_mmf/media_server-1.0.x/release.linux/*.a`（静态库：rtsp/http/flv/mov/rtp/mpeg/sdk/avcodec/avbsf）

## 编译与运行
```bash
make            # 产物 rtsp_grab (含 media_server 静态库, ~758KB)
scp rtsp_grab root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && \
  (RTSP_IP=10.222.2.1 LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./rtsp_grab 1280 720 1 >/tmp/rtsp.log 2>&1 &)'
# 主机观看 / 抓帧
ffplay -rtsp_transport tcp rtsp://10.222.2.1:8554/live
timeout 30 ffmpeg -y -rtsp_transport tcp -i rtsp://10.222.2.1:8554/live -frames:v 1 out.jpg
```

## 参数
```
./rtsp_grab [宽] [高] [type] [fps] [bind_ip]
```
- `type`：仅 **1=H265**（scpcom 未开放 `mmf_enc_h264_init`，被 `#if 0` 关闭）。
- `bind_ip` / 环境变量 `RTSP_IP`：RTSP 绑定 IP。**必须指定**，否则自动探测会选到 `wlan0`（见下）。走 USB 网用 `10.222.2.1`。

## 关键坑
1. **绑定 IP**：`rtsp_server_init(NULL,...)` 自动按 `end0→eth0→wlan0→usb0` 顺序选第一个有 IP 的网卡 → 板上会选 **wlan0(10.163.237.207)**，导致从 usb(10.222.2.1) 连不上（Connection refused）。→ 用 `RTSP_IP=10.222.2.1` 显式绑定（本目录 `rtsp_grab` 已支持）。
2. **H264 不可用**：scpcom `sophgo_middleware.c` 里 `mmf_enc_h264_init` 被 `#if 0`；内部 `_mmf_enc_h264_init` 是 static。本实验固定 H265。
3. **退出务必用 SIGTERM**：`killall rtsp_grab`（→ 触发信号处理清理，ION 归零）；**不要 `kill -9`**，否则泄漏 ION（实测 84MB/77%），需干净重启。
4. media_server 静态库为 riscv64 musl，直接可链接（无需重编）。

## 复用接口（后续 AI 路）
`rtsp_grab.c` 中 `mmf_vi_frame_pop` 得到的就是 NV21 帧，`/* AI/检测钩子 */` 处直接喂检测器即可，与推流共用同一帧。
