# MirocFly 常用命令速查（给用户）

> 目的：**基础命令自己查**（板子在不在线、怎么跑某功能…）。不够细就对了——详细版看：
> `Host_Lichee_RV_Nano/diary.md`（上位机操作手册）、`tools/README.md`（工具）、
> `Low_MCU/BateFPV_G473/connection/README.md`（对接/FC 配置）。

**先设一个别名（省去长参数，后续板子命令都用它）**
```bash
bsh(){ ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@10.117.42.1 "$@"; }
```

## 1. 板子在不在线
```bash
ip -br addr | grep 10.117          # 有 enp0s20*(RNDIS) 才可能有网
ping -c2 10.117.42.1                # 板地址会变（2026-11-03 起 10.117.42.1；旧 10.222.2.1 作废）
# 不通就 ip addr 看 USB-RNDIS 网段：板子通常与主机同网段、尾号 .1
bsh 'uptime'                       # 能登进去；若 up 很小=在反复重启→供电/USB 问题
# USB 掉线排查(i5)： journalctl -k | grep -iE "359f|error -71|disconnect"
#   多为 Hub/线/供电 → 去 Hub、直插主机后置口、短线、稳 5V
# 换镜像/换板后若报 host key 变了： ssh-keygen -R 10.117.42.1
```

## 1.5 板子无线（2026-11-05 配好）
```bash
# WiFi: Potential-Robot（2.4G）；板子 IP 192.168.1.165（DHCP，可能变）
# USB 路仍可用：10.117.42.1（两条路并存，USB 作调试后门）
ssh root@192.168.1.165                    # 无线登录
ssh root@10.117.42.1                      # USB 登录（拔了 USB 就没有）
iw dev wlan0 link                         # 看连的哪个 AP / 频段
iw dev wlan0 set power_save off           # 关省电（否则延迟抖动 4ms~1s）
cat /etc/wpa_supplicant.conf              # 账号配置
cat /etc/init.d/S45wifi                   # 开机自启脚本
```
> 2.4G 环境拥挤（扫描到 61 个 SSID），ping 抖动 3–60ms、偶有丢包；RTSP 走 WiFi 会卡，视觉内部 loopback 不受影响。

## 1.6 上下位机 MSP 链路（2026-11-09 实测）
```bash
# 板上：链路自检（ttyS0 @230400 已实测通）
/root/models/msp_link.rv --dev /dev/ttyS0 --baud 230400 --n 20 --send-rc
MSP_PROBE=1 /root/models/msp_link.rv --dev /dev/ttyS0 --baud 230400   # 探测支持的命令
MSP_RATE=1  /root/models/msp_link.rv --dev /dev/ttyS0 --baud 230400   # 各命令成功率
/root/models/msp_link.rv --dev /dev/ttyS0 --loopback                  # TX/RX 短接自测

# 控制端接真飞控（会发 RC，先拔桨）
/root/models/vision_bridge.rv --dev /dev/ttyS0 --baud 230400

# 飞控侧（USB CLI）：看串口与接收机
serial
get receiver_type
# 台架模式（无接收机时）：set receiver_type = MSP / save；飞前改回 SERIAL
```
> 授权门：`BOXMSPRCOVERRIDE` = **pid 41**（本板 bit20）；`pid 50` 是 CHANGEMISSION。
> 串口对应：丝印「串口3」= INAV `serial 2` = MSP（上位机）；丝印「串口4」= INAV `serial 3` = RX_SERIAL（接收机）。

## 2. 板子常用信息
```bash
bsh 'cd /root/mirocfly && ls'                                  # 已部署的程序
bsh 'cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary' # ION 占用(used 应=0)
bsh 'reboot'                                                   # 干净重启(ION 泄漏后)
```

## 3. 交叉编译 + 部署（在 i5）
```bash
cd Host_Lichee_RV_Nano/experiments/<实验目录> && make
scp <程序> root@10.117.42.1:/root/mirocfly/
```

