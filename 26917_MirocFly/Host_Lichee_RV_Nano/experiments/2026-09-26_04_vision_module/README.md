# MirocFly 相机层（实验 04: vision_module）

> ✅ **Form A 正解，阶段 0 的固化产物。**
> 用 **scpcom 公有头**编译 + 链接**板上 scpcom 库**（不部署自带 .so）。
> 根因与踩坑记录见 `../../difficulty_and_method.md`（B7 版本混用、B9 首帧黑、B10 输出清理）。

## 组成

| 文件 | 说明 |
|---|---|
| `camera.h` / `camera.c` | 可复用相机模块 API（NV21 零拷贝取帧） |
| `frame_grab.c` | CLI demo：抓 N 帧存 raw + 输出文件自动清理 |
| `sophgo_middleware.h` | scpcom `maix_mmf.h` 的裁剪头（仅声明用到的 mmf_* 接口） |
| `Makefile` | 交叉编译：编译 scpcom 树 `sophgo_middleware.c` + 本模块，链接 `../../board_libs` |

## API 速览

```c
mf_camera_cfg_t cfg; mf_camera_cfg_default(&cfg);
cfg.width = 1280; cfg.height = 720;
mf_camera_open(&cfg);
mf_camera_warmup(10);                 // 丢弃启动黑帧 (B9)
mf_frame_t f;
if (mf_camera_get_frame(&f, 5000) == 0) {
    // f.data = NV21 packed, f.width/height/size
    mf_camera_frame_free();           // 用完必须释放缓冲
}
mf_camera_close();
```

## 编译与运行

```bash
# 1) 首次: 拉取板上 scpcom 库作链接输入 (约 4.5MB, 不部署)
../../fetch_board_libs.sh

# 2) 交叉编译
make

# 3) 部署 + 运行 (板上无 timeout, 用后台+sleep+kill)
scp frame_grab root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && \
  (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5 >/tmp/fg.log 2>&1 &); \
  sleep 12; killall frame_grab'

# 4) 拉回并转图
scp root@10.222.2.1:/root/mirocfly/cam_1280x720.raw .
python3 ../2026-09-24_01_lowlevel_mmf/nv21_to_bmp.py cam_1280x720.raw 1280 720 out.bmp
```

## `frame_grab` 参数

```
./frame_grab [宽] [高] [帧数] [warmup] [keep] [hmirror] [vflip]
```
- `warmup`：抓帧前丢弃的前导帧数（默认 10，**别设 0**，启动帧是黑的）
- `keep`：`cam_*.{raw,bmp,png}` 最多保留数（默认 10，0=关闭；也可用 `MIROCFLY_KEEP`）
- `hmirror` / `vflip`：软件朝向开关（默认 0）。中间件默认等价旋转 180°，装好相机后按实测选。

## 注意

- **必须** `LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`。
- 链接期**必须** `-lgdc`（板载 `libvi/libvpss` 依赖它，musl 无惰性绑定）。
- 只做取帧；VI→VPSS 双路 / VENC→RTSP / 检测在后续模块。
