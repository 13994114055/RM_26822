# 2026-09-26_06_hsv_green（实验）

**目的**：阶段 1 的感知第一步——从相机帧检测**绿色荧光目标**，输出统一接口
`TargetInfo{ offsetX, offsetY, area, found }`（为后续 MSP 控制做准备）。本实验**只做检测+可视化，不接控制**。

**结果**：✅ 成功。3 米外约 20cm 的绿色目标，板端连续稳定检出 `offset≈(-73,-56) area≈400`，框准确。

## 目录
```
├── green_detect.cpp    # 板端: 取帧→HSV→最大轮廓→offset/area→存 green.bmp
├── camera.c / camera.h # 相机层(复制自实验04, 自包含)
├── sophgo_middleware.h
├── Makefile            # 链接 opencv-mobile(geometry,imgproc,features,core) + board_libs
├── pc_tune/
│   ├── detect_green.py # PC 调参: 最大连通块+质心+HSV统计 (PIL)
│   └── sample*.raw / *.bmp|png
└── green_board.png     # 板端检出效果(带框图)
```

## 数据流
```
VI pop NV21 → cv::Mat(COLOR_YUV2BGR_NV21) → resize(640×360) → BGR2HSV
  → inRange → 形态学开/闭 → findContours → 最大轮廓 → 质心/面积/偏移
  → (每5帧) 缩略图画框 → 直接写 green.bmp
```

## 编译 / 运行
```bash
make
scp green_detect root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && \
  (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_detect 1280 720 60 >/tmp/gd.log 2>&1 &); \
  sleep 16; killall green_detect';   # 板上无 timeout
# 拉回 green.bmp 查看
```
参数：`./green_detect [w] [h] [frames] [hmin hmax smin smax vmin vmax] [min_area]`
- `frames=0` 表示一直跑；H 为 **OpenCV 0-179**，S/V 0-255。
- 默认阈值（对应 PIL H85-130/S40-170/V55-170）：**H 60-91 / S 40-170 / V 55-170**。
- 远/小目标：调小 `min_area`（默认 20，缩略图 640×360 像素）或提高分辨率。

## 关键坑 / 结论
1. **PC 与 OpenCV 的 HSV 标度不同**：PIL H 0-255，OpenCV H 0-179。换算 `Hcv = Hpil*179/255`。
2. **OpenCV5 把 `findContours/contourArea/moments/boundingRect` 放进了 `geometry` 模块**：
   需 `#include <opencv2/geometry.hpp>` 且链接 `-lopencv_geometry`（否则 `not a member of 'cv'`）。
3. **opencv-mobile 无 imgcodecs**（不能 `imwrite`）；用**中间件 `mmf_enc_jpg` 也失败**：
   `CVI_VENC_SendFrame failed with -1`（输入是普通内存、非 DMA/VB 缓冲）。
   → 可视化改为**直接写 BMP**（纯 C 写 54B 头 + BGR 行），稳且零依赖。
4. **小目标过滤**：最初 `min_area=200` 在 640×360 上把 3 米外目标滤掉了；改 20 即检出。
5. 三态：绿物在框内 → `FOUND offset/area`；移出画面 → `no green`。

## 复用接口（后续控制）
检测结果即 `TargetInfo`：`offsetX/offsetY` 为像素偏移（正值右/下），`area` ∝ 1/距离²；下一步把它映射为 MSP 摇杆修正量。