## 4. 运行 / 停止（铁律）
```bash
# 运行必须带库路径；板上没有 timeout → 后台+sleep+killall；停止用 killall(SIGTERM)，别 kill -9
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./<程序> <参数> >/tmp/x.log 2>&1 &); sleep 12; killall <程序>; tail -20 /tmp/x.log'
bsh 'killall <程序>'     # 停止（相机类程序同一时刻只能跑一个！）
```

## 5. 相机 / 视觉类（详见 diary.md §2）
```bash
# 取帧
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5 >/tmp/fg.log 2>&1 &); sleep 12; killall frame_grab'
# RTSP（必须显式绑 IP）
bsh 'cd /root/mirocfly && (RTSP_IP=10.117.42.1 LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./rtsp_grab 1280 720 1 >/tmp/rtsp.log 2>&1 &)'
ffplay -rtsp_transport tcp rtsp://10.117.42.1:8554/live ;  bsh 'killall rtsp_grab'
# HSV 绿检测
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_detect 1280 720 60 >/tmp/gd.log 2>&1 &); sleep 16; killall green_detect; grep -aE "FOUND|no green" /tmp/gd.log'
```

## 6. 接管 / 飞行类（实验18/19/20；需 FC，串口 `/dev/ttyS0` @230400）
```bash
./paradigm mon  /dev/ttyS0 230400                          # 只读: q/alt/ch5-8/ovr
./paradigm dry  1280 720 0 /dev/ttyS0 230400               # 算值但不发 RC(安全)
./paradigm run  1280 720 0 /dev/ttyS0 230400               # 真机(经 MSP RC Override 接管三轴)
./run_paradigm.sh 120 0.4 150 60                           # 预启动(脱离电脑, 到点自停)
./stop_paradigm.sh                                         # 停
./flow_damp mon  /dev/ttyS0 230400                         # 只读光流/测距
./flow_damp damp /dev/ttyS0 230400                         # 光流水平阻尼
./takeover dry  1280 720 0                                 # (实验18)仅相机/PTY
```

## 7. 链路验证（板上）
```bash
bsh 'cd /root/mirocfly && ./msp_probe /dev/ttyS0 230400'   # 期望 … LINK OK
```

## 8. FC 配置（i5，USB VCP；docker 免 sudo）
```bash
# ⚠️ 用完必须 exit/save，并等 ~15–20s，否则 FC 的 UART2 MSP 不响应（板子链路会"NO RESPONSE"）
cd Low_MCU/BateFPV_G473 && docker run --rm --device=/dev/ttyACM0 -v "$PWD":/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --cmd version --cmd status
# 改配置：  … python3 fccli.py --cmd "set xxx = yyy" --cmd save
# 打开 Configurator 会独占 /dev/ttyACM0 → 先关（ killall inav-configurator ）
# 2026-10-23 起：FC 重启后可能枚举成 ttyACM0/1/…，别写死；用最新口：
#   cd Low_MCU/BateFPV_G473
#   PORT=$(ls -t /dev/ttyACM* | head -1)
#   docker run --rm --device="$PORT" -v "$PWD":/mcu -w /mcu/connection tpuc_mlir:latest \
#     python3 fccli.py --port "$PORT" --cmd "set align_board_pitch = -5" --cmd "save"
#   sleep 20; PORT=$(ls -t /dev/ttyACM* | head -1)
#   docker run --rm --device="$PORT" -v "$PWD":/mcu -w /mcu/connection tpuc_mlir:latest \
#     python3 fccli.py --port "$PORT" --cmd "get align_board_pitch" --cmd "exit"
# 单位 0.1°：-5 = -0.5°；-5° 要写 -50。
```

## 8.5 定高 / 解锁速查（2026-10-17）
```bash
# 定高开关：ch7 ≥1300 生效（FC: aux 4 3 2 1300 2100；现"中位即定高"，后续再拆出定点）
# 关键参数（10-17 晚）：nav_mc_hover_thr=1585（满电悬停实测）；inav_max_surface_altitude=400cm（高度源上限，"2m 软墙"已解）
# 2026-11-03 更新：nav_mc_hover_thr 1585→1615（三档 battery_profile 全写，save+读回确认；
#   依据 10-25 新机械定高日志 3.5V 段 rcCommand[3] 均值）。工具：Low_MCU/BateFPV_G473/connection/set_hover_thr.sh <值>
# 拨入定高后：油门杆 = 升降速率（零点 = 拨入时杆位；回中即停；压到底 ≈ 2m/s 快降）
# 解锁必要条件：① 油门杆到底(非中位) ② ch5 高位(≥1775；开关高档=2000) ③ 无其它阻止
#   阻止原因看 status 的 "Arming disabled flags"：
#   THR=油门没到底 / ARMSW=ch5 不在位 / DSHOTBEEPER=上电瞬态(几秒自消) / CLI=正在用 CLI/USB
# 验证定高：解锁后 mon 的 z 会跟测距动（未解锁 z 恒 0，属设计）；
#          黑匣子 flightModeFlags 含 8(bit3)=NAV_ALTHOLD 已生效，navFlags bit0=1=高度估计可信
```

## 8.6 光流 / 定点速查（2026-10-17 深夜）
```bash
# FC 现状（2026-10-20 更新）：align_opflow = CW90；opflow_scale = 4.000（tilt 校准 4.4–4.73 后的折中；旧的 1.133 严重偏小，是定点自激主因；yaw 5.19 无效 → 见 §8.7）
# P1 已写（10-17 深夜）：inav_allow_dead_reckoning=ON；aux 5 11 2 1700 2100（ch7≥1700=POSHOLD；⚠️aux 第二列=永久ID：POSHOLD=11）
# 板上只读观察：LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./flow_damp mon /dev/ttyS0 230400
# 目标约定：右移→transX 正、前移→transY 负（mon 的 trans = flow − body）
# 传感器最小工作距离 8cm（别贴地测）；纹理垫上方 30-40cm 数据最干净
```

## 8.7 光流 scale 校准（2026-10-20 结案；yaw 无效）

```bash
# 正确动作：绕 roll/pitch 倾斜，不是 yaw 自转！
# 源码 sensors/opflow.c:239-242 累计的是 bodyRate[X/Y]（roll/pitch）+ flow；
# 纯 yaw 不产生 X/Y，校准值（如 5.19）无效。
# 步骤：
#   拔桨；光流朝下；距纹理垫 0.3–0.6m；q≥35；
#   Configurator → Calibration → Optical Flow（或 MSP2 0x2032）；
#   30s 内左右 roll + 前后 pitch 倾斜 ±30–40°，约 60–90°/s，不平移、不 yaw；
#   可来回倾斜（代码累计幅值，方向不抵消）；bodyAcc>3600° 才会保存。
# 预期：本机 tilt 校准 4.4–4.73；默认 10.5 不是目标；不要手设。
# 当前（2026-10-23 读回）：opflow_scale=4.000；align_opflow=CW90；align_board_roll=0、align_board_pitch=-5（-0.5°；因更新机械结构后 set+save+读回；此前口头 0/1 实际读回 0/0，历史曾 -10/0）。
# 当前外环（2026-10-22）：nav_mc_pos_xy_p=65（P=75 已回退）、nav_mc_vel_xy_i=15、nav_mc_vel_xy_p=40、d=100、ff=40；
#           nav_mc_bank_angle=20；control_profile=1；ez_enabled=OFF。
# 2026-10-21：P=75 试飞持续左漂/发散（误差 30.7–85.4cm、navEPH 7→29），已回退 P=65；不要再加 P。
# 2026-10-22：三条日志都无干净 POSHOLD：10-21“第二次高度摔”POSHOLD 仅 0.75s 且入口在移动；10-22“一直在飘”只有 ALTHOLD；10-22“pitch=1”最后仅约 0.3s 无杆定高后打杆/坠落。
# 2026-10-22 定高无杆段：约 0.3m/s² 右向真实加速度、姿态 roll≈0、m2 高/m1 低 116–160us、navEPH 8–15；先查水平/CoG/电机桨，不要加 P。I2C Errors=50 先看趋势。
# 快照：Low_MCU/BateFPV_G473/drone/firmware/INAV_10.0.0_cli_20261020_scale40_p65_i15.txt（当前基线）
#       Low_MCU/BateFPV_G473/drone/firmware/INAV_10.0.0_cli_20261020_scale40_p75_i15.txt（P=75 失败测试配置，仅存档）
```

**known-tilt 复核公式**（可选）：支架绕水平铰链倾斜，量 `r`（转轴→光流传感器）、`h`（传感器→垫）、`θ`；`expected = r·sinθ`；`ratio = measured/expected`；`scale_new = scale_current × ratio`。`ratio>1`→scale 偏小；`<1`→偏大；≈1→正常。

**慢漂边界**：2–3cm/s 低于光流死区（<~50°/s；0.3m 约 0.26m/s），scale/PID 修不了；最终靠视觉/绝对参考。

## 9. 黑匣子解码（i5，`tools/blackbox_decode.py`）
```bash
python3 tools/blackbox_decode.py ~/Downloads/blackbox_log_*.TXT --list
python3 tools/blackbox_decode.py <log> --slow                              # 接管/模式/ESC
python3 tools/blackbox_decode.py <log> --index N --tmin 9 --npoints 60 \
  --trace "rcData[3],rcCommand[1],attitude[1],surfaceRaw,motor[0],motor[1],motor[2],motor[3]"
```

## 10. 关键坑（一句话记）
- 运行都要 `LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`。
- 相机类程序**互斥**；停止用 `killall`(SIGTERM)，**别 `kill -9`**（ION 泄漏）。
- 板子/FC 改完配置 → **等 15–20s**（FC 会重启、UART2 MSP 会短暂断）。
- 板子 USB 极易掉：**去 Hub、直插后置口、短线、稳 5V**（详见 `困难与解决办法.md` B18）。
- 开关映射（实验20）：`ch5`=解锁；`ch6>1300 或 ch8>1300`=接管；`ch8>900`=ANGLE；`ch6`=范式B；`ch7`=定点/定高。

## 11. 诊断 / 恢复速查（2026-10-18 新增）

```bash
# --- USB 直读 FC（/dev/ttyACM0，权限 666，不用开 Configurator）---
# ⚠️ FC 刚重启后 VCP 常不自动回来 → 物理重插；Configurator 开着会互相打架（读到其流量乱码）
# MSP v1 帧 = "$M<" [size] [cmd] [payload] [csum=size^cmd^payload...]；回复 = "$M>" 同构
# 常用 cmd: 102=RAW_IMU 105=RC(纯17×u16,无count字节) 108=ATTITUDE 113=ACTIVEBOXES 121=NAV_STATUS(9B)
#           70=DATAFLASH_SUMMARY 71=DATAFLASH_READ；106=RAW_GPS(18B,无GPS=全零,正常!)
# MSP v2: 0x2000=INAV_STATUS(armingFlags u32 @payload[9]) 0x2001=OPTICAL_FLOW(q,fX,fY,bodyX,bodyY)
#         0x201A=BLACKBOX_CONFIG(includeFlags u32 @payload[6]) 0x2220=FULL_LOCAL_POSE(pos/vel)

# --- 黑匣子遥测字段组（无需刷固件）：CLI `blackbox` 查看/开关 ---
#   blackbox                 # 看 Enabled 列表
#   blackbox NAV_POS         # 开：navEPH/EPV/navPos/navVel/navTgtVel/navTgtPos/navTgtHdg/navSurf
#   blackbox NAV_ACC         # 开：navAcc[0..2]（IMU 加速度 NEU）
#   blackbox -NAV_POS        # 关
#   save                     # 持久化（重启后生效）。读回用 MSP2 0x201A 的 includeFlags
# ⚠️ 开了以后坠机日志才有估计器数据（判位置环方向/增益的唯一途径）
# ⚠️ Configurator → Blackbox 页的 Erase 会清空全部日志——千万别点！

# --- 黑匣子 flash 占用 / 下载（USB 直读，不经 Configurator）---
# 70 → payload: ready(u8)+sectors(u32)+total(u32)+used(u32)（小端）；used=0 且不涨 = 没在写
# 71 请求 payload = addr(u32) + size(u16，可选，默认128)；回复 = addr(u32)+data
#   实测 ~192B/包 ≈ 18.6KB/s；只读尾部区间（used−N..used）最快
# DFU 读回固件（只读；按住 BOOT 插 USB，设备变 0483:df11）---
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb -v /tmp/opencode:/w --network none tpuc_mlir:dfu \
  bash -lc 'dfu-util -d 0483:df11 -a 0 -s 0x08000000:0x80000 -U /w/fc_readback.bin'
# 刷写（.hex 保留配置区；绝不烧 .bin）：
#   dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D /w/inav_10.0.0_BETAFPVG473.hex

# --- 血泪纪律 ---
# 1) ch7 一次性拨到位；4s 内连拨 5 次 = 手动紧急降落开关（上锁复位）
# 2) 解锁被拒先查：油门到底（低端读数贴 1060–1090 会偶发失败）/ YAW_HI 手势（重启后需重做）/ CLI status
#    实时确认：MSP v2 0x2000 的 armingFlags（bit2=ARMED, bit19=THROTTLE, bit11=NAVIGATION_UNSAFE）
# 3) 106≠NAV_STATUS（2026-10-19 结案）：106=RAW_GPS(18B 全零正常)、NAV_STATUS=121(9B)；
#    exp20 msp.h 的 106 是错的（待改 121）→ 在那之前 mon 的 navS 不可信，定点判据用黑匣子
# 4) 台架手推/抖动不能判方向/对齐：死区<~50°/s、饱和>~400°/s、q<35 失效→"速度衰减到零"伪影
# 5) 判断"定点对不对"优先看黑匣子 navTgtVel vs navVel vs navPos（2026-10-19 起可用）
```

## 12. 当前基线 / 复盘（2026-10-20 起）

- **定点自激主因 = `opflow_scale=1.133` 严重偏小**（实际约 4.4–4.7）；yaw 校准 5.19 无效；正确是 **roll/pitch tilt 30s** → 见 §8.7。
- 同时修正：`align_board_pitch=-42`（4.2° 假补偿）→ 0；`control_profile 3` → 1；`nav_mc_bank_angle` → 20。
- 当前：`opflow_scale=4.000`、`P=65/I=15`、`nav_mc_vel_xy_p=40`、`d=100`、`ff=40`、`bank=20`、`profile 1`、`ez=OFF`。
- 2026-10-21：**P=75 试飞持续左漂/发散（2.95s、误差 30.7–85.4cm、`navEPH` 7→29、`navVel y` −27→+20、yaw 稳定）→ 已回退 P=65**；不要再加 P。下次满电、不打杆、目标不动 5s；P=65 仍左漂就查 CoG/左侧电机/桨/`align`。
- 2026-10-22：**仍无干净 POSHOLD 样本**；10-22“一直在飘”只有 ALTHOLD，定高无杆段约 0.3m/s² 右向真实加速度、姿态近水平、m2 高/m1 低 116–160us、`navEPH` 8–15；优先查水平/CoG/电机桨，不要加 P。2026-10-23 因更新机械结构，CLI 读回：`align_board_roll=0`、`align_board_pitch=-5`（-0.5°；save 后读回；此前口头 0/1 未生效）。`I2C Errors=50` 先看趋势。
- 2–3cm/s 慢漂在光流死区以下，scale/PID 修不了；转视觉/绝对参考。
- 详细复盘：`复盘_2026-10-20_光流校准严重偏差与定点排错.md`；交接：`AGENTS.md`；下位机指南：§5.10 / §6-39~49。
